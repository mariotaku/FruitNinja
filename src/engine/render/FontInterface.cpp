#include "render/FontInterface.h"
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>

namespace Mortar {

// ASM-verified: 2026-06-14T00:00Z binary @ 0x0024f568,0x002502e0,0x00250470 (asm-inspector)
FontInterface::FontInterface(int atlasSize)
    : m_CacheSize(100)
    , m_FontScale(1.0f)
    , m_InvFontScale(1.0f)
    , m_GlobalSizeScale(1.0f)
    , m_Size(atlasSize)
    , m_Pixels(nullptr)
    , m_TextureID(0)
    , m_CursorX(0)
    , m_CursorY(0)
    , m_RowHeight(0)
    , m_Dirty(false)
    , m_DirtyX0(0), m_DirtyY0(0)
    , m_DirtyX1(0), m_DirtyY1(0)
{
    m_Pixels = (uint8_t*)calloc((size_t)(m_Size * m_Size), 1);
    EnsureTexture();
}

// Mirrors binary Initialize @ 0x00250470.
// fontScale and invFontScale are always 1.0 in practice;
// globalSizeScale is 0.9 only for Korean (language byte 0x13).
void FontInterface::InitialiseData(float fontScale, float globalSizeScale) {
    m_FontScale      = fontScale;
    m_InvFontScale   = (fontScale != 0.0f) ? (1.0f / fontScale) : 1.0f;
    m_GlobalSizeScale = globalSizeScale;
}

FontInterface::~FontInterface() {
    Clear();
}

void FontInterface::Clear() {
    if (m_TextureID) {
        glDeleteTextures(1, &m_TextureID);
        m_TextureID = 0;
    }
    free(m_Pixels);
    m_Pixels    = nullptr;
    m_CursorX   = 0;
    m_CursorY   = 0;
    m_RowHeight = 0;
    m_Dirty     = false;
}

void FontInterface::EnsureTexture() {
    if (m_TextureID) return;
    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Byte-aligned unpack for single-channel glyph data (see BuildPendingTextures).
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Allocate the full atlas as an alpha-only texture.
    // GL_ALPHA works on both desktop GL (compat) and WebGL/GLES2.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, m_Size, m_Size, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, m_Pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool FontInterface::PackGlyph(int width, int height, const uint8_t* bitmap,
                              GlyphAtlasEntry* out) {
    if (!m_Pixels) return false;
    // 1-pixel padding between glyphs to avoid filtering bleed.
    const int padX = 1, padY = 1;

    // Advance to next row if this glyph doesn't fit horizontally.
    if (m_CursorX + width + padX > m_Size) {
        m_CursorX  = 0;
        m_CursorY += m_RowHeight + padY;
        m_RowHeight = 0;
    }
    // Atlas full?
    if (m_CursorY + height > m_Size) {
        LOG_ERROR("FontInterface", "glyph atlas full (%dx%d)", m_Size, m_Size);
        return false;
    }

    // Copy glyph bitmap into the atlas CPU buffer.
    if (bitmap && width > 0 && height > 0) {
        for (int row = 0; row < height; row++) {
            uint8_t* dst = m_Pixels + (m_CursorY + row) * m_Size + m_CursorX;
            const uint8_t* src = bitmap + row * width;
            memcpy(dst, src, (size_t)width);
        }
    }

    MarkDirty(m_CursorX, m_CursorY, width, height);

    const float invS = 1.0f / (float)m_Size;
    out->u0 = (float)m_CursorX         * invS;
    out->v0 = (float)m_CursorY         * invS;
    out->u1 = (float)(m_CursorX + width)  * invS;
    out->v1 = (float)(m_CursorY + height) * invS;

    m_CursorX += width + padX;
    if (height > m_RowHeight) m_RowHeight = height;

    return true;
}

void FontInterface::BuildPendingTextures() {
    if (!m_Dirty || !m_Pixels || !m_TextureID) return;

    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    // Single-channel (GL_ALPHA) glyph rows are tightly packed at dw-byte stride.
    // GL_UNPACK_ALIGNMENT defaults to 4, which would mis-stride every row when dw
    // is not a multiple of 4 -> sheared/garbled glyphs. Force byte alignment.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Upload only the dirty rectangle.
    const int dw = m_DirtyX1 - m_DirtyX0;
    const int dh = m_DirtyY1 - m_DirtyY0;
    if (dw > 0 && dh > 0) {
        // Extract dirty rows into a contiguous temporary buffer.
        uint8_t* tmp = (uint8_t*)malloc((size_t)(dw * dh));
        if (tmp) {
            for (int row = 0; row < dh; row++) {
                memcpy(tmp + row * dw,
                       m_Pixels + (m_DirtyY0 + row) * m_Size + m_DirtyX0,
                       (size_t)dw);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            m_DirtyX0, m_DirtyY0, dw, dh,
                            GL_ALPHA, GL_UNSIGNED_BYTE, tmp);
            free(tmp);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    m_Dirty = false;
}

void FontInterface::MarkDirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!m_Dirty) {
        m_DirtyX0 = x;
        m_DirtyY0 = y;
        m_DirtyX1 = x + w;
        m_DirtyY1 = y + h;
        m_Dirty   = true;
    } else {
        if (x < m_DirtyX0)     m_DirtyX0 = x;
        if (y < m_DirtyY0)     m_DirtyY0 = y;
        if (x + w > m_DirtyX1) m_DirtyX1 = x + w;
        if (y + h > m_DirtyY1) m_DirtyY1 = y + h;
    }
}

} // namespace Mortar
