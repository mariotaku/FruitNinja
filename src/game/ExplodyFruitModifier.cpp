// ExplodyFruitModifier — v1.6.1 explody-fruit modifier.
// Binary ctor @ 0x00134d10, ParseSpecific @ 0x0013514c,
// ApplyModifier @ 0x00135574, FruitWasSliced @ 0x00135888.

#include "ExplodyFruitModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "hud/HUD.h"
#include "math/Vec3.h"
#include <tinyxml2.h>
#include <cstring>

// ---- FruitSplosion ----------------------------------------------------------

ExplodyFruitModifier::FruitSplosion::FruitSplosion(
    float param0, float param1, float param2, float param3,
    Fruit* fruit, const Vec3& vec)
    : HUDControl()
    , m_Param0(param0)
    , m_Param1(param1)
    , m_Param2(param2)
    , m_Param3(param3)
    , m_pFruit(fruit)
    , m_Vec(vec)
{
    memset(_pad94, 0, sizeof(_pad94));
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
    // ASM-spec ExplodyFruitModifier ctor (binary @ 0x00134ca8): +0x20=100.0f (DAT_00134cfc),
    // +0x24=0.25f, +0x28=0.0f (DAT_00134d00 / alias 0x00134d68), +0x2c=0.2f (DAT_00134d04 /
    // alias 0x00134d6c). Field names provisional (force-vs-delay inferred from usage shape).
    , m_ForceMin(100.0f)    // +0x20
    , m_ForceInc(0.25f)     // +0x24
    , m_ForceMax(0.0f)      // +0x28
    , m_Radius(0.2f)        // +0x2c
    // DIFFERS: port models +0x30 as Vec3 m_SplosionVec; binary +0x30 is a uint32 handle
    // (func_0x00111120 return), and vecX/Y/Z are parse-locals, not stored. TODO: re-RE
    // FruitSplosion ctor to retype the 6th arg + +0x30 (tracked separately).
    , m_SplosionVec()
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
// Binary reads 4 floats -> +0x20, +0x24, +0x28, +0x2c; a Vec3 -> +0x30.
// Post-parse: +0x28 += +0x24; +0x2c += +0x28.
void ExplodyFruitModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;
    xml->QueryFloatAttribute("forceMin", &m_ForceMin);
    xml->QueryFloatAttribute("forceInc", &m_ForceInc);
    xml->QueryFloatAttribute("forceMax", &m_ForceMax);
    xml->QueryFloatAttribute("radius",   &m_Radius);
    xml->QueryFloatAttribute("vecX",     &m_SplosionVec.x);
    xml->QueryFloatAttribute("vecY",     &m_SplosionVec.y);
    xml->QueryFloatAttribute("vecZ",     &m_SplosionVec.z);

    // Post-parse adjustments per binary @ 0x0013514c
    m_ForceMax += m_ForceInc;
    m_Radius   += m_ForceMax;
}

// @ 0x00135888
// Binary: if game_work byte[+0x330] != 0 -> return;
// else new FruitSplosion(0xa4)(+0x20,+0x24,+0x28,+0x2c, fruit, +0x30);
//      HUD::AddControl(hudRoot+0x40, this, false)
// TODO: 0x00135888 — wire to FruitManager's FruitWasSliced signal
// TODO: 0x00135888 — binary passes hudRoot+0x40 as first arg to AddControl; resolve HUD subtree offset
void ExplodyFruitModifier::FruitWasSliced(
    Fruit* fruit, int /*score*/, Mortar::Entity* /*entity*/)
{
    if (!fruit) return;
    // game_work byte[+0x330]: lives in buf1 region (buf1 = +0x2B1, offset +0x330-0x2B1 = +0x7F)
    if (reinterpret_cast<const uint8_t*>(&game_work)[0x330] != 0) return;

    FruitSplosion* splosion = new FruitSplosion(
        m_ForceMin, m_ForceInc, m_ForceMax, m_Radius, fruit, m_SplosionVec);

    if (game_work.mHud) {
        game_work.mHud->AddControl(splosion, false);
    }
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
    c->m_SplosionVec        = m_SplosionVec;
    return c;
}
