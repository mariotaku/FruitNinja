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

#include "math/Vec2.h"
#include "math/Vec3.h"
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
    void FancyBakedStringBuild(FontCacheObjectTTF* font, const char* text, float fontSize,
                               Colour mainCol, int p5, float circleRadius,
                               float glowSize,   Colour glowCol,
                               float shadowSize, Colour shadowCol,
                               float strokeSize, Colour strokeCol,
                               int shadowMode, float extraSize, int p15,
                               Colour extraCol1, Colour extraCol2);

    // Shutdown @0x0024b738: delete + null the 6 layers.
    void Shutdown();

    // Draw @0x0024b8e4: rebuild the main layer if needed, then draw all present layers
    // back-to-front, each with the main layer's refRect. Shadow gets pos + m_ShadowOffset.
    void Draw(const Vec3& pos, Vec2 scale, float tilt, ALIGNMENT_TYPE align);

    // Passthrough to the main layer's split recolour (finale ChangeText calls it 3x).
    void ApplyGradientSplit(Colour c, float y);

private:
    // Init @0x0024b1c8 head: zero the six layer ptrs + offset/colour fields before Build.
    void Init();

    Vec3            m_ShadowOffset; // +0x00 shadow layer draw offset
    Vec3            m_Field0c;      // +0x0c // TODO: +0xc unresolved, candidate m_Translation
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
static_assert(__builtin_offsetof(FancyBakedString, m_Field0c)      == 0x0c, "m_Field0c offset");
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
