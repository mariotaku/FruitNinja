// ExplodyFruitModifier — v1.6.1 explody-fruit modifier.
// Binary ctor @ 0x00134d10, ParseSpecific @ 0x0013514c,
// ApplyModifier @ 0x00135574, FruitWasSliced @ 0x00135888.

#include "ExplodyFruitModifier.h"
#include "entities/Fruit.h"
#include <tinyxml2.h>
#include <cstring>

// ---- FruitSplosion ----------------------------------------------------------

ExplodyFruitModifier::FruitSplosion::FruitSplosion(
    float param0, float param1, float param2, float param3,
    Fruit* fruit, int count)
    : HUDControl()
    , m_Param0(param0)
    , m_Param1(param1)
    , m_Param2(param2)
    , m_Param3(param3)
    , m_Count(count)
    , m_pFruit(fruit)
{
    memset(_pad8c, 0, sizeof(_pad8c));
}

ExplodyFruitModifier::FruitSplosion::~FruitSplosion() {}

// TODO: 0x00135888 — FruitSplosion::Update: particle burst update logic
void ExplodyFruitModifier::FruitSplosion::Update(float /*dt*/) {}

// TODO: 0x00135888 — FruitSplosion::DrawOrder: particle burst draw
void ExplodyFruitModifier::FruitSplosion::DrawOrder(
    const Vec3& /*hudScale*/, int /*layerMask*/) {}

void ExplodyFruitModifier::FruitSplosion::FruitWasKilled(Fruit* /*fruit*/) {}

void ExplodyFruitModifier::FruitSplosion::ADingoAteMyBaby(HUDControl* /*ctrl*/) {}

// ---- ExplodyFruitModifier ---------------------------------------------------

ExplodyFruitModifier::ExplodyFruitModifier()
    : GameModifier()
    , m_ForceMin(0.0f)
    , m_ForceInc(0.0f)
    , m_ForceMax(0.0f)
    , m_Radius(0.0f)
    , m_Count(0)
{}

ExplodyFruitModifier::~ExplodyFruitModifier() {}

void ExplodyFruitModifier::ResetSpecific() {}

int ExplodyFruitModifier::UpdateSpecific(float /*dt*/) { return 0; }

// @ 0x00135574
// Binary: chain base ApplyModifier, then register FruitWasSliced as Delegate3.
// TODO: 0x00135574 — register FruitWasSliced on FruitManager's slice signal
void ExplodyFruitModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    // Delegate registration deferred — signal infrastructure not yet ported.
}

// @ 0x0013514c
// Binary reads 4 float attrs from the XML element, then:
//   +0x28 += +0x24  (m_ForceMax += m_ForceInc)
//   +0x2c += +0x28  (m_Radius   += m_ForceMax)
// And reads an int attr (vector of size 3 -> stored at +0x30).
void ExplodyFruitModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;
    xml->QueryFloatAttribute("forceMin", &m_ForceMin);
    xml->QueryFloatAttribute("forceInc", &m_ForceInc);
    xml->QueryFloatAttribute("forceMax", &m_ForceMax);
    xml->QueryFloatAttribute("radius",   &m_Radius);
    xml->QueryIntAttribute  ("count",    &m_Count);

    // Post-parse adjustments per binary @ 0x0013514c
    m_ForceMax += m_ForceInc;
    m_Radius   += m_ForceMax;
}

// @ 0x00135888
// Binary: skip if owning HUD/state byte[+0x330]!=0; else new FruitSplosion(0xa4)
// and HUD::AddControl.
// TODO: 0x00135888 — wire to FruitManager's FruitWasSliced signal
// TODO: 0x00135888 — check HUD/state byte[+0x330] gate before spawning
// TODO: 0x00135888 — call HUD::AddControl(splosion) after creating FruitSplosion
void ExplodyFruitModifier::FruitWasSliced(
    Fruit* fruit, int /*score*/, Mortar::Entity* /*entity*/)
{
    if (!fruit) return;
    // Stub: FruitSplosion spawn deferred pending HUD::AddControl port.
    (void)new FruitSplosion(m_ForceMin, m_ForceInc, m_ForceMax, m_Radius, fruit, m_Count);
    // Note: in production the splosion would be owned by HUD; here it leaks.
    // Replace with HUD::AddControl call when available.
}

GameModifier* ExplodyFruitModifier::Clone() {
    ExplodyFruitModifier* c = new ExplodyFruitModifier();
    c->m_Duration           = m_Duration;
    c->field_0x08           = field_0x08;
    c->m_Duration_remaining = m_Duration_remaining;
    c->m_bDeferred          = m_bDeferred;
    c->m_DeferStart         = m_DeferStart;
    c->m_bApplied           = m_bApplied;
    c->m_pOwner             = m_pOwner;
    c->m_ForceMin           = m_ForceMin;
    c->m_ForceInc           = m_ForceInc;
    c->m_ForceMax           = m_ForceMax;
    c->m_Radius             = m_Radius;
    c->m_Count              = m_Count;
    return c;
}
