// SuperFruitControl — super-fruit (pomegranate/starfruit frenzy) state machine.
// Binary: ctor @ 0x001be1c8, restore-from-save ctor @ 0x001bea90,
//         Update @ 0x001bca10, Sliced @ 0x001bb994, ExplodeSuperFruit @ 0x001baa20,
//         SuperFruitThrown @ 0x001bbf48, SuperFruitSliced @ 0x001be630,
//         IsInSuperFruitState @ 0x001b9828, Reset @ 0x001bb52c, Release @ 0x001bb664,
//         StopAllFruit @ 0x001ba460, SaveSuperFruitState @ 0x001ba73c,
//         ComboCancel @ 0x001b9850.
//
// VFX-heavy bodies (ExplodeSuperFruit, SpawnRay, DrawExplosion, full Update
// phases beyond the timer ladder) are stubbed with // TODO: <addr> markers.

#include "SuperFruitControl.h"
#include "SuperFruitGlow.h"
#include "SuperFruitState.h"
#include "Fruit.h"
#include "FruitInfo.h"
#include "Bomb.h"
#include "SlashEntity.h"
#include "ActorManager.h"
#include "game/GameWork.h"
#include "game/GameOver.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <map>
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>

// Static map definition (24-byte std::map per CLAUDE.md).
std::map<Fruit*, SuperFruitControl*> SuperFruitControl::SuperFruitControls;

// Static session counter: number of pomegranates spawned this game.
// Binary: BSS global; accessed via IsInSuperFruitState / NumPomegranatesSpawnedThisGame.
static int s_PomegranatesSpawnedThisGame = 0;

// Static active-state flag. IsInSuperFruitState() reads game singleton +0x14.
// Port: shadow the flag here since game+0x14 is not yet mapped in the port.
static int s_SuperFruitActive = 0;

// Binary @ 0x001be1c8: fresh controller ctor.
SuperFruitControl::SuperFruitControl(Fruit* fruit)
    : m_pHostFruit(fruit)
    , m_HitCount(0.0f)
    , m_Timer(0.0f)
    , m_PrevTimer(0.0f)
    , m_SliceCount(0)
    , m_pLinkedSlasher(nullptr)
    , m_Lifetime(5.0f)   // default baseline; TODO: 0x001be1c8 -- resolve from binary DAT
    , m_FadeIn(0.0f)
    , m_Scale(0.0f)
    , m_SliceCooldown(0)
    , m_pGlow(nullptr)
{
    entityType = 6;  // super-fruit type in binary
    memset(_pad_own, 0, sizeof(_pad_own));
    memset(_pad_80, 0, sizeof(_pad_80));
    memset(_pad_98, 0, sizeof(_pad_98));
    memset(&m_WorkVec1, 0, sizeof(m_WorkVec1));
    memset(&m_WorkVec2, 0, sizeof(m_WorkVec2));
    memset(&m_WorkVec3, 0, sizeof(m_WorkVec3));
    memset(&m_WorkVec4, 0, sizeof(m_WorkVec4));
    memset(_pad_e0, 0, sizeof(_pad_e0));
    memset(&m_WorkVec5, 0, sizeof(m_WorkVec5));
    memset(&m_WorkVec6, 0, sizeof(m_WorkVec6));

    s_SuperFruitActive = 1;
    ++s_PomegranatesSpawnedThisGame;
    AttachGlow();
}

// Binary @ 0x001bea90: restore-from-save ctor.
SuperFruitControl::SuperFruitControl(Fruit* fruit, SuperFruitState& state)
    : m_pHostFruit(fruit)
    , m_HitCount(0.0f)
    , m_Timer(state.m_Timer)
    , m_PrevTimer(state.m_Timer)
    , m_SliceCount(state.m_SliceCount)
    , m_pLinkedSlasher(nullptr)
    , m_Lifetime(state.m_Lifetime)
    , m_FadeIn(1.0f)   // already visible when restored
    , m_Scale(1.0f)
    , m_SliceCooldown(0)
    , m_pGlow(nullptr)
{
    entityType = 6;
    memset(_pad_own, 0, sizeof(_pad_own));
    memset(_pad_80, 0, sizeof(_pad_80));
    memset(_pad_98, 0, sizeof(_pad_98));
    memset(&m_WorkVec1, 0, sizeof(m_WorkVec1));
    memset(&m_WorkVec2, 0, sizeof(m_WorkVec2));
    memset(&m_WorkVec3, 0, sizeof(m_WorkVec3));
    memset(&m_WorkVec4, 0, sizeof(m_WorkVec4));
    memset(_pad_e0, 0, sizeof(_pad_e0));
    memset(&m_WorkVec5, 0, sizeof(m_WorkVec5));
    memset(&m_WorkVec6, 0, sizeof(m_WorkVec6));

    s_SuperFruitActive = 1;
    AttachGlow();
}

SuperFruitControl::~SuperFruitControl()
{
    m_pHostFruit = nullptr;
    m_pGlow = nullptr;
}

// Binary @ 0x001bb664.
void SuperFruitControl::Release()
{
    // Notify glow: trigger fade-out (marks m_bPendingRemoval for removal)
    if (m_pGlow) {
        m_pGlow->Release();
        m_pGlow = nullptr;
    }
    m_pHostFruit = nullptr;
    s_SuperFruitActive = 0;
    Mortar::Entity::Release();
}

// Binary @ 0x001bca10. Per-frame state machine.
// Phase ladder keyed off m_Timer vs m_Lifetime thresholds.
// TODO: 0x001bca10 -- full phase-ladder (SpawnRay, zoom, time-scale slowdown,
//   explosion finale) not yet ported; only timer advance + fade-in/scale-in
//   + finale trigger are wired.
void SuperFruitControl::Update(float dt)
{
    m_PrevTimer = m_Timer;
    m_Timer += dt;

    // Decay hit count at -17.5 per second, floored at 0.0
    m_HitCount += dt * (-17.5f);
    if (m_HitCount < 0.0f) m_HitCount = 0.0f;

    // Fade-in: += dt * 3 until 1.0
    if (m_FadeIn < 1.0f) {
        m_FadeIn += dt * 3.0f;
        if (m_FadeIn > 1.0f) m_FadeIn = 1.0f;
    }

    // Scale-in: approach 1.0
    if (m_Scale < 1.0f) {
        m_Scale += dt;
        if (m_Scale > 1.0f) m_Scale = 1.0f;
    }

    // Decrement slice cooldown
    if (m_SliceCooldown > 0) {
        --m_SliceCooldown;
    }

    // Finale: when timer exceeds lifetime threshold
    if (m_Timer >= m_Lifetime) {
        ExplodeSuperFruit();
        // Mark host fruit for kill
        if (m_pHostFruit) {
            m_pHostFruit->flags |= ENT_KILLED;
        }
        // Self-destroy
        flags |= ENT_KILLED;
    }
}

// TODO: 0x001bd7c8 -- SuperFruitControl::Draw not yet ported
void SuperFruitControl::Draw(Renderer& /*r*/)
{
}

// TODO: 0x001bca10 -- SuperFruitControl::PostUpdate not yet ported
void SuperFruitControl::PostUpdate(float /*dt*/)
{
}

// Binary @ 0x001bb994. Per-hit combo response.
// Bumps m_SliceCount / m_HitCount, applies cooldown, accrues score.
// TODO: 0x001bb994 -- slash particles, combo-pitch SFX, FancyBakedString popup not yet ported
void SuperFruitControl::Sliced(Mortar::Entity* slashEntity)
{
    if (m_SliceCooldown > 0) return;

    ++m_SliceCount;
    m_HitCount += 1.0f;
    m_SliceCooldown = 6;  // TODO: 0x001bb994 -- resolve cooldown value from binary DAT

    // Null out linked slasher (binary @ 0x001bb994 nulls the stored SlashEntity).
    if (slashEntity) {
        m_pLinkedSlasher = nullptr;
    }

    // Accrue combo score: base 25 per hit (TODO: 0x001bb994 -- resolve from binary score table)
    int points = 25 * m_SliceCount;
    FN::AddToCurrentScore(points, 0, false, false);

    LOG_INFO("SUPERFRUIT", "Sliced() hit %d, score +%d", m_SliceCount, points);
}

// Binary @ 0x001baa20. Finale VFX + scoring payoff.
// TODO: 0x001baa20 -- 10/25 radial jibs, 8 lettered fragments, white flash,
//   fruit-colour reset not yet ported. Stub logs the event.
void SuperFruitControl::ExplodeSuperFruit()
{
    LOG_INFO("SUPERFRUIT", "ExplodeSuperFruit() slices=%d", m_SliceCount);

    // Bonus score for accumulated slices (TODO: 0x001baa20 -- exact formula from binary)
    if (m_SliceCount > 0) {
        int bonus = m_SliceCount * 50;
        FN::AddToCurrentScore(bonus, 0, false, false);
    }

    s_SuperFruitActive = 0;
}

// Binary @ 0x001b9850. Clears m_pLinkedSlasher if it matches se.
void SuperFruitControl::ComboCancel(SlashEntity* se)
{
    if (m_pLinkedSlasher == se) {
        m_pLinkedSlasher = nullptr;
    }
}

// Attach a SuperFruitGlow entity to the host fruit via ActorManager.
void SuperFruitControl::AttachGlow()
{
    // TODO: 0x001c06bc -- wire SuperFruitGlow through ActorManager pool when
    //   Entity pool allocation for type-6 entities is supported.
    // For now: create on heap and track via m_pGlow pointer.
    m_pGlow = new SuperFruitGlow(m_pHostFruit);
}

// -----------------------------------------------------------------------
// Static interface
// -----------------------------------------------------------------------

// Binary @ 0x001bbf48. Called when ANY fruit is thrown.
// Gate: FruitInfo[type].m_bIsSuperFruit != 0 && !(fruit->flags & 0x10).
void SuperFruitControl::SuperFruitThrown(Fruit* fruit)
{
    if (!fruit) return;
    // Already killed
    if (fruit->flags & ENT_KILLED) return;

    const FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (!info) return;

    // Gate: m_bIsSuperFruit flag at FRUIT_INFO+0x330
    if (!info->m_bIsSuperFruit) return;

    // Already registered
    if (SuperFruitControls.count(fruit)) return;

    LOG_INFO("SUPERFRUIT", "SuperFruitThrown() spawning controller for fruit=%p", static_cast<void*>(fruit));

    // Binary @ 0x001bbf48: scripted slow-arc override written onto the thrown
    // super-fruit. m_Gravity.x is always -5.0; the pos/vel/(gravity.y,gravity.z)
    // preset is selected by game_work.gameMode (==2 -> Arcade, else default).
    // Then a 51% chance (Rand32(100) < 51) mirrors the arc horizontally by
    // negating m_Gravity.y, pos.x and vel.x.
    fruit->m_Gravity.x = -5.0f;                          // [fruit+0x9c] = 0xc0a00000
    if (game_work.gameMode == 2) {                       // GAME_MODE_ARCADE; byte at game_work+0x04
        fruit->pos = Vec3(-35.0f, -260.0f, 0.0f);        // DAT_001bc104/0bc108/0bc10c
        fruit->vel = Vec3(0.5f, 8.5f, 0.0f);             // 0x3f000000, 0x41080000, DAT_001bc10c
        fruit->m_Gravity.z = -7.5f;                      // [fruit+0xa4] = 0xc0f00000
        fruit->m_Gravity.y = 0.0f;                       // [fruit+0xa0] = DAT_001bc10c
    } else {
        fruit->pos = Vec3(-340.0f, -100.0f, 0.0f);       // DAT_001bc110/0bc114/0bc10c
        fruit->vel = Vec3(5.0f, 5.0f, 0.0f);             // 0x40a00000, 0x40a00000, DAT_001bc10c
        fruit->m_Gravity.z = -4.5f;                      // [fruit+0xa4] = 0xc0900000
        fruit->m_Gravity.y = 0.01f;                      // [fruit+0xa0] = DAT_001bc118
    }
    // 51% chance: mirror the arc across the screen centreline.
    if (WaveManager::GetInstance()->GetRandom().Rand32(100) < 51) {  // cmp #0x32 / bhi
        fruit->m_Gravity.y = -fruit->m_Gravity.y;        // [fruit+0xa0]
        fruit->pos.x       = -fruit->pos.x;              // [fruit+0x10]
        fruit->vel.x       = -fruit->vel.x;              // [fruit+0x1c]
    }

    // TODO: 0x001bbf48 -- SuperFruitThrown SFX not yet ported

    SuperFruitControl* ctrl = new SuperFruitControl(fruit);
    SuperFruitControls[fruit] = ctrl;
}

// Binary @ 0x001be630. Slice dispatch: lookup map, forward or create.
void SuperFruitControl::SuperFruitSliced(Fruit* fruit, int /*idx*/, Mortar::Entity* slashEntity)
{
    if (!fruit) return;
    const FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (!info || !info->m_bIsSuperFruit) return;

    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.find(fruit);
    if (it != SuperFruitControls.end() && it->second) {
        it->second->Sliced(slashEntity);
    } else {
        // First hit: create controller (binary allocates 0x108 bytes here)
        SuperFruitControl* ctrl = new SuperFruitControl(fruit);
        ctrl->Sliced(slashEntity);
        SuperFruitControls[fruit] = ctrl;
    }
}

// Binary @ 0x001b9828. Returns true while a super fruit is active.
// Binary reads game+0x14; port uses shadow flag.
bool SuperFruitControl::IsInSuperFruitState()
{
    return s_SuperFruitActive != 0;
}

// Binary @ 0x001b98c0.
int SuperFruitControl::NumPomegranatesSpawnedThisGame()
{
    return s_PomegranatesSpawnedThisGame;
}

// Binary @ 0x001b99d4. Game-mode gating for final pomegranate spawn.
// TODO: 0x001b99d4 -- exact gating conditions not yet RE'd; return false (safe default).
bool SuperFruitControl::CanSpawnFinalPomegranate()
{
    return false;
}

// Binary @ 0x001b98f4. Spawns the terminal "super pomegranate" wave finale:
// two random decoy fruits chucked almost immediately (delay 0.01s), bumps the
// "super_pomegranates_spawned" save-stat, then chucks the actual super pomegranate
// (delay 0.1s). Returns true (binary returns CONCAT44(undef,1)).
//
// Binary call shape (WaveManager::SpawnFruit @ 0x00124298 returns the spawned
// Entity*, then Fruit::Chuck(delay, entity) overrides the spawner's default
// 0.21s chuck delay with the tighter finale delay). DAT constants resolved:
//   DAT_001b99bc = 0.01f  (decoy chuck delay)
//   DAT_001b99c0 = 0.1f   (super pomegranate chuck delay)
//   save-stat key string @ 0x002837d4 = "super_pomegranates_spawned"
//   fruit-type name string @ 0x002837ef = "super_pomegranate"
//   FruitSaveData = game_work.m_SaveData (binary: *(*(GameWork_glob)+0x50)).
bool SuperFruitControl::SpawnFinalPomegranate()
{
    // Two random decoy fruits, chucked near-instantly.
    Mortar::Entity* e0 = WaveManager::GetInstance()->SpawnFruit(1, -1, NULL, 0);
    if (e0) static_cast<Fruit*>(e0)->Chuck(0.01f);   // DAT_001b99bc

    Mortar::Entity* e1 = WaveManager::GetInstance()->SpawnFruit(1, -1, NULL, 0);
    if (e1) static_cast<Fruit*>(e1)->Chuck(0.01f);   // DAT_001b99bc

    // Increment the persistent "super_pomegranates_spawned" stat.
    const char* kStatKey = "super_pomegranates_spawned";
    uint32_t statHash = StringHash(kStatKey);
    if (game_work.m_SaveData) {
        game_work.m_SaveData->AddToTotal(kStatKey, statHash, 1, false, false);
    }

    // The actual super pomegranate, chucked slightly later.
    int superType = Fruit::FruitType("super_pomegranate", false);
    Mortar::Entity* e2 = WaveManager::GetInstance()->SpawnFruit(1, superType, NULL, 0);
    if (e2) static_cast<Fruit*>(e2)->Chuck(0.1f);    // DAT_001b99c0

    return true;
}

// Binary @ 0x001ba73c. Serializes active super-fruit state to XML.
void SuperFruitControl::SaveSuperFruitState(tinyxml2::XMLElement* parent)
{
    if (!parent) return;
    if (SuperFruitControls.empty()) return;

    // Serialize first active controller (binary stores at most one active at a time)
    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.begin();
    if (it == SuperFruitControls.end() || !it->second) return;

    SuperFruitControl* ctrl = it->second;

    tinyxml2::XMLDocument* doc = parent->GetDocument();
    if (!doc) return;
    tinyxml2::XMLElement* elem = doc->NewElement("superFruit");
    if (!elem) return;

    SuperFruitState state;
    state.m_Timer      = ctrl->m_Timer;
    state.m_Lifetime   = ctrl->m_Lifetime;
    state.m_SliceCount = ctrl->m_SliceCount;
    // Binary @ 0x001ba73c reads ctrl+0x2c -> serialized as XML attr "rot".
    // ctrl+0x2c is the controller's Entity-base scale.y (Entity::scale is the
    // Vec3 at +0x28; .y component sits at +0x2c). The binary repurposes the
    // controller's own scale.y as the saved spin/rotation value.
    state.m_Spin       = ctrl->scale.y;
    state.WriteToElement(elem);

    parent->InsertEndChild(elem);
}

// Binary @ 0x001bb52c. Game-reset: restore global time scale, clear all
// type-6 entities, clear map.
// TODO: 0x001bb52c -- global time-scale restore (game_work+0x40 group +0x24) not yet wired
void SuperFruitControl::Reset()
{
    // Kill all controllers and clear the map
    for (std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.begin();
         it != SuperFruitControls.end(); ++it)
    {
        if (it->second) {
            it->second->flags |= ENT_KILLED;
        }
    }
    SuperFruitControls.clear();

    s_SuperFruitActive = 0;
    s_PomegranatesSpawnedThisGame = 0;
}

// Binary @ 0x001ba460. Stops all in-flight fruit and bombs during the
// super-fruit explosion finale: clears any still-unspawned (chuck-delayed)
// fruits/bombs, then sweeps every live fruit (ActorManager type 0) and bomb
// (type 1), redirecting their velocity toward the explosion centre and
// freezing their physics. The explosion centre is this->m_WorkVec5 (+0xf0).
//
// Per-entity velocity redirect (both fruits and sliced-fruit halves and bombs):
//   dir    = Normalise(pos - centre)
//   newVel = (vel + dir * 5.0f) / 2.0f      // 5.0f = DAT (vmov 0x40a00000)
// (the /2.0f literal is the binary's local 2.0f operand to operator/).
//
// DAT constants (read from binary memory):
//   DAT_001ba6a4 = 0.0f (zeroed into the per-entity stop fields)
//   the copied Vec3 (GOT->0x0035f160) is _Vector3<float>::Zero == (0,0,0).
void SuperFruitControl::StopAllFruit()
{
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Explosion centre lives in this controller's work vector (+0xf0).
    const Vec3& centre = m_WorkVec5;

    // Remove fruits/bombs that haven't actually spawned yet (still in
    // chuck-delay) before redirecting the rest. Binary calls both here.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();

    // -------- type 0: fruits --------
    std::list<Mortar::Entity*>::iterator fit;
    Mortar::Entity* e = am->GetEntityFirst(0, fit);
    while (e != NULL) {
        Fruit* f = static_cast<Fruit*>(e);

        // Freeze fruit physics. These reproduce the binary's exact per-fruit
        // stop writes at Fruit+0x98 (float=0) and the zero-Vec3 at Fruit+0xa0,
        // plus the byte clear at Fruit+0x70.
        // DIFFERS: binary zeroes the raw fields at +0x98 / +0xa0 (Vec3) / +0x70;
        // the port expresses them through the named fields occupying those
        // offsets (m_ZPosition, m_Gravity.y/.z + m_VisualScale.x, m_SliceAngle).
        f->m_ZPosition = 0.0f;          // Fruit+0x98 = DAT_001ba6a4 (0.0f)
        f->m_Gravity.y = 0.0f;          // Fruit+0xa0 } zero-Vec3 copy
        f->m_Gravity.z = 0.0f;          // Fruit+0xa4 }
        f->m_VisualScale.x = 0.0f;      // Fruit+0xa8 }
        f->m_SliceAngle = 0;            // Fruit+0x70 (strb 0)

        // Only sliced fruits get their two half-bodies redirected.
        if (f->Sliced()) {
            // First body: pos +0x10 -> vel +0x1c.
            Vec3 dir = f->pos - centre;
            dir.Normalise();
            dir *= 5.0f;
            f->vel = (f->vel + dir) / 2.0f;

            // Second body: pos +0xc8 (m_SecondPos region) -> vel +0xd4.
            // DIFFERS: binary reads Fruit+0xc8 and writes Fruit+0xd4; the port's
            // named second-body fields sit at +0xb8/+0xc4, so this redirect uses
            // the same raw +0x10 offset relationship the binary uses (pos->vel).
            Vec3 dir2 = f->m_SecondPos - centre;
            dir2.Normalise();
            dir2 *= 5.0f;
            f->m_SecondVel = (f->m_SecondVel + dir2) / 2.0f;
        }

        e = am->GetEntityNext(0, fit);
    }

    // -------- type 1: bombs --------
    e = am->GetEntityFirst(1, fit);
    while (e != NULL) {
        Bomb* b = static_cast<Bomb*>(e);

        // Redirect bomb velocity toward the explosion centre (pos +0x10 -> vel +0x1c).
        Vec3 dir = b->pos - centre;
        dir.Normalise();
        dir *= 5.0f;
        b->vel = (b->vel + dir) / 2.0f;

        // Freeze bomb physics (binary writes at Bomb+0x8c / +0xa8 / +0x80).
        b->m_AccelForce = Vec3(0.0f, 0.0f, 0.0f);  // Bomb+0x8c zero-Vec3
        b->m_SpeedMult = 0.0f;                      // Bomb+0xa8 = DAT_001ba6a4 (0.0f)
        b->m_bMovement = 0;                         // Bomb+0x80 (strb 0)

        e = am->GetEntityNext(1, fit);
    }
}
