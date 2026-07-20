#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

#if defined(FRUIT_PLATFORM_WII)
#include "render/BakedFontWii.h"
#include "game/GameWork.h"   // game_work.languageFlag (active language)
#else
// Non-Wii only (task #54): Wii never opens a runtime .ttf / TtfFace at all --
// see FontCacheObjectTTF.h's m_Face guard.
#include "render/TtfBackend.h"
#endif

namespace Mortar {

FontCacheObjectTTF::FontCacheObjectTTF(const char* path, int defaultPixelSize)
    :
#if !defined(FRUIT_PLATFORM_WII)
      m_Face(nullptr)
    , m_CurrentCharHeight(-1)
    ,
#endif
      m_DefaultPixelSize(defaultPixelSize)
    , m_Atlas(nullptr)
#if defined(FRUIT_PLATFORM_WII)
    , m_BakedWii(nullptr)
#endif
{
#if defined(FRUIT_PLATFORM_WII)
    // Port specific (task #54): Wii never opens the runtime .ttf (no stb_truetype
    // linked, no FreeType face) -- `path` is unused; the baked FNT3 atlas
    // (BakedFontWii, constructed lazily on first GetGlyph/GetAscender/etc call)
    // is the sole glyph + face-metric source. m_Face stays nullptr forever on
    // Wii; IsValid() below reflects that this construction succeeded WITHOUT it.
    (void)path;
#else
    m_Face = TtfFace::Open(path, defaultPixelSize);
    if (!m_Face || !m_Face->IsValid()) {
        LOG_ERROR("FontCacheObjectTTF", "TtfFace::Open failed for '%s'", path);
        delete m_Face;
        m_Face = nullptr;
        return;
    }
#endif
    m_Atlas = new FontInterface();
    // Mirror binary Initialize @ 0x00250470: fontScale=1.0, globalSizeScale=1.0.
    // GameInitialise re-invokes InitialiseData with 0.9 for russian (langId 0x13)
    // per InitialiseData @0x0011c3f0. Port: default 1.0 here; override applied after
    // PreloadFontsTTF() in GameInitialise.cpp if game_work.languageFlag == 0x13.
    m_Atlas->InitialiseData(1.0f, 1.0f);
}

FontCacheObjectTTF::~FontCacheObjectTTF() {
#if !defined(FRUIT_PLATFORM_WII)
    delete m_Face;
    m_Face = nullptr;
#endif
    delete m_Atlas;
    m_Atlas = nullptr;
#if defined(FRUIT_PLATFORM_WII)
    delete m_BakedWii;
    m_BakedWii = nullptr;
    for (std::map<GLuint, FontAtlasPage*>::iterator it = m_BakedPages.begin();
         it != m_BakedPages.end(); ++it) {
        delete it->second;   // wrapper only; the GL texture is owned by m_BakedWii
    }
    m_BakedPages.clear();
#endif
}

bool FontCacheObjectTTF::IsValid() const {
#if defined(FRUIT_PLATFORM_WII)
    // Port specific (task #54): valid as soon as the atlas exists -- there is
    // no m_Face to check on Wii. A per-glyph baked miss is handled at the
    // Lookup call site (LOG_WARN + no-glyph), not here.
    return m_Atlas != nullptr;
#else
    return m_Face != nullptr && m_Face->IsValid();
#endif
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

#if !defined(FRUIT_PLATFORM_WII)
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
#endif // !FRUIT_PLATFORM_WII

const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t cp, float requestedSize,
                                                     FONT_EFFECT_ENUM effect, int radius) {
#if defined(FRUIT_PLATFORM_WII)
    if (!m_Atlas) return nullptr;
#else
    if (!m_Face || !m_Atlas) return nullptr;
#endif

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

#if defined(FRUIT_PLATFORM_WII)
    // Port specific (task #51): try the prebaked FreeType IA8 atlas store for
    // plain glyphs before rasterising via stb (which clips CJK / breaks Korean).
    // Effect glyphs (STROKE/BLUR/BEVEL) run the effect filter on the BAKED
    // glyph's coverage (task #51) so the shadow/glow is computed at the baked
    // (correct) size and aligns with the NONE base layer -- stb over-sizes CJK,
    // which mismatched the halo behind the text. A baked miss (glyph not in the
    // size's baked subset) falls through to the stb effect path below.
    if (effect == FONT_EFFECT_NONE) {
        GlyphAtlasEntry baked;
        if (TryBakedGlyph(cp, requestedSize, &baked)) {
            m_Cache[key] = baked;
            return &m_Cache[key];
        }
    } else {
        GlyphAtlasEntry bakedFx;
        if (TryBakedEffectGlyph(cp, requestedSize, effect, radius, &bakedFx)) {
            m_Cache[key] = bakedFx;
            return &m_Cache[key];
        }
    }
    // Port specific (task #54): no stb_truetype fallback on Wii (removed from
    // the build entirely -- see TtfBackendStb.cpp exclusion in
    // src/engine/CMakeLists.txt). A baked-atlas miss here is a genuine
    // plan-coverage gap (BakedFontWii::Lookup/GetGlyphCoverage already
    // LOG_WARN "miss cp=..." once per unique (cp,size)); render no glyph
    // rather than crash or silently substitute a wrong-language rasterizer.
    {
        GlyphAtlasEntry empty;
        memset(&empty, 0, sizeof(empty));
        m_Cache[key] = empty;
    }
    return nullptr;
#else
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
#endif // FRUIT_PLATFORM_WII
}

float FontCacheObjectTTF::GetKerningForPair(uint32_t a, uint32_t b, float requestedSize) {
#if defined(FRUIT_PLATFORM_WII)
    // Port specific (task #54): the baked pen model (TryBakedGlyph) ignores
    // pair-kerning by construction -- layoutX is derived from the glyph's own
    // advance only (ASM-verified GetKerning is a no-op stub for .fnt fonts;
    // the dynamic TTF path's kerning was itself best-effort). No live caller
    // reads this on Wii (grep confirmed zero call sites), so 0.0f preserves
    // the "no kern table" contract without needing m_Face.
    (void)a; (void)b; (void)requestedSize;
    return 0.0f;
#else
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
#endif
}

#if defined(FRUIT_PLATFORM_WII)
// Port specific (task #54): shared world-unit conversion for the three
// baked face-metric queries below. `rawPx` is SUPERSAMPLED px straight from
// the FNT3 header; applies the SAME `pxToWorld = inv * sizeScale / ss`
// formula TryBakedGlyph uses for bearing/advance (see that function's
// comment), so a face metric and a glyph's own bearingY land in the same
// world-unit space at the same requestedSize.
static float BakedMetricToWorld(float invFontScale, float requestedSize,
                                 int rawPx, int nativeSize, float supersample) {
    const float sizeScale = (nativeSize > 0) ? (requestedSize / (float)nativeSize) : 1.0f;
    const float ss = (supersample > 0.0f) ? supersample : 1.0f;
    const float pxToWorld = invFontScale * sizeScale / ss;
    return (float)rawPx * pxToWorld;
}
#endif

float FontCacheObjectTTF::GetAscender(float requestedSize) {
    if (!m_Atlas) return requestedSize;
#if defined(FRUIT_PLATFORM_WII)
    // Port specific (task #54): route through the baked FNT3 face metrics --
    // m_Face no longer exists on Wii once the runtime .ttf open is dropped.
    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    m_BakedWii->SetLanguage((int)game_work.languageFlag);
    int rawPx = 0, nativeSize = 0; float ss = 1.0f;
    if (!m_BakedWii->GetAscender(requestedSize, &rawPx, &nativeSize, &ss)) return requestedSize;
    return BakedMetricToWorld(m_Atlas->m_InvFontScale, requestedSize, rawPx, nativeSize, ss);
#else
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->GetAscender_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
#endif
}

float FontCacheObjectTTF::GetDescender(float requestedSize) {
    if (!m_Atlas) return 0.0f;
#if defined(FRUIT_PLATFORM_WII)
    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    m_BakedWii->SetLanguage((int)game_work.languageFlag);
    int rawPx = 0, nativeSize = 0; float ss = 1.0f;
    if (!m_BakedWii->GetDescender(requestedSize, &rawPx, &nativeSize, &ss)) return 0.0f;
    return BakedMetricToWorld(m_Atlas->m_InvFontScale, requestedSize, rawPx, nativeSize, ss);
#else
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return 0.0f;
    return (float)m_Face->GetDescender_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
#endif
}

float FontCacheObjectTTF::GetLineHeight(float requestedSize) {
    if (!m_Atlas) return requestedSize;
#if defined(FRUIT_PLATFORM_WII)
    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    m_BakedWii->SetLanguage((int)game_work.languageFlag);
    int rawPx = 0, nativeSize = 0; float ss = 1.0f;
    if (!m_BakedWii->GetLineHeight(requestedSize, &rawPx, &nativeSize, &ss)) return requestedSize;
    return BakedMetricToWorld(m_Atlas->m_InvFontScale, requestedSize, rawPx, nativeSize, ss);
#else
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->GetLineHeight_26_6()
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
#endif
}

#if defined(FRUIT_PLATFORM_WII)
// Task #60: see header doc. Bounds a single effect glyph's packed CELL size
// (supersampled texels, the same domain PackGlyphCell/TryBakedEffectGlyph's
// cellW/cellH use) without rasterising or looking up any specific codepoint,
// so it can run once per STRING rather than once per glyph.
//
// Bound derivation: GetLineHeight's raw supersampled px (ascender+descender
// span) is an upper bound on any single glyph's ink height for the active
// font/size -- no glyph in a well-formed font face extends past its own line
// height. CJK ink is roughly square (em-box advance ~= line height), and
// Latin ink is narrower than it is tall, so the same bound conservatively
// covers width too. Add the effect's pad (same padL/padT rule
// TryBakedEffectGlyph uses, rounded to texels by ssi) on both sides.
void FontCacheObjectTTF::BeginGlyphRun(int codepointCount, float requestedSize,
                                       FONT_EFFECT_ENUM effect, int radius) {
    if (!m_Atlas || codepointCount <= 0 || effect == FONT_EFFECT_NONE) return;

    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    m_BakedWii->SetLanguage((int)game_work.languageFlag);

    int rawPx = 0, nativeSize = 0; float ss = 1.0f;
    if (!m_BakedWii->GetLineHeight(requestedSize, &rawPx, &nativeSize, &ss) || rawPx <= 0) {
        return;   // no baked metric available -- fall through to normal per-glyph packing
    }

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
    const int ssi = (ss > 0.0f) ? (int)(ss + 0.5f) : 1;
    const int padLT = padL * ssi;
    const int padTT = padT * ssi;

    const int maxCellW = rawPx + 2 * padLT;
    const int maxCellH = rawPx + 2 * padTT;

    m_Atlas->BeginGlyphRun(codepointCount, maxCellW, maxCellH);
}

void FontCacheObjectTTF::EndGlyphRun() {
    if (m_Atlas) m_Atlas->EndGlyphRun();
}
#endif

#if defined(FRUIT_PLATFORM_WII)
// Port specific (task #51): return a stable FontAtlasPage wrapper for a baked
// GL texture id. BakedStringTTF groups glyphs into surfaces keyed by the
// FontAtlasPage* pointer, so the SAME id must always yield the SAME pointer.
// The wrapper carries only m_TextureID (the draw path -- BakedStringTTF::Draw --
// reads nothing else off the page); m_Pixels is NULL and the GL texture is owned
// by m_BakedWii, not freed here.
FontAtlasPage* FontCacheObjectTTF::BakedPageFor(GLuint texId) {
    if (texId == 0) return nullptr;
    std::map<GLuint, FontAtlasPage*>::iterator it = m_BakedPages.find(texId);
    if (it != m_BakedPages.end()) return it->second;
    FontAtlasPage* p = new FontAtlasPage();
    memset(p, 0, sizeof(*p));
    p->m_TextureID = texId;
    m_BakedPages[texId] = p;
    return p;
}

// Guaranteed transparent gutter (texels) between ANY two packed cells in a Wii
// baked atlas page -- must match bake-fonts.py's SHELF_PAD exactly (task #52).
// Bake-time transparent gutter between packed glyphs (bake-fonts.py SHELF_PAD).
static const float kAtlasGutterTexels = 2.0f;
// Hard ceiling for TryBakedGlyph/TryBakedEffectGlyph's UV overscan. It must be
// kAtlasGutterTexels - 1, NOT the full gutter: with BILINEAR filtering, a UV
// edge at +G texels blends the last gutter texel (+G-1) with the NEIGHBOUR's
// first ink texel (+G) -- so overscanning the FULL 2px gutter still bleeds a
// dark dash of the adjacent glyph (visible when BakedStringBox's shrink-to-fit
// pushes the ideal overscan =1/pxToWorld above the gutter). Clamping to G-1
// keeps both bilinear taps inside the transparent gutter. 1 texel is still
// ample headroom for the FinishMesh +1-world quad grow + hairline glyphs.
static const float kMaxOverscanTexels = kAtlasGutterTexels - 1.0f;

// Port specific (task #51): fill `out` from a prebaked IA8 atlas hit, matching
// the plain (effect==NONE) GlyphAtlasEntry the stb path would produce so
// BakedStringTTF / BakedStringBox / Font.cpp consume it unchanged. Returns
// false on a baked-store miss (caller falls through to stb).
//
// The baked atlas stores TIGHT FreeType bitmap rects (no baked-cell pad -- see
// tools/wii/prebaked-font-format.md), rasterised at the SUPERSAMPLED size
// (nativeSize*BAKE_SS, task #52), with a 2px transparent shelf gutter around each
// glyph. We reconstruct the cell contract with padL=padT=0 (the rect IS the cell)
// and sample the glyph rect UV with a +1 texel overscan into the gutter (see the
// OVERSCAN DECISION comment below), so ink registers at the same pen offset.
//
// SUPERSAMPLE SPLIT (mirrors the host kFontSupersample scheme): the atlas RECT
// (bg.x/y/w/h) is in supersampled texels and maps to a LOGICAL-size quad, so the
// magnification at the quad drops to 1/BAKE_SS => crisp after the EFB upscale.
// The METRIC px (bearing/advance) and the glyph's WORLD width/height are divided
// by bg.supersample to recover LOGICAL layout: layout is identical to the 1x
// bake, only atlas texel density grew. `metricScale = sizeScale / supersample`
// applies both the snap scale (requestedSize/nativeSize) and the SS divide.
//
// OVERSCAN DECISION (task #52, revised after on-device regression): the 1x path
// added +1 texel to the cell UV to mirror the dynamic path's +ss span. A FIRST
// pass removed the overscan entirely (tight rect, betting the 2px gutter alone
// gives clean edge AA) -- that DILUTED nothing, but on real GX hardware a
// hairline glyph (chonpu U+30FC, h=3 supersampled texels; hyphen, h~1-2) came
// out fully BLANK: a tight UV rect whose edges land exactly on bg.y/bg.y+bg.h
// leaves zero sampling headroom, and for a 2-3-texel-tall rect any hardware
// texel-center/edge-clamp rounding at the GX texture unit can walk the sampled
// range entirely off the ink rows. A generously-thick glyph never showed this
// (losing a fractional edge row is invisible against 20+ rows); a hairline has
// no margin to lose ANY row. Fix: restore a SMALL +1 texel overscan (not the old
// +1-into-a-1px-gutter, which diluted with only 1 texel of transparent margin
// past the ink) -- with SHELF_PAD=2 a +1 overscan still leaves 1 full texel of
// transparent gutter before the next glyph's cell, so there is no bleed, while
// giving the sampler the same +1 headroom the dynamic (stb/FreeType) NONE path
// always had. Combined with the 1.5x native resolution (more ink rows to begin
// with) this keeps thin bars both VISIBLE and not diluted.
bool FontCacheObjectTTF::TryBakedGlyph(uint32_t cp, float requestedSize,
                                       GlyphAtlasEntry* out) {
    if (!m_Atlas) return false;
    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    // Pick up language changes cheaply (no-op when unchanged).
    m_BakedWii->SetLanguage((int)game_work.languageFlag);

    BakedGlyphInfo bg;
    if (!m_BakedWii->Lookup(cp, requestedSize, &bg)) return false;

    memset(out, 0, sizeof(*out));

    const float inv   = m_Atlas->m_InvFontScale;
    // Baked at bg.nativeSize LOGICAL (bg.supersample x physically); requested at
    // requestedSize. Scale pixel metrics so the returned world-unit values match
    // the dynamic path at requestedSize.
    const float sizeScale = (bg.nativeSize > 0)
        ? (requestedSize / (float)bg.nativeSize) : 1.0f;
    const float ss = (bg.supersample > 0.0f) ? bg.supersample : 1.0f;
    // px -> world for a size-scaled SUPERSAMPLED-pixel metric quantity: divide by
    // ss to logicalise, scale by sizeScale for the snap, * inv to world.
    const float pxToWorld = inv * sizeScale / ss;

    const float fw = (float)bg.w;   // supersampled texels
    const float fh = (float)bg.h;

    // Legacy separate-bearing contract (BakedStringBox / Font.cpp). All px fields
    // are supersampled, so pxToWorld folds in the /ss divide -> logical world size.
    // width/height are set below (ink branch floors them at 1.0f; ink-less branch
    // leaves them at the natural 0).
    out->bearingX = (float)bg.bearingX * pxToWorld;
    out->bearingY = (float)bg.bearingY * pxToWorld;
    out->advanceX = (float)bg.advance  * pxToWorld;
    out->width    = fw * pxToWorld;   // overwritten (floored) below when bg.w/h > 0
    out->height   = fh * pxToWorld;   // overwritten (floored) below when bg.w/h > 0

    // Port specific (Wii invented prebaked atlas): bake-fonts.py stores each glyph
    // as a TIGHT bbox (ink at cell top-left), UNLIKE the binary's cell which folds
    // bearingX into the ink origin. Under the binary's tight pen model (pen step =
    // layoutX; ink drawn at penX - cellOriginX), the faithful "layoutX = advance -
    // bearingX, cellOriginX = 0" spaces ADJACENT INK by (advance - bearingX): CJK is
    // fine (bearingX ~= 0) but Latin/digits (bearingX > 0) crowd/overlap -- most
    // visibly with negative tracking (BakedStringBox m_Weight = -1, e.g. the
    // GameModeScreen "90 秒" plate). The binary avoids this because its atlas cell
    // already carries bearingX; the tight Wii atlas does not. So lay the tight atlas
    // out Font::DrawString-style instead: pen steps by FULL advance and the ink cell
    // is shifted RIGHT by bearingX (cellOriginX = -bearingX, in the ink branch below),
    // making adjacent ink spacing = the true advance. CJK is unchanged (bearingX ~= 0
    // => layoutX == advance, cellOriginX == 0). Wii-only (inside FRUIT_PLATFORM_WII).
    out->layoutX = (float)bg.advance * pxToWorld;
    out->layoutY = (float)(bg.bearingY - bg.h) * pxToWorld;

    if (bg.w > 0 && bg.h > 0 && bg.pageTextureID != 0 && bg.atlasDim > 0) {
        FontAtlasPage* page = BakedPageFor(bg.pageTextureID);
        out->page          = page;
        out->pageTextureID = bg.pageTextureID;

        const float invDim = 1.0f / (float)bg.atlasDim;
        // Cell UV overscan (task #52, base/effect ALIGNMENT fix): FinishMesh grows
        // EVERY drawable glyph's world quad by a FIXED +1.0f world unit on the far
        // edge (ASM-verified binary constant, independent of cellW/ss/pad). For the
        // overscanned UV edge to land EXACTLY on that +1.0f-grown quad edge (i.e.
        // for the true ink rect to be sampled WITHOUT stretching), the UV overscan
        // in TEXELS must be exactly `1.0f / pxToWorld` texels -- the texel count
        // that maps to precisely 1.0 world unit under this glyph's own pxToWorld.
        // A fixed "+1 texel" (this function's earlier revision) or a rounded "+ssi
        // texels" (the effect path's, ssi=round(ss)) both stretch the ink by a
        // few tenths of a world unit on the far edge -- harmless in isolation, but
        // base (which used +1) and effect (+ssi=2) stretched by DIFFERENT amounts,
        // so the two layers' ink no longer matched in size/position -> a
        // zero-offset shadow/stroke visibly misregistered by ~1 texel against the
        // base layer. Using the exact `uvOverscan` here (and the identical formula
        // in TryBakedEffectGlyph) makes the stretch ZERO in both paths, so they're
        // pixel-identical -- not just "close enough": the overscan formula depends
        // only on ss/inv/sizeScale, never on cellW or pad, so it's IDENTICAL for
        // base and effect on the same glyph at the same requestedSize.
        //
        // CLAMP (task #52, neighbour-bleed fix): `1.0f/pxToWorld` grows WITHOUT
        // BOUND as sizeScale shrinks (BakedStringBox's shrink-to-fit loop can
        // request well under the native size for a tight box, e.g. the MainScreen
        // "slice fruit" 3-line CJK plate) -- at sizeScale ~0.6-0.7 the exact
        // overscan exceeds kAtlasGutterTexels (2, == bake-fonts.py SHELF_PAD), the
        // ONLY gap the packer actually guarantees between neighbouring glyphs. Past
        // that, the overscanned UV samples real ink from the next glyph in the
        // atlas -- a dark dash/fragment a few texels below the baseline, worst on
        // glyphs whose packed neighbour-below happens to sit at exactly that
        // minimum 2-texel gap (observed: U+30A4 (i-katakana) at gap=2; U+3057
        // (shi) at gap=3 was unaffected at the same sizeScale). Clamping trades a
        // sub-texel ink stretch (imperceptible at the already-shrunk size that
        // triggers it) for guaranteed bleed-free sampling -- clamping affects base
        // and effect IDENTICALLY (same formula, same constant), so it cannot
        // reintroduce the base/effect misalignment fixed above.
        float uvOverscan = 1.0f / pxToWorld;   // texels; == ss/(inv*sizeScale)
        if (uvOverscan > kMaxOverscanTexels) uvOverscan = kMaxOverscanTexels;
        out->cellU0 = (float)bg.x * invDim;
        out->cellV0 = (float)bg.y * invDim;
        out->cellU1 = ((float)(bg.x + bg.w) + uvOverscan) * invDim;
        out->cellV1 = ((float)(bg.y + bg.h) + uvOverscan) * invDim;
        // Port specific: shift the tight-bbox ink RIGHT by bearingX so it lands at
        // penX + bearingX (FinishMesh draws ink at penX - cellOriginX). Combined with
        // layoutX = full advance above, adjacent ink is spaced by the true advance
        // (Font::DrawString-style) instead of the crowded advance-bearingX. See the
        // layoutX comment. CJK unaffected (bearingX ~= 0 => cellOriginX ~= 0).
        out->cellOriginX = -(float)bg.bearingX * pxToWorld;
        out->cellOriginY = 0.0f;   // padT = 0
        // DIFFERS: BakedStringTTF::FinishMesh (v1.6.1 @0x002480a8, ASM-verified) culls
        // any glyph whose world m_QuadSize is < 1.0 in either axis -- the binary's floor
        // for "has ink -> at least 1 drawable world unit", which FreeType's dynamic
        // rasterizer satisfies for free (a bitmap with ANY coverage always has >=1 row/
        // col at the size it was rasterised AT). The baked path instead rasterises ONCE
        // at BAKE_SS and divides by ss post-hoc, so a hairline (chonpu/hyphen, 1-3
        // supersampled texels) can cross below the 1.0-world floor purely from the /ss
        // divide -- something that never happens in the dynamic path because it always
        // re-rasterises AT pxToWorld's target size. Floor cellW/cellH (and the legacy
        // width/height, same consumers) at 1.0f whenever bg.w/bg.h > 0 (there IS ink) so
        // a supersample-induced sub-1.0 rounding can't defeat FinishMesh's own gate --
        // this restores the dynamic path's "ink never disappears" guarantee without
        // touching FinishMesh (which stays binary-faithful) or the bearing/advance/
        // layout position math (unfloored -- pure pen placement, not ink extent).
        out->cellW = fw * pxToWorld;
        out->cellH = fh * pxToWorld;
        if (out->cellW < 1.0f) out->cellW = 1.0f;
        if (out->cellH < 1.0f) out->cellH = 1.0f;
        out->width  = out->cellW;
        out->height = out->cellH;

        // Legacy UVs (Font.cpp / BakedStringBox legacy consumers): same exact
        // uvOverscan as the cell UVs above -- Font.cpp's quad also stays ink-tight
        // (width/height, unstretched) while the UV samples uvOverscan texels past
        // the ink edge into the gutter, matching the dynamic host path's
        // tight-quad/+ss-UV convention (here: +uvOverscan, the exact-world-unit
        // equivalent of the host's +ss).
        out->u0 = (float)bg.x * invDim;
        out->v0 = (float)bg.y * invDim;
        out->u1 = ((float)(bg.x + bg.w) + uvOverscan) * invDim;
        out->v1 = ((float)(bg.y + bg.h) + uvOverscan) * invDim;
    } else {
        // Ink-less glyph (space): no atlas sample; keep the advance/layout above.
        // page/pageTextureID stay 0, cell size stays 0 (draws nothing) -- matches
        // the dynamic path's empty-glyph legacy behaviour.
        out->cellW = 0.0f;
        out->cellH = 0.0f;
    }
    return true;
}

// Task #51/#52: build a BLUR/STROKE effect glyph from the baked coverage so the
// effect layer matches the NONE base layer's size + alignment. This mirrors the
// stb effect path's cell-build / filter / pack / entry-fill in GetGlyph EXACTLY
// -- the only change is the source coverage (baked un-tile, correct CJK size)
// and the metric source (baked SUPERSAMPLED-px records scaled by
// requestedSize/nativeSize and divided by BAKE_SS, same as TryBakedGlyph), so the
// effect entry registers with the NONE base entry at the same pen. The coverage
// is at supersampled resolution (task #52), so the LOGICAL cell pad and filter
// radius are scaled to supersampled texels (pad*ss, radius*ss) exactly like the
// host kFontSupersample path, then the world metrics divide back by ss.
bool FontCacheObjectTTF::TryBakedEffectGlyph(uint32_t cp, float requestedSize,
                                             FONT_EFFECT_ENUM effect, int radius,
                                             GlyphAtlasEntry* out) {
    if (!m_Atlas) {
        return false;
    }
    if (!m_BakedWii) {
        m_BakedWii = new BakedFontWii();
    }
    m_BakedWii->SetLanguage((int)game_work.languageFlag);

    BakedFontWii::GlyphCoverage cov;
    if (!m_BakedWii->GetGlyphCoverage(cp, requestedSize, &cov)) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    const float inv = m_Atlas->m_InvFontScale;
    const float sizeScale = (cov.nativeSize > 0)
        ? (requestedSize / (float)cov.nativeSize) : 1.0f;
    const float ss   = (cov.supersample > 0.0f) ? cov.supersample : 1.0f;
    const float pxToWorld = inv * sizeScale / ss;      // supersampled px -> logical world

    const float fw = (float)cov.w;   // supersampled texels
    const float fh = (float)cov.h;

    // Base (NONE) metrics -- identical to TryBakedGlyph so the effect layer's
    // pen matches the base layer's before the grown-pad shift is applied.
    out->bearingX = (float)cov.bearingX * pxToWorld;
    out->bearingY = (float)cov.bearingY * pxToWorld;
    out->advanceX = (float)cov.advance  * pxToWorld;
    out->width    = fw * pxToWorld;
    out->height   = fh * pxToWorld;
    // Port specific: match TryBakedGlyph's tight-bbox layout (full advance pen step;
    // ink shifted right by bearingX via cellOriginX below) so the EFFECT layer lands
    // on the SAME pen positions as the BASE layer -- otherwise the two layers diverge
    // (base shifted, effect not). See TryBakedGlyph's layoutX comment.
    out->layoutX  = (float)cov.advance * pxToWorld;
    out->layoutY  = (float)(cov.bearingY - cov.h)       * pxToWorld;

    // Per-effect cell pad, in LOGICAL px (same rule + binary constants as the stb
    // path). The cell is built at supersampled texel resolution, so pads/radius
    // scale to texels (padLT/padTT/radiusT) exactly like the host ss path; the
    // cellOrigin/bearing shifts are derived from padLT/padTT*pxToWorld (the
    // ACTUAL rounded texel pad), not padL/padT*inv -- see the cellOrigin comment
    // below for why (ssi rounding vs the exact fractional ss).
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

    // Round the LOGICAL pads/radius up to whole supersampled texels for the cell
    // (the cell BUFFER needs integer texel dims to allocate/blit into -- this
    // rounding only affects buffer size, not the UV overscan, see uvOverscan below).
    const int ssi     = (ss > 0.0f) ? (int)(ss + 0.5f) : 1;  // >= 1; 1.5 -> 2 texels/logical-px
    const int padLT   = padL * ssi;
    const int padTT   = padT * ssi;
    const int radiusT = radius * ssi;
    // Base/effect ALIGNMENT fix (task #52): see TryBakedGlyph's uvOverscan comment
    // for the full derivation. FinishMesh grows every quad by a fixed +1.0f world
    // unit regardless of cellW/pad/ss; the UV overscan therefore must be exactly
    // `1.0f/pxToWorld` texels (not the rounded ssi) for the overscanned UV edge to
    // land exactly on that +1.0f-grown quad edge -- otherwise the visible ink
    // stretches by a different amount than the base layer's, misregistering a
    // zero-offset effect against the base. This formula depends only on
    // ss/inv/sizeScale (never on cellW/pad), so it is IDENTICAL to TryBakedGlyph's
    // uvOverscan for the same glyph at the same requestedSize. Clamped to
    // kAtlasGutterTexels for the same neighbour-bleed reason (see TryBakedGlyph) --
    // the clamp constant matches exactly, so base and effect still clamp together.
    float uvOverscan = 1.0f / pxToWorld;   // texels; == ss/(inv*sizeScale)
    if (uvOverscan > kMaxOverscanTexels) uvOverscan = kMaxOverscanTexels;

    const int   pageSize = m_Atlas->GetSize();
    const float invS     = 1.0f / (float)pageSize;

    if (cov.w > 0 && cov.h > 0) {
        const int cellW = cov.w + 2 * padLT;
        const int cellH = cov.h + 2 * padTT;

        std::vector<uint8_t> cell((size_t)cellW * (size_t)cellH, 0);
        for (int row = 0; row < cov.h; row++) {
            memcpy(&cell[(size_t)(row + padTT) * cellW + padLT],
                   &cov.alpha[(size_t)row * cov.w],
                   (size_t)cov.w);
        }
        if (e == FONT_EFFECT_BLUR && radiusT > 0) {
            BuildBlur(&cell[0], cellW, cellH, radiusT);
        } else if (e == FONT_EFFECT_STROKE && radiusT > 0) {
            BuildStrokes(&cell[0], cellW, cellH, radiusT);
        }
        // BEVEL (4..11): +4/+4 pad (logical), no filter (BuildBevel is an empty stub).

        int x = 0, y = 0;
        FontAtlasPage* page = m_Atlas->PackGlyphCell(cellW, cellH, &cell[0], &x, &y);
        out->page          = page;
        out->pageTextureID = page ? page->m_TextureID : 0;

        // Cell UVs: +uvOverscan (exact, not the rounded ssi) texel span into the
        // packer's transparent gutter, matching FinishMesh's fixed +1.0f world-unit
        // quad growth with zero ink stretch (see uvOverscan comment above). The
        // packer keeps a >=2px gutter (SHELF_PAD=2 >= ceil(uvOverscan)=2) so this
        // stays bleed-free.
        out->cellU0 = (float)x * invS;
        out->cellV0 = (float)y * invS;
        out->cellU1 = out->cellU0 + ((float)cellW + uvOverscan) * invS;
        out->cellV1 = out->cellV0 + ((float)cellH + uvOverscan) * invS;
        // cellOrigin MUST be the exact WORLD size of the padLT/padTT TEXEL buffer
        // region, i.e. padLT*pxToWorld -- NOT padL*inv. On the host those are
        // identical because ss is a clean integer that cancels exactly
        // (padLT=padL*ss, invLogical=inv/ss => padLT*invLogical == padL*inv). On
        // Wii ss=1.5 is fractional, so the cell buffer is sized with the ROUNDED
        // ssi=2 (padLT=padL*ssi), while padL*inv silently assumes the UNROUNDED
        // ss -- a 0.33-world-unit mismatch between the padded cell's actual size
        // and its claimed origin, which is what misregistered the effect layer
        // against the base layer's pen (base has no pad, so it was buffer-size-
        // rounding-immune; this only bit the padded/effect side). Deriving
        // cellOrigin from padLT (the same integer the buffer/UV overscan use)
        // keeps ink position self-consistent regardless of the ssi rounding.
        // Port specific: cellOrigin normally = padLT (effect pad) so the padded cell's
        // ink registers at the pen. Additionally subtract bearingX (in world) so the
        // ink lands at penX + bearingX, matching the base layer's tight-bbox shift
        // (TryBakedGlyph cellOriginX = -bearingX). FinishMesh draws at penX - cellOriginX,
        // so cellOriginX = padLT - bearingX places effect ink at penX + bearingX. Keeps
        // base+effect layers coincident. CJK unaffected (bearingX ~= 0).
        out->cellOriginX = (float)padLT * pxToWorld - (float)cov.bearingX * pxToWorld;
        out->cellOriginY = (float)padTT * pxToWorld;
        out->cellW       = (float)cellW * pxToWorld;  // supersampled texels -> logical world
        out->cellH       = (float)cellH * pxToWorld;

        // Grown legacy contract (STROKE/BLUR/BEVEL): quad = whole cell, bearings
        // shifted by the pad so the effect registers with the sharp glyph at the
        // same pen -- identical to the stb effect path's `grown` branch. Uses
        // padLT/padTT*pxToWorld (the actual rounded buffer pad), matching
        // cellOrigin above, NOT padL*inv -- same ssi-rounding reasoning.
        out->u0 = (float)x * invS;
        out->v0 = (float)y * invS;
        out->u1 = (float)(x + cellW) * invS;
        out->v1 = (float)(y + cellH) * invS;
        out->bearingX -= (float)padLT * pxToWorld;
        out->bearingY += (float)padTT * pxToWorld;
        out->width  = out->cellW;
        out->height = out->cellH;
    } else {
        // Ink-less glyph (space): nothing to filter. Keep advance/layout; the
        // pad still defines a (transparent) cell so callers see a valid entry.
        const int cellW = 2 * padLT > 0 ? 2 * padLT : 1;
        const int cellH = 2 * padTT > 0 ? 2 * padTT : 1;
        std::vector<uint8_t> cell((size_t)cellW * (size_t)cellH, 0);
        int x = 0, y = 0;
        FontAtlasPage* page = m_Atlas->PackGlyphCell(cellW, cellH, &cell[0], &x, &y);
        out->page          = page;
        out->pageTextureID = 0;   // no ink
        out->cellU0 = (float)x * invS;
        out->cellV0 = (float)y * invS;
        out->cellU1 = out->cellU0 + ((float)cellW + uvOverscan) * invS;
        out->cellV1 = out->cellV0 + ((float)cellH + uvOverscan) * invS;
        // cellOrigin from padLT/padTT (matches the ink branch above) -- no visible
        // ink here (space), but keeps the ink-less cell's declared size consistent
        // with the buffer it was actually packed at.
        out->cellOriginX = (float)padLT * pxToWorld;
        out->cellOriginY = (float)padTT * pxToWorld;
        out->cellW = (float)cellW * pxToWorld;
        out->cellH = (float)cellH * pxToWorld;
    }
    return true;
}
#endif // FRUIT_PLATFORM_WII

} // namespace Mortar
