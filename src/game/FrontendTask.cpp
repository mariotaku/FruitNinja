//
// FrontendTask — State 1 handlers (alternate boot path, skip for port)
//

#include "GameTaskState.h"
#include "Game.h"
#include "debug/Logger.h"

void FrontendInit(unsigned long) {
    LOG_INFO("FRONTEND", "FrontendInit: transitioning to State 2 (Game)");
    Game* game = Game::GetInstance();
    if (game) game->taskStateIndex = 2;
}

void FrontendUpdate(float dt, bool active) { (void)dt; (void)active; }
void FrontendDraw(float dt, bool active) { (void)dt; (void)active; }
void FrontendExit() { LOG_INFO("FRONTEND", "FrontendExit"); }
