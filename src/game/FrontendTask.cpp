//
// FrontendTask — State 1 handlers (alternate boot path, skip for port)
//

#include "GameTaskState.h"
#include "Game.h"
#include "debug/Logger.h"
#include "game/GameWork.h"

void FrontendInit(unsigned long) {
    LOG_INFO("FRONTEND", "FrontendInit: transitioning to State 2 (Game)");
    Game* game = Game::GetInstance();
    if (game) game_work.taskStateIndex = 2;
}

void FrontendUpdate(float dt, bool active) { (void)dt; (void)active; }
void FrontendDraw(float dt, bool active) { (void)dt; (void)active; }
void FrontendExit() { LOG_INFO("FRONTEND", "FrontendExit"); }
