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

    // Render text string with full formatting support
    // Matches Font::DrawString (0x00198e44)
    // `scale` here is a per-glyph multiplier (atlas-pixel * scale = world size).
    // Supports: inline color tags [FFFFFF]text[/], word wrapping, alignment
    void DrawString(float scale, float maxWidth, float z,
                    const char* text, const Vec3& pos,
                    const Colour& colour, int alignment = 0);

    // Matches Font::Font_DrawString overload — `targetSize` is the desired
    // em line-height in world units; internally divides by m_LineHeight to
    // get the per-glyph multiplier. Used by ShopListItem and other UI
    // widgets that want "render at N pixels tall" semantics.
    void DrawStringSized(float targetSize, float maxWidth, float z,
                         const char* text, const Vec3& pos,
                         const Colour& colour, int alignment = 0) {
        float mul = (m_LineHeight > 0) ? (targetSize / (float)m_LineHeight) : targetSize;
        DrawString(mul, maxWidth, z, text, pos, colour, alignment);
    }

    // Measure text width without rendering
    float MeasureWidth(float scale, const char* text) const;

    // Get line height at given scale
    float GetLineHeight(float scale) const { return (float)m_LineHeight * scale * m_Scale; }

private:
    // Load atlas textures for all pages
    void LoadPageTextures(const std::string& basePath);
};

} // namespace Mortar

#endif
