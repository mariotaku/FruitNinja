#include "render/FontCacheObjectTTF.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>

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
    , m_CurrentSize(-1)
    , m_Atlas(nullptr)
{
    FT_Error err = FT_New_Face(ftLib, path, 0, &m_Face);
    if (err) {
        LOG_ERROR("FontCacheObjectTTF", "FT_New_Face failed for '%s' (err %d)", path, err);
        m_Face = nullptr;
        return;
    }
    m_Atlas = new FontInterface(512);
}

FontCacheObjectTTF::~FontCacheObjectTTF() {
    if (m_Face) {
        FT_Done_Face(m_Face);
        m_Face = nullptr;
    }
    delete m_Atlas;
    m_Atlas = nullptr;
}

bool FontCacheObjectTTF::SetPixelSize(int pixelSize) {
    if (pixelSize == m_CurrentSize) return true;
    FT_Error err = FT_Set_Pixel_Sizes(m_Face, 0, (FT_UInt)pixelSize);
    if (err) {
        LOG_ERROR("FontCacheObjectTTF", "FT_Set_Pixel_Sizes(%d) failed (err %d)",
                  pixelSize, err);
        return false;
    }
    m_CurrentSize = pixelSize;
    return true;
}

const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t cp, int pixelSize) {
    if (!m_Face || !m_Atlas) return nullptr;

    GlyphCacheKey key;
    key.codepoint = cp;
    key.pixelSize = pixelSize;

    std::map<GlyphCacheKey, GlyphAtlasEntry>::iterator it = m_Cache.find(key);
    if (it != m_Cache.end()) {
        return &it->second;
    }

    // Not cached yet: render with FreeType.
    if (!SetPixelSize(pixelSize)) return nullptr;

    FT_UInt glyphIndex = FT_Get_Char_Index(m_Face, (FT_ULong)cp);
    if (glyphIndex == 0) {
        // Codepoint not in font (glyph index 0 = .notdef / missing glyph).
        // Insert a null entry so we don't retry each frame.
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

    GlyphAtlasEntry entry;
    entry.bearingX = slot->bitmap_left;
    entry.bearingY = slot->bitmap_top;
    entry.advanceX = (int)(slot->advance.x >> 6);
    entry.width    = (int)bm.width;
    entry.height   = (int)bm.rows;

    if (bm.width > 0 && bm.rows > 0) {
        // FreeType renders top-down; bitmap.buffer layout matches GL (row-major).
        // For PIXEL_MODE_GRAY the stride (pitch) may differ from width.
        const uint8_t* src = bm.buffer;
        // Compact to width x rows if pitch != width.
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
        // Whitespace / zero-size glyph — no bitmap to pack.
        entry.u0 = entry.v0 = entry.u1 = entry.v1 = 0.0f;
    }

    m_Cache[key] = entry;
    return &m_Cache[key];
}

float FontCacheObjectTTF::GetKerningForPair(uint32_t a, uint32_t b, int pixelSize) {
    if (!m_Face) return 0.0f;
    if (!FT_HAS_KERNING(m_Face)) return 0.0f;
    if (!SetPixelSize(pixelSize)) return 0.0f;

    FT_UInt idxA = FT_Get_Char_Index(m_Face, (FT_ULong)a);
    FT_UInt idxB = FT_Get_Char_Index(m_Face, (FT_ULong)b);
    if (idxA == 0 || idxB == 0) return 0.0f;

    FT_Vector kern;
    FT_Error err = FT_Get_Kerning(m_Face, idxA, idxB, FT_KERNING_DEFAULT, &kern);
    if (err) return 0.0f;
    return (float)(kern.x >> 6);
}

int FontCacheObjectTTF::GetAscender(int pixelSize) {
    if (!m_Face || !SetPixelSize(pixelSize)) return pixelSize;
    return (int)(m_Face->size->metrics.ascender >> 6);
}

int FontCacheObjectTTF::GetDescender(int pixelSize) {
    if (!m_Face || !SetPixelSize(pixelSize)) return 0;
    return (int)(m_Face->size->metrics.descender >> 6);
}

int FontCacheObjectTTF::GetLineHeight(int pixelSize) {
    if (!m_Face || !SetPixelSize(pixelSize)) return pixelSize;
    return (int)(m_Face->size->metrics.height >> 6);
}

} // namespace Mortar
