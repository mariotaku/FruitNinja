#ifndef MORTAR_FONT_H
#define MORTAR_FONT_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <vector>
#include <string>

namespace Mortar {

// Alignment flags for DrawString (0x0F mask)
enum FontAlignment {
    FONT_ALIGN_LEFT   = 0x00,
    FONT_ALIGN_CENTER = 0x01,
    FONT_ALIGN_RIGHT  = 0x02,
    FONT_ALIGN_TOP    = 0x00,
    FONT_ALIGN_MIDDLE = 0x04,
    FONT_ALIGN_BOTTOM = 0x08
};

// Glyph entry parsed from BMFont .fnt
struct FontGlyph {
    int id;
    int x, y;           // position in atlas
    int width, height;   // glyph size
    int xoffset, yoffset; // rendering offset
    int xadvance;        // cursor advance
    int page;            // atlas page index
};

// Matches original Font (~0x430 bytes)
// BMFont .fnt text format loader + DrawString renderer
class Font : public ReferenceCounter {
public:
    FontGlyph m_Glyphs[256]; // 256 glyph entries (ASCII)
    int m_PageCount;
    int m_LineHeight;
    int m_Base;
    int m_ScaleW, m_ScaleH; // atlas dimensions
    float m_Scale;           // scale factor
    std::vector<SmartPtr<Texture>> m_PageTextures;

    Font();
    virtual ~Font();

    // Parse BMFont .fnt text format
    // Matches Font::Load (0x00199e9c)
    static SmartPtr<Font> Load(const char* path);

    // Render text string with full formatting support.
    // Matches Font_DrawString (0x00198e44).
    // `scale` is the em size in world units (e.g. 20.0, 25.0). It is applied
    // as MatrixStack::Scale(scale, scale, 1.0); glyph vertices are stored in
    // normalized atlas-pixel units (atlas_px / scaleW/H). NO division by
    // m_LineHeight is performed (binary confirmed).
    // Supports: inline color tags [FFFFFF]text[/], word wrapping, alignment.
    void DrawString(float scale, float maxWidth, float z,
                    const char* text, const Vec3& pos,
                    const Colour& colour, int alignment = 0);

    // Thin alias -- passes targetSize directly as the scale parameter.
    // Binary: DrawString (0x00199aa0) wrapper; scale = raw em pixel size,
    // NO division by m_LineHeight (that division was a port error, now removed).
    void DrawStringSized(float targetSize, float maxWidth, float z,
                         const char* text, const Vec3& pos,
                         const Colour& colour, int alignment = 0) {
        DrawString(targetSize, maxWidth, z, text, pos, colour, alignment);
    }

    // Returns normalized text width (atlas-pixel units / m_ScaleW).
    // Multiply by scale to get world-unit width.
    // `scale` parameter is unused (kept for API compatibility).
    float MeasureWidth(float scale, const char* text) const;

    // Get line height in world units at given scale.
    float GetLineHeight(float scale) const { return (float)m_LineHeight * (1.0f / (float)(m_ScaleH > 0 ? m_ScaleH : 1)) * scale; }

private:
    // Load atlas textures for all pages
    void LoadPageTextures(const std::string& basePath);
};

} // namespace Mortar

#endif
