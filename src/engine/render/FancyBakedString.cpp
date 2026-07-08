// Mortar::FancyBakedString -- multi-layer TTF label wrapper.
// v1.6.1 @0x0024b600 (ctor) / @0x0024b1c8 (Build) / @0x0024b8e4 (Draw) / @0x0024b738 (Shutdown).
//
// ASM-spec v1.6.1 Mortar::FancyBakedString ctor @0x0024b600 / Build @0x0024b1c8 /
//   Draw @0x0024b8e4 / Shutdown @0x0024b738. sizeof 0x38 (56B).

#include "render/FancyBakedString.h"

namespace Mortar {

FancyBakedString::FancyBakedString(FontCacheObjectTTF* font, const char* text, float size,
                                   Colour mainCol,
                                   float shadowSize, Colour shadowCol,
                                   float glowSize,   Colour glowCol,
                                   float strokeSize, Colour strokeCol,
                                   float extraSize,  Colour extraCol1, Colour extraCol2)
    : m_ShadowOffset(0.0f, 0.0f, 0.0f)
    , m_Field0c(0.0f, 0.0f, 0.0f)
    , m_pShadow(0)
    , m_pGlow(0)
    , m_pMain(0)
    , m_pStroke(0)
    , m_pExtra1(0)
    , m_pExtra2(0)
    , m_ShadowColour(0, 0, 0, 255)
    , m_GlowColour(0, 0, 0, 255)
{
    Build(font, text, size, mainCol,
          shadowSize, shadowCol, glowSize, glowCol,
          strokeSize, strokeCol, extraSize, extraCol1, extraCol2);
}

FancyBakedString::~FancyBakedString()
{
    Shutdown();
}

// Build @0x0024b1c8: create each optional layer only when its size arg > 0, each with
// its FONT_EFFECT. Main layer always created (NONE). Bevel pair gated together.
void FancyBakedString::Build(FontCacheObjectTTF* font, const char* text, float size,
                             Colour mainCol,
                             float shadowSize, Colour shadowCol,
                             float glowSize,   Colour glowCol,
                             float strokeSize, Colour strokeCol,
                             float extraSize,  Colour extraCol1, Colour extraCol2)
{
    if (shadowSize > 0.0f) {
        m_pShadow = new BakedStringTTF(font, text, size, shadowCol, (long)shadowSize,
                                       0.0f, FontCacheObjectTTF::FONT_EFFECT_BLUR);
    }
    if (glowSize > 0.0f) {
        m_pGlow = new BakedStringTTF(font, text, size, glowCol, (long)glowSize,
                                     0.0f, FontCacheObjectTTF::FONT_EFFECT_STROKE);
    }

    m_pMain = new BakedStringTTF(font, text, size, mainCol, 0,
                                 0.0f, FontCacheObjectTTF::FONT_EFFECT_NONE);

    if (strokeSize > 0.0f) {
        m_pStroke = new BakedStringTTF(font, text, size, strokeCol, (long)strokeSize,
                                       0.0f, FontCacheObjectTTF::FONT_EFFECT_INNER_GLOW);
    }
    if (extraSize > 0.0f) {
        // BEVEL == 4 (from #257); the port enum tops out at INNER_GLOW(3), so cast.
        FontCacheObjectTTF::FONT_EFFECT_ENUM bevel = (FontCacheObjectTTF::FONT_EFFECT_ENUM)4;
        m_pExtra1 = new BakedStringTTF(font, text, size, extraCol1, (long)extraSize, 0.0f, bevel);
        m_pExtra2 = new BakedStringTTF(font, text, size, extraCol2, (long)extraSize, 0.0f, bevel);
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

} // namespace Mortar
