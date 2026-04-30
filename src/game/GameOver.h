#ifndef FN_GAME_OVER_H
#define FN_GAME_OVER_H

// GameOver free function — 0x00169ed4
// Triggers game-over: guards on pauseFlag, creates GameOverScreen, adds to HUD.

// AddToCurrentScore free function — 0x0010a7ac
// Adds points to currentScore, fires scoreDelegate, plays tier SFX.
// Port stub: applies score delta only; delegate + tier SFX not yet ported.

namespace FN {
void GameOver(int endReason, float endScore, int endParam);
void AddToCurrentScore(int points, int param1, bool param2, bool param3);
}

#endif
