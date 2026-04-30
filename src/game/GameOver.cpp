// Analysed: 2026-04-30T12:00
// GameOver — 0x00169ed4

#include "GameOver.h"
#include "Game.h"
#include "WaveManager.h"
#include "screens/GameOverScreen.h"
#include "hud/HUD.h"

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
}

// 0x0010a7ac
// TODO: implement scoreDelegate.Call + tier SFX when delegate is ported.
void AddToCurrentScore(int points, int /*param1*/, bool /*param2*/, bool /*param3*/) {
    Game* game = Game::GetInstance();
    if (!game) return;
    game->currentScore += points;
}

} // namespace FN
