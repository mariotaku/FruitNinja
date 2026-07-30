//
// FrontendTask — State 1 handlers (alternate boot path, skip for port)
//

#include "GameTaskState.h"
#include "Game.h"
#include "debug/Logger.h"
#include "game/GameWork.h"

void FrontendInit(unsigned long) {
    LOG_INFO("FRONTEND", "FrontendInit: transitioning to State 2 (Game)");
    // Port shim: v1.6.1 FrontendInit @0x001d1898 is a different (unreachable) body --
    // the state table at 0x001E8A28 only registers the Game* handlers, so this
    // auto-transition is port-side. It must be an unconditional assignment:
    // Game::GetInstance is a function-local static and can never be null, so the
    // old `if (game)` was dead code that could only ever have stalled the boot.
    game_work.taskStateIndex = 2;
}

void FrontendUpdate(float dt, bool active) { (void)dt; (void)active; }
void FrontendDraw(float dt, bool active) { (void)dt; (void)active; }
void FrontendExit() { LOG_INFO("FRONTEND", "FrontendExit"); }
