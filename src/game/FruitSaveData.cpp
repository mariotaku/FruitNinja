#include "FruitSaveData.h"
#include "util/StringHash.h"

#include <cstring>

// FruitSaveData stub — every method is a no-op or returns a safe
// default. Real XML serialisation + stat aggregation land when the port
// wires save/load through TinyXML. See docs/systems/save-system.md.
//
// Analysed: 2026-04-23T02:00

// --- Construction / destruction ------------------------------------------

FruitSaveData::FruitSaveData()
    : m_CurrentScore(0)
    , m_CurrentMissCount(0)
    , m_GameMode(0)
    , m_CriticalChance(70)          // 0x46 — binary default (0x00129e74)
    , m_bDojoBGUnlocked(0)
    , m_WaveCount(0)
    , m_WaveDelay(0.0f)
    , m_WaveWait(0.0f)
    , m_blitzSpawnedThisGame(0)
    , m_blitzForceSpawnedCounter(0)
    , m_blitzSpawnTime(0.0f)
    , m_FruitQueueCount(0)
    , m_BombQueueCount(0)
    , m_GameTimer1(-1.0f)            // binary: 0xBF800000
    , m_BombHitTimer(0.0f)
    , m_ShakeIntensity(0.0f)
    , m_ShakeDecay(1.0f)
    , m_VersionInfo(0)
{
    for (int i = 0; i < 4; i++) {
        m_ModeHighScores[i] = 0;
        m_ModeBestCombos[i] = 0;
        m_ModePlayCounts[i] = 0;
    }
    for (int i = 0; i < 32; i++) m_FruitQueue[i] = -1;
    for (int i = 0; i < 11; i++) m_BombQueue[i] = -1;
}

FruitSaveData::~FruitSaveData() {}

// --- Stat tracking -------------------------------------------------------

// 0x0012b21c. Binary inserts/updates an entry in a std::map<ulong,
// SliceTotal>. Port: no persistent map yet — return the count so
// callers that store into Game::fruitTotal see a plausible value.
int FruitSaveData::AddToTotal(const char* /*name*/, uint32_t /*hash*/,
                              int count, bool /*trackFruit*/,
                              bool /*sendNetPacket*/) {
    return count;
}

int FruitSaveData::AddToTotal(const char* name, int count) {
    return AddToTotal(name, StringHash(name), count, false, false);
}

int  FruitSaveData::GetTotal(uint32_t /*hash*/) const      { return 0; }
void FruitSaveData::ClearCombo()                           {}
void FruitSaveData::FinishedGame()                         {}

// --- Achievements --------------------------------------------------------

bool FruitSaveData::IsAchievementUnlocked(uint32_t /*hash*/) { return false; }
void FruitSaveData::UnlockTotals()                           {}

// --- Save / Load ---------------------------------------------------------

void FruitSaveData::SaveGameState()          {}
void FruitSaveData::CheckDatesHaveChanged()  {}
void FruitSaveData::DownloadTweaks()         {}

// --- Per-frame tick ------------------------------------------------------

void FruitSaveData::Update(float /*dt*/, HUD* /*hud*/) {
    // 0x0012b3dc — advances achievement in-progress timers. Stub.
}

// --- Save/Load free functions -------------------------------------------

void FruitNinja_SaveCurrentData()                { /* 0x0016ccc8 */ }
void FruitNinja_SaveGame(FruitSaveData* /*s*/)   { /* 0x0012a2fc */ }
bool FruitNinja_LoadGame(FruitSaveData* /*s*/)   { /* 0x0012be74 */ return false; }
void FruitNinja_SaveOnExit()                     { /* 0x0016cf40 */ }
