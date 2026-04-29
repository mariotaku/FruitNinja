#ifndef FN_GAME_OVER_H
#define FN_GAME_OVER_H

// GameOver free function — 0x00169ed4
// Triggers game-over: guards on pauseFlag, creates GameOverScreen, adds to HUD.

namespace FN {
void GameOver(int endReason, float endScore, int endParam);
}

#endif
