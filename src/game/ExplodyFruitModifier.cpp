// ExplodyFruitModifier — v1.6.1 explody-fruit modifier.
// Binary ctor @ 0x00134d10, ParseSpecific @ 0x0013514c,
// ApplyModifier @ 0x00135574, FruitWasSliced @ 0x001358d4 (v1.6.1).

#include "ExplodyFruitModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "hud/HUD.h"
#include "util/StringHash.h"
#include <tinyxml2.h>
#include <cstring>

// ---- FruitSplosion ----------------------------------------------------------

// ctor @ 0x135620
ExplodyFruitModifier::FruitSplosion::FruitSplosion(
    float p0, float p1, float p2, float p3,
    Mortar::Entity* entity, int typeIndex)
    : HUDControl3d()
    , m_pEntity(entity)
    , m_Const80(0)
    , m_p0(p0)
    , m_p1(p1)
    , m_p2(p2)
    , m_p3(p3)
    , m_typeIndex(typeIndex)
    , m_pChainNext(nullptr)
    , m_pChainHead(nullptr)
    , m_ChainCount(0)
{
    // TODO: 0x00135620 — copy entity pos to m_Pos (+0x08); set flags=0x80 (+0x34);
    // register FruitWasKilled delegate; register ADingoAteMyBaby removal delegate;
    // chain into global splosion linked list for combo detection
}

ExplodyFruitModifier::FruitSplosion::~FruitSplosion() {}

// TODO: 0x00135620 — FruitSplosion::Update: particle burst update logic
void ExplodyFruitModifier::FruitSplosion::Update(float /*dt*/) {}

// TODO: 0x00135620 — FruitSplosion::DrawOrder: particle burst draw
void ExplodyFruitModifier::FruitSplosion::DrawOrder(
    const Vec3& /*hudScale*/, int /*layerMask*/) {}

// TODO: 0x00135620 — FruitWasKilled body not yet ported
void ExplodyFruitModifier::FruitSplosion::FruitWasKilled(Fruit* /*fruit*/) {}

// TODO: 0x00135620 — ADingoAteMyBaby body not yet ported
void ExplodyFruitModifier::FruitSplosion::ADingoAteMyBaby(HUDControl* /*ctrl*/) {}

// ---- ExplodyFruitModifier ---------------------------------------------------

// ctor @ 0x00134d10
ExplodyFruitModifier::ExplodyFruitModifier()
    : GameModifier()
    , m_ForceMin(100.0f)   // UNK_134d64
    , m_ForceInc(0.25f)
    , m_ForceMax(0.0f)
    , m_Radius(0.2f)       // UNK_134d68
    , m_FruitTypeIndex(0)
{}

ExplodyFruitModifier::~ExplodyFruitModifier() {}

void ExplodyFruitModifier::ResetSpecific() {}

int ExplodyFruitModifier::UpdateSpecific(float /*dt*/) { return 0; }

// @ 0x00135574
// Binary: chain base ApplyModifier, then (if m_BonusAccum+0x0c<=0) register
//   Delegate3<void,Fruit*,int,Mortar::Entity*>::Make(this,&ExplodyFruitModifier::FruitWasSliced)
//     += FruitManager::m_FruitWasSliced (Event3<Fruit*,int,Mortar::Entity*>).
// TODO: 0x00135574 — subscribe FruitWasSliced delegate to FruitManager's
//   Event3<Fruit*,int,Mortar::Entity*> m_FruitWasSliced.
//   FruitManager not yet ported; event owner unknown in current port.
void ExplodyFruitModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    // Delegate registration deferred — event owner (FruitManager) not yet ported.
}

// @ 0x0013514c
// Binary reads 4 floats -> +0x20,+0x24,+0x28,+0x2c; then FindIndex on 3 hashes -> +0x30.
// Post-parse: +0x28 += +0x24; +0x2c += +0x28.
void ExplodyFruitModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;

    xml->QueryFloatAttribute("forceMin", &m_ForceMin);
    xml->QueryFloatAttribute("forceInc", &m_ForceInc);
    xml->QueryFloatAttribute("forceMax", &m_ForceMax);
    xml->QueryFloatAttribute("radius",   &m_Radius);

    // FindIndex: hash 3 type-name strings, compare against XML "type" attr
    static const uint32_t kHashes[3] = {
        StringHash("type0"),
        StringHash("type1"),
        StringHash("type2")
    };
    const char* typeAttr = xml->Attribute("type");
    m_FruitTypeIndex = 0;
    if (typeAttr) {
        uint32_t h = StringHash(typeAttr);
        for (int i = 0; i < 3; ++i) {
            if (h == kHashes[i]) { m_FruitTypeIndex = (uint32_t)i; break; }
        }
    }

    // Post-parse adjustments per binary @ 0x0013514c
    m_ForceMax += m_ForceInc;
    m_Radius   += m_ForceMax;
}

// @ 0x001358d4
// Delegate3<void,Fruit*,int,Mortar::Entity*> target; subscribed in ApplyModifier.
// Binary: if game_work byte[+0x330] != 0 -> return;
// else new(0xa4) FruitSplosion(+0x20,+0x24,+0x28,+0x2c, fruit, +0x30 as int);
//      HUD::AddControl(GameHUD, splosion, 0)
// TODO: 0x001358d4 — binary passes hudRoot+0x40 as first arg to AddControl; resolve HUD subtree offset
void ExplodyFruitModifier::FruitWasSliced(
    Fruit* fruit, int /*score*/, Mortar::Entity* entity)
{
    if (!fruit) return;
    if (reinterpret_cast<const uint8_t*>(&game_work)[0x330] != 0) return;

    FruitSplosion* splosion = new FruitSplosion(
        m_ForceMin, m_ForceInc, m_ForceMax, m_Radius,
        entity, (int)m_FruitTypeIndex);

    if (game_work.mHud) {
        game_work.mHud->AddControl(splosion, false);
    }
}

GameModifier* ExplodyFruitModifier::Clone() {
    ExplodyFruitModifier* c = new ExplodyFruitModifier();
    c->m_Duration       = m_Duration;
    c->field_0x08       = field_0x08;
    c->m_BonusAccum     = m_BonusAccum;
    c->m_bDeferred      = m_bDeferred;
    c->m_DeferTime      = m_DeferTime;
    c->m_bApplied       = m_bApplied;
    c->m_pDeferInfo     = m_pDeferInfo;
    c->m_ForceMin       = m_ForceMin;
    c->m_ForceInc       = m_ForceInc;
    c->m_ForceMax       = m_ForceMax;
    c->m_Radius         = m_Radius;
    c->m_FruitTypeIndex = m_FruitTypeIndex;
    return c;
}
