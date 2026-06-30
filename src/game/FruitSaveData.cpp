// FruitSaveData -- save/load to FruitySave.xml + slice-total maps.
//
// Implements binary SaveGame @ 0x0012a2fc and
// LoadGame @ 0x0012be74. Coin balance is owned by ItemSave.xml
// via ItemManager and is only mirrored here for in-memory access.
//
// Analysed: 2026-04-23T02:00, REVISED 2026-05-02T00:00

#include "FruitSaveData.h"
#include "ScoreState.h"
#include "Game.h"
#include "ItemManager.h"
#include "AchievementManager.h"
#include "engine/util/StringHash.h"
#include "engine/xml/TiXml.h"
#include "engine/core/SystemManager.h"
#include "engine/asset/FileManager.h"

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

// AddToTotal @ 0x0012b21c.
// Inserts/updates SliceTotal entry, returns new count.
int FruitSaveData::AddToTotal(const char* name, uint32_t hash, int count,
                              bool trackSession, bool /*sendNetPacket*/) {
    if (!name || !*name) return 0;

    auto it = m_Totals.find(hash);
    if (it == m_Totals.end()) {
        m_Totals[hash] = SliceTotal(name, count);
    } else {
        it->second.count += count;
    }

    if (trackSession) {
        auto sit = m_SessionTotals.find(hash);
        if (sit == m_SessionTotals.end()) {
            m_SessionTotals[hash] = SliceTotal(name, count);
        } else {
            sit->second.count += count;
        }
    }

    // Network packet: defunct -- no-op.
    return m_Totals[hash].count;
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

// Save format version. Binary uses GetVersionTotal() which encodes
// build info; port pins to a single byte for now and bumps when the
// schema changes.
// const not constexpr (4.4 doesn't accept constexpr).
static const int k_SaveVersion = 1;

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
// SaveGame @ 0x0012a2fc
// ----------------------------------------------------------------------
void SaveGame(FruitSaveData* save) {
    if (!save) return;

    TiXmlDocument doc;
    TiXmlElement root = doc.NewElement("save_file");

    // Top-level scalar attributes (per binary writer order).
    char buf[64];
    snprintf(buf, sizeof(buf), "%d.%d.%d", k_SaveVersion, 0, 0);
    root.SetAttribute("version", buf);
    root.SetAttribute("highscore", save->m_highscore);

    // Per-mode attrs: "<MODE>highscore", "<MODE>_unposted" (combo, only
    // if > 0), "<MODE>_dolg" (play count).
    char attrName[32];
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

    // SliceTotal elements: cumulative totals first, then session-only.
    // Range-for replaced with iterator form for GCC 4.4 (asm-verify cross
    // toolchain) parser compatibility; same semantics in both compilers.
    for (std::map<uint32_t, SliceTotal>::iterator it = save->m_Totals.begin();
         it != save->m_Totals.end(); ++it) {
        TiXmlElement e = doc.NewElement("SliceTotal");
        e.SetAttribute("name",  it->second.name.c_str());
        e.SetAttribute("count", it->second.count);
        root.InsertEndChild(e);
    }
    for (std::map<uint32_t, SliceTotal>::iterator it = save->m_SessionTotals.begin();
         it != save->m_SessionTotals.end(); ++it) {
        TiXmlElement e = doc.NewElement("SliceTotal");
        e.SetAttribute("u", "true");
        e.SetAttribute("name",  it->second.name.c_str());
        e.SetAttribute("count", it->second.count);
        root.InsertEndChild(e);
    }

    // Pending unlocks map ("unlocked" container with timer attr).
    if (!save->m_PendingUnlocks.empty()) {
        TiXmlElement ach = doc.NewElement("unlocked");
        for (std::map<uint32_t, AchievementItem>::iterator it = save->m_PendingUnlocks.begin();
             it != save->m_PendingUnlocks.end(); ++it) {
            TiXmlElement e = doc.NewElement("achievement");
            e.SetAttribute("name", it->second.m_Name);
            e.SetAttribute("timer", it->second.m_Timer);
            ach.InsertEndChild(e);
        }
        root.InsertEndChild(ach);
    }

    // Unlocked achievements ("achievements" container).
    if (!save->m_UnlockedAchievements.empty()) {
        TiXmlElement unl = doc.NewElement("achievements");
        for (std::map<uint32_t, AchievementItem>::iterator it = save->m_UnlockedAchievements.begin();
             it != save->m_UnlockedAchievements.end(); ++it) {
            TiXmlElement e = doc.NewElement("achievement");
            e.SetAttribute("name", it->second.m_Name);
            unl.InsertEndChild(e);
        }
        root.InsertEndChild(unl);
    }

    // Per-mode score history (<wave_counts_MODE> blocks).
    for (int m = 0; m < 4; m++) {
        if (save->m_ModeScoreHistory[m].empty()) continue;
        char tag[48];
        snprintf(tag, sizeof(tag), "wave_counts_%s", k_ModeNames[m]);
        TiXmlElement container = doc.NewElement(tag);
        for (std::map<int, int>::iterator it = save->m_ModeScoreHistory[m].begin();
             it != save->m_ModeScoreHistory[m].end(); ++it) {
            TiXmlElement e = doc.NewElement("game_count");
            e.SetAttribute("score",   it->first);
            e.SetAttribute("waveIdx", it->second);
            container.InsertEndChild(e);
        }
        root.InsertEndChild(container);
    }

    // ActiveGame <que> block: only when m_bHasActiveGame is set.
    // Port skips entity/wave list serialisation for now; just emits
    // the scalar resume fields so resume coordinates persist across
    // a non-clean exit.
    if (save->m_bHasActiveGame) {
        TiXmlElement que = doc.NewElement("que");
        que.SetAttribute("mode",            k_ModeNames[save->m_GameMode <= 3 ? save->m_GameMode : 0]);
        que.SetAttribute("hasDropped",      save->m_bWasGameOver ? "true" : "false");
        que.SetAttribute("count",           save->m_CurrentScore);
        que.SetAttribute("misses",          save->m_CurrentMissCount);
        que.SetAttribute("count1",          save->m_ComboCount);
        que.SetAttribute("count2",          save->m_LastSlasher);
        que.SetAttribute("timer",           save->m_TimeRemainingSave);
        que.SetAttribute("globalWaveDt",    save->m_ProbabilityOverideFlag);
        que.SetAttribute("go_state",        save->m_GameOverScreenState);
        que.SetAttribute("go_time",         save->m_GameOverTimer);
        que.SetAttribute("go_bombHitTime",  save->m_BombHitTimer);
        que.SetAttribute("go_body",         save->m_GameOverField1);
        que.SetAttribute("go_head",         save->m_GameOverField2);
        que.SetAttribute("go_fruit",        save->m_GameOverField3);
        que.SetAttribute("go_fact",         save->m_GameOverField4);
        que.SetAttribute("go_showHighScore", save->newBestThisGame ? "true" : "false");
        que.SetAttribute("go_setScore",     save->secondaryFlag ? "true" : "false");
        que.SetAttribute("nextComboBonus",  save->m_NextComboBonus);
        que.SetAttribute("shake_time",      save->m_ShakeIntensity);
        que.SetAttribute("shake_max_time",  save->m_ShakeDecay);

        // TODO: resolve XML attr literal name for m_WaveScalar_v161 (GOT 0xfffb06e6).
        // Using "waveScalar" as placeholder; round-trip is self-consistent regardless.
        que.SetAttribute("waveScalar", save->m_WaveScalar_v161);

        TiXmlElement wi = doc.NewElement("wave_info");
        wi.SetAttribute("waveCount",                save->m_pCurrentWave_P1);
        wi.SetAttribute("waveDelay",                save->m_WaveDelay);
        wi.SetAttribute("waveWait",                 save->m_WaveWait);
        wi.SetAttribute("blitzSpawnedThisGame",     save->m_blitzSpawnedThisGame);
        wi.SetAttribute("blitzForceSpawnedCounter", save->m_blitzForceSpawnedCounter);
        wi.SetAttribute("blitzSpawnTime",           save->m_blitzSpawnTime);
        que.InsertEndChild(wi);

        root.InsertEndChild(que);
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
// LoadGame @ 0x0012be74
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

    // Top-level scalar attributes.
    root.QueryIntAttribute("highscore", &save->m_highscore);

    char attrName[32];
    for (int m = 0; m < 4; m++) {
        snprintf(attrName, sizeof(attrName), "%shighscore", k_ModeNames[m]);
        root.QueryIntAttribute(attrName, &save->m_ModeHighScores[m]);

        snprintf(attrName, sizeof(attrName), "%s_unposted", k_ModeNames[m]);
        root.QueryIntAttribute(attrName, &save->m_ModeBestCombos[m]);

        snprintf(attrName, sizeof(attrName), "%s_dolg", k_ModeNames[m]);
        root.QueryIntAttribute(attrName, &save->m_LastPlayedDay[m]);
    }

    root.QueryIntAttribute("critical_chance", &save->m_CriticalChance);

    const char* ratedAttr = root.Attribute("rated");
    if (ratedAttr) save->m_bDojoBGUnlocked = (strcmp(ratedAttr, "true") == 0) ? 1 : 0;

    const char* p2pAttr = root.Attribute("p2pCancelled");
    if (p2pAttr) save->m_bP2PCancelled = (strcmp(p2pAttr, "true") == 0) ? 1 : 0;

    // SliceTotal elements (cumulative + session).
    for (TiXmlElement e = root.FirstChildElement("SliceTotal"); e;
         e = e.NextSiblingElement("SliceTotal")) {
        const char* name = e.Attribute("name");
        if (!name || !*name) continue;
        int count = 0;
        e.QueryIntAttribute("count", &count);
        const char* uAttr = e.Attribute("u");
        bool isSession = (uAttr && strcmp(uAttr, "true") == 0);

        uint32_t hash = StringHash(name);
        if (isSession) {
            save->m_SessionTotals[hash] = SliceTotal(name, count);
        } else {
            save->m_Totals[hash] = SliceTotal(name, count);
        }
    }

    // Pending unlocks: <unlocked> container (with timer attr).
    {
        TiXmlElement progress = root.FirstChildElement("unlocked");
        if (progress) {
            for (TiXmlElement e = progress.FirstChildElement("achievement"); e;
                 e = e.NextSiblingElement("achievement")) {
                const char* name = e.Attribute("name");
                if (!name || !*name) continue;
                AchievementItem item;
                strncpy(item.m_Name, name, sizeof(item.m_Name) - 1);
                item.m_Name[sizeof(item.m_Name) - 1] = '\0';
                e.QueryFloatAttribute("timer", &item.m_Timer);
                save->m_PendingUnlocks[StringHash(name)] = item;
            }
        }
    }

    // Unlocked achievements: <achievements> container.
    {
        TiXmlElement unl = root.FirstChildElement("achievements");
        if (unl) {
            for (TiXmlElement e = unl.FirstChildElement("achievement"); e;
                 e = e.NextSiblingElement("achievement")) {
                const char* name = e.Attribute("name");
                if (!name || !*name) continue;
                AchievementItem item;
                strncpy(item.m_Name, name, sizeof(item.m_Name) - 1);
                item.m_Name[sizeof(item.m_Name) - 1] = '\0';
                save->m_UnlockedAchievements[StringHash(name)] = item;
            }
        }
    }

    // Per-mode score history.
    for (int m = 0; m < 4; m++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "wave_counts_%s", k_ModeNames[m]);
        TiXmlElement container = root.FirstChildElement(tag);
        if (!container) continue;
        for (TiXmlElement e = container.FirstChildElement("game_count"); e;
             e = e.NextSiblingElement("game_count")) {
            int score = 0, waveIdx = 0;
            e.QueryIntAttribute("score",   &score);
            e.QueryIntAttribute("waveIdx", &waveIdx);
            save->m_ModeScoreHistory[m][score] = waveIdx;
        }
    }

    // ActiveGame <que> block.
    {
        TiXmlElement que = root.FirstChildElement("que");
        if (que) {
            save->m_bHasActiveGame = 1;
            const char* mode = que.Attribute("mode");
            if (mode) {
                for (int m = 0; m < 4; m++) {
                    if (strcmp(mode, k_ModeNames[m]) == 0) {
                        save->m_GameMode = (uint32_t)m;
                        break;
                    }
                }
            }
            const char* hasDropped = que.Attribute("hasDropped");
            if (hasDropped) save->m_bWasGameOver = (strcmp(hasDropped, "true") == 0) ? 1 : 0;
            que.QueryIntAttribute("count",   &save->m_CurrentScore);
            que.QueryIntAttribute("misses",  &save->m_CurrentMissCount);
            que.QueryIntAttribute("count1",  &save->m_ComboCount);
            que.QueryIntAttribute("count2",  &save->m_LastSlasher);
            que.QueryFloatAttribute("timer",          &save->m_TimeRemainingSave);
            que.QueryFloatAttribute("globalWaveDt",   &save->m_ProbabilityOverideFlag);
            que.QueryIntAttribute("go_state",         &save->m_GameOverScreenState);
            que.QueryFloatAttribute("go_time",        &save->m_GameOverTimer);
            que.QueryFloatAttribute("go_bombHitTime", &save->m_BombHitTimer);
            que.QueryIntAttribute("go_body",          &save->m_GameOverField1);
            que.QueryIntAttribute("go_head",          &save->m_GameOverField2);
            que.QueryIntAttribute("go_fruit",         &save->m_GameOverField3);
            que.QueryIntAttribute("go_fact",          &save->m_GameOverField4);
            const char* showHs = que.Attribute("go_showHighScore");
            if (showHs) save->newBestThisGame = (strcmp(showHs, "true") == 0) ? 1 : 0;
            const char* setScore = que.Attribute("go_setScore");
            if (setScore) save->secondaryFlag = (strcmp(setScore, "true") == 0) ? 1 : 0;
            que.QueryFloatAttribute("nextComboBonus", &save->m_NextComboBonus);
            que.QueryFloatAttribute("shake_time",     &save->m_ShakeIntensity);
            que.QueryFloatAttribute("shake_max_time", &save->m_ShakeDecay);
            // TODO: resolve XML attr literal name for m_WaveScalar_v161 (GOT 0xfffb06e6).
            // ParseSaveFile @ 0x154c8c loads it as a float; using "waveScalar" as placeholder.
            que.QueryFloatAttribute("waveScalar", &save->m_WaveScalar_v161);

            TiXmlElement wi = que.FirstChildElement("wave_info");
            if (wi) {
                wi.QueryIntAttribute("waveCount",   &save->m_pCurrentWave_P1);
                wi.QueryFloatAttribute("waveDelay", &save->m_WaveDelay);
                wi.QueryFloatAttribute("waveWait",  &save->m_WaveWait);
                wi.QueryIntAttribute("blitzSpawnedThisGame",     &save->m_blitzSpawnedThisGame);
                wi.QueryIntAttribute("blitzForceSpawnedCounter", &save->m_blitzForceSpawnedCounter);
                wi.QueryFloatAttribute("blitzSpawnTime",         &save->m_blitzSpawnTime);
            }
        }
    }

    // Validate game mode (clamp to 0..3).
    if (save->m_GameMode > 3) save->m_GameMode = 0;

    // Daily-reset hook.
    save->CheckDatesHaveChanged();

    return true;
}

// ----------------------------------------------------------------------
// FruitNinja_SaveCurrentData @ 0x0016ccc8
//
// Binary builds a STACK-LOCAL snapshot of pSaveData with live game state
// copied in (so the writer sees the most-recent score/missCount/etc.
// without mutating pSaveData itself). Port matches that pattern: copies
// pSaveData, populates the snapshot's live fields from Game, then
// SaveGame(&snapshot). pSaveData itself is left untouched so existing
// in-memory state survives the save.
// ----------------------------------------------------------------------
void FruitNinja_SaveCurrentData(bool /*fullSave*/) {
    Game* g = Game::GetInstance();
    if (!g || !game_work.m_SaveData) return;

    // ItemSave.xml is always written (coin balance + bought/equipped).
    ItemManager::GetInstance()->SaveItemInfo();

    // Snapshot: deep-copy current pSaveData, then overwrite live fields.
    FruitSaveData snapshot = *game_work.m_SaveData;

    snapshot.m_CurrentScore     = game_work.currentScore;
    snapshot.m_CurrentMissCount = (int)game_work.missCount;
    snapshot.m_GameMode         = (uint32_t)game_work.gameMode;
    snapshot.m_CriticalChance   = game_work.m_ScoreThreshold;

    // Snapshot combo globals into save fields before writing.
    // Binary: SaveCurrentData @ 0x0016cd08/0x0016cd34.
    snapshot.SnapshotComboState();

    // Binary does NOT update +0x40 in SaveCurrentData; it is rebuilt as the
    // CLASSIC-mode alias by ParseSaveFile on next load.

    // Bomb-hit timer: binary saves only when timer is meaningfully
    // active (zen-mode special case). Port saves unconditionally for
    // simplicity.
    snapshot.m_BombHitTimer = game_work.m_BombHitTimer;

    SaveGame(&snapshot);
}

// ----------------------------------------------------------------------
// FruitNinja_SaveOnExit @ 0x0016cf40
// ----------------------------------------------------------------------
void FruitNinja_SaveOnExit() {
    FruitNinja_SaveCurrentData(true);
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

// File-global re-entrant-save guard (v1.6.1 isSaving). Accessed via GetIsSavingBool().
// Set to true at save start to prevent re-entrant saves on suspend; cleared after save.
// NOTE: set/clear around the actual save body is deferred pending RE of SaveGame/SaveCurrentData
// entry/exit sites. This pass adds only the global + accessor.
static bool isSaving = false;

// v1.6.1 GetIsSavingBool @0x001ca458 (_Z14GetIsSavingBoolv)
bool* GetIsSavingBool() {
    return &isSaving;
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
