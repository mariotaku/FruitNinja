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
#include "game/FruitSaveData.h"
#include "game/SettingsSave.h"  // GameTaskSaveOnExit: port-specific settings persistence
#include "hud/HUD.h"  // GameTaskSaveOnExit: mHud->Save() needs the full HUD definition

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

// ASM-spec v1.6.1 GameTaskUpdate @0x0011a290 / GameTaskExit @0x00119e84: `updated` file static
// (GameTask.cpp, not a GameTaskState struct field). Set true in the same-state active-update
// branch before the Update dispatch; cleared by GameTaskExit. GameTaskDraw gates on this (not
// on "initialized") -- the binary does not draw the single frame between Init and first Update.
static bool s_updated = false;

// ASM-spec v1.6.1 GameTaskDraw @0x00119dfc / GameTaskUpdate @0x0011a290: drawDt accumulator
// (GameTask.cpp file static, not a GameTaskState struct field). GameTaskUpdate accumulates
// clamped dt into this every update; GameTaskDraw consumes it (ignoring its own dt param) and
// resets it to 0 after dispatch.
static float s_drawDt = 0.0f;

GameTaskState* GetTaskState() { return &s_taskState; }

// ASM-spec v1.6.1 s_flashTexture @0x00316790 (see GameTaskState.h for the
// merge rationale -- task #141).
Mortar::SmartPtr<Mortar::Texture> g_FlashTexture;

void FlashTexture_UnloadStatics() {
    g_FlashTexture.SetNull();
}

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
        s_drawDt += dt;

        // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290: per-frame FruitSaveData::Update
        // (gated bomb-hit-timer<=0) before state dispatch. Runs every frame, all modes
        // incl arcade -- drives achievement/combo save progression.
        if (game_work.m_BombHitTimer <= 0.0f && game_work.m_SaveData) {
            game_work.m_SaveData->Update(dt, game_work.mHud);
        }

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
            s_updated = true;
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
// The dt param is ignored (matches binary -- Game::Draw's dt is discarded); the
// accumulated s_drawDt is used and reset instead. See s_drawDt comment above.
void GameTaskDraw(float /*dt*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    uint8_t stateIdx = game_work.taskStateIndex;
    // ASM-verified: 2026-07-03T00:00Z v1.6.1 GameTaskDraw @ 0x00119dfc (asm-inspector)
    // flM_Dt assign and drawDt reset are UNCONDITIONAL -- only the draw dispatch itself
    // is gated. Previously both were nested inside the gate, so during a screen
    // transition (gate false, s_updated cleared by GameTaskExit) s_drawDt accumulated
    // across many frames, producing a huge dt on the first post-transition draw (#346).
    game_work.dt = s_drawDt;
    if (stateIdx == s_taskState.prevState && s_updated) {
        s_drawFuncs[stateIdx](s_drawDt, true);
    }
    s_drawDt = 0.0f;
}

// Matches GameTaskExit (v1.6.1 @ 0x00119e84, 22 lines)
void GameTaskExit() {
    if (s_taskState.initialized) {
        s_exitFuncs[s_taskState.prevState]();
        s_taskState.initialized = false;
        s_updated = false;
    }
}

// ASM-spec v1.6.1 GameTaskSaveOnExit @0x001ce170: suspends updates and saves without teardown.
// Used on app-suspend (Paused/SaveOnExit) instead of GameTaskExit so in-game state is preserved.
void GameTaskSaveOnExit() {
    game_work.m_bUpdatesSuspended = 1;
    // Port specific: persist the port-side settings (SettingsSave.xml) on every
    // app-exit/suspend path, not only when the Settings popup finishes its close
    // animation (SettingsScreen::UpdateAnim) -- otherwise a live-applied setting
    // changed and then quit/backgrounded before the popup fully closed is lost.
    // Must run BEFORE the guards below: they are game-save re-entrancy gates,
    // and settings are independent of the game save (must persist even with no
    // HUD). SaveSettings does its own IDBFS flush on web.
    SaveSettings();
    if (*GetIsSavingBool() != 0) return;
    if (!game_work.mHud) return;
    game_work.mHud->Save();
    SaveCurrentData(true);
}
