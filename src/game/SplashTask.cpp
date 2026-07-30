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
#include "debug/Logger.h"
#include "game/GameWork.h"

void SplashInit(unsigned long) {
    LOG_INFO("SPLASH", "SplashInit: transitioning to State 2 (Game)");
    // Port shim: v1.6.1 SplashInit @0x001d2768 is a different (unreachable) body --
    // see the file header. Unconditional assignment; Game::GetInstance is a
    // function-local static and can never be null, so the old `if (game)` was dead.
    game_work.taskStateIndex = 2;  // auto-transition to Game state
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
    LOG_INFO("SPLASH", "SplashExit");
    // Original: InputManager::ClearActions, destroy MenuBackground
}
