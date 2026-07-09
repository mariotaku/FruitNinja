// Mortar::FancyBakedString -- multi-layer TTF label wrapper.
// v1.6.1 @0x0024b600 (ctor) / @0x0024b1c8 (Build) / @0x0024b8e4 (Draw) / @0x0024b738 (Shutdown).
//
// ASM-spec v1.6.1 Mortar::FancyBakedString ctor @0x0024b600 / Build @0x0024b1c8 /
//   Draw @0x0024b8e4 / Shutdown @0x0024b738. sizeof 0x38 (56B).

#include "render/FancyBakedString.h"

namespace Mortar {

FancyBakedString::FancyBakedString(FontCacheObjectTTF* font, const char* text, float fontSize,
                                   Colour mainCol, int p5, float circleRadius,
                                   float glowSize,   Colour glowCol,
                                   float shadowSize, Colour shadowCol,
                                   float strokeSize, Colour strokeCol,
                                   int shadowMode, float extraSize, int p15,
                                   Colour extraCol1, Colour extraCol2)
{
    FancyBakedStringBuild(font, text, fontSize, mainCol, p5, circleRadius,
                          glowSize, glowCol, shadowSize, shadowCol,
                          strokeSize, strokeCol, shadowMode, extraSize, p15,
                          extraCol1, extraCol2);
}

FancyBakedString::~FancyBakedString()
{
    Shutdown();
}

// Init: zero the six layer ptrs + offset/colour fields (Build head @0x0024b1c8).
void FancyBakedString::Init()
{
    m_ShadowOffset = Vec3(0.0f, 0.0f, 0.0f);
    m_LineOffset   = Vec3(0.0f, 0.0f, 0.0f);
    m_pShadow = 0;
    m_pGlow   = 0;
    m_pMain   = 0;
    m_pStroke = 0;
    m_pExtra1 = 0;
    m_pExtra2 = 0;
    m_ShadowColour = Colour(0, 0, 0, 255);
    m_GlowColour   = Colour(0, 0, 0, 255);
}

// FancyBakedStringBuild @0x0024b1c8: create each optional layer only when its size arg
// > 0, each with its FONT_EFFECT. Main layer always created (NONE). Bevel pair gated
// together. p5 -> alignSigned (weight); per-layer size -> effectSize (6th float).
// circleRadius>0 arcs each created layer.
void FancyBakedString::FancyBakedStringBuild(FontCacheObjectTTF* font, const char* text, float fontSize,
                                             Colour mainCol, int p5, float circleRadius,
                                             float glowSize,   Colour glowCol,
                                             float shadowSize, Colour shadowCol,
                                             float strokeSize, Colour strokeCol,
                                             int shadowMode, float extraSize, int /*p15*/,
                                             Colour extraCol1, Colour extraCol2)
{
    Init();

    if (shadowSize > 0.0f) {
        FontCacheObjectTTF::FONT_EFFECT_ENUM se = (shadowMode == 1)
            ? FontCacheObjectTTF::FONT_EFFECT_STROKE
            : FontCacheObjectTTF::FONT_EFFECT_BLUR;
        m_pShadow = new BakedStringTTF(font, text, fontSize, shadowCol, (long)p5, shadowSize, se);
        if (circleRadius > 0.0f) m_pShadow->ApplyFormatting_Circle(circleRadius);
    }
    if (glowSize > 0.0f) {
        m_pGlow = new BakedStringTTF(font, text, fontSize, glowCol, (long)p5, glowSize,
                                     FontCacheObjectTTF::FONT_EFFECT_STROKE);
        if (circleRadius > 0.0f) m_pGlow->ApplyFormatting_Circle(circleRadius);
    }

    m_pMain = new BakedStringTTF(font, text, fontSize, mainCol, (long)p5, 0.0f,
                                 FontCacheObjectTTF::FONT_EFFECT_NONE);
    if (circleRadius > 0.0f) m_pMain->ApplyFormatting_Circle(circleRadius);

    if (strokeSize > 0.0f) {
        m_pStroke = new BakedStringTTF(font, text, fontSize, strokeCol, (long)p5, strokeSize,
                                       FontCacheObjectTTF::FONT_EFFECT_INNER_GLOW);
        if (circleRadius > 0.0f) m_pStroke->ApplyFormatting_Circle(circleRadius);
    }
    if (extraSize > 0.0f) {
        // BEVEL == 4 (from #257); the port enum tops out at INNER_GLOW(3), so cast.
        FontCacheObjectTTF::FONT_EFFECT_ENUM bevel = (FontCacheObjectTTF::FONT_EFFECT_ENUM)4;
        m_pExtra1 = new BakedStringTTF(font, text, fontSize, extraCol1, (long)p5, extraSize, bevel);
        if (circleRadius > 0.0f) m_pExtra1->ApplyFormatting_Circle(circleRadius);
        m_pExtra2 = new BakedStringTTF(font, text, fontSize, extraCol2, (long)p5, extraSize, bevel);
        if (circleRadius > 0.0f) m_pExtra2->ApplyFormatting_Circle(circleRadius);
    }
}

// Shutdown @0x0024b738: delete + null the 6 layers.
void FancyBakedString::Shutdown()
{
    delete m_pShadow; m_pShadow = 0;
    delete m_pGlow;   m_pGlow   = 0;
    delete m_pMain;   m_pMain   = 0;
    delete m_pStroke; m_pStroke = 0;
    delete m_pExtra1; m_pExtra1 = 0;
    delete m_pExtra2; m_pExtra2 = 0;
}

// Draw @0x0024b8e4: rebuild the main layer if needed, then draw all present layers
// back-to-front sharing the main layer's refRect. Shadow drawn at pos + m_ShadowOffset.
void FancyBakedString::Draw(const Vec3& pos, Vec2 scale, float tilt, ALIGNMENT_TYPE align)
{
    if (!m_pMain) return;
    if (!m_pMain->m_SurfacesBuilt) m_pMain->FullInternalRebuild();
    MortarRectangleT<long>* rr = m_pMain->GetRefRect();

    if (m_pShadow) m_pShadow->Draw(pos + m_ShadowOffset, scale, tilt, align, rr);
    if (m_pGlow)   m_pGlow->Draw(pos, scale, tilt, align, rr);
    m_pMain->Draw(pos, scale, tilt, align, rr);
    if (m_pStroke) m_pStroke->Draw(pos, scale, tilt, align, rr);
    if (m_pExtra1) m_pExtra1->Draw(pos, scale, tilt, align, rr);
    if (m_pExtra2) m_pExtra2->Draw(pos, scale, tilt, align, rr);
}

void FancyBakedString::ApplyGradientSplit(Colour c, float y)
{
    if (m_pMain) m_pMain->ApplyGradientSplit(c, y);
}

// ApplyGradient @0x0024ad2c: uniform colour -- top==bottom==c.
void FancyBakedString::ApplyGradient(Colour c)
{
    if (m_pMain) m_pMain->ApplyGradient_TopBottom(c, c);
}

// ApplyGradient @0x0024accc: two-stop top/bottom gradient.
void FancyBakedString::ApplyGradient(Colour top, Colour bottom)
{
    if (m_pMain) m_pMain->ApplyGradient_TopBottom(top, bottom);
}

// ApplyGradient @0x0024ae84: top/bottom base gradient plus a mid-colour split at
// the 0.5 plane.
void FancyBakedString::ApplyGradient(Colour top, Colour mid, Colour bottom)
{
    if (!m_pMain) return;
    m_pMain->ApplyGradient_TopBottom(top, bottom);
    m_pMain->ApplyGradientSplit(mid, 0.5f);
}

// ApplyMetallicGradient @0x0024abf4: top/bottom base gradient (c0/c3) plus two
// close-set split bands (0.51/0.49) that fake a metallic highlight streak.
void FancyBakedString::ApplyMetallicGradient(Colour c0, Colour c1, Colour c2, Colour c3)
{
    if (!m_pMain) return;
    m_pMain->ApplyGradient_TopBottom(c0, c3);
    m_pMain->ApplyGradientSplit(c1, 0.51f);
    m_pMain->ApplyGradientSplit(c2, 0.49f);
}

// ApplyStrokeGradient @0x0024afb0: two-stop top/bottom gradient on the glow/stroke layer.
void FancyBakedString::ApplyStrokeGradient(Colour top, Colour bottom)
{
    if (m_pGlow) m_pGlow->ApplyGradient_TopBottom(top, bottom);
}

// ApplyStrokeGradient @0x0024b010: three-stop gradient on the glow/stroke layer.
void FancyBakedString::ApplyStrokeGradient(Colour top, Colour mid, Colour bottom)
{
    if (!m_pGlow) return;
    m_pGlow->ApplyGradient_TopBottom(top, bottom);
    m_pGlow->ApplyGradientSplit(mid, 0.5f);
}

} // namespace Mortar
