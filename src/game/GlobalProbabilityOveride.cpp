#include "game/GlobalProbabilityOveride.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/PowerUpManager.h"
#include "entities/Fruit.h"
#include "engine/util/StringHash.h"
#include "engine/math/Random.h"
#include "game/ItemParseUtil.h"
#include "engine/xml/TiXml.h"

#include <cstring>
#include <cstdlib>

// ----------------------------------------------------------------------------
// Local helpers
// ----------------------------------------------------------------------------

// T_872 @0x0012118c — float in [0.0, 1.0); used to interpolate min..max.
static float T_872() {
    return Math::g_Random.RandF(1.0f);
}

// T_877 @0x001212e0 — int range pick: returns a value in [lo, hi] via T_872.
// When lo==hi, returns lo. Otherwise scales T_872() into (hi-lo) and adds lo.
static int T_877(int lo, int hi) {
    if (lo == hi) return lo;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    return lo + (int)(T_872() * (float)(hi - lo + 1));
}

// LerpF_int — float t in [0,1] -> int in [lo, hi].
// Used in base CheckForOverride for countdown seeding.
static int LerpF_int(float t, int lo, int hi) {
    return lo + (int)(t * (float)(hi - lo));
}

// ----------------------------------------------------------------------------
// GlobalProbabilityOveride base
// ----------------------------------------------------------------------------

// ASM-spec v1.6.1 GlobalProbabilityOveride::GlobalProbabilityOveride @0x00120ab0
GlobalProbabilityOveride::GlobalProbabilityOveride()
    : m_TotalChance(0)
    , m_SaveKey(0)
    , m_SaveSubId(0)
    , m_ModeMask(0xFFFFFFFF)
    , m_AmountMin(0)
    , m_AmountMax(0)
    , m_AlwaysAllow(true)
    , m_MinFruitCount(0)
{
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::~GlobalProbabilityOveride @0x00120fb0
GlobalProbabilityOveride::~GlobalProbabilityOveride()
{
    if (m_SaveKey) {
        free(m_SaveKey);   // allocated by CloneString (= strdup = malloc)
        m_SaveKey = 0;
    }
    // m_TypeChances destructs naturally (frees its buffer)
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::Parse @0x001215e4
void GlobalProbabilityOveride::Parse(TiXmlElement* e)
{
    if (!e) return;

    CloneString(&m_SaveKey, e->Attribute("name"));
    m_SaveSubId = m_SaveKey ? StringHash(m_SaveKey) : 0u;
    m_ModeMask  = ParseModeMask(e->Attribute("mode"));

    e->QueryIntAttribute("minWait",            &m_AmountMin);
    e->QueryIntAttribute("maxWait",            &m_AmountMax);
    e->QueryIntAttribute("dontSpawnBeforeWave", &m_MinFruitCount);

    const char* canSpawn = e->Attribute("canSpawnWithPowers");
    if (canSpawn)
        m_AlwaysAllow = (CompareWords("true", canSpawn) != 0);

    m_TotalChance = 0;
    for (TiXmlElement f = e->FirstChildElement("fruit"); f; f = f.NextSiblingElement("fruit")) {
        TypeChance tc;
        const char* type = f.Attribute("type");
        if (type) tc.m_TypeName = type;
        int chance = 100;
        f.QueryIntAttribute("chance", &chance);
        m_TotalChance += chance;
        tc.m_Chance    = chance;
        tc.m_CumChance = m_TotalChance;
        m_TypeChances.push_back(tc);
    }

    ParseSpecific(e);   // vtable slot1
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::PickFruit @0x00120e04
int GlobalProbabilityOveride::PickFruit()
{
    if (m_TotalChance <= 0 || m_TypeChances.empty()) return 0;
    int r = (int)Math::g_Random.Rand32((uint32_t)m_TotalChance);
    for (size_t i = 0; i < m_TypeChances.size(); ++i) {
        if (r < m_TypeChances[i].m_CumChance) {
            int ft = Fruit::FruitType(m_TypeChances[i].m_TypeName.c_str(), 0);
            if (ft >= 0) return ft;
        }
    }
    return 0;
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::CanSpawn @0x00120d2c
// Binary dereferences WaveManager::GetInstance()'s result unguarded (bl 0x0010d064 ;
// ldr r3,[r0,#0x234]) and calls PowerUpManager::GetInstance() once, also unguarded
// (bl 0x0010aca0 ; bl GetActiveProgression). Neither null-check exists in the binary.
bool GlobalProbabilityOveride::CanSpawn()
{
    WaveManager* wm = WaveManager::GetInstance();
    WAVE_INFO* wave = wm->m_pCurrentWave[0];
    // If current wave has GamesMin==0 (no game-count requirement), allow immediately.
    if (wave && wave->m_GamesMin == 0) return true;
    // Binary @0x00120db4: cmp r2,r3 / bgt where r2=m_SavedWaveDelay, r3=m_MinFruitCount
    // -> plain signed >  ->  faithful condition is m_MinFruitCount < m_SavedWaveDelay.
    if (m_MinFruitCount < wm->m_SavedWaveDelay) {
        if (m_AlwaysAllow) return true;
        if (Fruit::NumberOfPowerupFruits() < 1)
            return PowerUpManager::GetInstance()->GetActiveProgression(0.0f) >= 2.0f;
    }
    return false;
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::ParseSpecific @0x00121c78 (base no-op)
void GlobalProbabilityOveride::ParseSpecific(TiXmlElement* /*e*/)
{
    // Defunct: base ParseSpecific — no-op stub; v1.6.1 GlobalProbabilityOveride::ParseSpecific @0x00121c78
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::CheckForOverride @0x001211cc
// (downgraded from ASM-verified 2026-06-26: that pass missed a port-added
// game_work.m_SaveData null guard the binary does not have -- the binary enters
// GetModeBitMask directly and passes game_work.pM_SaveData to GetTotal unguarded.)
bool GlobalProbabilityOveride::CheckForOverride(int& out)
{
    if (!(GetModeBitMask((GAME_MODE)game_work.gameMode) & m_ModeMask)) return false;

    int n = game_work.m_SaveData->GetTotal(m_SaveSubId);
    if (n == 0) {
        // Seed the countdown
        int amt = LerpF_int(T_872(), m_AmountMin, m_AmountMax);
        game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId, amt, true, true);
        return false;
    }
    if (n == 1) {
        if (CanSpawn()) {
            game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId, -1, true, true);
            out = PickFruit();
            return true;
        }
    } else if (n > 1) {
        game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId, -1, true, true);
    }
    return false;
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::PushbackSpawn @0x00120b70 (slot3)
// Binary body is a single unguarded tail call:
//   FruitSaveData::SetTotal(game_work.pM_SaveData, m_SaveKey, 3, true, true)
void GlobalProbabilityOveride::PushbackSpawn()
{
    game_work.m_SaveData->SetTotal(m_SaveKey, 3, true, true);
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::NewGameStarted @0x00121c7c (slot4, base no-op)
void GlobalProbabilityOveride::NewGameStarted()
{
    // Defunct: base NewGameStarted — no-op stub; v1.6.1 GlobalProbabilityOveride::NewGameStarted @0x00121c7c
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::FruitWasKilled @0x00120a7c
void GlobalProbabilityOveride::FruitWasKilled(Fruit* f)
{
    // Binary: if (!f || Fruit::IsActive(f)) return; (IsActive = not killed/inactive)
    if (!f || f->IsActive()) return;
    PushbackSpawn();
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::FruitWasThrown @0x00120dc4
void GlobalProbabilityOveride::FruitWasThrown(Fruit* f)
{
    if (!f) return;
    if (CanSpawn()) return;
    f->KillFruit(false);
    PushbackSpawn();
}

// ----------------------------------------------------------------------------
// GlobalProbabilityOveridePointBased
// ----------------------------------------------------------------------------

// ASM-spec v1.6.1 GlobalProbabilityOveridePointBased::GlobalProbabilityOveridePointBased @0x0012ac30
GlobalProbabilityOveridePointBased::GlobalProbabilityOveridePointBased()
    : GlobalProbabilityOveride()
    , m_Every(0)
    , m_EveryMax(0)
    , m_From(0)
    , m_FromMax(0)
{
}

GlobalProbabilityOveridePointBased::~GlobalProbabilityOveridePointBased()
{
}

// ASM-spec v1.6.1 GlobalProbabilityOveridePointBased::ParseSpecific @0x00120c44 (slot1)
// Reads every/everyMin -> m_Every (copy to m_EveryMax), from/fromMin -> m_From (copy to m_FromMax),
// then override with everyMin/everyMax/fromMin/fromMax.
void GlobalProbabilityOveridePointBased::ParseSpecific(TiXmlElement* e)
{
    if (!e) return;
    e->QueryIntAttribute("every",    &m_Every);
    m_EveryMax = m_Every;
    e->QueryIntAttribute("from",     &m_From);
    m_FromMax  = m_From;
    // Override with Min/Max variants if present
    e->QueryIntAttribute("everyMin", &m_Every);
    e->QueryIntAttribute("everyMax", &m_EveryMax);
    e->QueryIntAttribute("fromMin",  &m_From);
    e->QueryIntAttribute("fromMax",  &m_FromMax);
}

// ASM-spec v1.6.1 GlobalProbabilityOveridePointBased::CheckForOverride @0x00121320
// (downgraded from ASM-verified 2026-06-26: that pass missed a port-added
// game_work.m_SaveData null guard the binary does not have.)
// Score-based gate: fires when saved score threshold n <= current game score
// and CanSpawn(). NewGameStarted seeds n = T_877(from, fromMax) as initial
// score milestone. After firing, rearms by adding T_877(every, everyMax) to n.
// When PushbackSpawn deferred (n < 0), restores to positive and skips.
bool GlobalProbabilityOveridePointBased::CheckForOverride(int& out)
{
    if (!(GetModeBitMask((GAME_MODE)game_work.gameMode) & m_ModeMask)) return false;

    int n = game_work.m_SaveData->GetTotal(m_SaveSubId);
    int score = game_work.currentScore;
    if (n <= score) {
        if (CanSpawn()) {
            if (n < 0) {
                // PushbackSpawn deferred: restore to positive.
                game_work.m_SaveData->SetTotal(m_SaveKey, -n, false, false);
            }
            out = PickFruit();
            // Rearm: advance score threshold by T_877(every, everyMax).
            game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId,
                T_877(m_Every, m_EveryMax), false, false);
            return true;
        }
    }
    return false;
}

// ASM-spec v1.6.1 GlobalProbabilityOveridePointBased::PushbackSpawn @0x00120bf4 (slot3)
// If GetTotal > 0, set negative (defer until next time); else no-op.
// Binary calls GetTotal(game_work.pM_SaveData, ...) unguarded -- no null check.
void GlobalProbabilityOveridePointBased::PushbackSpawn()
{
    int n = game_work.m_SaveData->GetTotal(m_SaveSubId);
    if (n > 0)
        game_work.m_SaveData->SetTotal(m_SaveKey, -n, false, false);
}

// ASM-spec v1.6.1 GlobalProbabilityOveridePointBased::NewGameStarted @0x0012140c (slot4)
// Sets the initial score threshold via T_877(m_From, m_FromMax).
// Binary loads game_work.pM_SaveData into r0 and calls SetTotal unguarded.
void GlobalProbabilityOveridePointBased::NewGameStarted()
{
    game_work.m_SaveData->SetTotal(m_SaveKey, T_877(m_From, m_FromMax), false, false);
}

// ----------------------------------------------------------------------------
// GlobalProbabilityOverideTimed
// ----------------------------------------------------------------------------

// ASM-spec v1.6.1 GlobalProbabilityOverideTimed::GlobalProbabilityOverideTimed @0x0012ac64
GlobalProbabilityOverideTimed::GlobalProbabilityOverideTimed()
    : GlobalProbabilityOveride()
{
}

GlobalProbabilityOverideTimed::~GlobalProbabilityOverideTimed()
{
}

// ASM-spec v1.6.1 GlobalProbabilityOverideTimed::ParseSpecific @0x00120aac (slot1, no-op)
void GlobalProbabilityOverideTimed::ParseSpecific(TiXmlElement* /*e*/)
{
    // Defunct: Timed ParseSpecific — no-op stub; v1.6.1 GlobalProbabilityOverideTimed::ParseSpecific @0x00120aac
}

// ASM-spec v1.6.1 GlobalProbabilityOverideTimed::CheckForOverride @0x00120e90 (slot2)
// Fires when saved time n >= 0 and (float)n < game_work.m_ElapsedGameTime and CanSpawn.
// Binary enters GetModeBitMask directly; no game_work.m_SaveData null guard.
bool GlobalProbabilityOverideTimed::CheckForOverride(int& out)
{
    if (!(GetModeBitMask((GAME_MODE)game_work.gameMode) & m_ModeMask)) return false;

    int n = game_work.m_SaveData->GetTotal(m_SaveSubId);
    if (n >= 0 && (float)n < game_work.m_ElapsedGameTime && CanSpawn()) {
        out = PickFruit();
        // Re-arm: advance by m_AmountMax*2 seconds (negative delta -> future target).
        game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId,
            m_AmountMax * 2, false, false);
        return true;
    }
    return false;
}

// ASM-spec v1.6.1 GlobalProbabilityOverideTimed::PushbackSpawn @0x00120bac (slot3)
// Adds m_AmountMax*2 to the timer total (push forward).
// Binary body is a single unguarded tail call to FruitSaveData::AddToTotal.
void GlobalProbabilityOverideTimed::PushbackSpawn()
{
    game_work.m_SaveData->AddToTotal(m_SaveKey, m_SaveSubId, m_AmountMax * 2, false, false);
}

// ASM-spec v1.6.1 GlobalProbabilityOverideTimed::NewGameStarted @0x0012145c (slot4)
// Sets the initial time target via T_877(m_AmountMin, m_AmountMax).
// Binary loads game_work.pM_SaveData into r0 and calls SetTotal unguarded.
void GlobalProbabilityOverideTimed::NewGameStarted()
{
    game_work.m_SaveData->SetTotal(m_SaveKey, T_877(m_AmountMin, m_AmountMax), false, false);
}
