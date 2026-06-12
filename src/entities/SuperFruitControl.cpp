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
#include "SlashEntity.h"
#include "ActorManager.h"
#include "game/GameWork.h"
#include "game/GameOver.h"
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

    // TODO: 0x001bbf48 -- scripted slow-arc velocity override and mirror not yet ported
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

// Binary @ 0x001b98f4. Spawns the terminal pomegranate.
// TODO: 0x001b98f4 -- spawn logic not yet ported
void SuperFruitControl::SpawnFinalPomegranate()
{
    // TODO: 0x001b98f4 -- SpawnFinalPomegranate not yet ported
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
    state.m_Spin       = 0.0f;  // TODO: 0x001ba73c -- resolve spin field from _pad_a8
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

// Binary @ 0x001ba460. Stops all fruit during super-fruit freeze phase.
// TODO: 0x001ba460 -- StopAllFruit: freeze/kill active type-0 entities not yet ported
void SuperFruitControl::StopAllFruit()
{
    // TODO: 0x001ba460 -- walk ActorManager type-0 list, set ENT_KILLED on non-super fruits
}
