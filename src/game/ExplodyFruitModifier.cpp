// ExplodyFruitModifier — v1.6.1 explody-fruit modifier.
// Binary ctor @ 0x00134d10, ParseSpecific @ 0x0013514c,
// ApplyModifier @ 0x00135574, FruitWasSliced @ 0x001358d4 (v1.6.1).

#include "ExplodyFruitModifier.h"
#include "GameWork.h"
#include "game/GameOver.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"
#include "game/WaveManager.h"
#include "hud/HUD.h"
#include "hud/MissControl.h"
#include "util/StringHash.h"
#include "engine/util/Delegate.h"
#include "asset/TextureManager.h"
#include <tinyxml2.h>
#include <cmath>
#include <cstring>

// File-static combo-chain head and active-splosion scratch pointer.
// Both aliases of binary .bss 0x002D9FE8 (GOT[0x8EB8]).
// Used as chain head in FruitSplosion ctor and as active-splosion
// marker during CollisionResponse in FruitSplosion::Update.
// RE-ported: 0x1356fc / 0x1354ec — single file-static reused for both roles.
static ExplodyFruitModifier::FruitSplosion* s_pChainHead = nullptr;

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
    // ASM @ 0x00135620 (disasm 0x135654..0x135824).
    // HUDControl3d() base ctor already ran (member-init list). The scalar fields
    // m_p0..m_p3 (+0x84..+0x90), m_Const80 (+0x80 = DAT_00135868 = 0), m_pEntity
    // (+0x7c), m_typeIndex (+0x94), m_pChainNext/m_pChainHead (+0x98/+0x9c = 0)
    // are set by the init list above, matching the binary's stores.

    // +0x08 m_Pos = entity pos (binary loads entity+0x10..0x18 via ldmia,
    // stores to this+0x08..0x10). Mortar::Entity::pos is the Vec3 at +0x10.
    if (m_pEntity) {
        pos = m_pEntity->pos;
    }
    // +0x10 m_Pos.z overwritten with DAT_0013586c = 0xC59C3800f = -5000.0f
    // (binary: vstr s15,[r4,#0x10] right after the pos copy).
    pos.z = -5000.0f;

    // +0x34 m_LayerFlags = 0x80 (binary: mov r3,#0x80; str r3,[r4,#0x34]).
    m_LayerFlags = 0x80;

    // +0x74 m_Texture = LoadLocalisedTexture("explosion_radius.tex")
    // (binary: r1 = GOT-rel string @ 0x00280d8c; bl LoadLocalisedTexture 0x0010a758;
    //  result SmartPtr<Texture>::operator= into this+0x74).
    m_Texture = Mortar::TextureManager::LoadLocalisedTexture("explosion_radius.tex");

    // RE-ported: 0x001356b8 — subscribe FruitWasKilled to the per-fruit kill event.
    // Binary: Delegate1<void,Fruit*>::Make(this,&FruitSplosion::FruitWasKilled)
    // then Event1<Fruit*>::operator+= on (m_pEntity + 0x178).
    static_cast<Fruit*>(m_pEntity)->m_OnKilled +=
        Mortar::Delegate1<void, Fruit*>::Make(this, &FruitSplosion::FruitWasKilled);

    // RE-ported: 0x001356fc — chain into the file-static FruitSplosion combo list.
    if (s_pChainHead == nullptr) {
        m_ChainCount = 1;
    } else {
        m_ChainCount = 0;
        FruitSplosion* head = (s_pChainHead->m_pChainHead != nullptr)
                              ? s_pChainHead->m_pChainHead
                              : s_pChainHead;
        m_pChainHead = head;
        head->m_pChainNext = this;
        int n = ++head->m_ChainCount;
        if (n > 2 && m_typeIndex == 1) {
            MissControl::GetFree()->MakeCombo(pos, head->m_ChainCount, 0);
            FN::AddToCurrentScore(head->m_ChainCount, 0, false, false);
        }
        // Binary: this->m_RemoveCallback = Make(head, &ADingoAteMyBaby)
        // (str r7=r4+0x38 with object=head, not this)
        m_RemoveCallback =
            Mortar::Delegate1<void, HUDControl*>::Make(head, &FruitSplosion::ADingoAteMyBaby);
    }
}

ExplodyFruitModifier::FruitSplosion::~FruitSplosion()
{
    // RE-ported: 0x134EC0/0x134F88 — unsubscribe kill-event only if entity still alive.
    if (m_pEntity) {
        static_cast<Fruit*>(m_pEntity)->m_OnKilled -=
            Mortar::Delegate1<void, Fruit*>::Make(this, &FruitSplosion::FruitWasKilled);
    }
}

// RE-ported: 0x001352A0 — timer + radius collision push + alpha fade.
void ExplodyFruitModifier::FruitSplosion::Update(float dt)
{
    // RE-ported: 0x1352a0 — pause guard (game_work.m_Paused at +0x02)
    if (game_work.m_Paused) return;

    float wdt = WaveManager::GetInstance()->GetWavedt(0);
    float thr = m_p2;  // forceMax = EXPAND-phase end threshold
    m_Const80 += dt * wdt;

    if (m_Const80 <= thr) {
        // EXPAND + collision phase
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        // T.947 inverse-lerp: (m_Const80 - 0) / (m_p3 - 0), clamped [0,1]
        float tNorm = m_Const80 / m_p3;
        if (tNorm > 1.0f) tNorm = 1.0f;
        if (tNorm < 0.0f) tNorm = 0.0f;
        // SinTransition(t, 90.0f): sin(t * pi/2)
        float tSin = sinf(tNorm * 1.5707963f);
        float reach = m_p0 * tSin;  // local_28

        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* ent = am->GetEntityFirst(0, it);
             ent != nullptr;
             ent = am->GetEntityNext(0, it))
        {
            if (ent == m_pEntity) continue;
            Fruit* fruit = static_cast<Fruit*>(ent);
            if (fruit->IsActive() && !fruit->Sliced()) {
                Vec3 d = ent->pos - pos;
                if (d.MagnitudeSqr() < reach * reach) {
                    d.Normalise();
                    d *= 10.0f;
                    s_pChainHead = this;
                    ent->CollisionResponse(m_pEntity, 0, 0, nullptr);
                    s_pChainHead = nullptr;
                }
            }
        }
        // TODO: 0x135304 — expand visible quad: size = (*GOT[0x75D4] Vec3) * 2.0f * 0.84375f
        //   Requires shared size-base global behind GOT[0x75D4]; source not yet identified.
    } else {
        // FADE-OUT phase
        // inverse-lerp(m_Const80, m_p1, m_p3) clamped [0,1]
        float f;
        if (m_p3 == m_p1) {
            f = (m_Const80 >= m_p1) ? 1.0f : 0.0f;
        } else {
            f = (m_Const80 - m_p1) / (m_p3 - m_p1);
            if (f > 1.0f) f = 1.0f;
            if (f < 0.0f) f = 0.0f;
        }
        int a = (int)(f * 255.0f);
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        m_DrawColour.a = (uint8_t)a;
        if (m_p1 <= m_Const80 && m_p3 == 0.0f) m_bPendingRemoval = 1;
    }
}

// RE-ported: 0x00134E00 — single-instruction tail-call thunk to HUDControl3d::DrawOrder.
// No custom body; binary b 0x0010ddcc = HUDControl3d::DrawOrder (base).
void ExplodyFruitModifier::FruitSplosion::DrawOrder(
    const Vec3& hudScale, int layerMask)
{
    HUDControl3d::DrawOrder(hudScale, layerMask);
}

// RE-ported: 0x00134DEC — null the back-reference when the tracked fruit dies.
void ExplodyFruitModifier::FruitSplosion::FruitWasKilled(Fruit* fruit)
{
    if (m_pEntity == static_cast<Mortar::Entity*>(fruit))
        m_pEntity = nullptr;
}

// RE-ported: 0x00134E04 — removal callback for chain head; 'this' is the HEAD.
void ExplodyFruitModifier::FruitSplosion::ADingoAteMyBaby(HUDControl* ctrl)
{
    if (static_cast<HUDControl*>(m_pChainNext) == ctrl) {
        if (m_ChainCount > 2 && m_typeIndex == 1) {
            MissControl::GetFree()->MakeCombo(pos, m_ChainCount, 0);
            FN::AddToCurrentScore(m_ChainCount, 0, false, false);
        }
        m_bPendingRemoval = 1;
    }
}

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

void ExplodyFruitModifier::ResetSpecific() {
    Fruit::FruitWasSlicedEvent() -=
        Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
            this, &ExplodyFruitModifier::FruitWasSliced);
}

int ExplodyFruitModifier::UpdateSpecific(float /*dt*/) { return 0; }

// @ 0x00135574
// Binary: chain base ApplyModifier, then (if m_BonusAccum+0x0c<=0) register
//   Delegate3<void,Fruit*,int,Mortar::Entity*>::Make(this,&ExplodyFruitModifier::FruitWasSliced)
//     += g_FruitWasSliced (Fruit.cpp file-static, GOT 0x332a34).
void ExplodyFruitModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_BonusAccum <= 0) {
        Fruit::FruitWasSlicedEvent() +=
            Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
                this, &ExplodyFruitModifier::FruitWasSliced);
    }
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

// @ 0x001358d4 (function entry 0x00135888)
// Delegate3<void,Fruit*,int,Mortar::Entity*> target; subscribed in ApplyModifier.
// Binary disasm:
//   type  = (u8)fruit->m_FruitType           ; ldrb r0,[r1,#0x3c]  (r1 = first delegate arg = Fruit*)
//   info  = Fruit::FruitInfo(type)           ; = &g_FruitInfoArray[type] (stride 0x338 in v1.6.1)
//   if (info->m_bIsSuperFruit /*+0x330*/) return;   ; ldrb r3,[r0,#0x330]; cmp r3,#0; bne return
//   new(0xa4) FruitSplosion(modifier+0x20,+0x24,+0x28,+0x2c, FRUIT*, modifier+0x30 as int)
//       NOTE: the ctor's entity arg (r1) is the FRUIT pointer (r7), NOT the 3rd Entity* delegate arg.
//   HUD::AddControl(*(GameWork+0x40), splosion, 0)   ; *(GameWork+0x40) == game_work.mHud
// DIFFERS: the prior port body checked game_work[0x330] (a band-aid) and passed the 3rd 'entity'
//   delegate arg to the ctor; both are wrong per binary. Corrected to the FruitInfo super-fruit
//   gate and to passing the fruit. Binary does NOT null-check fruit or the HUD here.
// LAYOUT NOTE: v1.6.1 reads the HUD pointer at GameWork+0x40 (confirmed in FruitWasSliced and
//   WaveManager::Reset @0x0012bd0c). The port header places mHud at +0x3c with a 4-byte pad at
//   +0x40 (v1.5.1 layout). Symbolic game_work.mHud is used here so this function stays faithful
//   regardless; the GameWork +0x3c/+0x40 reconciliation is a separate struct-wide task.
void ExplodyFruitModifier::FruitWasSliced(
    Fruit* fruit, int /*score*/, Mortar::Entity* /*entity*/)
{
    const ::FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (info->m_bIsSuperFruit != 0) return;   // FRUIT_INFO+0x330: super-fruit doesn't explode

    FruitSplosion* splosion = new FruitSplosion(
        m_ForceMin, m_ForceInc, m_ForceMax, m_Radius,
        fruit, (int)m_FruitTypeIndex);

    game_work.mHud->AddControl(splosion, false);
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
