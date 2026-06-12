#ifndef FN_ENGINE_RENDER_BAKEDSTRINGBOX_H
#define FN_ENGINE_RENDER_BAKEDSTRINGBOX_H

// BakedStringBox — TTF-backed wrapped text label with shrink-to-fit and
// rotated draw. Port specific: the binary uses the Bada IFont/IGlyphCache
// path for this class. The port reimplements it over FontCacheObjectTTF.
//
// Binary: MainScreen+0xe0, operator new(200 = 0xc8). Layout RE by
// re-analyst a7cd670. sizeof == 200 in the binary; the port does NOT
// need to match that layout because BakedStringBox is never addressed by
// binary-side offsetof assertions (it is a v1.6.1 addition past the
// previously-verified MainScreen+0x120 layout boundary).
//
// API modelled from re-analyst a7cd670 spec:
//   BakedStringBox(font, fontSize, width, height, align, wrapMode, lineSpacing)
//   SetText(const char*)
//   SetColour(const Colour&, int setBase)
//   SetHorizontalLineSpacing(float)
//   FitIntoVerticalBounds()
//   SetTranslation(const Vec3&, int flag)
//   Draw(float rotationDegrees, Vec2 scale, int center)

#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <vector>
#include <cstring>

namespace Mortar {

class Font;
class FontCacheObjectTTF;
struct GlyphAtlasEntry;

// One laid-out line of glyphs ready to draw.
struct BakedStringBoxLine {
    std::vector<QUADCUSTOMVERTEX> verts; // 6 verts per glyph (tri-strip)
    float width;   // total pixel advance (unscaled)
    float height;  // line ascender + descender in pixels (unscaled)

    BakedStringBoxLine() : width(0.0f), height(0.0f) {}
};

class BakedStringBox {
public:
    // Binary ctor arg mapping (re-analyst a7cd670):
    //   font        : FontCacheObjectTTF* (TTF face, 256x256 atlas)
    //   fontSize    : 9.0f (initial render pixel size)
    //   width       : 75 (wrap box width in world units = pixels in orig ortho)
    //   height      : 30 (max box height in world units)
    //   align       : 0x0d (centred + fit)
    //   wrapMode    : 3
    //   lineSpacing : 3 (pixels between lines)
    BakedStringBox(FontCacheObjectTTF* font,
                   float fontSize,
                   float width,
                   float height,
                   int align,
                   int wrapMode,
                   float lineSpacing);
    ~BakedStringBox();

    // Set the string to display. Triggers a layout rebuild on next Draw.
    void SetText(const char* text);

    // Set the glyph colour. setBase==0 matches the binary call pattern
    // (sets m_Colour without touching a "base" colour slot).
    void SetColour(const Colour& colour, int setBase);

    // Set horizontal line spacing (pass -1 for "auto" as in binary call).
    void SetHorizontalLineSpacing(float spacing);

    // Shrink fontSize in 1-pixel steps (floor 6.0px) until all wrapped lines
    // fit within m_BoxHeight. Rebuilds layout at each candidate size.
    void FitIntoVerticalBounds();

    // Set the translation used by Draw. flag==1 triggers an immediate layout
    // rebuild (matches binary call site where flag=1).
    void SetTranslation(const Vec3& pos, int flag);

    // Draw the laid-out glyph quads.
    //   rotationDegrees : tilt in degrees (positive = clockwise on screen)
    //   scale           : Vec2(1,1) in the binary call site
    //   center          : 1 = centre the block on m_Pos
    void Draw(float rotationDegrees, Vec2 scale, int center);

private:
    FontCacheObjectTTF* m_Font;   // non-owning ref (owned by Font + FontTTFRegistry)
    float   m_FontSize;           // current render pixel size
    float   m_BoxWidth;           // wrap box width in world units
    float   m_BoxHeight;          // wrap box max height in world units
    int     m_Align;              // alignment flags (binary 0x0d)
    int     m_WrapMode;           // wrap mode (binary 3)
    float   m_LineSpacing;        // additional spacing between lines
    float   m_HorizLineSpacing;   // from SetHorizontalLineSpacing (-1 = auto)

    Colour  m_Colour;
    Vec3    m_Pos;

    // Laid-out lines (rebuilt by Layout()).
    std::vector<BakedStringBoxLine> m_Lines;
    bool    m_Dirty;              // true when text/size/pos changed
    char    m_Text[256];          // cached text copy

    // Rebuild the laid-out lines from m_Text at m_FontSize.
    void Layout();

    // Measure total height of currently laid-out lines (includes spacing).
    float TotalHeight() const;
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_BAKEDSTRINGBOX_H
