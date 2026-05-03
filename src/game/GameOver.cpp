// Analysed: 2026-04-30T12:00, REVISED 2026-05-02T00:00
// GameOver — 0x00169ed4

#include "GameOver.h"
#include "Game.h"
#include "WaveManager.h"
#include "FruitSaveData.h"
#include "screens/GameOverScreen.h"
#include "hud/HUD.h"
#include "engine/util/StringHash.h"

#include <ctime>
#include <cstdio>     // snprintf -- explicit for Sourcery 4.4 newlib

namespace FN {

// 0x00169ed4
void GameOver(int endReason, float endScore, int endParam) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // re-entry guard: pauseFlag at g_GameData+0x05
    if (game->pauseFlag != 0) return;

    game->pauseFlag = 1;

    WaveManager::GetInstance()->ClearUnspawned();

    // TODO: FruitSaveData field lookups at 0x120/0x11C/0x124/0x128 for
    // expressionIdx/bgPatternIdx/pomCount/starCount when save fields ported.
    // Using 0/-1 sentinels per spec.
    int expressionIdx = 0;
    int bgPatternIdx  = -1;
    int pomCount      = 0;
    int starCount     = 0;

    GameOverScreen* gos = new GameOverScreen(
        "GameOver", endReason, endScore,
        expressionIdx, bgPatternIdx, pomCount, starCount);

    // +0x164: pGameOverScreen
    game->pGameOverScreen = gos;

    // TODO: FruitSaveData::AddToTotal("GamesPlayed-...") + unique-day tracking
    // if (endReason == -1) { ... }

    // sd[0x120] = sd[0x128] = sd[0x124] = sd[0x11C] = -1 (clear after passing to ctor)
    // TODO: clear FruitSaveData fields when ported

    gos->Init();
    game->hud->AddControl(gos);

    // Binary @ 0x00169f94: bump <MODE>_today and write m_LastPlayedDay[mode].
    // 0x00169fec: sd->m_LastPlayedDay[mode] = GetDaysSince1900().
    if (game->pSaveData) {
        static const char* k_ModeNames[4] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
        int mode = (int)game->gameMode;
        if (mode >= 0 && mode < 4) {
            // Compute days-since-1900 inline (same helper as FruitSaveData.cpp).
            static const int DAYS_FROM_1900_TO_EPOCH = 25569;
            int today = (int)(time(nullptr) / 86400) + DAYS_FROM_1900_TO_EPOCH;

            // Build "<MODE>_today" key and hash it.
            char todayKey[32];
            std::snprintf(todayKey, sizeof(todayKey), "%s_today", k_ModeNames[mode]);
            uint32_t todayHash = StringHash(todayKey);

            game->pSaveData->AddToTotal(todayKey, todayHash, 1, false, false);
            game->pSaveData->m_LastPlayedDay[mode] = today;
        }
    }
}

// 0x0010a7ac
// TODO: implement scoreDelegate.Call + tier SFX when delegate is ported.
void AddToCurrentScore(int points, int /*param1*/, bool /*param2*/, bool /*param3*/) {
    Game* game = Game::GetInstance();
    if (!game) return;
    game->currentScore += points;
}

// Binary free functions @ 0x0010a4b8 / 0x0010a4e8.
// Defunct sig: playerIdx ignored (online MP scrubbed) — binary @ 0x0010a4b8 / 0x0010a4e8.
void SetScore(int score, int /*playerIdx*/) {
    Game* game = Game::GetInstance();
    if (game && game->pSaveData)
        game->pSaveData->m_CurrentScore = score;   // FruitSaveData+0x64
}

void SetMissCount(int n, int /*playerIdx*/) {
    Game* game = Game::GetInstance();
    if (game && game->pSaveData)
        game->pSaveData->m_CurrentMissCount = n;   // FruitSaveData+0x68
}

} // namespace FN
