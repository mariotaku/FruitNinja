#include "render/FontCacheObjectTTF.h"
#include "render/TtfBackend.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace Mortar {

FontCacheObjectTTF::FontCacheObjectTTF(const char* path, int defaultPixelSize)
    : m_Face(nullptr)
    , m_DefaultPixelSize(defaultPixelSize)
    , m_CurrentCharHeight(-1)
    , m_Atlas(nullptr)
{
    m_Face = TtfFace::Open(path, defaultPixelSize);
    if (!m_Face || !m_Face->IsValid()) {
        LOG_ERROR("FontCacheObjectTTF", "TtfFace::Open failed for '%s'", path);
        delete m_Face;
        m_Face = nullptr;
        return;
    }
    m_Atlas = new FontInterface();
    // Mirror binary Initialize @ 0x00250470: fontScale=1.0, globalSizeScale=1.0.
    // GameInitialise re-invokes InitialiseData with 0.9 for russian (langId 0x13)
    // per InitialiseData @0x0011c3f0. Port: default 1.0 here; override applied after
    // PreloadFontsTTF() in GameInitialise.cpp if game_work.languageFlag == 0x13.
    m_Atlas->InitialiseData(1.0f, 1.0f);
}

FontCacheObjectTTF::~FontCacheObjectTTF() {
    delete m_Face;
    m_Face = nullptr;
    delete m_Atlas;
    m_Atlas = nullptr;
}

bool FontCacheObjectTTF::IsValid() const {
    return m_Face != nullptr && m_Face->IsValid();
}

// Compute the FT 26.6 char height from the raw requestedSize and atlas scale factors.
// Binary SetFontSize @ 0x0024f568:
//   scaledHeight = requestedSize * m_GlobalSizeScale
//   char_height_26.6 = trunc(max(0, scaledHeight * m_FontScale * 64.0))
//   (binary then calls its own Bada IFont cache API with m_CacheSize -- not
//   FreeType; see SetCharSize below for the port-side FT_Set_Char_Size call)
// ASM-verified: 2026-06-14T00:00Z v1.6.1 binary @ 0x0024f568,0x002502e0,0x00250470 (asm-inspector)
static long ComputeCharHeight26_6(float requestedSize,
                                  float globalSizeScale,
                                  float fontScale) {
    float scaledHeight = requestedSize * globalSizeScale;
    float raw = scaledHeight * fontScale * 64.0f;
    if (raw < 0.0f) raw = 0.0f;
    return (long)raw; // trunc toward zero
}

// BuildBlur -- separable squared-tent blur filter for shadow/glow glyph rasterisation.
// ASM-spec v1.6.1 Mortar::BuildBlur @0x0024f030:
//   K = 2*radius+1; per-tap weight w = clamp((1 - |radius-i|/radius) + 0.2, 0, 1); wt = w*w;
//   normalize wt so sum==1; two-pass separable convolution (vertical pass then horizontal
//   pass), samples clipped at the buffer edge (no wrap/extend); output floored at 0.
// Port specific: the binary packs coverage into the high byte of a 16-bit pixel
// (px=(cov<<8)|0xFF) and filters only that byte; the port's atlas glyph buffer is
// already a plain 8-bit coverage byte per pixel (FontInterface.h "glyph atlas is
// RGBA" note), so the filter runs directly on it -- identical weights/math, only the
// storage width differs.
static void BuildBlur(uint8_t* buf, int width, int height, int radius) {
    if (radius <= 0 || width <= 0 || height <= 0) return;

    const int K = 2 * radius + 1;
    std::vector<float> wt((size_t)K);
    float sum = 0.0f;
    for (int i = 0; i < K; i++) {
        int d = radius - i;
        if (d < 0) d = -d;
        float w = (1.0f - (float)d / (float)radius) + 0.2f;
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
        wt[i] = w * w;
        sum += wt[i];
    }
    if (sum > 0.0f) {
        for (int i = 0; i < K; i++) wt[i] /= sum;
    }

    std::vector<uint8_t> tmp((size_t)width * (size_t)height);

    // Pass 1: vertical (buf -> tmp), clip rows [0,H).
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            float acc = 0.0f;
            for (int k = 0; k < K; k++) {
                int sy = y + (k - radius);
                if (sy < 0 || sy >= height) continue;
                acc += (float)buf[sy * width + x] * wt[k];
            }
            int iv = (int)acc;
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            tmp[(size_t)y * width + x] = (uint8_t)iv;
        }
    }

    // Pass 2: horizontal (tmp -> buf), clip cols [0,W).
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float acc = 0.0f;
            for (int k = 0; k < K; k++) {
                int sx = x + (k - radius);
                if (sx < 0 || sx >= width) continue;
                acc += (float)tmp[(size_t)y * width + sx] * wt[k];
            }
            int iv = (int)acc;
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            buf[(size_t)y * width + x] = (uint8_t)iv;
        }
    }
}

// ASM-spec v1.6.1 FontCacheObjectTTF::BuildStrokes @0x0024edb8 (SDF outline; uint8_t coverage buf W*H, texel radius R):
//  pass1: for each px, best = min over dy,dx in [-(R+1)..R+1] of ( sqrt(dx*dx+dy*dy) - cov(yy,xx)/255 ),
//         counting only texels with cov!=0; dist = found ? best : 99999
//  pass2: if (R <= dist) a=0; else { t=pow(dist/R, R+1); a = dist>=0.01 ? max(0,(int)((1-t)*255)) : 255 }  buf[i]=a
static void BuildStrokes(uint8_t* buf, int width, int height, int radius) {
    if (radius <= 0 || width <= 0 || height <= 0) return;

    const std::vector<uint8_t> src(buf, buf + (size_t)width * (size_t)height);
    std::vector<float> dist((size_t)width * (size_t)height, 99999.0f);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float best = 99999.0f;
            bool found = false;
            for (int dy = -(radius + 1); dy <= (radius + 1); dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= height) continue;
                for (int dx = -(radius + 1); dx <= (radius + 1); dx++) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= width) continue;
                    uint8_t cov = src[(size_t)yy * width + xx];
                    if (cov == 0) continue;
                    float d = sqrtf((float)(dx * dx + dy * dy)) - (float)cov / 255.0f;
                    if (!found || d < best) { best = d; found = true; }
                }
            }
            dist[(size_t)y * width + x] = found ? best : 99999.0f;
        }
    }

    const float R = (float)radius;
    for (int i = 0; i < width * height; i++) {
        float d = dist[i];
        uint8_t a;
        if (R <= d) {
            a = 0;
        } else {
            float t = powf(d / R, R + 1.0f);
            if (d >= 0.01f) {
                int iv = (int)((1.0f - t) * 255.0f);
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                a = (uint8_t)iv;
            } else {
                a = 255;
            }
        }
        buf[i] = a;
    }
}

bool FontCacheObjectTTF::SetCharSize(long charHeight_26_6) {
    if (charHeight_26_6 == m_CurrentCharHeight) return true;
    if (!m_Atlas || !m_Face) return false;
    // Port specific: m_CacheSize (100) is the binary's Bada IFont cache-slot
    // constant (v1.6.1 FontInterface ctor @0x002502e0). The pre-refactor code
    // passed it straight through as FreeType's FT_Set_Char_Size vert_res
    // (default 72), which rasterises at charHeight_26_6*(100/72) actual
    // device pixels. The TtfFace seam (TtfBackend.h) has no vert_res concept
    // (stb_truetype only knows literal pixel height), so that DPI scale is
    // folded into charHeight_26_6 HERE, backend-neutrally, before calling
    // SetPixelSize -- both backends agree that SetPixelSize's argument is a
    // LITERAL pixel height at standard 72dpi (TtfBackendFreetype.cpp's
    // FT_Set_Char_Size call uses vert_res=72, a no-op scale). This is NOT a
    // dpi bug -- every consumer of GlyphAtlasEntry's world-unit metrics
    // (BakedStringTTF's layoutX/Y, cellW/H, cellOrigin, all consumed RAW with
    // no further scale) is calibrated against this exact render resolution.
    // Changing this factor is a GLOBAL rescale of every TTF metric and
    // shrinks all TTF text ~1.39x (100/72) vs. the real game -- confirmed via
    // HLE screenshot comparison. Local callers that need a pixel-size-relative
    // scale (Font.cpp's static DrawStringTTF path) must normalize locally
    // using the SAME 100/72 factor instead of changing this.
    const long dpiScaledCharHeight = (long)((double)charHeight_26_6 * (100.0 / 72.0));
    m_Face->SetPixelSize(dpiScaledCharHeight);
    m_CurrentCharHeight = charHeight_26_6;
    return true;
}

const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t cp, float requestedSize,
                                                     FONT_EFFECT_ENUM effect, int radius) {
    if (!m_Face || !m_Atlas) return nullptr;

    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);

    // Cache-key hygiene: BLUR and STROKE both consume a radius; collapse to 0 only for
    // NONE so callers that always pass effect=NONE keep sharing the same cache entry.
    if (effect == FONT_EFFECT_NONE) radius = 0;

    GlyphCacheKey key;
    key.codepoint      = cp;
    key.charHeight26_6 = ch26;
    key.effect          = (uint8_t)effect;
    key.radius          = (uint8_t)radius;

    std::map<GlyphCacheKey, GlyphAtlasEntry>::iterator it = m_Cache.find(key);
    if (it != m_Cache.end()) {
        return &it->second;
    }

    // Port specific: HD font supersampling (binary bakes glyphs at device res; we oversample Nx for crisp upscaling).
    // Ask the backend to rasterize at kFontSupersample x the logical size so the atlas holds hi-res glyph bitmaps.
    // The cache key stays at the logical ch26 so callers sharing a logical size share the same cache entry.
    if (!SetCharSize(ch26 * (long)kFontSupersample)) return nullptr;

    unsigned glyphIndex = m_Face->GetGlyphIndex(cp);
    if (glyphIndex == 0) {
        GlyphAtlasEntry empty;
        memset(&empty, 0, sizeof(empty));
        m_Cache[key] = empty;
        return nullptr;
    }

    // FT_Set_Transform is IDENTITY with zero delta in the binary (matrix
    // {0x10000,0,0,0x10000}) -- the glyph is rasterised at its natural position
    // and bearing lives in the metrics only. The port never calls
    // FT_Set_Transform, which is the same thing.
    // ASM-spec v1.6.1 Mortar::RenderGlyph @0x0024f5dc.
    TtfRasterGlyph glyph;
    if (!m_Face->RasterizeGlyph(glyphIndex, glyph)) {
        LOG_ERROR("FontCacheObjectTTF", "RasterizeGlyph cp=%u gi=%u failed", cp, glyphIndex);
        return nullptr;
    }

    // Convert 26.6 metrics to world units: 26.6 value * (1/64) * invFontScale.
    // Port specific: HD font supersampling -- divide by kFontSupersample so layout
    // dimensions are identical to the non-supersampled case (the atlas holds Nx
    // texels but quads are logical-size).
    const int   ss         = kFontSupersample;
    const float invWorld   = m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)ss);
    const float invLogical = m_Atlas->m_InvFontScale * (1.0f / (float)ss); // texel -> logical px

    GlyphAtlasEntry entry;
    memset(&entry, 0, sizeof(entry));

    // Legacy separate-bearing contract (BakedStringBox / Font.cpp consumers).
    entry.bearingX = (float)glyph.bearingX_26_6 * invWorld;
    entry.bearingY = (float)glyph.bearingY_26_6 * invWorld;
    entry.advanceX = (float)glyph.advanceX_26_6 * invWorld;
    entry.width    = (float)glyph.width  * invLogical;
    entry.height   = (float)glyph.height * invLogical;

    // ASM-verified: v1.6.1 RenderGlyph @0x0024f5dc + RenderGlyphMetrics @0x0024fe5c:
    //  layoutX = trunc(advance.x/64) - bitmap_left (FT_GlyphSlot +0x40 adv, +0x64 bearingL).
    //  Binary is intentionally tight by bitmap_left/glyph; cellOrigin.x=0 (no re-add). Faithful.
    // layoutY = (horiBearingY - height)/64 (ink bottom, baseline-relative).
    entry.layoutX = ((float)(glyph.advanceX_26_6 >> 6) - (float)glyph.bitmapLeft)
                    * m_Atlas->m_InvFontScale * (1.0f / (float)ss);
    entry.layoutY = (float)(glyph.bearingY_26_6 - glyph.inkHeight_26_6) * invWorld;

    // Baked-bearing cell padding, LOGICAL device px, symmetric both sides.
    // ASM-spec v1.6.1 Mortar::RenderGlyph @0x0024f5dc, per effect:
    //   NONE(0) / INNER_GLOW(3) / default: padL=0, padT=1
    //   STROKE(1) / BLUR(2):               padL=radius+1, padT=radius+2
    //   BEVEL (effect in [4..11]):         +4 to both padL and padT
    const int e = (int)effect;
    int padL = 0, padT = 1;
    if (e == FONT_EFFECT_STROKE || e == FONT_EFFECT_BLUR) {
        padL = radius + 1;
        padT = radius + 2;
    }
    if (e >= 4 && e <= 11) {
        padL += 4;
        padT += 4;
    }

    const int   pageSize = m_Atlas->GetSize();
    const float invS     = 1.0f / (float)pageSize;

    if (glyph.width > 0 && glyph.height > 0) {
        // Port specific: HD font supersampling -- pads scale to texel space so the
        // LOGICAL cell origin stays the exact binary integers (padL, padT).
        const int padLT  = padL * ss;
        const int padTT  = padT * ss;
        const int cellWT = glyph.width  + 2 * padLT;
        const int cellHT = glyph.height + 2 * padTT;

        std::vector<uint8_t> cell((size_t)cellWT * (size_t)cellHT, 0);
        // TtfRasterGlyph::bitmap contract: pitch == width (TtfBackend.h), so
        // each source row is exactly glyph.width bytes -- one memcpy per row.
        for (int row = 0; row < glyph.height; row++) {
            memcpy(&cell[(size_t)(row + padTT) * cellWT + padLT],
                   glyph.bitmap + (size_t)row * glyph.width,
                   (size_t)glyph.width);
        }
        if (e == FONT_EFFECT_BLUR && radius > 0) {
            BuildBlur(&cell[0], cellWT, cellHT, radius * ss);
        } else if (e == FONT_EFFECT_STROKE && radius > 0) {
            BuildStrokes(&cell[0], cellWT, cellHT, radius * ss);
        }
        // v1.6.1 Mortar::FontCacheObjectTTF::BuildBevel @0x0024ed16 is an empty stub
        // (bx lr / returns this). Effects 4..11 get the +4/+4 cell pad but NO filter --
        // the sharp glyph in the padded cell IS the binary's output. Faithful as-is.

        // DIFFERS: binary TextureAtlas @0x00269c9c, faithful multi-page model.
        int x = 0, y = 0;
        FontAtlasPage* page = m_Atlas->PackGlyphCell(cellWT, cellHT, &cell[0], &x, &y);
        entry.page = page;

        // CalcUVs (v1.6.1 rec UVs, no inset -- FinishMesh applies the 1/512 inset):
        //   U0 = glyphX/pageW, V0 = glyphY/pageH,
        //   U1 = U0 + (cellW+1)/pageW, V1 = V0 + (cellH+1)/pageH   (device px)
        // Port specific: +1 device px = +kFontSupersample texels; the port packs
        // with margin 0 (binary "margin" rec offset is folded into glyphX here).
        entry.cellU0 = (float)x * invS;
        entry.cellV0 = (float)y * invS;
        entry.cellU1 = entry.cellU0 + (float)(cellWT + ss) * invS;
        entry.cellV1 = entry.cellV0 + (float)(cellHT + ss) * invS;

        entry.cellOriginX = (float)padL * m_Atlas->m_InvFontScale;
        entry.cellOriginY = (float)padT * m_Atlas->m_InvFontScale;
        entry.cellW       = (float)cellWT * invLogical;
        entry.cellH       = (float)cellHT * invLogical;

        // Legacy contract fill. Effect glyphs (STROKE/BLUR/BEVEL) keep the old
        // grown-by-pad semantics (quad = whole cell, bearings shifted by the pad
        // so the effect layer registers with the sharp glyph at the same pen);
        // plain glyphs keep the old tight ink rect (pad excluded) so
        // BakedStringBox / Font.cpp output is unchanged by the cell padding.
        const bool grown = (e == FONT_EFFECT_STROKE || e == FONT_EFFECT_BLUR
                            || (e >= 4 && e <= 11));
        if (grown) {
            entry.u0 = (float)x * invS;
            entry.v0 = (float)y * invS;
            entry.u1 = (float)(x + cellWT) * invS;
            entry.v1 = (float)(y + cellHT) * invS;
            entry.bearingX -= (float)padL * m_Atlas->m_InvFontScale;
            entry.bearingY += (float)padT * m_Atlas->m_InvFontScale;
            entry.width  = entry.cellW;
            entry.height = entry.cellH;
        } else {
            entry.u0 = (float)(x + padLT) * invS;
            entry.v0 = (float)(y + padTT) * invS;
            entry.u1 = (float)(x + padLT + glyph.width)  * invS;
            entry.v1 = (float)(y + padTT + glyph.height) * invS;
        }
        entry.pageTextureID = page ? page->m_TextureID : 0;
    } else {
        // Empty (ink-less) glyph, e.g. space: 1x1 transparent LOGICAL cell,
        // cellOrigin=(0,0), layout metrics kept from the raw backend metrics above.
        // ASM-spec v1.6.1 Mortar::RenderGlyph @0x0024f5dc (empty-glyph branch).
        std::vector<uint8_t> cell((size_t)(ss * ss), 0);
        int x = 0, y = 0;
        FontAtlasPage* page = m_Atlas->PackGlyphCell(ss, ss, &cell[0], &x, &y);
        entry.page = page;
        entry.cellU0 = (float)x * invS;
        entry.cellV0 = (float)y * invS;
        entry.cellU1 = entry.cellU0 + (float)(ss + ss) * invS;
        entry.cellV1 = entry.cellV0 + (float)(ss + ss) * invS;
        entry.cellOriginX = 0.0f;
        entry.cellOriginY = 0.0f;
        entry.cellW = (float)ss * invLogical;
        entry.cellH = (float)ss * invLogical;
        // Legacy contract: no ink -- tight rect stays zero-size, pageTextureID 0
        // (preserves the old "empty glyph draws nothing" behaviour for
        // BakedStringBox / Font.cpp).
    }

    m_Cache[key] = entry;
    return &m_Cache[key];
}

float FontCacheObjectTTF::GetKerningForPair(uint32_t a, uint32_t b, float requestedSize) {
    if (!m_Face || !m_Atlas) return 0.0f;

    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size to match GetGlyph.
    if (!SetCharSize(ch26 * (long)kFontSupersample)) return 0.0f;

    long kern26 = m_Face->GetKerning_26_6(a, b); // 0 if no kern table (matches binary GetKerning stub)
    // kern26 is 26.6; convert to world units and divide by N to restore logical scale.
    // Port specific: HD font supersampling -- divide by kFontSupersample.
    return (float)kern26 * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetAscender(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->GetAscender_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetDescender(float requestedSize) {
    if (!m_Atlas) return 0.0f;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return 0.0f;
    return (float)m_Face->GetDescender_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetLineHeight(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->GetLineHeight_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

} // namespace Mortar
