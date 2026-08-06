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
    // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290: the state-change retry branches back to
    // 0x0011a2a4 (the dt sign test), so the whole loop body below -- including the rawDt
    // store -- runs again on the retry frame.
    while (true) {
        // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290: `ldr r3,[r4,r3] ;
        // vstr.32 s16,[r3,#0x3c]` @0x0011a2b0 -- game_work from the GOT, stored BEFORE the
        // clamp and before anything else. No Game::GetInstance, no null test. Nothing in
        // the port reads +0x3c yet; the store is here so it holds the binary's value when
        // a future consumer is ported.
        game_work.rawDt = rawDt;

        // Clamp dt (matches original logic)
        // DIFFERS: v1.6.1 GameTaskUpdate @0x0011a290 keeps dt in s16 across the retry and
        // re-clamps the ALREADY-scaled value (loop head 0x0011a2a4), so the second pass
        // scales by 0.94 again. The port re-derives dt from the untouched rawDt param, so
        // the retry frame's dt is 1/0.94 larger. State-change frames only; not chased.
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

        // DIFFERS: v1.6.1 GameTaskUpdate @0x0011a290 clears game_work.m_bFrameDirty (+0x610)
        // unconditionally here (`strb r3,[r7,#0x610]` @0x0011a328) -- the binary's ONLY store
        // of 0 to that field program-wide. The port clears it at the tail of GameUpdate
        // instead (GameInit.cpp). Reason: +0x610 is a back/pause input latch set by
        // RegressMenuCallback / ShowPauseMenuCallback and read by MenuButton::Update
        // (@0x0019ad1c), which the port reaches from mHud->Update INSIDE the state dispatch
        // below. The port pumps input synchronously immediately before GameTaskUpdate
        // (Game::stepUpdate -> DispatchForSimTick), so clearing here would zero the latch
        // between the set and the read every frame and kill the back-key forced slice.
        // Bada delivered those callbacks outside the frame tick, so the binary's placement
        // works there. Moving the clear needs the input-dispatch ordering RE'd first.

        // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290: FruitSaveData::Update runs ONLY on the
        // same-state active path (0x0011a38c), after PowerManager::Update/GetState -- not
        // before the init/state-change dispatch. See the same-state branch below.

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
            // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290
            // gate: param_2(active) = (PowerManager::GetState()==0 && game_work.bM_Mode[+0x02]==0)
            // bM_Mode[+0x02]=0 means gameplay-active; non-zero means paused/inactive.
            Mortar::PowerManager::GetInstance()->Update();
            uint32_t pmState = Mortar::PowerManager::GetInstance()->GetState();
            // bM_Mode is latched at 0x0011a36c, BEFORE the FruitSaveData::Update call below.
            bool modeGate = game_work.bM_Mode;
            s_updated = true;
            // 0x0011a380: prevState is re-written from game_work.taskStateIndex here even
            // though the branch was taken because they already match.
            s_taskState.prevState = game_work.taskStateIndex;

            // ASM-spec v1.6.1 GameTaskUpdate @0x0011a290: per-frame FruitSaveData::Update
            // drives achievement/combo save progression. The only gate is the bomb-hit timer
            // (`vcmpe.f32 s15,#0` on [r7,#0x10], `bhi` skip @0x0011a388). Both +0x50 and
            // +0x40 are loaded straight into the call at 0x0011a38c/0x0011a394, untested.
            if (game_work.m_BombHitTimer <= 0.0f) {
                game_work.m_SaveData->Update(dt, game_work.mHud);
            }

            bool canUpdate = (!modeGate) && (pmState == 0);
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
    // ASM-spec v1.6.1 GameTaskDraw @0x00119dfc: 23 instructions, entry is
    // `ldr r3,[r4,r3]; vstr.32 s0,[r3,#0x38]; ldrb r3,[r3,#0x0]` -- game_work from
    // the GOT. No Game::GetInstance, no null test.
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
    // NOTE: this mHud early return is GENUINE, not a port addition. v1.6.1 has
    // `cmp r0,#0x0 ; ldmiaeq sp!,{r4,r5,r6,pc}` at 0x001ce1a0/0x001ce1a4 -- a
    // conditional return straight out of the function.
    if (!game_work.mHud) return;
    game_work.mHud->Save();
    SaveCurrentData(true);
}
