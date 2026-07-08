#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

// Pull in FreeType headers only in this translation unit.
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

namespace Mortar {

FontCacheObjectTTF::FontCacheObjectTTF(FT_Library ftLib, const char* path,
                                       int defaultPixelSize)
    : m_FTLib(ftLib)
    , m_Face(nullptr)
    , m_DefaultPixelSize(defaultPixelSize)
    , m_CurrentCharHeight(-1)
    , m_Atlas(nullptr)
{
    FT_Error err = FT_New_Face(ftLib, path, 0, &m_Face);
    if (err) {
        LOG_ERROR("FontCacheObjectTTF", "FT_New_Face failed for '%s' (err %d)", path, err);
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
    if (m_Face) {
        FT_Done_Face(m_Face);
        m_Face = nullptr;
    }
    delete m_Atlas;
    m_Atlas = nullptr;
}

// Compute the FT 26.6 char height from the raw requestedSize and atlas scale factors.
// Binary SetFontSize @ 0x0024f568:
//   scaledHeight = requestedSize * m_GlobalSizeScale
//   char_height_26.6 = trunc(max(0, scaledHeight * m_FontScale * 64.0))
//   FT_Set_Char_Size(face, 0, char_height_26.6, 0, m_CacheSize)
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
    if (!m_Atlas) return false;
    FT_Error err = FT_Set_Char_Size(m_Face,
                                    /*char_width*/0,
                                    (FT_F26Dot6)charHeight_26_6,
                                    /*horz_res*/0,
                                    /*vert_res(dpi)*/(FT_UInt)m_Atlas->m_CacheSize);
    if (err) {
        LOG_ERROR("FontCacheObjectTTF",
                  "FT_Set_Char_Size(0,%ld,0,%d) failed (err %d)",
                  charHeight_26_6, m_Atlas->m_CacheSize, err);
        return false;
    }
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
    // Ask FreeType to rasterize at kFontSupersample x the logical size so the atlas holds hi-res glyph bitmaps.
    // The cache key stays at the logical ch26 so callers sharing a logical size share the same cache entry.
    if (!SetCharSize(ch26 * (long)kFontSupersample)) return nullptr;

    FT_UInt glyphIndex = FT_Get_Char_Index(m_Face, (FT_ULong)cp);
    if (glyphIndex == 0) {
        GlyphAtlasEntry empty;
        memset(&empty, 0, sizeof(empty));
        m_Cache[key] = empty;
        return nullptr;
    }

    FT_Error err = FT_Load_Glyph(m_Face, glyphIndex,
                                  FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
    if (err) {
        LOG_ERROR("FontCacheObjectTTF", "FT_Load_Glyph cp=%u err=%d", cp, err);
        return nullptr;
    }

    FT_GlyphSlot slot = m_Face->glyph;
    FT_Bitmap&   bm   = slot->bitmap;

    // Convert all metrics to world units: FT 26.6 value * (1/64) * invFontScale.
    // With invFontScale=1.0 this is simply metric_26.6 / 64.0.
    // Port specific: HD font supersampling -- divide by kFontSupersample so layout dimensions are
    // identical to the non-supersampled case (the atlas holds Nx texels but quads are logical-size).
    const float inv = m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);

    GlyphAtlasEntry entry;
    entry.bearingX = (float)slot->metrics.horiBearingX * inv;
    entry.bearingY = (float)slot->metrics.horiBearingY * inv;
    entry.advanceX = (float)slot->advance.x            * inv;
    // Port specific: HD font supersampling -- width/height are raw atlas pixels (used for atlas packing);
    // divide by kFontSupersample to restore the logical quad dimensions used by BakedStringBox.
    entry.width    = (float)bm.width  / (float)kFontSupersample;
    entry.height   = (float)bm.rows   / (float)kFontSupersample;

    if (bm.width > 0 && bm.rows > 0) {
        if (effect == FONT_EFFECT_BLUR && radius > 0) {
            // ASM-spec v1.6.1 RenderGlyph @0x0024f5dc (effect==2 BLUR):
            //   padL = radiusTexel+1, padT = radiusTexel+2 each side;
            //   buffer W = glyphW + 2*(radiusTexel+1), H = glyphH + 2*(radiusTexel+2);
            //   blit sharp glyph at (padL, padT); then BuildBlur(buf, W, H, radiusTexel).
            // radius (the param) is LOGICAL (pre-supersample) px; the port rasters
            // at kFontSupersample x, so the texel-space radius used for the pad and
            // the filter kernel is radius * kFontSupersample.
            const int radiusTexel = radius * kFontSupersample;
            const int padL = radiusTexel + 1;
            const int padT = radiusTexel + 2;
            const int padW = (int)bm.width + 2 * padL;
            const int padH = (int)bm.rows  + 2 * padT;

            std::vector<uint8_t> padded((size_t)padW * (size_t)padH, 0);
            for (unsigned int row = 0; row < bm.rows; row++) {
                memcpy(&padded[(size_t)(row + (unsigned int)padT) * padW + padL],
                       bm.buffer + row * bm.pitch,
                       bm.width);
            }
            BuildBlur(&padded[0], padW, padH, radiusTexel);

            // Grow bearing/size so the blurred (padded) quad registers with the sharp
            // glyph at the same pen position: bearingX-=padL, bearingY+=padT,
            // width/height += 2*pad -- all converted back to world/logical units.
            entry.bearingX -= (float)padL / (float)kFontSupersample;
            entry.bearingY += (float)padT / (float)kFontSupersample;
            entry.width     = (float)padW / (float)kFontSupersample;
            entry.height    = (float)padH / (float)kFontSupersample;

            // DIFFERS: binary TextureAtlas @0x00269c9c, faithful multi-page model.
            m_Atlas->PackGlyph(padW, padH, &padded[0], &entry);
        } else if (effect == FONT_EFFECT_STROKE && radius > 0) {
            // ASM-spec v1.6.1 RenderGlyph @0x0024f5dc effect==1: pad = BLUR pad (padL=R+1, padT=R+2); grow bearing/size same as BLUR branch.
            const int radiusTexel = radius * kFontSupersample;
            const int padL = radiusTexel + 1;
            const int padT = radiusTexel + 2;
            const int padW = (int)bm.width + 2 * padL;
            const int padH = (int)bm.rows  + 2 * padT;

            std::vector<uint8_t> padded((size_t)padW * (size_t)padH, 0);
            for (unsigned int row = 0; row < bm.rows; row++) {
                memcpy(&padded[(size_t)(row + (unsigned int)padT) * padW + padL],
                       bm.buffer + row * bm.pitch,
                       bm.width);
            }
            BuildStrokes(&padded[0], padW, padH, radiusTexel);

            entry.bearingX -= (float)padL / (float)kFontSupersample;
            entry.bearingY += (float)padT / (float)kFontSupersample;
            entry.width     = (float)padW / (float)kFontSupersample;
            entry.height    = (float)padH / (float)kFontSupersample;

            // DIFFERS: binary TextureAtlas @0x00269c9c, faithful multi-page model.
            m_Atlas->PackGlyph(padW, padH, &padded[0], &entry);
        } else {
            const uint8_t* src = bm.buffer;
            uint8_t* compact = nullptr;
            if (bm.pitch != (int)bm.width) {
                compact = (uint8_t*)malloc((size_t)(bm.width * bm.rows));
                if (compact) {
                    for (unsigned int row = 0; row < bm.rows; row++) {
                        memcpy(compact + row * bm.width,
                               bm.buffer + row * bm.pitch,
                               bm.width);
                    }
                    src = compact;
                }
            }
            // PackGlyph always succeeds: allocates a new atlas page if the current one
            // is full. DIFFERS: binary TextureAtlas @0x00269c9c, faithful multi-page model.
            m_Atlas->PackGlyph((int)bm.width, (int)bm.rows, src, &entry);
            if (compact) free(compact);
        }
    } else {
        entry.u0 = entry.v0 = entry.u1 = entry.v1 = 0.0f;
        entry.pageTextureID = 0;
    }

    m_Cache[key] = entry;
    return &m_Cache[key];
}

float FontCacheObjectTTF::GetKerningForPair(uint32_t a, uint32_t b, float requestedSize) {
    if (!m_Face) return 0.0f;
    if (!FT_HAS_KERNING(m_Face)) return 0.0f;
    if (!m_Atlas) return 0.0f;

    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size to match GetGlyph.
    if (!SetCharSize(ch26 * (long)kFontSupersample)) return 0.0f;

    FT_UInt idxA = FT_Get_Char_Index(m_Face, (FT_ULong)a);
    FT_UInt idxB = FT_Get_Char_Index(m_Face, (FT_ULong)b);
    if (idxA == 0 || idxB == 0) return 0.0f;

    FT_Vector kern;
    FT_Error err = FT_Get_Kerning(m_Face, idxA, idxB, FT_KERNING_DEFAULT, &kern);
    if (err) return 0.0f;
    // kern.x is 26.6; convert to world units and divide by N to restore logical scale.
    // Port specific: HD font supersampling -- divide by kFontSupersample.
    return (float)kern.x * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetAscender(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->size->metrics.ascender
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetDescender(float requestedSize) {
    if (!m_Atlas) return 0.0f;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return 0.0f;
    return (float)m_Face->size->metrics.descender
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

float FontCacheObjectTTF::GetLineHeight(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    // Port specific: HD font supersampling -- set face at Nx size; divide result by N.
    if (!m_Face || !SetCharSize(ch26 * (long)kFontSupersample)) return requestedSize;
    return (float)m_Face->size->metrics.height
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f) * (1.0f / (float)kFontSupersample);
}

} // namespace Mortar
