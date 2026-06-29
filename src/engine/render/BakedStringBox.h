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
// API (v1.6.1 BakedStringBox ctor @ 0x002465fc — 7 explicit args):
//   BakedStringBox(font, fontSize, width, height, align, maxLines, lineSpacing)
//     width/height  : int in binary (fields 0x50/0x24); stored as float internally.
//     lineSpacing   : extra leading added to fontSize for the per-line baseline pitch:
//                     step = (int)(fontSize + (lineSpacing - (baseFontSize - fontSize)*0.5))
//                     Typical values: 0 (no extra gap), 3 (13px pitch at size 10), 7.
//   SetText(const char*)
//   SetColour(Colour, bool eager)     — binary writes m_FillTop + m_ColourMode; see TODO in .cpp
//   SetHorizontalLineSpacing(int)     — binary writes m_AlignMode; pass -1 for auto
//   FitIntoVerticalBounds()
//   SetTranslation(const Vec3&, bool preShift)
//   Draw(Vec2 scale, float rotation, bool center)

#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <vector>
#include <cstdint>
#include <cstring>

namespace Mortar {

class Font;
class FontCacheObjectTTF;
struct GlyphAtlasEntry;

// One laid-out line of glyphs ready to draw.
struct BakedStringBoxLine {
    std::vector<QUADCUSTOMVERTEX> verts; // 6 verts per glyph (tri-strip)
    // One GL texture ID per drawable glyph (parallel to every 6-vert group in verts).
    // Stored as uint32_t (same bit width as GLuint) to avoid pulling GL headers here.
    // Used by Draw to batch consecutive same-page glyphs. Port specific: the binary
    // groups by TextureAtlasPage* implicitly; here we store the resolved GL texture ID.
    std::vector<uint32_t> glyphPageTexIDs;
    float width;        // total world-unit advance (unscaled)
    float height;       // binary step = line pitch (world units)
    float maxBearingY;  // max bearingY across glyphs (above baseline, world units)
    float minBottom;    // min (bearingY - height) across glyphs (below baseline, world units, <=0)

    BakedStringBoxLine() : width(0.0f), height(0.0f), maxBearingY(0.0f), minBottom(0.0f) {}
};

class BakedStringBox {
public:
    // ASM-spec v1.6.1 BakedStringBox ctor @ 0x002465fc: 7 args, 7th = m_LineSpacing.
    //   font        : FontCacheObjectTTF* (TTF face, 256x256 atlas)
    //   fontSize    : initial render pixel size (also stored as m_BaseFontSize)
    //   width       : wrap box width  (int in binary, field 0x50; stored as float in port)
    //   height      : max box height  (int in binary, field 0x24; stored as float in port)
    //   align       : alignment flags (e.g. 0x0f = centre-H+centre-V; bits 0-1: 3=centre-H, 2=right, 0/1=left; bits 2-3: 0xc=centre-V)
    //                 binary type is ALIGNMENT_TYPE enum; port uses int (same underlying value).
    //   maxLines    : binary arg6; stored as m_MaxLines; FitIntoVerticalBounds @ 0x00246fbc uses HEIGHT predicate, not this count
    //   lineSpacing : extra leading stored at m_LineSpacing; step = (int)(fontSize + lineSpacing)
    //                 Typical values: 0 (no extra gap), 3 (+3px/line), 5, 7.
    // ASM-spec v1.6.1 Mortar::BakedStringBox ctor @0x002465fc: 7 args; width/height int in binary.
    BakedStringBox(FontCacheObjectTTF* font,
                   float fontSize,
                   int width,
                   int height,
                   int align,
                   int maxLines,
                   int lineSpacing);
    ~BakedStringBox();

    // Set the string to display. Triggers a layout rebuild on next Draw.
    void SetText(const char* text);

    // Set the glyph colour. eager==true triggers an immediate gradient apply.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetColour @0x002454e0: binary writes m_FillTop +
    // m_ColourMode=1 + m_MetallicFlag=0; on eager(bool!=0) calls FancyBakedString::ApplyGradient
    // per line. Port writes m_Colour only.
    // TODO: v1.6.1 BakedStringBox::SetColour @0x002454e0 — binary writes m_FillTop + m_ColourMode=1
    //   + m_MetallicFlag=0 and on eager(bool!=0) calls FancyBakedString::ApplyGradient per line;
    //   port writes m_Colour only.
    void SetColour(Colour colour, bool eager);

    // Set horizontal line spacing / alignment mode. Pass -1 for auto.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetHorizontalLineSpacing @0x0024565c:
    // body = m_AlignMode = param; m_DirtyMesh = true.
    // BINARY MISNOMER: despite the name, this writes m_AlignMode (justification),
    // NOT m_LineSpacing (the line-pitch addend). The two are unrelated fields.
    void SetHorizontalLineSpacing(int spacing);

    // Shrink fontSize in 1-pixel steps (floor 6.0px) until all wrapped lines
    // fit within m_BoxHeight. Rebuilds layout at each candidate size.
    void FitIntoVerticalBounds();

    // Set the translation used by Draw. preShift==true pre-shifts by -(boxW/2) in X
    // and +(boxH/2) in Y (integer truncation, v1.6.1 BakedStringBox::SetTranslation @0x00246238).
    // Does NOT dirty the layout — position is a draw-time anchor only.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetTranslation @0x00246238: (_Vector3<float>, bool).
    void SetTranslation(const Vec3& pos, bool preShift);

    // Draw the laid-out glyph quads.
    //   scale    : Vec2(1,1) in the binary call site
    //   rotation : tilt in degrees (positive = clockwise on screen)
    //   center   : true = centre the block on m_Pos
    // ASM-spec v1.6.1 Mortar::BakedStringBox::Draw @0x00246e20: (Vec2 scale, float rotation, bool center).
    void Draw(Vec2 scale, float rotation, bool center);

    // SetGradient  binary @ 0x0024566c
    // Applies a vertical gradient to laid-out glyphs (gradTop/gradBottom Colours).
    // perGlyph==0 uses the lazy dirty path; perGlyph==1 applies per-line immediately.
    void SetGradient(Colour top, Colour bottom, bool perGlyph);

    // SetMetallicGradient  binary @ 0x002458e0
    // 4-stop metallic fill: m_ColourMode=4, m_MetallicFlag=1.
    // Fields: m_FillTop/Bottom at 0x7c/0x80, m_FillCol2/Col3 at 0x84/0x88.
    // Binary colour order: c0=top, c1=bottom(port), c2, c3=bottom (full); port arg order matches.
    void SetMetallicGradient(Colour top, Colour bottom, Colour c2, Colour c3, bool flag);

    // SetWorldspaceClipping  binary @ 0x0015ab58 (AddLine call site)
    // Sets a world-space clip rect for Draw. (x0, y0) = top-left corner; w, h = width/height in worldspace units.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetWorldspaceClipping @0x00114554: (int x0, int y0, int w, int h).
    // ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: args are (-240, -46, 400, 108).
    // DIFFERS: original = CPU ClipAgainstPlanes geometry clip (v1.6.1 BakedStringBox::ClipToRectangle @0x00246358
    //   / RebuildClipping @0x002464d0), using glScissor because GLES2 has no fixed-function user clip planes
    //   and per-glyph CPU mesh clipping isn't ported.
    void SetWorldspaceClipping(int x0, int y0, int w, int h);

    // Update  binary @ 0x0015ab80 (AddLine call site)
    // Forces an immediate layout rebuild (flushes dirty state).
    // ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: called after SetText/SetColour/SetWorldspaceClipping.
    void Update();

    // ReshapeBounds  binary @ 0x00245ab8
    // Writes m_MaxLines=p3, m_BoxWidth=w, m_BoxHeight=h, m_LineSpacing=p4, m_Dirty=true unconditionally.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::ReshapeBounds @0x00245ab8: (int width, int height, int maxLines, int lineSpacing).
    void ReshapeBounds(int width, int height, int maxLines, int lineSpacing);

    // SetFontSize  binary call site v1.6.1 PauseScreen::Update @0x001a5ebc
    // Sets m_FontSize and m_BaseFontSize, marks dirty.
    // TODO: v1.6.1 BakedStringBox::SetFontSize -- confirm exact binary field writes.
    void SetFontSize(float size);

    // GetFontSize  — returns m_FontSize, the current render pixel size.
    // After FitIntoVerticalBounds() shrinks the box, this returns the post-shrink size.
    // Used by AboutScreen ctor @0x0015b764 min-fontSize equalization pass.
    float GetFontSize() const { return m_FontSize; }

    // SetShadow  binary @ 0x002462c0
    // Sets the shadow parameters (scale, colour, offset, enable flag).
    // Fields: 0x70=scale, 0x74=col, 0x78=flag, 0x18=offset, 0x00=dirty byte.
    //
    // Draw() renders a shadow pass BEFORE the foreground: each glyph is redrawn
    // in m_ShadowCol at anchor + m_ShadowOffset (world units). The pass fires when:
    //   flag==0: m_ShadowScale > 0.0f
    //   flag!=0: m_ShadowScale >= 0.0f  (inner-glow mode; game uses offset=(0,0))
    //
    // DIFFERS (v1.6.1 FancyBakedString::Draw @0x0024b8e4): binary blurs the shadow
    // glyph atlas slice (FetchGlyph with blur_radius = ceil(scale*invFontScale)).
    // The port draws a solid copy at the offset with no pixel blur, because
    // FontCacheObjectTTF has no glyph-blur rasterisation path.
    // TODO: v1.6.1 BakedStringTTF::BuildGlyphs @0x00248b28 -- port glyph-blur.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetShadow @0x002462c0: (float, Colour, _Vector3<float>, int).
    // Note: SetColour/SetTranslation use bool; SetShadow uses int — they are NOT uniform.
    void SetShadow(float scale, Colour col, Vec3 offset, int flag);

    // SetStroke  binary @ 0x00245314 (1 colour) / 0x0024536c (2) / 0x002453f0 (3)
    // Outline/stroke of `width` px drawn behind the glyph fill (after shadow, before fg).
    // count 1/2/3 selects how many concentric stroke colours are layered.
    // Change-detection gate matches SetGradient/SetShadow: dirties the bake on any field change.
    //
    // Draw() renders the stroke pass as 8 solid copies of the glyph quads offset in
    // cardinal + diagonal directions by m_StrokeWidth (diagonal scaled by 0.707), all in
    // m_StrokeCol0. This approximates a solid outline.
    //
    // DIFFERS (v1.6.1 FancyBakedString::Draw @0x0024b8e4): binary draws ONE blurred
    // expanded-glyph (m_pGlow) pass. Multi-colour gradient stroke (m_StrokeCount>=2/3,
    // ApplyStrokeGradient) and the inner-stroke layer (m_Field68) are not yet ported.
    void SetStroke(float width, const Colour& c0);
    void SetStroke(float width, const Colour& c0, const Colour& c1);
    void SetStroke(float width, const Colour& c0, const Colour& c1, const Colour& c2);

    // Box dimension accessors -- v1.6.1 IngamePopup::Draw @0x0016d41c reads these to
    // compute the rotation-aware text anchor delta. Binary fields: +0x24 (boxW), +0x28 (boxH).
    // Port stores floats; cast to int mirrors binary signed-int field reads.
    int GetBoxWidth()  const { return (int)m_BoxWidth; }
    int GetBoxHeight() const { return (int)m_BoxHeight; }

    // ComputeBaselineY  binary RebuildAlignments @ 0x00245c78
    // Pure stateless vertical baseline for line `lineIdx` (0-based).
    // Extracted for unit testability; Draw() calls with lineIdx=0 and lets
    // the render loop apply the per-line step offset.
    //
    // center-V single (nLines==1) -- else-branch @0x00245e74, metric-INDEPENDENT:
    //   -boxH*0.5 - (fontSize+4.0)*0.5
    //   Runtime-verified: FontInterface::GetInstance()[0] == 0x48 != 0 always takes this branch.
    //   VFP const 4.0 = 0x40800000. maxBearingY/minBottom are NOT used.
    // center-V multi (nLines>1), per line i:
    //   (step*nLines)*0.5 - step*0.5 - boxH*0.5 - maxSpan*0.5 - i*step
    //   where maxSpan = max(maxBearingY-minBottom) across all lines
    // top-anchored ((align&0x8)==0):
    //   -(ascentSpan*0.5) - step*0.5 - descent
    //   where ascentSpan = maxBearingY-minBottom, descent = -minBottom
    // bottom-anchored:
    //   boxH
    //
    // ASM-verified: 2026-06-29 v1.6.1 RebuildAlignments @0x00245c78 (asm-inspector + runtime HLE)
    static float ComputeBaselineY(int align, int nLines, int lineIdx,
                                  float maxBearingY, float minBottom,
                                  float boxH, float step, float maxSpan,
                                  float fontSize);

private:
    FontCacheObjectTTF* m_Font;   // non-owning ref (owned by Font + FontTTFRegistry)
    float   m_FontSize;           // current render pixel size (shrunk by FitInto)
    float   m_BaseFontSize;       // original font size (binary uses for step formula)
    float   m_BoxWidth;           // wrap box width in world units
    float   m_BoxHeight;          // wrap box max height in world units
    int     m_Align;              // alignment flags (binary 0x0d)
    int     m_MaxLines;           // binary arg6; FitIntoVerticalBounds @ 0x00246fbc uses HEIGHT predicate, not this count
    // m_LineSpacing (binary ctor arg 7, "param8"): extra leading added to fontSize for the
    // per-line baseline pitch. step = (int)(fontSize + m_LineSpacing - (m_BaseFontSize-m_CurrentFontSize)*0.5).
    int     m_LineSpacing;        // binary field 0x48 (7th ctor arg); step = (int)(fontSize + m_LineSpacing)

    int     m_AlignMode;            // binary field written by SetHorizontalLineSpacing; -1 = auto
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
    // (lazy), perGlyph!=0 -> per-line FancyBakedString::ApplyGradient (BakeGradient in port).
    // v1.6.1 SetGradient @ 0x0024566c
    Colour  m_GradTop;            // binary field 0x7C (m_FillTop)
    Colour  m_GradBottom;         // binary field 0x80 (m_FillBottom)
    Colour  m_GradCol2;           // binary field 0x84 (m_FillCol2, metallic c2)
    Colour  m_GradCol3;           // binary field 0x88 (m_FillCol3, metallic c3)
    int     m_GradMode;           // binary field 0x8C (m_ColourMode: 2=gradient, 4=metallic)
    bool    m_MetallicFlag;       // binary field 0x90 (int m_MetallicFlag; SetMetallicGradient sets 1, SetGradient clears to 0)

    // Stroke/outline fields (binary v1.6.1 @ 0x54..0x64):
    float   m_StrokeWidth;        // binary 0x54
    int     m_StrokeCount;        // binary 0x58 (0 = no stroke, else 1/2/3)
    Colour  m_StrokeCol0;         // binary 0x5c
    Colour  m_StrokeCol1;         // binary 0x60
    Colour  m_StrokeCol2;         // binary 0x64

    // Worldspace clip rect (from SetWorldspaceClipping).
    // (m_ClipX0, m_ClipY0) = top-left corner; m_ClipW/m_ClipH = extent in worldspace units.
    float   m_ClipX0, m_ClipY0, m_ClipW, m_ClipH;
    bool    m_HasClip;

    // Laid-out lines (rebuilt by Layout()).
    std::vector<BakedStringBoxLine> m_Lines;
    bool    m_Dirty;              // true when text/colour/size changed (NOT position)
    char    m_Text[256];          // cached text copy

    // Rebuild the laid-out lines from m_Text at m_FontSize.
    // Tokeniser: East-Asian codepoints (v1.6.1 WordWrap::IsEastAsianChar @0x002508ec)
    // are emitted as individual single-codepoint tokens, so CJK text wraps between
    // any two consecutive characters. Latin/symbol runs stop at spaces, newlines, or
    // the first East-Asian codepoint. Space advances (spAdv) are suppressed between
    // two adjacent CJK tokens everywhere (v1.6.1 WordWrap::CanBreakLineAt @0x002509cc).
    void Layout();

    // Bake the active gradient (m_GradMode>=2) into vertex colours across all m_Lines.
    // ASM-spec v1.6.1 BakedStringBox::SetGradient @0x0024566c: per-glyph bake via
    // FancyBakedString::ApplyGradient @0x0024accc / Transform_LinearGradient_TopBottom @0x00247a48.
    // ASM-spec v1.6.1 FancyBakedString::ApplyMetallicGradient @0x0024abf4: c0->c3 base +
    // 2 horizontal-band splits (0.51/0.49) via Transform_GradientSplit @0x0024954c.
    void BakeGradient();

    // Measure total ink height of currently laid-out lines:
    //   maxBearingY(line0) + (N-1)*step + (-minBottom(lineN-1))
    // where step == m_Lines[0].height (= (int)(fontSize + m_LineSpacing)).
    float TotalHeight() const;
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_BAKEDSTRINGBOX_H
