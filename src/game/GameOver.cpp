// Analysed: 2026-04-30T12:00, REVISED 2026-05-02T00:00
// GameOver — 0x00169ed4

#include "GameOver.h"
#include "Game.h"
#include "WaveManager.h"
#include "FruitSaveData.h"
#include "screens/GameOverScreen.h"
#include "hud/HUD.h"
#include "engine/util/StringHash.h"

#include <algorithm>
#include <ctime>
#include <cstdio>     // snprintf -- explicit for Sourcery 4.4 newlib
#include "game/GameWork.h"

namespace FN {

// 0x00169ed4
void GameOver(int endReason, float endScore, int endParam) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // re-entry guard: levelTransitionFlag at g_GameData+0x05
    if (game_work.m_LevelTransitionFlag != 0) return;

    game_work.m_LevelTransitionFlag = 1;

    WaveManager::GetInstance()->ClearUnspawned();

    // FruitSaveData carries the sensei choice fields at +0x11C/0x120/0x124/0x128
    // (m_GameOverField1..4). Binary reads them to pick which sensei head/body
    // texture variant + per-game pom/star counts to display. Wiring proper:
    //   expressionIdx <- m_GameOverField2 (+0x120)
    //   bgPatternIdx  <- m_GameOverField1 (+0x11C)
    //   tabIndex      <- m_GameOverField3 (+0x124)
    //   starCount     <- m_GameOverField4 (+0x128)
    // The fields default to -1 (sentinel) and are written by the gameplay
    // achievement / bonus path which the port hasn't fully RE'd yet.
    // DIFFERS: when a field == -1 we substitute 1 (the first valid texture
    // variant) so sensei body + head are visible. Once the gameplay-side
    // setters land, the substitution can come out.
    FruitSaveData* save = game_work.m_SaveData;
    // Substitute 1 when the gameplay-side setter hasn't written a real value
    // (sentinel -1). Inlined per-field instead of a helper lambda -- the
    // cross-toolchain (GCC 4.4.1) doesn't support C++11 lambdas.
    int expressionIdx = (save && save->m_GameOverField2 > 0) ? save->m_GameOverField2 : 1;
    int bgPatternIdx  = (save && save->m_GameOverField1 > 0) ? save->m_GameOverField1 : 1;
    int tabIndex      = save ? std::max(0, save->m_GameOverField3) : 0;
    int starCount     = save ? std::max(0, save->m_GameOverField4) : 0;

    GameOverScreen* gos = new GameOverScreen(
        "GameOver", endReason, endScore,
        expressionIdx, bgPatternIdx, tabIndex, starCount);

    // +0x164: pGameOverScreen
    game_work.pGameOverScreen = gos;

    // TODO: FruitSaveData::AddToTotal("GamesPlayed-...") + unique-day tracking
    // if (endReason == -1) { ... }

    // sd[0x120] = sd[0x128] = sd[0x124] = sd[0x11C] = -1 (clear after passing to ctor)
    // TODO: clear FruitSaveData fields when ported

    gos->Init();
    game_work.mHud->AddControl(gos);

    // Binary @ 0x00169f94: bump <MODE>_today and write m_LastPlayedDay[mode].
    // 0x00169fec: sd->m_LastPlayedDay[mode] = GetDaysSince1900().
    if (game_work.m_SaveData) {
        static const char* k_ModeNames[4] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
        int mode = (int)game_work.gameMode;
        if (mode >= 0 && mode < 4) {
            // Compute days-since-1900 inline (same helper as FruitSaveData.cpp).
            static const int DAYS_FROM_1900_TO_EPOCH = 25569;
            int today = (int)(time(nullptr) / 86400) + DAYS_FROM_1900_TO_EPOCH;

            // Build "<MODE>_today" key and hash it.
            char todayKey[32];
            std::snprintf(todayKey, sizeof(todayKey), "%s_today", k_ModeNames[mode]);
            uint32_t todayHash = StringHash(todayKey);

            game_work.m_SaveData->AddToTotal(todayKey, todayHash, 1, false, false);
            game_work.m_SaveData->m_LastPlayedDay[mode] = today;
        }
    }
}

// 0x0010a7ac
// TODO: implement scoreDelegate.Call + tier SFX when delegate is ported.
void AddToCurrentScore(int points, int /*param1*/, bool /*param2*/, bool /*param3*/) {
    Game* game = Game::GetInstance();
    if (!game) return;
    game_work.currentScore += points;
}

// Binary free functions @ 0x0010a4b8 / 0x0010a4e8.
// Defunct sig: playerIdx ignored (online MP scrubbed) — binary @ 0x0010a4b8 / 0x0010a4e8.
// ASM-verified: 2026-05-10 binary @ 0x0010a4b8 (asm-inspector). Writes
// score to Game+0x18 (the live `currentScore` that ScoreControl reads),
// NOT to pSaveData->m_CurrentScore (which earlier port had wrong --
// game-start SetScore(0,-1) failed to reset the live score, so the
// previous run's final score persisted into the new game).
void SetScore(int score, int /*playerIdx*/) {
    Game* game = Game::GetInstance();
    if (game) game_work.currentScore = score;
}

void SetMissCount(int n, int /*playerIdx*/) {
    Game* game = Game::GetInstance();
    if (game && game_work.m_SaveData)
        game_work.m_SaveData->m_CurrentMissCount = n;   // FruitSaveData+0x68
}

} // namespace FN
