// FruitSaveData -- save/load to FruitySave.xml + slice-total maps.
//
// Implements binary SaveGame @ 0x001530dc and
// LoadGame @ 0x0015591c. Coin balance is owned by ItemSave.xml
// via ItemManager and is only mirrored here for in-memory access.
//
// Analysed: 2026-04-23T02:00, REVISED 2026-05-02T00:00

#include "FruitSaveData.h"
#include "ScoreState.h"
#include "Game.h"
#include "ItemManager.h"
#include "AchievementManager.h"
#include "PowerUpManager.h"
#include "GameMode.h"
#include "ScreenEffect.h"
#include "engine/util/StringHash.h"
#include "engine/xml/TiXml.h"
#include "engine/core/SystemManager.h"
#include "engine/asset/FileManager.h"

// GetVersionString -- v1.6.1 Game::SelfVersion @0x0011fbd8 (defined in AboutScreen.cpp);
// returns the literal build version "1.6.1". No public header declares it.
const char* GetVersionString();

#include <cstdio>
#include <cstring>
#include <cstdlib>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <ctime>
#include "game/GameWork.h"

// ----------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------

// Matches binary ctor @ 0x00129e74. Defaults from doc + RE.
FruitSaveData::FruitSaveData()
    : m_reserved30(0)
    , m_bHasActiveGame(0)
    , m_bDojoBGUnlocked(0)
    , m_bP2PCancelled(0)
    , m_highscore(0)
    , m_CurrentScore(0)
    , m_CurrentMissCount(0)
    , m_GameMode(0)
    , m_bWasGameOver(0)
    , m_LastSlasher(-1)
    , m_ComboCount(0)
    , m_FruitQueueCount(0)
    , m_Speed_P0(0.0f)
    , m_Speed_P0_alias(0.0f)
    , m_Speed_P1(0.0f)
    , m_TimeRemainingSave(-1.0f)
    , m_CriticalChance(70)              // 0x46
    , m_GameOverScreenState(-1)
    , m_GameOverTimer(-1.0f)
    , m_GameOverField1(-1)
    , m_GameOverField2(-1)
    , m_GameOverField3(-1)
    , m_GameOverField4(-1)
    , newBestThisGame(0)
    , secondaryFlag(0)
    , m_BombHitTimer(0.0f)
    , m_NextComboBonus(-1.0f)
    , m_ShakeIntensity(0.0f)
    , m_ShakeDecay(1.0f)                // 0x3F800000
    , m_pCurrentWave_P1(0)
    , m_WaveDelay(0.0f)
    , m_WaveWait(0.0f)
    , m_ProbabilityOverideFlag(1.0f)
    , m_WaveScalar_v161(1.0f)
    , m_blitzSpawnedThisGame(0)
    , m_blitzForceSpawnedCounter(0)
    , m_blitzSpawnTime(0.0f)
    , m_VersionInfo(0)
    , m_reserved1fc(0)
    , m_BestComboLength(0)
{
    for (int i = 0; i < 4; i++) {
        m_ModeHighScores[i] = 0;
        m_ModeBestCombos[i] = 0;
        m_LastPlayedDay[i] = 0;
    }
    for (int i = 0; i < 32; i++) m_FruitQueue[i] = -1;
    for (int i = 0; i < 11; i++) m_BestComboFruits[i] = -1;
}

FruitSaveData::~FruitSaveData() {}

// ----------------------------------------------------------------------
// Stat tracking
// ----------------------------------------------------------------------

// ASM-spec v1.6.1 FruitSaveData::AddToTotal @ 0x001546f0
// Binary: if (trackSession != 0) shifts `this` by +0x18 (m_SessionTotals base);
// all subsequent map ops use the SHIFTED this->m_Totals -- resolving to either
// real m_Totals (trackSession=false) or real m_SessionTotals (trackSession=true).
// EITHER/OR selection: never touches both maps. Returns selected map's new count.
// sendNetPacket param gates AchievementManager::UnlockSpecificFruitAchievement
// (no-op stub in port).
int FruitSaveData::AddToTotal(const char* name, uint32_t hash, int count,
                              bool trackSession, bool /*sendNetPacket*/) {
    if (!name || !*name) return 0;

    std::map<uint32_t, SliceTotal>& target = trackSession ? m_SessionTotals : m_Totals;
    std::map<uint32_t, SliceTotal>::iterator it = target.find(hash);
    if (it != target.end()) {
        it->second.count += count;
        // Defunct: sendNetPacket gated AchievementManager::UnlockSpecificFruitAchievement
        //   in binary; stub is a no-op in the port.
        return it->second.count;
    } else {
        target[hash] = SliceTotal(name, count);
        return count;
    }
}

int FruitSaveData::AddToTotal(const char* name, int count) {
    if (!name || !*name) return 0;
    return AddToTotal(name, StringHash(name), count, false, false);
}

// Binary @ 0x00152e58 -- hashes name, delegates.
int FruitSaveData::GetTotal(const char* name) {
    return GetTotal(StringHash(name));
}

// GetTotal @ 0x00152760.
int FruitSaveData::GetTotal(uint32_t hash) {
    std::map<uint32_t, SliceTotal>::iterator it = m_Totals.find(hash);
    if (it != m_Totals.end()) return it->second.count;
    it = m_SessionTotals.find(hash);
    return (it != m_SessionTotals.end()) ? it->second.count : 0;
}

// ClearTotals -- wipes the entire m_Totals map.
// Binary @ 0x00153ebc (PauseScreen::QuitGameCallback), 0x00153f68 (RetryGameCallback),
// and GameOverScreen state-0 exit path.
void FruitSaveData::ClearTotals() {
    m_Totals.clear();
}

// ClearTotal -- erases one entry from m_Totals by hash.
// Called by WaveManager::ResetSpeed and AddSpeed to clear "blitz_bonus" count.
void FruitSaveData::ClearTotal(uint32_t hash) {
    m_Totals.erase(hash);
}

// ClearCombo -- resets the all-time best-combo record.
// v1.6.1 FruitSaveData::ClearCombo @ 0x001526c0 (thunk 0x001106b0):
//   m_BestComboLength = 0 (+0x210); m_BestComboFruits[0..10] = -1 (+0x214..+0x23c).
void FruitSaveData::ClearCombo() {
    m_BestComboLength = 0;
    for (int i = 0; i < 11; i++) m_BestComboFruits[i] = -1;
}

// FinishedGame @ 0x0012a034. Decays all m_ModeScoreHistory survivor values by 1.
// Binary: for each mode, for each entry in the map, if val >= 0, val--.
void FruitSaveData::FinishedGame() {
    for (int mode = 0; mode < 4; mode++) {
        for (std::map<int, int>::iterator it = m_ModeScoreHistory[mode].begin();
             it != m_ModeScoreHistory[mode].end(); ++it) {
            if (it->second >= 0) it->second--;
        }
    }
}

// SnapshotComboState -- copy g_LastSlasher / g_ComboCount into save fields.
// Binary: SaveCurrentData @ 0x0016cd08 writes save[+0x74] = *GOT[lastSlasher]
//         and @ 0x0016cd34 writes save[+0x78] = *GOT[comboCount].
void FruitSaveData::SnapshotComboState() {
    m_LastSlasher = g_LastSlasher;
    m_ComboCount  = g_ComboCount;
}

// RestoreComboState -- copy save fields back into g_LastSlasher / g_ComboCount.
// Binary: WaveManager::Resume @ 0x00124b54 writes *GOT[lastSlasher] = save[+0x74]
//         and @ 0x00124b68 writes *GOT[comboCount] = save[+0x78].
void FruitSaveData::RestoreComboState() {
    g_LastSlasher = m_LastSlasher;
    g_ComboCount  = m_ComboCount;
}

// SetCurrentModeHighscore @ 0x0010a388.
// Writes m_ModeHighScores[currentMode] when newScore strictly beats stored value.
// The tiered improvement gate (currentHigh/2 < score) lives in the caller
// (GameOverScreen::Update case 6) -- that is a separate task (#50).
// Returns true iff the stored highscore was actually replaced.
bool FruitSaveData::SetCurrentModeHighscore(int newScore) {
    Game* g = Game::GetInstance();
    if (!g) return false;
    int mode = (int)game_work.gameMode;
    if (mode < 0 || mode >= 4) return false;
    if (m_ModeHighScores[mode] < newScore) {
        m_ModeHighScores[mode] = newScore;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------
// Achievements
// ----------------------------------------------------------------------

// 0x00129c50 -- returns 2 if pending, 1 if unlocked, 0 otherwise.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00129c50 (re-analyst)
int FruitSaveData::IsAchievementUnlocked(uint32_t hash) {
    if (m_PendingUnlocks.find(hash) != m_PendingUnlocks.end()) return 2;
    return (m_UnlockedAchievements.find(hash) != m_UnlockedAchievements.end()) ? 1 : 0;
}

void FruitSaveData::UnlockTotals() {
    // Note: AchievementManager is a no-op stub (#52 audit confirmed safe to skip).
    // 0x00124f10 -- "total-X" thresholds; full impl blocked on AchievementManager port.
}

// Binary @ 0x0012b38c. Queue an achievement unlock. Skip if already pending or unlocked.
// Stagger semantics: if any popup is still in the queue, delay the new one by 3.0s
// so popups don't stomp each other; otherwise fire on the next Update tick (0.0f).
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0012b38c (re-analyst)
//   ldr.w r3,[this,#0x170]  -> m_PendingUnlocks._M_node_count (std::map
//   stores its cached size at base+0x14; map base = +0x15c).
int FruitSaveData::AddToQue(const char* name, uint32_t hash) {
    if (IsAchievementUnlocked(hash) != 0) return 0;
    float timer = m_PendingUnlocks.empty() ? 0.0f : 3.0f;
    AchievementItem& slot = m_PendingUnlocks[hash];
    strncpy(slot.m_Name, name, sizeof(slot.m_Name) - 1);
    slot.m_Name[sizeof(slot.m_Name) - 1] = '\0';
    slot.m_Timer = timer;
    return 1;
}

// ----------------------------------------------------------------------
// Save game-state snapshot
// ----------------------------------------------------------------------

// SaveGameState @ 0x00129ca8 -- resets resume snapshot (entities + waves).
void FruitSaveData::SaveGameState() {
    m_EntityStates.clear();
    m_WaveStates.clear();
    m_bHasActiveGame = 0;
}

void FruitSaveData::CheckDatesHaveChanged() {
    // Daily-reset stub. Binary compares stored timestamp against today
    // and resets daily counters when the day rolls over.
}

void FruitSaveData::DownloadTweaks() {
    // Defunct online service.
}

// ----------------------------------------------------------------------
// Per-frame tick
// ----------------------------------------------------------------------

// Binary @ 0x0012b3dc. Achievement timer tick: find the pending entry with the
// smallest timer, decrement it, fire when it reaches zero, then move to unlocked map.
// Name prefix '0'..'9' -> UnlockAchievementInNetwork (defunct online ID); else -> ItemManager::UnlockItem.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0012b3dc (re-analyst)
void FruitSaveData::Update(float dt, HUD* hud) {
    if (m_PendingUnlocks.empty()) return;

    // Find the entry with the smallest timer.
    std::map<uint32_t, AchievementItem>::iterator nextIt = m_PendingUnlocks.end();
    float smallest = 1e30f;
    for (std::map<uint32_t, AchievementItem>::iterator it = m_PendingUnlocks.begin();
         it != m_PendingUnlocks.end(); ++it) {
        if (it->second.m_Timer < smallest) {
            smallest = it->second.m_Timer;
            nextIt = it;
        }
    }
    if (nextIt == m_PendingUnlocks.end()) return;

    AchievementItem& item = nextIt->second;
    bool wasPositive = (item.m_Timer > 0.0f);
    item.m_Timer -= dt;

    if (wasPositive && item.m_Timer <= 0.0f) {
        // Crossed the threshold: dispatch unlock.
        uint32_t hash = nextIt->first;
        if (item.m_Name[0] >= '0' && item.m_Name[0] <= '9') {
            // Defunct: online achievement ID -- no-op in port.
            AchievementManager::GetInstance()->UnlockAchievementInNetwork(item.m_Name);
        } else {
            ItemManager::GetInstance()->UnlockItem(hash);
        }
        AchievementManager::GetInstance()->UnlockedAchievement(hash, hud);
    }

    if (item.m_Timer <= 0.0f) {
        // Move to unlocked map; erase from pending.
        uint32_t hash = nextIt->first;
        m_UnlockedAchievements[hash] = item;
        m_PendingUnlocks.erase(nextIt);
    }
}

// ----------------------------------------------------------------------
// XML helpers
// ----------------------------------------------------------------------

namespace {

// Mode names per binary GetModeName @ 0x0010b15c. Used to construct
// per-mode attribute keys ("CLASSIChighscore", "ARCADE_dolg", etc.).
const char* k_ModeNames[4] = {
    "CLASSIC",
    "CASINO",
    "ARCADE",
    "ZEN",
};

// GetDaysSince1900 -- days elapsed since 1900-01-01.
// Binary: used by GameOver @ 0x00169fec to write m_LastPlayedDay[mode].
// 25569 = number of days from 1900-01-01 to 1970-01-01 (Unix epoch).
int GetDaysSince1900() {
    static const int DAYS_FROM_1900_TO_EPOCH = 25569;
    return (int)(time(nullptr) / 86400) + DAYS_FROM_1900_TO_EPOCH;
}

// Build "<modeName><suffix>" into out (small stack buffer).
void MakeModeAttr(char* out, size_t outsz, int mode, const char* suffix) {
    snprintf(out, outsz, "%s%s", k_ModeNames[mode], suffix);
}

// Build a comma-separated "v0,v1,...,vN-1" string into buf (no trailing comma).
// Used by SaveGame for the <state> typesToPickFrom / best_combo attrs.
void BuildIntCsv(char* buf, size_t bufsz, const int* vals, int count) {
    if (bufsz == 0) return;
    buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < count; ++i) {
        int n = snprintf(buf + pos, bufsz - pos, (i == 0) ? "%d" : ",%d", vals[i]);
        if (n < 0 || (size_t)n >= bufsz - pos) break;
        pos += (size_t)n;
    }
}

// Parse a comma-separated int CSV into vals[maxCount]; returns the count parsed.
// Inverse of BuildIntCsv; used by ParseSaveFile for typesToPickFrom / best_combo.
int ParseIntCsv(const char* s, int* vals, int maxCount) {
    if (!s) return 0;
    int count = 0;
    const char* p = s;
    while (*p && count < maxCount) {
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        vals[count++] = atoi(p);
        while (*p && *p != ',') ++p;
    }
    return count;
}

// Resolve the on-disk save path. Binary uses /Home/FruitySave.xml on
// Bada; the port writes to <data_dir>/FruitySave.xml so the file lives
// next to the asset tree.
std::string GetSavePath() {
#if defined(__EMSCRIPTEN__)
    // Port specific: on the web build, saves go to the IDBFS-backed /save
    // mount rather than the read-only MEMFS asset bundle.
    return std::string("/save/FruitySave.xml");
#else
    Game* g = Game::GetInstance();
    if (!g) return std::string("FruitySave.xml");
    return g->data_dir + "/FruitySave.xml";
#endif
}

} // namespace

// PlayedModeToday @ 0x0012a248. Returns true iff gameMode was played today
// (m_LastPlayedDay[gameMode] == GetDaysSince1900()) and the per-mode
// "<MODE>_today" total is > 0.
// Key format confirmed via GameOver.cpp AddToTotal site (0x00169f94).
bool FruitSaveData::PlayedModeToday(int gameMode) {
    if (gameMode < 0 || gameMode >= 4) return false;
    if (m_LastPlayedDay[gameMode] != GetDaysSince1900()) return false;
    char buf[68];
    snprintf(buf, sizeof(buf), "%s_today", k_ModeNames[gameMode]);
    return GetTotal(buf) > 0;
}

// ----------------------------------------------------------------------
// ASM-spec v1.6.1 SaveGame @0x001530dc
//
// Emits the binary's FruitySave.xml schema: root <save_file> + <total>
// (cumulative then session) + <que> pending unlocks + <unlocked> confirmed
// + <state> game-state (gated) with child <wave_info>/<wave>/<spawner>
// + <wave_counts_MODE> x4 (always) + <powers> (always, last).
// Full element/attr/field/offset map: tmp/port291/save_load_spec.md.
//
// Deferred: the <state> live-actor <ent> list (read from the live ActorManager,
// not from m_EntityStates) -- see the TODO inside the <state> block.
// ----------------------------------------------------------------------
void SaveGame(FruitSaveData* save) {
    if (!save) return;

    TiXmlDocument doc;
    TiXmlElement root = doc.NewElement("save_file");

    // --- root attrs ---
    root.SetAttribute("version", GetVersionString());
    root.SetAttribute("highscore", save->m_highscore);

    // Per-mode attrs (interleaved per mode, matching the binary loop):
    // "<MODE>highscore", "<MODE>_unposted" (only if > 0), "<MODE>_dolg".
    char attrName[40];
    for (int m = 0; m < 4; m++) {
        MakeModeAttr(attrName, sizeof(attrName), m, "highscore");
        root.SetAttribute(attrName, save->m_ModeHighScores[m]);

        if (save->m_ModeBestCombos[m] > 0) {
            MakeModeAttr(attrName, sizeof(attrName), m, "_unposted");
            root.SetAttribute(attrName, save->m_ModeBestCombos[m]);
        }

        MakeModeAttr(attrName, sizeof(attrName), m, "_dolg");
        root.SetAttribute(attrName, save->m_LastPlayedDay[m]);
    }

    root.SetAttribute("critical_chance", save->m_CriticalChance);
    root.SetAttribute("rated",         save->m_bDojoBGUnlocked ? "true" : "false");
    root.SetAttribute("p2pCancelled",  save->m_bP2PCancelled   ? "true" : "false");
    root.SetAttribute("appLicensedState", game_work.m_gameDataLicensedState);

    // --- <total> elements: cumulative totals first, then session-only ---
    for (std::map<uint32_t, SliceTotal>::iterator it = save->m_Totals.begin();
         it != save->m_Totals.end(); ++it) {
        TiXmlElement e = doc.NewElement("total");
        e.SetAttribute("type",  it->second.name.c_str());
        e.SetAttribute("score", it->second.count);
        root.InsertEndChild(e);
    }
    for (std::map<uint32_t, SliceTotal>::iterator it = save->m_SessionTotals.begin();
         it != save->m_SessionTotals.end(); ++it) {
        TiXmlElement e = doc.NewElement("total");
        e.SetAttribute("u", "true");
        e.SetAttribute("type",  it->second.name.c_str());
        e.SetAttribute("score", it->second.count);
        root.InsertEndChild(e);
    }

    // --- <que>: pending unlocks (ALWAYS emitted) ---
    {
        TiXmlElement que = doc.NewElement("que");
        for (std::map<uint32_t, AchievementItem>::iterator it = save->m_PendingUnlocks.begin();
             it != save->m_PendingUnlocks.end(); ++it) {
            TiXmlElement e = doc.NewElement("ach");
            e.SetAttribute("name", it->second.m_Name);
            e.SetDoubleAttribute("time", (double)it->second.m_Timer);
            que.InsertEndChild(e);
        }
        root.InsertEndChild(que);
    }

    // --- <unlocked>: confirmed unlocks (ALWAYS emitted) ---
    {
        TiXmlElement unl = doc.NewElement("unlocked");
        for (std::map<uint32_t, AchievementItem>::iterator it = save->m_UnlockedAchievements.begin();
             it != save->m_UnlockedAchievements.end(); ++it) {
            TiXmlElement e = doc.NewElement("ach");
            e.SetAttribute("name", it->second.m_Name);
            unl.InsertEndChild(e);
        }
        root.InsertEndChild(unl);
    }

    // --- <state>: game state. Gate: active game OR bomb-hit in flight. ---
    if (save->m_bHasActiveGame != 0 || save->m_BombHitTimer > 0.0f) {
        TiXmlElement st = doc.NewElement("state");
        st.SetAttribute("hasDropped",       save->m_bWasGameOver ? "true" : "false");
        st.SetAttribute("score",            save->m_CurrentScore);
        st.SetAttribute("misses",           save->m_CurrentMissCount);
        st.SetAttribute("mode",             GetModeName((GAME_MODE)save->m_GameMode));
        st.SetAttribute("consecutiveCount", save->m_ComboCount);
        st.SetAttribute("consecutiveType",  save->m_LastSlasher);
        st.SetDoubleAttribute("timer",      (double)save->m_TimeRemainingSave);
        st.SetDoubleAttribute("gameTime",   (double)game_work.m_ElapsedGameTime);

        // globalWaveDt <- m_WaveScalar_v161 (+0x150), written as a "%f" string
        // (binary OS_SPrintf's it into a stack buffer then SetAttribute as a string).
        char waveDtBuf[32];
        snprintf(waveDtBuf, sizeof(waveDtBuf), "%f", save->m_WaveScalar_v161);
        st.SetAttribute("globalWaveDt", waveDtBuf);

        if (save->m_FruitQueueCount > 0) {
            char csv[256];
            BuildIntCsv(csv, sizeof(csv), save->m_FruitQueue, save->m_FruitQueueCount);
            st.SetAttribute("typesToPickFrom", csv);
        }
        if (save->m_BestComboLength > 0) {
            char csv[128];
            BuildIntCsv(csv, sizeof(csv), save->m_BestComboFruits, save->m_BestComboLength);
            st.SetAttribute("best_combo", csv);
        }

        st.SetAttribute("go_state",            save->m_GameOverScreenState);
        st.SetDoubleAttribute("go_time",       (double)save->m_GameOverTimer);
        st.SetDoubleAttribute("go_bombHitTime", (double)save->m_BombHitTimer);
        st.SetDoubleAttribute("go_transition", (double)save->m_NextComboBonus);
        st.SetAttribute("go_body",  save->m_GameOverField1);
        st.SetAttribute("go_head",  save->m_GameOverField2);
        st.SetAttribute("go_fruit", save->m_GameOverField3);
        st.SetAttribute("go_fact",  save->m_GameOverField4);
        st.SetAttribute("go_showHighScore", save->newBestThisGame ? "true" : "false");
        st.SetAttribute("go_setScore",      save->secondaryFlag   ? "true" : "false");

        // Speed block: only when desiredSpeed (m_Speed_P0_alias) > 0.
        if (save->m_Speed_P0_alias > 0.0f) {
            st.SetDoubleAttribute("speedLossTime",  (double)save->m_Speed_P0);
            st.SetDoubleAttribute("desiredSpeed",   (double)save->m_Speed_P0_alias);
            st.SetDoubleAttribute("nextComboBonus", (double)save->m_Speed_P1);
        }

        st.SetDoubleAttribute("shake_time", (double)save->m_ShakeIntensity);
        // DIFFERS: original SaveGame @0x001530dc writes shake_max_time from m_ShakeIntensity
        // (+0x138) -- the SAME field as shake_time, NOT m_ShakeDecay. ParseSaveFile loads
        // shake_max_time into m_ShakeDecay, so a round-trip sets m_ShakeDecay := m_ShakeIntensity.
        // Replicated intentionally as a faithful binary quirk.
        st.SetDoubleAttribute("shake_max_time", (double)save->m_ShakeIntensity);

        // child <wave_info> (ParseWaveInfo inverse) + <wave>/<spawner> lists.
        {
            TiXmlElement wi = doc.NewElement("wave_info");
            wi.SetAttribute("waveCount",            save->m_pCurrentWave_P1);
            wi.SetAttribute("numberOfWavesSpawned", (int)save->m_WaveDelay);
            wi.SetDoubleAttribute("waveDelay",      (double)save->m_WaveWait);
            wi.SetDoubleAttribute("waveWait",       (double)save->m_ProbabilityOverideFlag);
            wi.SetAttribute("blitzSpawnedThisGame",     save->m_blitzSpawnedThisGame);
            wi.SetAttribute("blitzForceSpawnedCounter", save->m_blitzForceSpawnedCounter);
            wi.SetDoubleAttribute("blitzSpawnTime",     (double)save->m_blitzSpawnTime);

            for (std::list<WaveState>::iterator wit = save->m_WaveStates.begin();
                 wit != save->m_WaveStates.end(); ++wit) {
                TiXmlElement w = doc.NewElement("wave");
                w.SetAttribute("inc",   (int)wit->waveIdx);
                w.SetAttribute("index", (int)wit->index);
                for (std::list<SpawnState>::iterator sit = wit->spawners.begin();
                     sit != wit->spawners.end(); ++sit) {
                    TiXmlElement sp = doc.NewElement("spawner");
                    sp.SetDoubleAttribute("delay", (double)sit->timer);
                    sp.SetAttribute("toSpawn",     (int)sit->count);
                    w.InsertEndChild(sp);
                }
                wi.InsertEndChild(w);
            }
            st.InsertEndChild(wi);
        }

        // TODO: v1.6.1 0x001530dc <ent>/<powers> live-actor serialisation
        // (needs ActorManager::GetEntityFirst/Next + SuperFruitControl::
        // SaveSuperFruitState wiring). The <state> entity list is read from the
        // live ActorManager, not from m_EntityStates -- deferred for now.

        root.InsertEndChild(st);
    }

    // --- <wave_counts_MODE> x4: per-mode score history (ALWAYS, even if empty) ---
    for (int m = 0; m < 4; m++) {
        char tag[40];
        snprintf(tag, sizeof(tag), "wave_counts_%s", k_ModeNames[m]);
        TiXmlElement container = doc.NewElement(tag);
        for (std::map<int, int>::iterator it = save->m_ModeScoreHistory[m].begin();
             it != save->m_ModeScoreHistory[m].end(); ++it) {
            TiXmlElement e = doc.NewElement("game_count");
            e.SetAttribute("waveIdx", it->first);
            e.SetAttribute("games",   it->second);
            container.InsertEndChild(e);
        }
        root.InsertEndChild(container);
    }

    // --- <powers> (ALWAYS, last): active power-up state via SaveActivePowerUps ---
    {
        TiXmlElement powers = doc.NewElement("powers");
        PowerUpManager::GetInstance()->SaveActivePowerUps(&powers);
        root.InsertEndChild(powers);
    }

    doc.InsertEndChild(root);
    doc.SaveFile(GetSavePath().c_str());
#if defined(__EMSCRIPTEN__)
    // Port specific: flush the IDBFS /save mount to IndexedDB after each
    // write so data survives page reload/close.
    EM_ASM({ FS.syncfs(false, function(err) {}); });
#endif
}

// ----------------------------------------------------------------------
// ASM-spec v1.6.1 ParseWaveInfo @0x00154510
//
// Reads the <wave_info> scalar attrs into the WaveManager-resume fields, then
// rebuilds m_WaveStates from the <wave>/<spawner> child lists. Always returns 1.
// NOTE the binary's attr->field map: "numberOfWavesSpawned"->m_WaveDelay (read as
// an int into the float slot), "waveDelay"->m_WaveWait, "waveWait"->m_ProbabilityOverideFlag.
// ----------------------------------------------------------------------
int ParseWaveInfo(TiXmlElement* elem, FruitSaveData* data) {
    if (!elem || !data) return 1;

    elem->QueryIntAttribute("waveCount", &data->m_pCurrentWave_P1);   // +0x140

    // +0x144 is read as an int in the binary; the port slot is a float, so read
    // into an int and cast (the field semantically holds an integer wave count).
    int nws = (int)data->m_WaveDelay;
    elem->QueryIntAttribute("numberOfWavesSpawned", &nws);
    data->m_WaveDelay = (float)nws;

    elem->QueryFloatAttribute("waveDelay", &data->m_WaveWait);                 // +0x148
    elem->QueryFloatAttribute("waveWait",  &data->m_ProbabilityOverideFlag);   // +0x14c
    elem->QueryIntAttribute("blitzSpawnedThisGame",     &data->m_blitzSpawnedThisGame);
    elem->QueryIntAttribute("blitzForceSpawnedCounter", &data->m_blitzForceSpawnedCounter);
    elem->QueryFloatAttribute("blitzSpawnTime",         &data->m_blitzSpawnTime);

    data->m_WaveStates.clear();   // +0x154
    for (TiXmlElement w = elem->FirstChildElement("wave"); w;
         w = w.NextSiblingElement("wave")) {
        WaveState ws;
        // "inc" is a float in the binary (counter stored as float); the port slot
        // is an int, so read into a float and round.
        float inc = (float)ws.waveIdx;
        w.QueryFloatAttribute("inc", &inc);
        ws.waveIdx = (int)inc;
        int idx = (int)ws.index;
        w.QueryIntAttribute("index", &idx);
        ws.index = (uint32_t)idx;

        for (TiXmlElement sp = w.FirstChildElement("spawner"); sp;
             sp = sp.NextSiblingElement("spawner")) {
            SpawnState ss;
            sp.QueryFloatAttribute("delay", &ss.timer);   // SpawnState.timer
            int toSpawn = (int)ss.count;
            sp.QueryIntAttribute("toSpawn", &toSpawn);     // SpawnState.count (read as int)
            ss.count = (float)toSpawn;
            ws.spawners.push_back(ss);
        }
        data->m_WaveStates.push_back(ws);
    }
    return 1;
}

// ----------------------------------------------------------------------
// ASM-spec v1.6.1 ParseSaveFile @0x00154c8c (_Z13ParseSaveFileP9TiXmlNodeP13FruitSaveData)
//
// Recursive tag-walker. Container tags (save_file, state) read their own attrs
// then fall through to recurse into element children; leaf handlers fully
// consume their subtree and return. Uses the binary's exact element/attr names.
// ----------------------------------------------------------------------
void ParseSaveFile(TiXmlNode* node, FruitSaveData* data) {
    if (!node || !node->m_node || !data) return;
    TiXmlElement self(node->m_node);
    const char* tag = self.Name();
    if (!tag || !*tag) return;

    if (strcmp(tag, "save_file") == 0) {
        const char* ver = self.Attribute("version");
        if (ver) ParseVersionInfo(ver, data);
        self.QueryIntAttribute("highscore",        &data->m_highscore);
        self.QueryIntAttribute("critical_chance",  &data->m_CriticalChance);
        self.QueryIntAttribute("appLicensedState", &game_work.m_gameDataLicensedState);
        const char* rated = self.Attribute("rated");
        if (rated) data->m_bDojoBGUnlocked = (strcmp(rated, "true") == 0) ? 1 : 0;
        const char* p2p = self.Attribute("p2pCancelled");
        if (p2p) data->m_bP2PCancelled = (strcmp(p2p, "true") == 0) ? 1 : 0;
        char an[40];
        for (int m = 0; m < 4; m++) {
            snprintf(an, sizeof(an), "%shighscore", k_ModeNames[m]);
            self.QueryIntAttribute(an, &data->m_ModeHighScores[m]);
            snprintf(an, sizeof(an), "%s_unposted", k_ModeNames[m]);
            self.QueryIntAttribute(an, &data->m_ModeBestCombos[m]);
            snprintf(an, sizeof(an), "%s_dolg", k_ModeNames[m]);
            self.QueryIntAttribute(an, &data->m_LastPlayedDay[m]);
        }
        // fall through to recurse into children (total/que/unlocked/state/wave_counts_*)
    } else if (strcmp(tag, "que") == 0) {
        ParseAchievements(&self, data, true);   // pending unlocks
        return;
    } else if (strcmp(tag, "unlocked") == 0) {
        ParseAchievements(&self, data, false);  // confirmed unlocks
        return;
    } else if (strcmp(tag, "total") == 0) {
        const char* type = self.Attribute("type");
        if (type && *type) {
            int score = 0;
            self.QueryIntAttribute("score", &score);
            const char* u = self.Attribute("u");
            bool isSession = (u && strcmp(u, "true") == 0);
            data->AddToTotal(type, StringHash(type), score, isSession, false);
        }
        return;
    } else if (strcmp(tag, "ent") == 0) {
        // Version guard: only resume entities for a matching build.
        if (data->m_GameMode <= 3 && data->m_VersionInfo == GetVersionTotal()) {
            EntityState es;
            const char* vel  = self.Attribute("vel");
            const char* pos  = self.Attribute("pos");
            const char* grav = self.Attribute("grav");
            if (vel)  { Vec3 v = ParseVector(vel);  es.m_Velocity[0] = v.x; es.m_Velocity[1] = v.y; es.m_Velocity[2] = v.z; }
            if (pos)  { Vec3 v = ParseVector(pos);  es.m_Position[0] = v.x; es.m_Position[1] = v.y; es.m_Position[2] = v.z; }
            if (grav) { Vec3 v = ParseVector(grav); es.m_Overlay[0]  = v.x; es.m_Overlay[1]  = v.y; es.m_Overlay[2]  = v.z; }
            self.QueryIntAttribute("type", &es.m_KindIndex);
            const char* hit = self.Attribute("hit");
            if (hit) es.m_BombHitFlag = (uint8_t)((strcmp(hit, "true") == 0) ? 1 : 0);
            // TODO: v1.6.1 0x00154c8c <ent> wait/sliceWait/<superFruitState> are not
            // represented in the port's lean EntityState resume struct; only
            // pos/vel/grav/type/hit are restored. (Save side defers <ent> entirely.)
            data->m_EntityStates.push_back(es);
        }
        return;
    } else if (strcmp(tag, "powers") == 0) {
        if (data->m_GameMode <= 3 && data->m_VersionInfo == GetVersionTotal()) {
            PowerUpManager::GetInstance()->LoadActivePowerUps(&self, (int)data->m_GameMode);
        }
        return;
    } else if (strcmp(tag, "state") == 0) {
        // game_work._137 = 1 in the binary marks "resume snapshot present". The port's
        // resume path keys off FruitSaveData::m_bHasActiveGame instead (WaveManager::Resume
        // gate @ WaveManager.cpp + GameInit), so set that as the _137 stand-in.
        // TODO: v1.6.1 game_work._137 offset unconfirmed -- set that field instead once resolved.
        data->m_bHasActiveGame = 1;
        data->m_EntityStates.clear();

        const char* mode = self.Attribute("mode");
        if (mode) {
            unsigned int gm = ParseGameMode(StringHash(mode));
            if (gm < 4) data->m_GameMode = gm;
        }
        self.QueryIntAttribute("score",  &data->m_CurrentScore);
        self.QueryIntAttribute("misses", &data->m_CurrentMissCount);
        const char* hasDropped = self.Attribute("hasDropped");
        if (hasDropped) data->m_bWasGameOver = (strcmp(hasDropped, "true") == 0) ? 1 : 0;
        self.QueryIntAttribute("consecutiveCount", &data->m_ComboCount);
        self.QueryIntAttribute("consecutiveType",  &data->m_LastSlasher);
        self.QueryFloatAttribute("timer",    &data->m_TimeRemainingSave);
        self.QueryFloatAttribute("gameTime", &game_work.m_ElapsedGameTime);
        self.QueryFloatAttribute("globalWaveDt", &data->m_WaveScalar_v161);   // +0x150

        const char* types = self.Attribute("typesToPickFrom");
        if (types) data->m_FruitQueueCount = ParseIntCsv(types, data->m_FruitQueue, 32);
        const char* bc = self.Attribute("best_combo");
        if (bc) data->m_BestComboLength = ParseIntCsv(bc, data->m_BestComboFruits, 11);

        self.QueryIntAttribute("go_head",  &data->m_GameOverField2);
        self.QueryIntAttribute("go_body",  &data->m_GameOverField1);
        self.QueryIntAttribute("go_fruit", &data->m_GameOverField3);
        self.QueryIntAttribute("go_fact",  &data->m_GameOverField4);
        const char* shs = self.Attribute("go_showHighScore");
        if (shs) data->newBestThisGame = (strcmp(shs, "true") == 0) ? 1 : 0;
        const char* ssa = self.Attribute("go_setScore");
        if (ssa) data->secondaryFlag = (strcmp(ssa, "true") == 0) ? 1 : 0;
        self.QueryIntAttribute("go_state",  &data->m_GameOverScreenState);
        self.QueryFloatAttribute("go_time", &data->m_GameOverTimer);
        self.QueryFloatAttribute("go_bombHitTime", &data->m_BombHitTimer);
        self.QueryFloatAttribute("go_transition",  &data->m_NextComboBonus);
        self.QueryFloatAttribute("shake_time",     &data->m_ShakeIntensity);
        // shake_max_time -> m_ShakeDecay (binary quirk: SaveGame wrote it from m_ShakeIntensity).
        self.QueryFloatAttribute("shake_max_time", &data->m_ShakeDecay);
        self.QueryFloatAttribute("speedLossTime",  &data->m_Speed_P0);
        self.QueryFloatAttribute("desiredSpeed",   &data->m_Speed_P0_alias);
        self.QueryFloatAttribute("nextComboBonus", &data->m_Speed_P1);
        // fall through to recurse into children (wave_info / ent / powers)
    } else if (strcmp(tag, "wave_info") == 0) {
        if (data->m_GameMode <= 3 && data->m_VersionInfo == GetVersionTotal()) {
            ParseWaveInfo(&self, data);
        }
        return;
    } else {
        // wave_counts_<MODE> dispatch; otherwise fall through to recurse.
        for (int m = 0; m < 4; m++) {
            char wc[40];
            snprintf(wc, sizeof(wc), "wave_counts_%s", k_ModeNames[m]);
            if (strcmp(tag, wc) == 0) {
                for (TiXmlElement e = self.FirstChildElement("game_count"); e;
                     e = e.NextSiblingElement("game_count")) {
                    int waveIdx = 0, games = 0;
                    e.QueryIntAttribute("waveIdx", &waveIdx);
                    e.QueryIntAttribute("games",   &games);
                    data->m_ModeScoreHistory[m][waveIdx] = games;
                }
                return;
            }
        }
        // unknown tag: recurse into its children.
    }

    // RECURSE: walk element children.
    for (TiXmlElement c = self.FirstChildElement(); c; c = c.NextSiblingElement())
        ParseSaveFile(&c, data);
}

// ----------------------------------------------------------------------
// ASM-spec v1.6.1 LoadGame @0x0015591c
//
// Loads FruitySave.xml and dispatches the whole document through ParseSaveFile
// (binary pattern). Applies the version-mismatch reset (discard stale
// version-sensitive stats) and the post-parse game-mode clamp.
// ----------------------------------------------------------------------
bool LoadGame(FruitSaveData* save) {
    if (!save) return false;

    TiXmlDocument doc;
    std::string sp = GetSavePath();
    if (!doc.LoadFile(sp.c_str())) {
        return false;  // expected on first run
    }

    TiXmlElement root = doc.FirstChildElement("save_file");
    if (!root) return false;

    save->m_EntityStates.clear();
    save->m_WaveStates.clear();

    ParseSaveFile(&root, save);

    // Version-mismatch reset: when the save predates the running build, discard
    // the version-sensitive stats (binary LoadGame @0x0015591c tail).
    if (save->m_VersionInfo != GetVersionTotal()) {
        save->ClearTotal(StringHash("unrated_games"));
        for (int m = 0; m < 4; m++) save->m_ModeScoreHistory[m].clear();
        save->m_bDojoBGUnlocked = 0;
    }

    // Validate game mode (clamp to 0..3).
    if (save->m_GameMode > 3) save->m_GameMode = 0;

    // Daily-reset hook.
    save->CheckDatesHaveChanged();

    return true;
}

// ----------------------------------------------------------------------
// isSaving flag -- v1.6.1 @0x001ca458 (GetIsSavingBool)
// Set by SaveCurrentData at entry; cleared after SaveGame returns.
// GameTaskSaveOnExit reads it to avoid re-entrant saves.
// ----------------------------------------------------------------------
static bool isSaving = false;

bool* GetIsSavingBool() {
    return &isSaving;
}

// ----------------------------------------------------------------------
// ASM-spec v1.6.1 SaveCurrentData @ 0x001cde20
//
// Binary builds a STACK-LOCAL snapshot of pSaveData with live game state
// copied in (so the writer sees the most-recent score/missCount/etc.
// without mutating pSaveData itself). Port matches that pattern: copies
// pSaveData, populates the snapshot's live fields from Game, then
// SaveGame(&snapshot). pSaveData itself is left untouched so existing
// in-memory state survives the save.
// ----------------------------------------------------------------------
void SaveCurrentData(bool /*fullSave*/) {
    Game* g = Game::GetInstance();
    if (!g || !game_work.m_SaveData) return;

    *GetIsSavingBool() = true;

    // ItemSave.xml is always written (coin balance + bought/equipped).
    ItemManager::GetInstance()->SaveItemInfo();

    // Snapshot: deep-copy current pSaveData, then overwrite live fields.
    FruitSaveData snapshot = *game_work.m_SaveData;

    snapshot.m_CurrentScore     = game_work.currentScore;
    snapshot.m_CurrentMissCount = (int)game_work.missCount;
    snapshot.m_GameMode         = (uint32_t)game_work.gameMode;
    snapshot.m_CriticalChance   = game_work.m_ScoreThreshold;

    // Snapshot combo globals into save fields before writing.
    // Binary: SaveCurrentData @ 0x001cde20 offset 0x001ccd08/0x001ccd34.
    snapshot.SnapshotComboState();

    // Binary does NOT update +0x40 in SaveCurrentData; it is rebuilt as the
    // CLASSIC-mode alias by ParseSaveFile on next load.

    // Bomb-hit timer: binary saves only when timer is meaningfully
    // active (zen-mode special case). Port saves unconditionally for
    // simplicity.
    snapshot.m_BombHitTimer = game_work.m_BombHitTimer;

    SaveGame(&snapshot);
    *GetIsSavingBool() = false;
}

// Defunct: online tweaks -- no-op stub; v1.6.1 binary @ 0x0012a080
// Binary finds m_SessionTotals entry by hash(name) and writes count = value.
void FruitSaveData::DownloadedTweakValue(char const*, int) {}

// Defunct: online achievements -- no-op stub; v1.6.1 binary @ 0x0012a194
// Binary gates on NetworkManager::IsOnline()||IsP2POnline(), then pushes each
// numeric-prefixed unlocked achievement via UnlockAchievementInNetwork.
void FruitSaveData::PublishUnlockedAchievements() {}

// Binary @ 0x0012b2b0 -- SetTotal.
// old = GetTotal(name); AddToTotal(name, hash, value-old, ...); return old.
// The delta (value - old) makes the cumulative total settle at exactly `value`.
unsigned int FruitSaveData::SetTotal(char const* name, int value,
                                     bool trackSession, bool sendNetPacket) {
    unsigned int old = (unsigned int)GetTotal(name);
    uint32_t hash = StringHash(name);
    AddToTotal(name, hash, value - (int)old, trackSession, sendNetPacket);
    return old;
}

// Binary @ 0x0012a0fc -- TotalExists(name): hash the name, delegate to TotalExists(hash).
bool FruitSaveData::TotalExists(char const* name) {
    return TotalExists(StringHash(name));
}

// Binary @ 0x00129bb4 -- TotalExists(hash): true if hash is present in
// m_Totals (+0x00) or m_SessionTotals (+0x18).
bool FruitSaveData::TotalExists(unsigned long hash) {
    if (m_Totals.find(hash) != m_Totals.end()) return true;
    return m_SessionTotals.find(hash) != m_SessionTotals.end();
}

// ASM-spec v1.6.1 ParseVersionInfo @0x00152f30 (_Z16ParseVersionInfoPKcP13FruitSaveData)
// Parses the "version" string from the save file header and stores the packed int.
void ParseVersionInfo(const char* s, FruitSaveData* sd) {
    sd->m_VersionInfo = GetVersionFromString(s);
}

// File-scope accumulator for FruitCounter. Binary: BSS global at file scope.
static int total_fruit = 0;

// ASM-spec v1.6.1 FruitCounter @0x00159f10 (_Z12FruitCounterPKciiPv)
// Iteration callback: accumulates count into total_fruit; arg3/arg4 unused.
int FruitCounter(const char* /*name*/, int count, int /*extra*/, void* /*ctx*/) {
    total_fruit += count;
    return 1;
}

// ASM-spec v1.6.1 GetSaveFileFullPath @0x00152610 (_Z19GetSaveFileFullPathv)
// DIFFERS: original returns "/Home/FruitySave.xml" (v1.6.1 @0x00152610),
//   using GetSavePath() for port I/O because Bada /Home path is N/A on the host.
const char* GetSaveFileFullPath() {
    return "/Home/FruitySave.xml";
}

// ASM-spec v1.6.1 GetLoadFileFullPath @0x0015262c (_Z19GetLoadFileFullPathv)
// DIFFERS: original returns "FruitySave.xml" (relative, v1.6.1 @0x0015262c),
//   using GetSavePath() for port I/O because the asset tree layout differs on host.
const char* GetLoadFileFullPath() {
    return "FruitySave.xml";
}

// v1.6.1 GetUserFilePath @0x00154494 (_Z15GetUserFilePathPcPKci)
// Dead function in binary (no live callers; only EXTERNAL entry-point xref).
// Resolves user-data file path: appends filename to save-root dir if available,
// otherwise falls back to a bounded copy of filename alone.
// Port: GetSaveRootDirectory is a defunct no-op stub returning 0, so the fallback
// branch (snprintf) is always taken.
char* GetUserFilePath(char* outBuf, const char* filename, int maxLen) {
    FileManager& fm = FileManager::GetInstance();
    int ok = fm.GetSaveRootDirectory(outBuf, "\\Halfbrick\\FruitNinja\\", true);
    if (ok == 0) {
        snprintf(outBuf, (size_t)maxLen, "%s", filename);
    } else {
        strcat(outBuf, filename);
    }
    return outBuf;
}
