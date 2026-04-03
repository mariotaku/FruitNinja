//
// FrontendTask — State 1 handlers (alternate boot path, skip for port)
//

#include "GameTaskState.h"
#include "Game.h"
#include <cstdio>

void FrontendInit(unsigned long) {
    printf("FrontendInit: transitioning to State 2 (Game)\n");
    Game* game = Game::GetInstance();
    if (game) game->state = 2;
}

void FrontendUpdate(float dt, bool active) { (void)dt; (void)active; }
void FrontendDraw(float dt, bool active) { (void)dt; (void)active; }
void FrontendExit() { printf("FrontendExit\n"); }
