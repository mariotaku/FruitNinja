//
// GameTaskState — 3-state dispatcher
// Reimplemented from GameTaskUpdate (v1.6.1 @ 0x0011a290, 87 lines)
//

#include "GameTaskState.h"
#include "Game.h"
#include "engine/system/PowerManager.h"
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"

// Verified timing constants from binary (read_memory)
static const float TASK_MAX_RAW_DT = 0.1f;     // DAT_0010a708
static const float TASK_DT_SCALE   = 0.94f;     // DAT_0010a70c (approx)
static const float TASK_DT_CLAMP   = 0.1f;      // DAT_0010a710
static const float TASK_DT_TO_MS   = 1000.0f;   // DAT_0010a714

// Task state singleton
static GameTaskState s_taskState;

// State handler tables (3 states × 4 handlers)
static StateInitFn   s_initFuncs[3]   = { SplashInit,   FrontendInit,   GameInit };
static StateUpdateFn s_updateFuncs[3] = { SplashUpdate,  FrontendUpdate, GameUpdate };
static StateDrawFn   s_drawFuncs[3]   = { SplashDraw,    FrontendDraw,   GameDraw };
static StateExitFn   s_exitFuncs[3]   = { SplashExit,    FrontendExit,   GameExit };

// Flag: state change was requested during init
static bool s_stateChangeRequested = false;

GameTaskState* GetTaskState() { return &s_taskState; }

// Matches GameTaskUpdate (v1.6.1 @ 0x0011a290, 87 lines)
void GameTaskUpdate(float rawDt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    while (true) {
        // Clamp dt (matches original logic)
        float dt;
        if (rawDt > 0.0f && rawDt < TASK_MAX_RAW_DT) {
            dt = rawDt * TASK_DT_SCALE;
        } else {
            dt = TASK_DT_CLAMP;
        }

        float frameMs = dt * TASK_DT_TO_MS;
        uint8_t stateIdx = game_work.taskStateIndex;

        game_work.dt = dt;
        game_work.m_FrameTimer += (int)frameMs;
        s_taskState.totalTime += dt;

        if (!s_taskState.initialized) {
            // First frame of new state: call init handler
            s_taskState.prevState = stateIdx;
            s_initFuncs[stateIdx](0);
            s_taskState.initialized = true;

            if (!s_stateChangeRequested) return;
            s_stateChangeRequested = false;
            continue;  // re-enter loop for new state
        }

        if (stateIdx == s_taskState.prevState) {
            // ASM-verified: 2026-06-20T00:00Z v1.6.1 GameTaskUpdate @ 0x0011a290 (asm-inspector)
            // gate: param_2(active) = (PowerManager::GetState()==0 && game_work.bM_Mode[+0x02]==0)
            // bM_Mode[+0x02]=0 means gameplay-active; non-zero means paused/inactive.
            Mortar::PowerManager::GetInstance()->Update();
            uint32_t pmState = Mortar::PowerManager::GetInstance()->GetState();
            bool canUpdate = (!game_work.bM_Mode) && (pmState == 0);
            s_updateFuncs[stateIdx](dt, canUpdate);
        } else {
            // State changed: exit old, loop will init new
            GameTaskExit();
            s_taskState.prevState = stateIdx;
            s_taskState.initialized = false;
        }
        return;
    }
}

// Matches GameTaskDraw (v1.6.1 @ 0x00119dfc, 23 lines)
void GameTaskDraw(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    uint8_t stateIdx = game_work.taskStateIndex;
    if (stateIdx == s_taskState.prevState && s_taskState.initialized) {
        s_drawFuncs[stateIdx](dt, true);
    }
}

// Matches GameTaskExit (v1.6.1 @ 0x0011a320, 22 lines)
void GameTaskExit() {
    if (s_taskState.initialized) {
        s_exitFuncs[s_taskState.prevState]();
        s_taskState.initialized = false;
    }
}
