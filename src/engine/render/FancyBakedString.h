#ifndef FN_ENGINE_RENDER_FANCYBAKEDSTRING_H
#define FN_ENGINE_RENDER_FANCYBAKEDSTRING_H

// Mortar::FancyBakedString -- multi-layer TTF label wrapper around BakedStringTTF.
//
// Binary: v1.6.1 Mortar::FancyBakedString ctor @0x0024b600 / Build @0x0024b1c8 /
//   Draw @0x0024b8e4 / Shutdown @0x0024b738. sizeof 0x38 (=56). Standalone,
//   NON-virtual (no base class, no vtable).
//
// Composes up to six BakedStringTTF layers, each a full copy of the same text baked
// with a different FONT_EFFECT, drawn back-to-front so the main glyph sits on top of
// its shadow/glow and under its stroke/bevel highlight:
//   m_pShadow  (shadowMode==1 ? STROKE : BLUR)  (gate shadowSize>0)  at pos + m_ShadowOffset
//   m_pGlow    STROKE     (gate glowSize>0)
//   m_pMain    NONE       (always)             owns the shared refRect
//   m_pStroke  INNER_GLOW (gate strokeSize>0)
//   m_pExtra1  BEVEL(4)   (gate extraSize>0, created with m_pExtra2)
//   m_pExtra2  BEVEL(4)   (gate extraSize>0)
//
// Each per-layer size arg (shadowSize/glowSize/strokeSize/extraSize) is passed to the
// BakedStringTTF ctor as its effectSize (6th float, drives the effect radius/count);
// p5 is passed as alignSigned (weight) to every layer. If circleRadius>0 each created
// layer gets ApplyFormatting_Circle(circleRadius).
//
// Every layer is drawn with the MAIN layer's refRect (BakedStringTTF::GetRefRect) so
// all layers align to the FG-label bbox regardless of their own effect padding.
//
// Usage:
//   FancyBakedString* s = new FancyBakedString(font, "PLAY", 40.0f, mainCol,
//                             0, 0.0f,              // p5, circleRadius
//                             glowSize, glowCol,
//                             shadowSize, shadowCol,
//                             strokeSize, strokeCol,
//                             0, 0.0f, 0,           // shadowMode, extraSize, p15
//                             extraCol1, extraCol2);
//   s->Draw(pos, Vec2(1,1), 0.0f, Mortar::ALIGN_CENTRE);
//   s->ApplyGradientSplit(col, 0.5f);   // recolour top/bottom split on the main layer
//   delete s;                            // Shutdown() frees the 6 layers
//
// Cross-build portability: no lambdas, no auto, no range-for, no enum class, non-virtual.

#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"
#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"

namespace Mortar {

// FancyBakedString -- sizeof 0x38 (=56). v1.6.1 @0x0024b600.
class FancyBakedString {
public:
    // ctor @0x0024b600 -> FancyBakedStringBuild @0x0024b1c8. Creates each optional layer
    // only when its size arg > 0; the main layer is always created. extraSize gates BOTH
    // bevel layers (created together). 17-arg ABI matches the binary exactly.
    FancyBakedString(FontCacheObjectTTF* font, const char* text, float fontSize,
                     Colour mainCol, int p5, float circleRadius,
                     float glowSize,   Colour glowCol,
                     float shadowSize, Colour shadowCol,
                     float strokeSize, Colour strokeCol,
                     int shadowMode, float extraSize, int p15,
                     Colour extraCol1, Colour extraCol2);

    // ~FancyBakedString -> Shutdown @0x0024b738.
    ~FancyBakedString();

    // FancyBakedStringBuild @0x0024b1c8: per-layer gated construction (see class doc).
    // mainCol is a non-const ref to match the binary's mangled ABI; read-only inside.
    void FancyBakedStringBuild(FontCacheObjectTTF* font, const char* text, float fontSize,
                               Colour& mainCol, int p5, float circleRadius,
                               float glowSize,   Colour glowCol,
                               float shadowSize, Colour shadowCol,
                               float strokeSize, Colour strokeCol,
                               int shadowMode, float extraSize, int p15,
                               Colour extraCol1, Colour extraCol2);

    // Shutdown @0x0024b738: delete + null the 6 layers.
    void Shutdown();

    // Draw @0x0024b8e4: rebuild the main layer if needed, then draw all present layers
    // back-to-front, each with the main layer's refRect. Shadow gets pos + m_ShadowOffset.
    void Draw(const _Vector3<float>& pos, _Vector2<float> scale, float tilt, ALIGNMENT_TYPE align);

    // Accessor for BakedStringBox::RebuildAlignments @0x00245c78 to write/read the
    // per-line local draw offset (+0x0c). See m_LineOffset field comment.
    _Vector3<float>& LineOffset() { return m_LineOffset; }

    // Rendered mesh ink bounds of the main (foreground) layer -- the same refRect
    // FancyBakedString::Draw shares with the glow/shadow/stroke/bevel layers.
    // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78: the per-line
    // horizontal align width is |GetBounds.right - GetBounds.left| read off the
    // ALREADY-BUILT line, i.e. the actual rendered extent (BakedStringTTF::UpdateBounds
    // @0x00247ed0/@0x00247dd4 -- floor(minVertX)..ceil(maxVertX) over every glyph quad),
    // NOT a pre-render ink-width estimate. The main layer is built eagerly by its ctor
    // (FullInternalRebuild), so bounds are valid immediately after FancyBakedStringBuild.
    MortarRectangleT<long>* GetBounds() { return m_pMain ? m_pMain->GetRefRect() : 0; }

    // Passthrough to the main layer's split recolour (finale ChangeText calls it 3x).
    void ApplyGradientSplit(Colour c, float y);

    // ApplyGradient @0x0024ad2c: uniform-colour main-layer gradient (top==bottom==c).
    // ASM-spec v1.6.1 FancyBakedString::ApplyGradient @0x0024ad2c.
    void ApplyGradient(Colour c);

    // ApplyGradient @0x0024accc: two-stop top/bottom main-layer gradient.
    // ASM-spec v1.6.1 FancyBakedString::ApplyGradient @0x0024accc.
    void ApplyGradient(Colour top, Colour bottom);

    // ApplyGradient @0x0024ae84: three-stop main-layer gradient -- top/bottom base,
    // then a mid-colour split at the 0.5 plane.
    // ASM-spec v1.6.1 FancyBakedString::ApplyGradient @0x0024ae84.
    void ApplyGradient(Colour top, Colour mid, Colour bottom);

    // ApplyMetallicGradient @0x0024abf4: top/bottom base gradient plus two close-set
    // split bands (0.51/0.49) that fake a metallic highlight streak on the main layer.
    // The gradient look is pure GPU interpolation: each ApplyGradientSplit flat-
    // recolours only the seam (edge-intersection) verts at its plane; every other
    // vertex keeps the TopBottom ramp colour, so GL smooth-shades between them.
    // ASM-spec v1.6.1 FancyBakedString::ApplyMetallicGradient @0x0024abf4.
    void ApplyMetallicGradient(Colour c0, Colour c1, Colour c2, Colour c3);

    // ApplyStrokeGradient @0x0024afb0: two-stop top/bottom gradient on the glow/stroke
    // layer (m_pGlow).
    // ASM-spec v1.6.1 FancyBakedString::ApplyStrokeGradient @0x0024afb0.
    void ApplyStrokeGradient(Colour top, Colour bottom);

    // ApplyStrokeGradient @0x0024b010: three-stop gradient on the glow/stroke layer.
    // ASM-spec v1.6.1 FancyBakedString::ApplyStrokeGradient @0x0024b010.
    void ApplyStrokeGradient(Colour top, Colour mid, Colour bottom);

private:
    // Init @0x0024b1c8 head: zero the six layer ptrs + offset/colour fields before Build.
    void Init();

    _Vector3<float> m_ShadowOffset; // +0x00 shadow layer draw offset

    // +0x0c: per-line LOCAL draw offset WITHIN a BakedStringBox (NOT read by
    // standalone FancyBakedString::Draw -- that method's shadow offset comes from
    // +0x00/m_ShadowOffset, never from here). WRITTEN by
    // BakedStringBox::RebuildAlignments @0x00245c78 per line; READ by the box's
    // per-line Draw as (boxPos + m_LineOffset). x = +0x0c horizontal align offset,
    // y = +0x10 baseline Y, z = +0x14 unused.
    // ASM-spec v1.6.1 FancyBakedString::m_LineOffset @+0x0c (RebuildAlignments @0x00245c78).
    _Vector3<float> m_LineOffset;   // +0x0c
    BakedStringTTF* m_pShadow;      // +0x18 BLUR
    BakedStringTTF* m_pGlow;        // +0x1c STROKE
    BakedStringTTF* m_pMain;        // +0x20 NONE (always present)
    BakedStringTTF* m_pStroke;      // +0x24 INNER_GLOW
    BakedStringTTF* m_pExtra1;      // +0x28 BEVEL
    BakedStringTTF* m_pExtra2;      // +0x2c BEVEL
    Colour          m_ShadowColour; // +0x30
    Colour          m_GlowColour;   // +0x34

#if defined(__bada__)
    friend struct FancyBakedStringLayoutAssert;
#endif
};

#if defined(__bada__)
static_assert(sizeof(FancyBakedString) == 0x38, "FancyBakedString sizeof mismatch");
struct FancyBakedStringLayoutAssert {
static_assert(__builtin_offsetof(FancyBakedString, m_ShadowOffset) == 0x00, "m_ShadowOffset offset");
static_assert(__builtin_offsetof(FancyBakedString, m_LineOffset)   == 0x0c, "m_LineOffset offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pShadow)      == 0x18, "m_pShadow offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pGlow)        == 0x1c, "m_pGlow offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pMain)        == 0x20, "m_pMain offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pStroke)      == 0x24, "m_pStroke offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pExtra1)      == 0x28, "m_pExtra1 offset");
static_assert(__builtin_offsetof(FancyBakedString, m_pExtra2)      == 0x2c, "m_pExtra2 offset");
static_assert(__builtin_offsetof(FancyBakedString, m_ShadowColour) == 0x30, "m_ShadowColour offset");
static_assert(__builtin_offsetof(FancyBakedString, m_GlowColour)   == 0x34, "m_GlowColour offset");
};
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FANCYBAKEDSTRING_H
