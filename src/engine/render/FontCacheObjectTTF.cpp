#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

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
    m_Atlas = new FontInterface(512);
    // Mirror binary Initialize @ 0x00250470: fontScale=1.0, globalSizeScale=1.0.
    // Caller may invoke InitialiseData again with a language-specific globalSizeScale.
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
// ASM-verified: 2026-06-14T00:00Z binary @ 0x0024f568,0x002502e0,0x00250470 (asm-inspector)
static long ComputeCharHeight26_6(float requestedSize,
                                  float globalSizeScale,
                                  float fontScale) {
    float scaledHeight = requestedSize * globalSizeScale;
    float raw = scaledHeight * fontScale * 64.0f;
    if (raw < 0.0f) raw = 0.0f;
    return (long)raw; // trunc toward zero
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

const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t cp, float requestedSize) {
    if (!m_Face || !m_Atlas) return nullptr;

    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);

    GlyphCacheKey key;
    key.codepoint      = cp;
    key.charHeight26_6 = ch26;

    std::map<GlyphCacheKey, GlyphAtlasEntry>::iterator it = m_Cache.find(key);
    if (it != m_Cache.end()) {
        return &it->second;
    }

    if (!SetCharSize(ch26)) return nullptr;

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
    const float inv = m_Atlas->m_InvFontScale * (1.0f / 64.0f);

    GlyphAtlasEntry entry;
    entry.bearingX = (float)slot->metrics.horiBearingX * inv;
    entry.bearingY = (float)slot->metrics.horiBearingY * inv;
    entry.advanceX = (float)slot->advance.x            * inv;
    entry.width    = (float)bm.width;
    entry.height   = (float)bm.rows;

    if (bm.width > 0 && bm.rows > 0) {
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
        bool packed = m_Atlas->PackGlyph((int)bm.width, (int)bm.rows, src, &entry);
        if (compact) free(compact);
        if (!packed) {
            LOG_ERROR("FontCacheObjectTTF", "atlas full, glyph cp=%u dropped", cp);
            return nullptr;
        }
    } else {
        entry.u0 = entry.v0 = entry.u1 = entry.v1 = 0.0f;
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
    if (!SetCharSize(ch26)) return 0.0f;

    FT_UInt idxA = FT_Get_Char_Index(m_Face, (FT_ULong)a);
    FT_UInt idxB = FT_Get_Char_Index(m_Face, (FT_ULong)b);
    if (idxA == 0 || idxB == 0) return 0.0f;

    FT_Vector kern;
    FT_Error err = FT_Get_Kerning(m_Face, idxA, idxB, FT_KERNING_DEFAULT, &kern);
    if (err) return 0.0f;
    // kern.x is 26.6; convert to world units.
    return (float)kern.x * m_Atlas->m_InvFontScale * (1.0f / 64.0f);
}

float FontCacheObjectTTF::GetAscender(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    if (!m_Face || !SetCharSize(ch26)) return requestedSize;
    return (float)m_Face->size->metrics.ascender
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f);
}

float FontCacheObjectTTF::GetDescender(float requestedSize) {
    if (!m_Atlas) return 0.0f;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    if (!m_Face || !SetCharSize(ch26)) return 0.0f;
    return (float)m_Face->size->metrics.descender
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f);
}

float FontCacheObjectTTF::GetLineHeight(float requestedSize) {
    if (!m_Atlas) return requestedSize;
    long ch26 = ComputeCharHeight26_6(requestedSize,
                                      m_Atlas->m_GlobalSizeScale,
                                      m_Atlas->m_FontScale);
    if (!m_Face || !SetCharSize(ch26)) return requestedSize;
    return (float)m_Face->size->metrics.height
           * m_Atlas->m_InvFontScale * (1.0f / 64.0f);
}

} // namespace Mortar
