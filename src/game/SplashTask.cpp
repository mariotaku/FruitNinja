//
// SplashTask — State 0 handlers
// Original: SplashInit 0x16f648, SplashUpdate 0x16f5d8, SplashDraw 0x16f554, SplashExit 0x16f59c
//
// Defunct: SplashTask -- these four functions are dead code in the shipped binary.
// The dispatch table at 0x001E8A28 only registers GameDraw/GameUpdate/GameInit/GameExit;
// Splash and Frontend addresses never appear. The actual splash is implemented as an
// in-frame overlay inside GameUpdate/GameDraw (see GameInit.cpp + StartupEffects.cpp).
// File kept for source-archaeology value only; SplashInit/SplashUpdate/SplashDraw/SplashExit
// are unreachable at runtime.
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
