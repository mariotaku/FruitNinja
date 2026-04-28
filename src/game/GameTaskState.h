#ifndef FN_GAME_TASK_STATE_H
#define FN_GAME_TASK_STATE_H

//
// GameTaskState — 3-state dispatcher matching GameTaskUpdate (0x10a5d4)
//
// State table (3 entries × 4 handlers):
//   State 0: Splash  — auto-transition to Game
//   State 1: Frontend — alternate boot (skip for port)
//   State 2: Game    — main gameplay
//
// Each state has: Init, Update, Draw, Exit handlers
//
// Port specific: the binary's GameTaskUpdate reads an anonymous
// per-task struct via the GOT; there is no `GameTaskState` class
// symbol in the binary. The name `GameTaskState` is an analyst label
// (the FN01 Ghidra script applies it as `/FruitNinja/GameTaskState`)
// and is also used here for clarity. The binary's translation unit
// that owns this state is `GameTask.cpp`. The full struct in the
// binary is 0x118 (280) bytes / 19 fields per docs/structs/game.md
// "GameTask State"; the port currently models only the 4 fields it
// actively uses -- expand as more callers are ported.
//

#include <cstdint>
#include "asset/Texture.h"
#include "asset/TextureManager.h"

// Per-task state struct (matches original GameTaskState at ~0x120 bytes)
// See docs/structs/game.md "GameTask State" section.
struct GameTaskState {
    float totalTime;        // +0x00: accumulated time
    float prevStateDt;      // dt from previous state
    uint8_t prevState;      // last state index
    bool initialized;       // true after Init called

    // +0xfc: background texture (loaded in GameInit)
    SmartPtr<Mortar::Texture> pBackgroundTexture;

    GameTaskState() : totalTime(0), prevStateDt(0), prevState(0), initialized(false) {}
};

// State handler function types (match original function pointer table)
typedef void (*StateInitFn)(unsigned long);
typedef void (*StateUpdateFn)(float dt, bool active);
typedef void (*StateDrawFn)(float dt, bool active);
typedef void (*StateExitFn)(void);

// The 3-state dispatcher — matches GameTaskUpdate (0x10a5d4, 87 lines)
void GameTaskUpdate(float rawDt);
void GameTaskDraw(float dt);
void GameTaskExit();

// Get the task state singleton
GameTaskState* GetTaskState();

// Splash handlers (State 0) — src/game/SplashTask.cpp
void SplashInit(unsigned long);
void SplashUpdate(float dt, bool active);
void SplashDraw(float dt, bool active);
void SplashExit();

// Frontend handlers (State 1) — stub, transitions to State 2
void FrontendInit(unsigned long);
void FrontendUpdate(float dt, bool active);
void FrontendDraw(float dt, bool active);
void FrontendExit();

// Game handlers (State 2) — src/game/GameInit.cpp, GameUpdate.cpp, etc.
void GameInit(unsigned long);
void GameUpdate(float dt, bool active);
void GameDraw(float dt, bool active);
void GameExit_Handler();

#endif
