//
// SplashTask — State 0 handlers
// Original: SplashInit 0x16f648, SplashUpdate 0x16f5d8, SplashDraw 0x16f554, SplashExit 0x16f59c
//
// In the port: Splash auto-transitions to State 2 (Game) on the first frame.
// The original waits for MenuBackground + input, but we skip that for now.
//

#include "GameTaskState.h"
#include "Game.h"
#include <cstdio>

void SplashInit(unsigned long) {
    printf("SplashInit: transitioning to State 2 (Game)\n");
    Game* game = Game::GetInstance();
    if (game) {
        game->taskStateIndex = 2;  // auto-transition to Game state
    }
}

void SplashUpdate(float dt, bool active) {
    (void)dt; (void)active;
    // Original: InputManager::Update, SoundManager::Update, SplashInputEvent
    // For port: Splash is skipped (auto-transition in Init)
}

void SplashDraw(float dt, bool active) {
    (void)dt; (void)active;
    // Original: no-op (stub)
}

void SplashExit() {
    printf("SplashExit\n");
    // Original: InputManager::ClearActions, destroy MenuBackground
}
