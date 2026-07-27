#ifndef FN_ENGINE_RENDER_BAKEDSTRINGBOX_H
#define FN_ENGINE_RENDER_BAKEDSTRINGBOX_H

// BakedStringBox — TTF-backed wrapped text label with shrink-to-fit and
// rotated draw. Port specific: the binary uses the Bada IFont/IGlyphCache
// path for this class. The port reimplements it over FontCacheObjectTTF.
//
// Binary: MainScreen+0xe0, operator new(200 = 0xc8).
// sizeof == 200 in the binary; port layout matches on ARM32 (Bada/cross-build).
// Field offsets verified vs SetShadow/SetColour/SetWorldspaceClipping/ReshapeBounds.
// ASM-spec v1.6.1 Mortar::BakedStringBox layout @0x002465fc (ctor) -- 200B;
// field offsets verified vs SetShadow/SetColour/SetWorldspaceClipping/ReshapeBounds.
//
// SHAPE: thin per-line FancyBakedString dispatcher (matches v1.6.1 exactly).
// m_Lines is a std::vector<FancyBakedString*> -- one 6-layer FancyBakedString
// per wrapped line -- NOT raw glyph quads. RebuildMeshes() @0x00246944 wraps
// m_Text into m_WrappedLines, builds one FancyBakedString per line (17-arg
// ctor), applies the active gradient/stroke-gradient, and stores each line's
// LOCAL draw offset (horizontal align + vertical baseline) into the line's
// own LineOffset() (+0x0c). Draw() @0x00246e20 is then a thin loop: read
// LineOffset(), rotate/scale, translate by the box anchor, call
// FancyBakedString::Draw per line. All glyph rasterisation, shadow/glow/
// stroke/bevel layering, and gradient baking happens INSIDE FancyBakedString/
// BakedStringTTF -- the box no longer touches vertex data directly.
//
// API (v1.6.1 BakedStringBox ctor @ 0x002465fc — 7 explicit args):
//   BakedStringBox(font, fontSize, width, height, align, maxLines, lineSpacing)
//     width/height  : int in binary (fields +0x24/+0x28); stored as int.
//     lineSpacing   : extra leading added to fontSize for the per-line baseline pitch:
//                     step = (int)(fontSize + (lineSpacing - (baseFontSize - fontSize)*0.5))
//                     Typical values: 0 (no extra gap), 3 (13px pitch at size 10), 7.
//   SetText(const char*)
//   SetColour(Colour, bool eager)     — binary writes m_FillTop + m_ColourMode; see TODO in .cpp
//   SetHorizontalLineSpacing(int)     — binary writes m_AlignMode; pass -1 for auto
//   FitIntoVerticalBounds()
//   SetTranslation(Vec3, bool preShift)
//   Draw(Vec2 scale, float rotation, bool center)

#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "core/MortarTypes.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace Mortar {

class Font;
class FontCacheObjectTTF;
class FancyBakedString;
struct GlyphAtlasEntry;

class BakedStringBox {
public:
    // ASM-spec v1.6.1 BakedStringBox ctor @ 0x002465fc: 7 args, 7th = m_LineSpacing.
    //   font        : FontCacheObjectTTF* (TTF face, 256x256 atlas)
    //   fontSize    : initial render pixel size (also stored as m_BaseFontSize)
    //   width       : wrap box width  (int in binary, field +0x24; stored as int)
    //   height      : max box height  (int in binary, field +0x28; stored as int)
    //   align       : alignment flags (e.g. 0x0f = centre-H+centre-V; bits 0-1: 3=centre-H, 2=right, 0/1=left; bits 2-3: 0xc=centre-V)
    //                 binary type is ALIGNMENT_TYPE enum (1 byte, -fshort-enums); port matches.
    //   maxLines    : binary arg6; stored as m_MaxLines; FitIntoVerticalBounds @ 0x00246fbc uses HEIGHT predicate, not this count
    //   lineSpacing : extra leading stored at m_LineSpacing; step = (int)(fontSize + lineSpacing)
    //                 Typical values: 0 (no extra gap), 3 (+3px/line), 5, 7.
    // ASM-spec v1.6.1 Mortar::BakedStringBox ctor @0x002465fc: 7 args; width/height int in binary.
    BakedStringBox(FontCacheObjectTTF* font,
                   float fontSize,
                   int width,
                   int height,
                   ALIGNMENT_TYPE align,
                   int maxLines,
                   int lineSpacing);
    ~BakedStringBox();

    // Set the string to display. Triggers a layout rebuild on next Draw.
    void SetText(const char* text);

    // Set the glyph colour. eager==true triggers an immediate per-line apply.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetColour @0x002454e0: change-detect on
    // m_GradTop(+0x7c); on change writes m_GradTop=colour, m_GradMode(+0x8c)=1,
    // m_MetallicFlag(+0x90)=0; if eager!=0 calls FancyBakedString::ApplyGradient(colour)
    // per line (no m_Dirty set -- immediate-only), else m_Dirty=true (lazy rebuild).
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
    void SetTranslation(_Vector3<float> pos, bool preShift);

    // Draw the laid-out glyph quads.
    //   scale    : Vec2(1,1) in the binary call site
    //   rotation : tilt in degrees (positive = clockwise on screen)
    //   center   : true = centre the block on m_Pos
    // ASM-spec v1.6.1 Mortar::BakedStringBox::Draw @0x00246e20: (Vec2 scale, float rotation, bool center).
    void Draw(_Vector2<float> scale, float rotation, bool center);

    // SetGradient  binary @ 0x0024566c
    // Applies a vertical gradient to laid-out glyphs (gradTop/gradBottom Colours).
    // perGlyph==0 uses the lazy dirty path; perGlyph==1 calls FancyBakedString::
    // ApplyGradient(top,bottom) per existing line immediately (no m_Dirty set).
    void SetGradient(Colour top, Colour bottom, bool perGlyph);

    // SetGradient (3-colour)  binary @ 0x00245780
    // ASM-spec v1.6.1 BakedStringBox::SetGradient @0x00245780: m_GradMode=3 (distinct from
    // 2-colour mode=2 and metallic mode=4). perGlyph==0 -> lazy m_Dirty=true; !=0 -> immediate
    // FancyBakedString::ApplyGradient(top,mid,bottom) per line. Storage: m_GradTop=top,
    // m_GradBottom=mid, m_GradCol2=bottom (binary reuses m_FillBottom for MID here).
    void SetGradient(Colour top, Colour mid, Colour bottom, bool perGlyph);

    // SetMetallicGradient  binary @ 0x002458e0
    // 4-stop metallic fill: m_ColourMode=4, m_MetallicFlag=1.
    // Fields: m_FillTop/Bottom at 0x7c/0x80, m_FillCol2/Col3 at 0x84/0x88.
    // Binary colour order: c0=top, c1=bottom(port), c2, c3=bottom (full); port arg order matches.
    void SetMetallicGradient(Colour top, Colour bottom, Colour c2, Colour c3, bool flag);

    // SetWorldspaceClipping  binary @ 0x0015ab58 (AddLine call site)
    // Sets a world-space clip rect for Draw. (x0, y0) = top-left corner; w, h = width/height in worldspace units.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetWorldspaceClipping @0x00245c28: (int x0, int y0, int w, int h).
    // Stores the rect as int at +0x94..+0xa0, then sets m_HasClip(+0xa4) = true AND
    // m_Visible(+0x01) = true. It also negates y0 when FontInterface::GetInstance()[+0x150]
    // == -1.0f -- see the .cpp TODO; the port has no FontInterface singleton so that
    // y-sign flip is not applied.
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
    // Draw() renders a shadow pass BEFORE the foreground: each glyph is refetched as a
    // separately-rasterised BLUR-effect glyph (v1.6.1 RenderGlyph @0x0024f5dc, BuildBlur
    // @0x0024f030; FontCacheObjectTTF::GetGlyph(cp, size, FONT_EFFECT_BLUR, radius)) and
    // drawn in m_ShadowCol at anchor + m_ShadowOffset (world units). The pass fires when:
    //   flag==0: m_ShadowScale > 0.0f
    //   flag!=0: m_ShadowScale >= 0.0f  (inner-glow mode; game uses offset=(0,0))
    // radius = clamp(ceil(m_ShadowScale * atlas->m_FontScale), 0, 32) [RASTER px] -- v1.6.1
    // BakedStringTTF ctor @0x00249a5c formula, computed in Draw() from the current atlas.
    // ASM-verified: 2026-07-08 v1.6.1 FontInterface::Initialize @0x00250470 sets +0xc=m_FontScale
    //   (forward), +0x10=m_InvFontScale; the ctor scales the blur radius by +0xc (m_FontScale),
    //   and the shadow *offset* by +0x10 (m_InvFontScale). (asm-inspector)
    // ASM-spec v1.6.1 Mortar::BakedStringBox::SetShadow @0x002462c0: (float, Colour, _Vector3<float>, int).
    // Note: SetColour/SetTranslation use bool; SetShadow uses int — they are NOT uniform.
    void SetShadow(float scale, Colour col, _Vector3<float> offset, int flag);

    // SetStroke  binary @ 0x00245314 (1 colour) / 0x0024536c (2) / 0x002453f0 (3)
    // Outline/stroke of `width` px drawn behind the glyph fill (after shadow, before fg).
    // count 1/2/3 selects how many concentric stroke colours are layered.
    // Change-detection gate matches SetGradient/SetShadow: dirties the bake on any field change.
    //
    // Draw() renders the stroke pass as a single SEPARATELY-RASTERISED SDF-outline glyph
    // (v1.6.1 RenderGlyph @0x0024f5dc effect==1, BuildStrokes @0x0024edb8) refetched via
    // FontCacheObjectTTF::GetGlyph(cp, size, FONT_EFFECT_STROKE, radius), drawn in
    // m_StrokeCol0 at the same pen position as the sharp glyph -- matching the shadow
    // BLUR pass's pattern. radius = clamp(ceil(m_StrokeWidth * atlas->m_FontScale), 0, 32).
    //
    // 2/3-colour stroke gradient (m_StrokeCount>=2/3): per-line top->bottom lerp over the
    // glow(stroke) mesh's own Y-bbox; count==3 additionally overpaints m_StrokeCol1 on the
    // upper half. See ApplyStrokeGradient ASM-spec comments in RebuildMeshes (BakedStringBox.cpp).
    void SetStroke(float width, const Colour& c0);
    void SetStroke(float width, const Colour& c0, const Colour& c1);
    void SetStroke(float width, const Colour& c0, const Colour& c1, const Colour& c2);

    // Box dimension accessors -- v1.6.1 IngamePopup::Draw @0x0016d41c reads these to
    // compute the rotation-aware text anchor delta. Binary fields: +0x24 (boxW), +0x28 (boxH).
    int GetBoxWidth()  const { return m_BoxWidth; }
    int GetBoxHeight() const { return m_BoxHeight; }

    // GetTextWidth -- binary v1.6.1 BakedStringBox::GetBounds + MortarRectangleT::Width
    // Returns the rendered text width in world units: the max per-line TIGHT
    // (baked-bearing/layoutX, matches what BakedStringTTF actually draws) advance
    // width across all wrapped lines (0.0f if no lines). Triggers a lazy
    // RebuildMeshes() if m_Dirty is set so the result always reflects the current
    // text. The binary returns integer pixel width; the port lays out 1:1
    // world=pixel so the float max-line-width is equivalent. Used by
    // ShopListItem::DrawFloatingText @0x001b4bc8 to anchor NEW/SELECTED badges --
    // callers positioning elements off this value get the ACTUAL rendered extent,
    // not an over-estimate.
    float GetTextWidth() const;

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
    // v1.6.1 Mortar::BakedStringBox layout -- 200B on ARM32.
    // Offsets shown are binary ARM32. x64 host differs (pointer/vector size).
    // All static_asserts gated by #if defined(__bada__) below.

    bool    m_Dirty;               // +0x00 true when text/colour/size changed (NOT position)
    bool    m_Visible;             // +0x01 set true by SetWorldspaceClipping
                                   // +0x02..0x03 implicit padding (bool+bool, vector 4-aligned)
    // v1.6.1 shape: one 6-layer FancyBakedString per wrapped line (NOT raw glyph quads).
    // Owned pointers -- DeleteStrings() frees them; RebuildMeshes() rebuilds the whole
    // vector from m_Text on every dirty pass.
    std::vector<FancyBakedString*> m_Lines; // +0x04 (12B on ARM32)
    int     m_Field10;             // +0x10 (filler)
    int     m_Field14;             // +0x14 (filler)
    _Vector3<float> m_ShadowOffset;        // +0x18 (12B)
    int     m_BoxWidth;            // +0x24 (int in binary; was float)
    int     m_BoxHeight;           // +0x28 (int in binary; was float)
    int     m_MaxLines;            // +0x2c FitIntoVerticalBounds @ 0x00246fbc uses HEIGHT predicate, not this count
    ALIGNMENT_TYPE m_Align;        // +0x30 alignment flags; 1B enum in binary (matches, -fshort-enums)
    char*   m_Text;                // +0x34 strdup'd string; null until SetText called
    _Vector3<float> m_Pos;                 // +0x38 (12B)
    int     m_AlignMode;           // +0x44 written by SetHorizontalLineSpacing; -1 = auto
    // m_LineSpacing: extra leading added to fontSize for the per-line baseline pitch.
    // step = (int)(fontSize + m_LineSpacing - (m_BaseFontSize-m_FontSize)*0.5).
    int     m_LineSpacing;         // +0x48 7th ctor arg
    float   m_BaseFontSize;        // +0x4c original font size (binary step formula base)
    FontCacheObjectTTF* m_Font;    // +0x50 non-owning ref (owned by Font + FontTTFRegistry)
    // Per-line FancyBakedString layer params (v1.6.1 RebuildMeshes @0x00246944 passes
    // these into the 17-arg per-line build call):
    //   m_StrokeWidth/m_StrokeCol0        -> GLOW layer (glowSize/glowCol)
    //   m_StrokeLayerWidth/m_StrokeLayerColour -> m_pStroke layer (strokeSize/strokeCol)
    //   m_ExtraWidth/m_Extra1Colour/m_Extra2Colour -> extra1/extra2 BEVEL layers
    float   m_StrokeWidth;         // +0x54 GLOW layer width (FancyBakedString glowSize)
    int     m_StrokeCount;         // +0x58 0=no stroke, 1/2/3=concentric colours
    Colour  m_StrokeCol0;          // +0x5c GLOW layer colour (FancyBakedString glowCol)
    Colour  m_StrokeCol1;          // +0x60
    Colour  m_StrokeCol2;          // +0x64
    float   m_StrokeLayerWidth;    // +0x68 stroke (m_pStroke) layer width
    Colour  m_StrokeLayerColour;   // +0x6c stroke (m_pStroke) layer colour
    float   m_ShadowScale;         // +0x70
    Colour  m_ShadowCol;           // +0x74
    int     m_ShadowFlag;          // +0x78 int in binary (was bool)
    // Gradient / fill fields. SetColour writes m_GradTop+m_GradMode=1 (solid colour).
    // SetGradient writes m_GradTop+m_GradBottom+m_GradMode=2.
    // SetMetallicGradient writes all four stops + m_GradMode=4.
    // ASM-spec v1.6.1 BakedStringBox::SetGradient @ 0x0024566c.
    Colour  m_GradTop;             // +0x7c m_FillTop; primary fill / gradient-top colour
    Colour  m_GradBottom;          // +0x80 m_FillBottom
    Colour  m_GradCol2;            // +0x84 m_FillCol2 (metallic c2)
    Colour  m_GradCol3;            // +0x88 m_FillCol3 (metallic c3)
    int     m_GradMode;            // +0x8c m_ColourMode: 1=solid, 2=gradient, 4=metallic
    int     m_MetallicFlag;        // +0x90 int in binary (was bool)
    // Worldspace clip rect. Stored as int (binary emits str, not vcvt/vstr).
    int     m_ClipX0;              // +0x94 (int in binary; was float)
    int     m_ClipY0;              // +0x98 (int in binary; was float)
    int     m_ClipW;               // +0x9c (int in binary; was float)
    int     m_ClipH;               // +0xa0 (int in binary; was float)
    bool    m_HasClip;             // +0xa4
    bool    m_FieldA5;             // +0xa5 (filler)
                                   // +0xa6..0xa7 implicit padding
    float   m_ExtraWidth;          // +0xa8 extra1/extra2 (BEVEL) layer width
    int     m_ExtraParam;          // +0xac FancyBakedString per-line ctor p15 (extra-layer param)
    Colour  m_Extra1Colour;        // +0xb0 extra1 (BEVEL) layer colour
    Colour  m_Extra2Colour;        // +0xb4 extra2 (BEVEL) layer colour
    std::vector<std::string> m_WrappedLines; // +0xb8 (12B on ARM32) wrapped-line text, parallel to m_Lines
    float   m_FontSize;            // +0xc4 current render pixel size (shrunk by FitInto)

    // RebuildMeshes -- rebuild m_WrappedLines + m_Lines from m_Text at m_BaseFontSize.
    // ASM-spec v1.6.1 Mortar::BakedStringBox::RebuildMeshes @0x00246944: shrink-by-linecount
    // loop (FitStrings/MeasureWrap, floor 6.0px) picks m_FontSize; then one FancyBakedString
    // per wrapped line is constructed (17-arg ctor), the active gradient/stroke-gradient is
    // applied, and each line's LOCAL draw offset is written into its own LineOffset() via
    // ComputeBaselineY. DeleteStrings() frees the previous line set first.
    // Tokeniser: East-Asian codepoints (v1.6.1 WordWrap::IsEastAsianChar @0x002508ec)
    // are emitted as individual single-codepoint tokens, so CJK text wraps between
    // any two consecutive characters. Latin/symbol runs stop at spaces, newlines, or
    // the first East-Asian codepoint. Space advances (spAdv) are suppressed between
    // two adjacent CJK tokens everywhere (v1.6.1 WordWrap::CanBreakLineAt @0x002509cc).
    void RebuildMeshes();

    // DeleteStrings -- delete + clear every FancyBakedString* in m_Lines.
    // Called by RebuildMeshes() (before rebuilding) and ~BakedStringBox().
    void DeleteStrings();

    // Measure total ink height of currently laid-out lines:
    //   maxBearingY(line0) + (N-1)*step + (-minBottom(lineN-1))
    // Re-measures via the FitStrings wrap pass (no per-line ink-extent cache is kept on
    // FancyBakedString lines -- sizeof(BakedStringBox) is pinned at 200B).
    float TotalHeight() const;

#ifdef __bada__
    // GCC 4.4 rejects offsetof on private members from namespace scope -- use the
    // canonical friend-struct layout-assert pattern (as BakedStringTTF).
    friend struct BakedStringBoxLayoutAssert;
#endif
};

// Layout assertions -- compiled only on Bada/cross-build (ARM32/wasm32 with __bada__).
// Host x64 layout differs (pointer=8B, vector=24B). These pin the binary-faithful
// field offsets verified vs SetShadow/SetColour/SetWorldspaceClipping/ReshapeBounds.
// ASM-spec v1.6.1 Mortar::BakedStringBox layout @0x002465fc (ctor) -- 200B;
// field offsets verified vs SetShadow/SetColour/SetWorldspaceClipping/ReshapeBounds.
#if defined(__bada__)
static_assert(sizeof(BakedStringBox)                   == 200, "BakedStringBox sizeof mismatch");
struct BakedStringBoxLayoutAssert {
static_assert(__builtin_offsetof(BakedStringBox, m_Visible)      ==   1, "BakedStringBox m_Visible offset");
static_assert(__builtin_offsetof(BakedStringBox, m_ShadowOffset) ==  24, "BakedStringBox m_ShadowOffset offset");
static_assert(__builtin_offsetof(BakedStringBox, m_BoxWidth)     ==  36, "BakedStringBox m_BoxWidth offset");
static_assert(__builtin_offsetof(BakedStringBox, m_BoxHeight)    ==  40, "BakedStringBox m_BoxHeight offset");
static_assert(__builtin_offsetof(BakedStringBox, m_MaxLines)     ==  44, "BakedStringBox m_MaxLines offset");
static_assert(__builtin_offsetof(BakedStringBox, m_Text)         ==  52, "BakedStringBox m_Text offset");
static_assert(__builtin_offsetof(BakedStringBox, m_Pos)          ==  56, "BakedStringBox m_Pos offset");
static_assert(__builtin_offsetof(BakedStringBox, m_LineSpacing)  ==  72, "BakedStringBox m_LineSpacing offset");
static_assert(__builtin_offsetof(BakedStringBox, m_ShadowScale)  == 112, "BakedStringBox m_ShadowScale offset");
static_assert(__builtin_offsetof(BakedStringBox, m_ShadowFlag)   == 120, "BakedStringBox m_ShadowFlag offset");
static_assert(__builtin_offsetof(BakedStringBox, m_GradTop)      == 124, "BakedStringBox m_GradTop offset");
static_assert(__builtin_offsetof(BakedStringBox, m_GradMode)     == 140, "BakedStringBox m_GradMode offset");
static_assert(__builtin_offsetof(BakedStringBox, m_MetallicFlag) == 144, "BakedStringBox m_MetallicFlag offset");
static_assert(__builtin_offsetof(BakedStringBox, m_ClipX0)       == 148, "BakedStringBox m_ClipX0 offset");
static_assert(__builtin_offsetof(BakedStringBox, m_HasClip)      == 164, "BakedStringBox m_HasClip offset");
static_assert(__builtin_offsetof(BakedStringBox, m_FontSize)     == 196, "BakedStringBox m_FontSize offset");
};
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_BAKEDSTRINGBOX_H
