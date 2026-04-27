// FruitSaveData -- save/load to FruitySave.xml + slice-total maps.
//
// Implements binary FruitNinja_SaveGame @ 0x0012a2fc and
// FruitNinja_LoadGame @ 0x0012be74. Coin balance is owned by ItemSave.xml
// via ItemManager and is only mirrored here for in-memory access.
//
// Analysed: 2026-04-23T02:00, REVISED 2026-04-27T22:00

#include "FruitSaveData.h"
#include "Game.h"
#include "ItemManager.h"
#include "engine/util/StringHash.h"

#include <tinyxml2.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ----------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------

// Matches binary ctor @ 0x00129e74. Defaults from doc + RE.
FruitSaveData::FruitSaveData()
    : m_Coins(0)
    , m_CoinsTotal(0)
    , m_LevelStartCoins(0)
    , field_0x30(0)
    , m_bHasActiveGame(0)
    , m_bDojoBGUnlocked(0)
    , field_0x3c(0)
    , m_highscore(0)
    , m_CurrentScore(0)
    , m_CurrentMissCount(0)
    , m_GameMode(0)
    , m_bWasGameOver(0)
    , m_HighScoreRef1(-1)
    , m_HighScoreRef2(0)
    , m_FruitQueueCount(0)
    , m_CameraShakeX(0.0f)
    , m_CameraShakeY(0.0f)
    , m_CameraShakeZ(0.0f)
    , m_GameTimer1(-1.0f)
    , m_CriticalChance(70)              // 0x46
    , m_GameOverScreenState(-1)
    , m_GameOverTimer(-1.0f)
    , m_GameOverField1(-1)
    , m_GameOverField2(-1)
    , m_GameOverField3(-1)
    , m_GameOverField4(-1)
    , m_bResumeFlag1(0)
    , m_bResumeFlag2(0)
    , m_BombHitTimer(0.0f)
    , m_field134(-1.0f)
    , m_ShakeIntensity(0.0f)
    , m_ShakeDecay(1.0f)                // 0x3F800000
    , m_WaveCount(0)
    , m_WaveDelay(0.0f)
    , m_WaveWait(0.0f)
    , m_field14c(1.0f)
    , m_blitzSpawnedThisGame(0)
    , m_blitzForceSpawnedCounter(0)
    , m_blitzSpawnTime(0.0f)
    , m_VersionInfo(0)
    , m_BombQueueCount(0)
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

// ----------------------------------------------------------------------
// Coin API
// ----------------------------------------------------------------------

// AddCoins @ 0x0010a3bc.
void FruitSaveData::AddCoins(int delta) {
    m_Coins += delta;
    if (delta > 0) m_CoinsTotal += delta;
}

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

// GetTotal @ 0x0012a110.
int FruitSaveData::GetTotal(uint32_t hash) const {
    auto it = m_Totals.find(hash);
    return (it != m_Totals.end()) ? it->second.count : 0;
}

// ClearCombo @ 0x00129b94 -- clears the in-session map.
void FruitSaveData::ClearCombo() {
    m_SessionTotals.clear();
}

// FinishedGame @ 0x0012a034 -- session post-round bookkeeping. Stub.
void FruitSaveData::FinishedGame() {
    // Binary: decrements modifier counters in m_Totals. Port no-ops
    // until modifier system is fully wired.
}

// ----------------------------------------------------------------------
// Achievements
// ----------------------------------------------------------------------

bool FruitSaveData::IsAchievementUnlocked(uint32_t hash) {
    Game* g = Game::GetInstance();
    if (!g || !g->pSaveData) return false;
    return g->pSaveData->m_Achievements.find(hash) != g->pSaveData->m_Achievements.end();
}

void FruitSaveData::UnlockTotals() {
    // 0x00124f10 -- "total-X" thresholds. Stubbed; depends on
    // AchievementManager's full table being ported.
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

void FruitSaveData::Update(float /*dt*/, HUD* /*hud*/) {
    // 0x0012b3dc -- achievement in-progress timer ticks. Stub for now.
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

// Build "<modeName><suffix>" into out (small stack buffer).
void MakeModeAttr(char* out, size_t outsz, int mode, const char* suffix) {
    snprintf(out, outsz, "%s%s", k_ModeNames[mode], suffix);
}

// Resolve the on-disk save path. Binary uses /Home/FruitySave.xml on
// Bada; the port writes to <data_dir>/FruitySave.xml so the file lives
// next to the asset tree.
std::string GetSavePath() {
    Game* g = Game::GetInstance();
    if (!g) return std::string("FruitySave.xml");
    return g->data_dir + "/FruitySave.xml";
}

// Save format version. Binary uses GetVersionTotal() which encodes
// build info; port pins to a single byte for now and bumps when the
// schema changes.
constexpr int k_SaveVersion = 1;

} // namespace

// ----------------------------------------------------------------------
// FruitNinja_SaveGame @ 0x0012a2fc
// ----------------------------------------------------------------------
void FruitNinja_SaveGame(FruitSaveData* save) {
    if (!save) return;

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLElement* root = doc.NewElement("save_file");

    // Top-level scalar attributes (per binary writer order).
    char buf[64];
    snprintf(buf, sizeof(buf), "%d.%d.%d", k_SaveVersion, 0, 0);
    root->SetAttribute("version", buf);
    root->SetAttribute("highscore", save->m_highscore);

    // Per-mode attrs: "<MODE>highscore", "<MODE>_unposted" (combo, only
    // if > 0), "<MODE>_dolg" (play count).
    char attrName[32];
    for (int m = 0; m < 4; m++) {
        MakeModeAttr(attrName, sizeof(attrName), m, "highscore");
        root->SetAttribute(attrName, save->m_ModeHighScores[m]);

        if (save->m_ModeBestCombos[m] > 0) {
            MakeModeAttr(attrName, sizeof(attrName), m, "_unposted");
            root->SetAttribute(attrName, save->m_ModeBestCombos[m]);
        }

        MakeModeAttr(attrName, sizeof(attrName), m, "_dolg");
        root->SetAttribute(attrName, save->m_ModePlayCounts[m]);
    }

    root->SetAttribute("critical_chance", save->m_CriticalChance);
    root->SetAttribute("rated",         save->m_bDojoBGUnlocked ? "true" : "false");
    root->SetAttribute("p2pCancelled",  save->field_0x3c        ? "true" : "false");

    // SliceTotal elements: cumulative totals first, then session-only.
    for (auto& kv : save->m_Totals) {
        tinyxml2::XMLElement* e = doc.NewElement("SliceTotal");
        e->SetAttribute("name",  kv.second.name.c_str());
        e->SetAttribute("count", kv.second.count);
        root->InsertEndChild(e);
    }
    for (auto& kv : save->m_SessionTotals) {
        tinyxml2::XMLElement* e = doc.NewElement("SliceTotal");
        e->SetAttribute("u", "true");
        e->SetAttribute("name",  kv.second.name.c_str());
        e->SetAttribute("count", kv.second.count);
        root->InsertEndChild(e);
    }

    // Achievement progress map ("achievement" container).
    if (!save->m_AchievementProgress.empty()) {
        tinyxml2::XMLElement* ach = doc.NewElement("achievement");
        for (auto& kv : save->m_AchievementProgress) {
            tinyxml2::XMLElement* e = doc.NewElement("achievement");
            e->SetAttribute("name", kv.second.name.c_str());
            e->SetAttribute("progress", kv.second.progress);
            ach->InsertEndChild(e);
        }
        root->InsertEndChild(ach);
    }

    // Unlocked achievements ("unlocked" container).
    if (!save->m_Achievements.empty()) {
        tinyxml2::XMLElement* unl = doc.NewElement("unlocked");
        for (auto& kv : save->m_Achievements) {
            tinyxml2::XMLElement* e = doc.NewElement("achievement");
            e->SetAttribute("name", kv.second.name.c_str());
            unl->InsertEndChild(e);
        }
        root->InsertEndChild(unl);
    }

    // Per-mode score history (<wave_counts_MODE> blocks).
    for (int m = 0; m < 4; m++) {
        if (save->m_ModeScoreHistory[m].empty()) continue;
        char tag[48];
        snprintf(tag, sizeof(tag), "wave_counts_%s", k_ModeNames[m]);
        tinyxml2::XMLElement* container = doc.NewElement(tag);
        for (auto& kv : save->m_ModeScoreHistory[m]) {
            tinyxml2::XMLElement* e = doc.NewElement("game_count");
            e->SetAttribute("score",   kv.first);
            e->SetAttribute("waveIdx", kv.second);
            container->InsertEndChild(e);
        }
        root->InsertEndChild(container);
    }

    // ActiveGame <que> block: only when m_bHasActiveGame is set.
    // Port skips entity/wave list serialisation for now; just emits
    // the scalar resume fields so resume coordinates persist across
    // a non-clean exit.
    if (save->m_bHasActiveGame) {
        tinyxml2::XMLElement* que = doc.NewElement("que");
        que->SetAttribute("mode",            k_ModeNames[save->m_GameMode <= 3 ? save->m_GameMode : 0]);
        que->SetAttribute("hasDropped",      save->m_bWasGameOver ? "true" : "false");
        que->SetAttribute("count",           save->m_CurrentScore);
        que->SetAttribute("misses",          save->m_CurrentMissCount);
        que->SetAttribute("timer",           save->m_GameTimer1);
        que->SetAttribute("globalWaveDt",    save->m_field14c);
        que->SetAttribute("go_state",        save->m_GameOverScreenState);
        que->SetAttribute("go_time",         save->m_GameOverTimer);
        que->SetAttribute("go_bombHitTime",  save->m_BombHitTimer);
        que->SetAttribute("go_body",         save->m_GameOverField1);
        que->SetAttribute("go_head",         save->m_GameOverField2);
        que->SetAttribute("go_fruit",        save->m_GameOverField3);
        que->SetAttribute("go_fact",         save->m_GameOverField4);
        que->SetAttribute("go_showHighScore", save->m_bResumeFlag1 ? "true" : "false");
        que->SetAttribute("go_setScore",     save->m_bResumeFlag2 ? "true" : "false");
        que->SetAttribute("nextComboBonus",  save->m_field134);
        que->SetAttribute("shake_time",      save->m_ShakeIntensity);
        que->SetAttribute("shake_max_time",  save->m_ShakeDecay);

        tinyxml2::XMLElement* wi = doc.NewElement("wave_info");
        wi->SetAttribute("waveCount",                save->m_WaveCount);
        wi->SetAttribute("waveDelay",                save->m_WaveDelay);
        wi->SetAttribute("waveWait",                 save->m_WaveWait);
        wi->SetAttribute("blitzSpawnedThisGame",     save->m_blitzSpawnedThisGame);
        wi->SetAttribute("blitzForceSpawnedCounter", save->m_blitzForceSpawnedCounter);
        wi->SetAttribute("blitzSpawnTime",           save->m_blitzSpawnTime);
        que->InsertEndChild(wi);

        root->InsertEndChild(que);
    }

    doc.InsertEndChild(root);
    doc.SaveFile(GetSavePath().c_str());
}

// ----------------------------------------------------------------------
// FruitNinja_LoadGame @ 0x0012be74
// ----------------------------------------------------------------------
bool FruitNinja_LoadGame(FruitSaveData* save) {
    if (!save) return false;

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(GetSavePath().c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        return false;  // expected on first run
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("save_file");
    if (!root) return false;

    save->m_EntityStates.clear();
    save->m_WaveStates.clear();

    // Top-level scalar attributes.
    root->QueryIntAttribute("highscore", &save->m_highscore);

    char attrName[32];
    for (int m = 0; m < 4; m++) {
        snprintf(attrName, sizeof(attrName), "%shighscore", k_ModeNames[m]);
        root->QueryIntAttribute(attrName, &save->m_ModeHighScores[m]);

        snprintf(attrName, sizeof(attrName), "%s_unposted", k_ModeNames[m]);
        root->QueryIntAttribute(attrName, &save->m_ModeBestCombos[m]);

        snprintf(attrName, sizeof(attrName), "%s_dolg", k_ModeNames[m]);
        root->QueryIntAttribute(attrName, &save->m_ModePlayCounts[m]);
    }

    root->QueryIntAttribute("critical_chance", &save->m_CriticalChance);

    const char* ratedAttr = root->Attribute("rated");
    if (ratedAttr) save->m_bDojoBGUnlocked = (strcmp(ratedAttr, "true") == 0) ? 1 : 0;

    const char* p2pAttr = root->Attribute("p2pCancelled");
    if (p2pAttr) save->field_0x3c = (strcmp(p2pAttr, "true") == 0) ? 1 : 0;

    // SliceTotal elements (cumulative + session).
    for (tinyxml2::XMLElement* e = root->FirstChildElement("SliceTotal"); e;
         e = e->NextSiblingElement("SliceTotal")) {
        const char* name = e->Attribute("name");
        if (!name || !*name) continue;
        int count = 0;
        e->QueryIntAttribute("count", &count);
        const char* uAttr = e->Attribute("u");
        bool isSession = (uAttr && strcmp(uAttr, "true") == 0);

        uint32_t hash = StringHash(name);
        if (isSession) {
            save->m_SessionTotals[hash] = SliceTotal(name, count);
        } else {
            save->m_Totals[hash] = SliceTotal(name, count);
        }
    }

    // Achievement progress: <achievement> container.
    if (tinyxml2::XMLElement* progress = root->FirstChildElement("achievement")) {
        for (tinyxml2::XMLElement* e = progress->FirstChildElement("achievement"); e;
             e = e->NextSiblingElement("achievement")) {
            const char* name = e->Attribute("name");
            if (!name || !*name) continue;
            float prog = 0.0f;
            e->QueryFloatAttribute("progress", &prog);
            save->m_AchievementProgress[StringHash(name)] = AchievementItem(name, prog);
        }
    }

    // Unlocked achievements: <unlocked> container.
    if (tinyxml2::XMLElement* unl = root->FirstChildElement("unlocked")) {
        for (tinyxml2::XMLElement* e = unl->FirstChildElement("achievement"); e;
             e = e->NextSiblingElement("achievement")) {
            const char* name = e->Attribute("name");
            if (!name || !*name) continue;
            save->m_Achievements[StringHash(name)] = AchievementItem(name, 1.0f);
        }
    }

    // Per-mode score history.
    for (int m = 0; m < 4; m++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "wave_counts_%s", k_ModeNames[m]);
        tinyxml2::XMLElement* container = root->FirstChildElement(tag);
        if (!container) continue;
        for (tinyxml2::XMLElement* e = container->FirstChildElement("game_count"); e;
             e = e->NextSiblingElement("game_count")) {
            int score = 0, waveIdx = 0;
            e->QueryIntAttribute("score",   &score);
            e->QueryIntAttribute("waveIdx", &waveIdx);
            save->m_ModeScoreHistory[m][score] = waveIdx;
        }
    }

    // ActiveGame <que> block.
    if (tinyxml2::XMLElement* que = root->FirstChildElement("que")) {
        save->m_bHasActiveGame = 1;
        const char* mode = que->Attribute("mode");
        if (mode) {
            for (int m = 0; m < 4; m++) {
                if (strcmp(mode, k_ModeNames[m]) == 0) {
                    save->m_GameMode = (uint32_t)m;
                    break;
                }
            }
        }
        const char* hasDropped = que->Attribute("hasDropped");
        if (hasDropped) save->m_bWasGameOver = (strcmp(hasDropped, "true") == 0) ? 1 : 0;
        que->QueryIntAttribute("count",   &save->m_CurrentScore);
        que->QueryIntAttribute("misses",  &save->m_CurrentMissCount);
        que->QueryFloatAttribute("timer",          &save->m_GameTimer1);
        que->QueryFloatAttribute("globalWaveDt",   &save->m_field14c);
        que->QueryIntAttribute("go_state",         &save->m_GameOverScreenState);
        que->QueryFloatAttribute("go_time",        &save->m_GameOverTimer);
        que->QueryFloatAttribute("go_bombHitTime", &save->m_BombHitTimer);
        que->QueryIntAttribute("go_body",          &save->m_GameOverField1);
        que->QueryIntAttribute("go_head",          &save->m_GameOverField2);
        que->QueryIntAttribute("go_fruit",         &save->m_GameOverField3);
        que->QueryIntAttribute("go_fact",          &save->m_GameOverField4);
        const char* showHs = que->Attribute("go_showHighScore");
        if (showHs) save->m_bResumeFlag1 = (strcmp(showHs, "true") == 0) ? 1 : 0;
        const char* setScore = que->Attribute("go_setScore");
        if (setScore) save->m_bResumeFlag2 = (strcmp(setScore, "true") == 0) ? 1 : 0;
        que->QueryFloatAttribute("nextComboBonus", &save->m_field134);
        que->QueryFloatAttribute("shake_time",     &save->m_ShakeIntensity);
        que->QueryFloatAttribute("shake_max_time", &save->m_ShakeDecay);

        if (tinyxml2::XMLElement* wi = que->FirstChildElement("wave_info")) {
            wi->QueryIntAttribute("waveCount",   &save->m_WaveCount);
            wi->QueryFloatAttribute("waveDelay", &save->m_WaveDelay);
            wi->QueryFloatAttribute("waveWait",  &save->m_WaveWait);
            wi->QueryIntAttribute("blitzSpawnedThisGame",     &save->m_blitzSpawnedThisGame);
            wi->QueryIntAttribute("blitzForceSpawnedCounter", &save->m_blitzForceSpawnedCounter);
            wi->QueryFloatAttribute("blitzSpawnTime",         &save->m_blitzSpawnTime);
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
    if (!g || !g->pSaveData) return;

    // ItemSave.xml is always written (coin balance + bought/equipped).
    ItemManager::GetInstance()->SaveItemInfo();

    // Snapshot: deep-copy current pSaveData, then overwrite live fields.
    FruitSaveData snapshot = *g->pSaveData;

    snapshot.m_CurrentScore     = g->currentScore;
    snapshot.m_CurrentMissCount = (int)g->missCount;
    snapshot.m_GameMode         = (uint32_t)g->gameMode;
    snapshot.m_CriticalChance   = g->m_ScoreThreshold;

    // Update high score for the current mode if the run beat it.
    int mode = (int)g->gameMode;
    if (mode >= 0 && mode < 4) {
        if (g->currentScore > snapshot.m_ModeHighScores[mode]) {
            snapshot.m_ModeHighScores[mode] = g->currentScore;
        }
    }
    if (g->currentScore > snapshot.m_highscore) {
        snapshot.m_highscore = g->currentScore;
    }

    // Bomb-hit timer: binary saves only when timer is meaningfully
    // active (zen-mode special case). Port saves unconditionally for
    // simplicity.
    snapshot.m_BombHitTimer = g->bombHitTimer;

    FruitNinja_SaveGame(&snapshot);
}

// ----------------------------------------------------------------------
// FruitNinja_SaveOnExit @ 0x0016cf40
// ----------------------------------------------------------------------
void FruitNinja_SaveOnExit() {
    FruitNinja_SaveCurrentData(true);
}
