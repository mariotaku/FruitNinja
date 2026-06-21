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
    float width;        // total world-unit advance (unscaled)
    float height;       // binary step = line pitch (world units)
    float maxBearingY;  // max bearingY across glyphs (above baseline, world units)
    float minBottom;    // min (bearingY - height) across glyphs (below baseline, world units, <=0)

    BakedStringBoxLine() : width(0.0f), height(0.0f), maxBearingY(0.0f), minBottom(0.0f) {}
};

class BakedStringBox {
public:
    // Binary ctor arg mapping (re-analyst a7cd670, reconciled against 0x002465fc):
    //   font        : FontCacheObjectTTF* (TTF face, 256x256 atlas)
    //   fontSize    : 9.0f (initial render pixel size)
    //   width       : 75 (wrap box width, int in binary at field 0x50)
    //   height      : 30 (max box height, int in binary at field 0x24)
    //   align       : 0x0d (centred + fit)
    //   maxLines    : 3 (binary arg6; RebuildMeshes @ 0x00246944 shrink-until-fit criterion)
    //   lineSpacing : 3 (pixels between lines)
    //   param8      : int stored at field 0x48 (binary trailing arg; callers pass 0 or 1).
    //                 Added as a trailing default param (default 0) so existing 7-arg callers are valid.
    // DIFFERS: original width/height are int (fields 0x50/0x24); port uses float for the renderer.
    BakedStringBox(FontCacheObjectTTF* font,
                   float fontSize,
                   float width,
                   float height,
                   int align,
                   int maxLines,
                   float lineSpacing,
                   int param8 = 0);
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

    // SetGradient  binary @ 0x0024566c
    // Applies a vertical gradient to laid-out glyphs (gradTop/gradBottom Colours).
    // perGlyph==0 uses the lazy dirty path; perGlyph==1 applies per-line immediately.
    void SetGradient(Colour top, Colour bottom, bool perGlyph);

    // SetMetallicGradient  binary @ 0x002458e0
    // 4-stop metallic fill: m_ColourMode=4, m_MetallicFlag=1.
    // Fields: m_FillTop/Bottom at 0x7c/0x80, m_FillCol2/Col3 at 0x84/0x88.
    // Port renders 2-stop (top/bottom) pending full 4-stop metallic path.
    // TODO: 4-stop metallic render path (binary SetMetallicGradient @0x002458e0)
    void SetMetallicGradient(Colour top, Colour bottom, Colour c2, Colour c3, bool flag);

    // SetWorldspaceClipping  binary @ 0x0015ab58 (AddLine call site)
    // Sets a world-space clip rect for Draw. (x0, y0) = min corner, (x1, y1) = max.
    // ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: args are (-240, -46, 400, 108).
    // TODO: v1.6.1 0x0015ab58 (BakedStringBox::SetWorldspaceClipping) -- full clip impl not RE'd; stored only.
    void SetWorldspaceClipping(float x0, float y0, float x1, float y1);

    // Update  binary @ 0x0015ab80 (AddLine call site)
    // Forces an immediate layout rebuild (flushes dirty state).
    // ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: called after SetText/SetColour/SetWorldspaceClipping.
    void Update();

    // SetRotation  binary @ 0x0015a1c4 (DrawMarquee call site)
    // Stores a persistent rotation (degrees) applied by subsequent Draw calls with rot=stored.
    // ASM-spec v1.6.1 AboutScreen::DrawMarquee @0x0015a138: m_HeadingBox->SetRotation(90.0f).
    // TODO: v1.6.1 0x0015a1c4 (BakedStringBox::SetRotation) -- exact binary field offset not RE'd.
    void SetRotation(float degrees);

    // ReshapeBounds  binary call site v1.6.1 PauseScreen::Update @0x001a5ebc
    // Resizes the box (width, height) and marks dirty. flag1/flag2: binary args, exact
    // field mapping not fully RE'd -- stored as align/maxLines-override or ignored.
    // TODO: v1.6.1 BakedStringBox::ReshapeBounds -- confirm flag1/flag2 fields in binary.
    void ReshapeBounds(float width, float height, int flag1, int flag2);

    // SetFontSize  binary call site v1.6.1 PauseScreen::Update @0x001a5ebc
    // Sets m_FontSize and m_BaseFontSize, marks dirty.
    // TODO: v1.6.1 BakedStringBox::SetFontSize -- confirm exact binary field writes.
    void SetFontSize(float size);

    // GetRotation -- port helper so DrawMarquee can retrieve the stored rotation.
    float GetRotation() const { return m_StoredRotation; }

    // SetShadow  binary @ 0x002462c0
    // Sets the shadow parameters (scale, colour, offset, enable flag).
    // Fields: 0x70=scale, 0x74=col, 0x78=flag, 0x18=offset, 0x00=dirty byte.
    void SetShadow(float scale, Colour col, Vec3 offset, bool flag);

    // SetStroke  binary @ 0x00245314 (1 colour) / 0x0024536c (2) / 0x002453f0 (3)
    // Outline/stroke of `width` px drawn behind the glyph fill. count 1/2/3 selects
    // how many concentric stroke colours are layered. Change-detection gate matches
    // SetGradient/SetShadow: dirties the bake on any field change.
    void SetStroke(float width, const Colour& c0);
    void SetStroke(float width, const Colour& c0, const Colour& c1);
    void SetStroke(float width, const Colour& c0, const Colour& c1, const Colour& c2);

private:
    FontCacheObjectTTF* m_Font;   // non-owning ref (owned by Font + FontTTFRegistry)
    float   m_FontSize;           // current render pixel size (shrunk by FitInto)
    float   m_BaseFontSize;       // original font size (binary uses for step formula)
    float   m_BoxWidth;           // wrap box width in world units
    float   m_BoxHeight;          // wrap box max height in world units
    int     m_Align;              // alignment flags (binary 0x0d)
    int     m_MaxLines;           // max lines for shrink-to-fit (binary arg6, value 3; RebuildMeshes @ 0x00246944)
    float   m_LineSpacing;        // additional spacing between lines
    float   m_HorizLineSpacing;   // from SetHorizontalLineSpacing (-1 = auto)
    int     m_Param8;             // binary field 0x48; trailing ctor arg (default 0)

    Colour  m_Colour;
    Vec3    m_Pos;

    // Shadow fields (binary @ 0x18 offset, 0x70..0x78):
    Vec3    m_ShadowOffset;       // binary field 0x18 (3 floats)
    float   m_ShadowScale;        // binary field 0x70
    Colour  m_ShadowCol;          // binary field 0x74
    bool    m_ShadowFlag;         // binary field 0x78

    // Gradient fields (binary @ 0x7c..0x90):
    // ASM-spec v1.6.1 BakedStringBox::SetGradient @ 0x0024566c: change-detect on
    // {m_FillTop[0x7C], m_FillBottom[0x80], m_ColourMode[0x8C]!=2, m_MetallicFlag[0x90]!=0};
    // on change set mode=2, store top/bottom, metallic=0; bool perGlyph==0 -> m_DirtyMesh=1
    // (lazy), perGlyph!=0 -> per-line FancyBakedString::ApplyGradient.
    // DIFFERS: port omits the perGlyph!=0 immediate per-line ApplyGradient branch
    // (no FancyBakedString in port; gradient re-applied in Draw). v1.6.1 SetGradient @ 0x0024566c
    Colour  m_GradTop;            // binary field 0x7C (m_FillTop)
    Colour  m_GradBottom;         // binary field 0x80 (m_FillBottom)
    Colour  m_GradCol2;           // binary field 0x84 (m_FillCol2, metallic c2)
    Colour  m_GradCol3;           // binary field 0x88 (m_FillCol3, metallic c3)
    int     m_GradMode;           // binary field 0x8C (m_ColourMode: 2=gradient, 4=metallic)
    bool    m_MetallicFlag;       // binary field 0x90 (int m_MetallicFlag; SetMetallicGradient sets 1, SetGradient clears to 0)
    bool    m_GradFlag;           // PORT-ONLY (no binary field); models clearing m_MetallicFlag@0x90

    // Stroke/outline fields (binary v1.6.1 @ 0x54..0x64):
    float   m_StrokeWidth;        // binary 0x54
    int     m_StrokeCount;        // binary 0x58 (0 = no stroke, else 1/2/3)
    Colour  m_StrokeCol0;         // binary 0x5c
    Colour  m_StrokeCol1;         // binary 0x60
    Colour  m_StrokeCol2;         // binary 0x64

    // Worldspace clip rect (from SetWorldspaceClipping); stored but not yet applied in Draw.
    float   m_ClipX0, m_ClipY0, m_ClipX1, m_ClipY1;
    bool    m_HasClip;

    // Persistent rotation in degrees (from SetRotation).
    float   m_StoredRotation;

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
