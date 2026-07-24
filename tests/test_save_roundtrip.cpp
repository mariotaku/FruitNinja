// test_save_roundtrip.cpp -- FruitSaveData SaveGame/LoadGame round-trip (#291 Part 2).
//
// Proves the port's save/load now emits and reads the BINARY's FruitySave.xml
// schema (root <save_file> + <total> + <que> + <unlocked> + <state> + <wave_info>/
// <wave>/<spawner> + <wave_counts_MODE> + <powers>) and that a save round-trips
// through a fresh FruitSaveData.
//
// Pure in-process: no GPU, no audio, no SDL. A Game object is constructed only to
// satisfy the MortarGame singleton that LoadGame's version-match check reads
// (GetVersionTotal). We deliberately leak it (never delete) to avoid Game::shutdown
// touching uninitialised subsystems at teardown.
//
// This test asserts the emitted element names are the binary's (<state>, <total>,
// <unlocked>) and NOT the port's old ones (<que>-for-state / <SliceTotal> /
// <achievements>).

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define fn_getcwd _getcwd
#else
#include <unistd.h>
#define fn_getcwd getcwd
#endif

#include "game/FruitSaveData.h"
#include "Game.h"
#include "game/GameWork.h"
#include "engine/core/MortarGame.h"
#include "engine/core/SystemManager.h"
#include "engine/util/StringHash.h"
#include "engine/xml/TiXml.h"

// Defined in AboutScreen.cpp; no public header declares it.
const char* GetVersionString();

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL (line %d): %s\n", __LINE__, #cond); ++g_fail; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    long _va = (long)(a), _vb = (long)(b); \
    if (_va != _vb) { std::printf("FAIL (line %d): %s == %s : got %ld want %ld\n", \
        __LINE__, #a, #b, _va, _vb); ++g_fail; } \
} while (0)

#define CHECK_F(a, b) do { \
    double _va = (double)(a), _vb = (double)(b); \
    if (std::fabs(_va - _vb) > 1e-3) { std::printf("FAIL (line %d): %s ~= %s : got %f want %f\n", \
        __LINE__, #a, #b, _va, _vb); ++g_fail; } \
} while (0)

int main() {
    std::setvbuf(stdout, NULL, _IONBF, 0);

    // --- Minimal environment: MortarGame singleton + version + save path ---
    Game* game = new Game();  // intentionally leaked
    // Absolute data_dir: save/load use absolute paths (like the real game), which
    // bypass the FileManager data-root routing in TiXmlDocument::LoadFile. A bare
    // "." would be treated as a relative asset path and routed through FileManager.
    {
        char cwd[1024];
        game->data_dir = fn_getcwd(cwd, sizeof(cwd)) ? std::string(cwd) : std::string(".");
        // Save/load resolve via save_dir (GetSavePath = save_dir + "/FruitySave.xml");
        // point it at the same writable cwd this test reads/writes from.
        game->save_dir = game->data_dir;
    }
    Mortar::MortarGame::GetInstance()->m_versionCombined =
        GetVersionFromString(GetVersionString());

    // Global game_work fields that <state> serialises through (not FruitSaveData members).
    game_work.m_gameDataLicensedState = 7;
    game_work.m_ElapsedGameTime = 88.5f;

    // --- Build a representative source save ---
    FruitSaveData src;
    src.m_highscore = 12345;
    src.m_ModeHighScores[0] = 1000; src.m_ModeHighScores[1] = 2000;
    src.m_ModeHighScores[2] = 3000; src.m_ModeHighScores[3] = 4000;
    src.m_ModeBestCombos[0] = 5;    src.m_ModeBestCombos[2] = 7;   // 1,3 stay 0 (not emitted)
    src.m_LastPlayedDay[0] = 40001; src.m_LastPlayedDay[1] = 40002;
    src.m_LastPlayedDay[2] = 40003; src.m_LastPlayedDay[3] = 40004;
    src.m_CriticalChance = 55;
    src.m_bDojoBGUnlocked = 1;
    src.m_bP2PCancelled = 1;

    // Totals: two cumulative-only (apple/banana) + one session-only (sess_combo).
    // Set the maps directly (disjoint names) so the round-trip is not tangled with
    // AddToTotal's accumulate semantics.
    src.m_Totals[StringHash("apple")]  = SliceTotal("apple", 100);
    src.m_Totals[StringHash("banana")] = SliceTotal("banana", 50);
    src.m_SessionTotals[StringHash("sess_combo")] = SliceTotal("sess_combo", 7);

    // Pending unlock (with countdown timer) + confirmed unlock.
    {
        AchievementItem a;
        std::strcpy(a.m_Name, "slice-1000");
        a.m_Timer = 2.5f;
        src.m_PendingUnlocks[StringHash("slice-1000")] = a;
    }
    {
        AchievementItem a;
        std::strcpy(a.m_Name, "first-blood");
        src.m_UnlockedAchievements[StringHash("first-blood")] = a;
    }

    // Active <state> block.
    src.m_bHasActiveGame = 1;
    src.m_CurrentScore = 266;
    src.m_CurrentMissCount = 2;
    src.m_GameMode = 2;              // ARCADE
    src.m_bWasGameOver = 0;
    src.m_ComboCount = 4;            // consecutiveCount
    src.m_ComboFruitType = 1;       // consecutiveType
    src.m_TimeRemainingSave = 45.5f;
    src.m_WaveScalar_v161 = 1.5f;   // globalWaveDt
    src.m_GameOverScreenState = 3;
    src.m_GameOverTimer = 1.25f;
    src.m_BombHitTimer = 0.5f;
    src.m_NextComboBonus = 0.75f;   // go_transition
    src.m_GameOverField1 = 10; src.m_GameOverField2 = 11;
    src.m_GameOverField3 = 12; src.m_GameOverField4 = 13;
    src.newBestThisGame = 1;
    src.secondaryFlag = 0;
    src.m_Speed_P0 = 3.0f;          // speedLossTime (gated on m_Speed_P0_alias > 0)
    src.m_Speed_P0_alias = 2.0f;    // desiredSpeed (the gate)
    src.m_Speed_P1 = 1.0f;          // nextComboBonus
    src.m_ShakeIntensity = 0.8f;    // shake_time AND shake_max_time (binary quirk)
    src.m_ShakeDecay = 0.3f;        // NOT written; load overwrites from shake_max_time
    src.m_FruitQueueCount = 3;
    src.m_FruitQueue[0] = 1; src.m_FruitQueue[1] = 4; src.m_FruitQueue[2] = 7;
    src.m_BestComboLength = 4;
    src.m_BestComboFruits[0] = 2; src.m_BestComboFruits[1] = 3;
    src.m_BestComboFruits[2] = 5; src.m_BestComboFruits[3] = 8;

    // wave_info + one wave with one spawner.
    src.m_pCurrentWave_P1 = 6;               // waveCount
    src.m_WaveDelay = 3.0f;                  // numberOfWavesSpawned (int)
    src.m_WaveWait = 1.5f;                   // waveDelay attr
    src.m_ProbabilityOverideFlag = 0.25f;    // waveWait attr
    src.m_blitzSpawnedThisGame = 2;
    src.m_blitzForceSpawnedCounter = 1;
    src.m_blitzSpawnTime = 4.0f;
    {
        WaveState ws;
        ws.waveIdx = 3;
        ws.index = 9;
        SpawnState ss;
        ss.count = 5.0f;
        ss.timer = 0.5f;
        ws.spawners.push_back(ss);
        src.m_WaveStates.push_back(ws);
    }

    // wave_counts_<MODE> maps.
    src.m_ModeScoreHistory[0][3] = 10;   // CLASSIC: waveIdx=3 -> games=10
    src.m_ModeScoreHistory[2][5] = 20;   // ARCADE:  waveIdx=5 -> games=20

    // --- Save ---
    SaveGame(&src);

    // --- Assert the emitted XML uses the BINARY element names ---
    std::string savePath = game->save_dir + "/FruitySave.xml";
    {
        TiXmlDocument doc;
        CHECK(doc.LoadFile(savePath.c_str()));
        TiXmlElement root = doc.FirstChildElement("save_file");
        CHECK((bool)root);
        if (root) {
            CHECK((bool)root.FirstChildElement("state"));      // game state (binary)
            CHECK((bool)root.FirstChildElement("total"));      // totals (binary)
            CHECK((bool)root.FirstChildElement("unlocked"));   // confirmed unlocks (binary)
            CHECK((bool)root.FirstChildElement("que"));        // pending unlocks (binary)
            CHECK((bool)root.FirstChildElement("powers"));     // power-ups container
            CHECK((bool)root.FirstChildElement("wave_counts_ARCADE"));
            // Old port element names must NOT be present.
            CHECK(!root.FirstChildElement("SliceTotal"));
            CHECK(!root.FirstChildElement("achievements"));
        }
    }

    // Corrupt game_work fields so the reload has to restore them.
    game_work.m_gameDataLicensedState = 0;
    game_work.m_ElapsedGameTime = 0.0f;

    // --- Load into a fresh object ---
    FruitSaveData dst;
    CHECK(LoadGame(&dst));

    // --- Top-level scalars ---
    CHECK_EQ(dst.m_highscore, 12345);
    CHECK_EQ(dst.m_ModeHighScores[0], 1000); CHECK_EQ(dst.m_ModeHighScores[1], 2000);
    CHECK_EQ(dst.m_ModeHighScores[2], 3000); CHECK_EQ(dst.m_ModeHighScores[3], 4000);
    CHECK_EQ(dst.m_ModeBestCombos[0], 5);    CHECK_EQ(dst.m_ModeBestCombos[1], 0);
    CHECK_EQ(dst.m_ModeBestCombos[2], 7);    CHECK_EQ(dst.m_ModeBestCombos[3], 0);
    CHECK_EQ(dst.m_LastPlayedDay[0], 40001); CHECK_EQ(dst.m_LastPlayedDay[3], 40004);
    CHECK_EQ(dst.m_CriticalChance, 55);
    CHECK_EQ(dst.m_bDojoBGUnlocked, 1);
    CHECK_EQ(dst.m_bP2PCancelled, 1);

    // game_work-backed <save_file>/<state> fields.
    CHECK_EQ(game_work.m_gameDataLicensedState, 7);   // appLicensedState
    CHECK_F(game_work.m_ElapsedGameTime, 88.5);       // gameTime

    // --- Totals ---
    CHECK_EQ(dst.m_Totals[StringHash("apple")].count, 100);
    CHECK(dst.m_Totals[StringHash("apple")].name == "apple");
    CHECK_EQ(dst.m_Totals[StringHash("banana")].count, 50);
    CHECK_EQ(dst.m_SessionTotals[StringHash("sess_combo")].count, 7);
    CHECK(dst.m_SessionTotals[StringHash("sess_combo")].name == "sess_combo");
    // Binary AddToTotal(trackSession=true) selects m_SessionTotals ONLY -- EITHER/OR,
    // not both. v1.6.1 @ 0x001546f0 shifts `this` by +0x18 (m_SessionTotals base);
    // m_Totals is never touched when trackSession=true.
    CHECK(dst.m_Totals.find(StringHash("sess_combo")) == dst.m_Totals.end());

    // --- Achievements ---
    {
        std::map<uint32_t, AchievementItem>::iterator it =
            dst.m_PendingUnlocks.find(StringHash("slice-1000"));
        CHECK(it != dst.m_PendingUnlocks.end());
        if (it != dst.m_PendingUnlocks.end()) {
            CHECK(std::strcmp(it->second.m_Name, "slice-1000") == 0);
            CHECK_F(it->second.m_Timer, 2.5);
        }
    }
    {
        std::map<uint32_t, AchievementItem>::iterator it =
            dst.m_UnlockedAchievements.find(StringHash("first-blood"));
        CHECK(it != dst.m_UnlockedAchievements.end());
        if (it != dst.m_UnlockedAchievements.end())
            CHECK(std::strcmp(it->second.m_Name, "first-blood") == 0);
    }

    // --- <state> block ---
    CHECK_EQ(dst.m_bHasActiveGame, 1);     // set by the state handler (port _137 stand-in)
    CHECK_EQ(dst.m_CurrentScore, 266);
    CHECK_EQ(dst.m_CurrentMissCount, 2);
    CHECK_EQ(dst.m_GameMode, 2u);          // ARCADE
    CHECK_EQ(dst.m_bWasGameOver, 0);
    CHECK_EQ(dst.m_ComboCount, 4);         // consecutiveCount
    CHECK_EQ(dst.m_ComboFruitType, 1);     // consecutiveType
    CHECK_F(dst.m_TimeRemainingSave, 45.5);
    CHECK_F(dst.m_WaveScalar_v161, 1.5);   // globalWaveDt
    CHECK_EQ(dst.m_GameOverScreenState, 3);
    CHECK_F(dst.m_GameOverTimer, 1.25);
    CHECK_F(dst.m_BombHitTimer, 0.5);
    CHECK_F(dst.m_NextComboBonus, 0.75);   // go_transition
    CHECK_EQ(dst.m_GameOverField1, 10); CHECK_EQ(dst.m_GameOverField2, 11);
    CHECK_EQ(dst.m_GameOverField3, 12); CHECK_EQ(dst.m_GameOverField4, 13);
    CHECK_EQ(dst.newBestThisGame, 1);
    CHECK_EQ(dst.secondaryFlag, 0);
    CHECK_F(dst.m_Speed_P0, 3.0);          // speedLossTime
    CHECK_F(dst.m_Speed_P0_alias, 2.0);    // desiredSpeed
    CHECK_F(dst.m_Speed_P1, 1.0);          // nextComboBonus
    CHECK_F(dst.m_ShakeIntensity, 0.8);    // shake_time
    // Binary quirk: shake_max_time is written from m_ShakeIntensity, so load sets
    // m_ShakeDecay to 0.8 (NOT the original 0.3). Assert the faithful quirk.
    CHECK_F(dst.m_ShakeDecay, 0.8);

    // FruitQueue CSV.
    CHECK_EQ(dst.m_FruitQueueCount, 3);
    CHECK_EQ(dst.m_FruitQueue[0], 1); CHECK_EQ(dst.m_FruitQueue[1], 4);
    CHECK_EQ(dst.m_FruitQueue[2], 7);
    // best_combo CSV.
    CHECK_EQ(dst.m_BestComboLength, 4);
    CHECK_EQ(dst.m_BestComboFruits[0], 2); CHECK_EQ(dst.m_BestComboFruits[1], 3);
    CHECK_EQ(dst.m_BestComboFruits[2], 5); CHECK_EQ(dst.m_BestComboFruits[3], 8);

    // --- wave_info ---
    CHECK_EQ(dst.m_pCurrentWave_P1, 6);
    CHECK_F(dst.m_WaveDelay, 3.0);                 // numberOfWavesSpawned (int round-trip)
    CHECK_F(dst.m_WaveWait, 1.5);                  // waveDelay attr
    CHECK_F(dst.m_ProbabilityOverideFlag, 0.25);   // waveWait attr
    CHECK_EQ(dst.m_blitzSpawnedThisGame, 2);
    CHECK_EQ(dst.m_blitzForceSpawnedCounter, 1);
    CHECK_F(dst.m_blitzSpawnTime, 4.0);

    // --- wave states ---
    CHECK_EQ((long)dst.m_WaveStates.size(), 1);
    if (!dst.m_WaveStates.empty()) {
        WaveState& ws = dst.m_WaveStates.front();
        CHECK_EQ(ws.waveIdx, 3);
        CHECK_EQ((long)ws.index, 9);
        CHECK_EQ((long)ws.spawners.size(), 1);
        if (!ws.spawners.empty()) {
            SpawnState& ss = ws.spawners.front();
            CHECK_F(ss.timer, 0.5);
            CHECK_F(ss.count, 5.0);
        }
    }

    // --- wave_counts maps ---
    CHECK_EQ(dst.m_ModeScoreHistory[0][3], 10);
    CHECK_EQ(dst.m_ModeScoreHistory[2][5], 20);

    // Clean up the on-disk file.
    std::remove(savePath.c_str());

    if (g_fail == 0) {
        std::printf("PASS: save/load round-trip (binary FruitySave.xml schema)\n");
        return 0;
    }
    std::printf("FAIL: %d assertion(s) failed\n", g_fail);
    return 1;
}
