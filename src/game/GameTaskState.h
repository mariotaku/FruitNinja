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
#include "asset/Mesh.h"
#include "util/SmartPtr.h"

class PauseScreen;

// Per-task state struct (matches original GameTaskState at ~0x120 bytes)
// See docs/structs/game.md "GameTask State" section.
// Binary fields added per docs/systems/gameinit-todos.md steps 8-10, 12.
struct GameTaskState {
    float totalTime;        // +0x00: accumulated time
    float prevStateDt;      // dt from previous state
    uint8_t prevState;      // last state index
    bool initialized;       // true after Init called

    // +0x04: PauseScreen pointer — allocated in GameInit step 12.
    // Binary: g_TaskState +0x04. Step 14 AddControl batch registers this.
    PauseScreen* pPauseScreen;  // step 12

    // +0x0c: "first frame" flag — cleared in step 10; semantics TBD.
    // TODO: confirm xrefs to GameTaskState+0x0c (RE gap, step 10).
    bool firstFrame;            // step 10

    // +0x111: unknown flag — cleared to 0 in step 10.
    // TODO: confirm xrefs to GameTaskState+0x111 (RE gap, step 10).
    bool field_0x111;           // step 10

    // +0x112: GameInit-complete / re-entry guard.
    // Set to 1 at the end of step 10 to prevent GameInit from re-running.
    // Also read at GameInit entry: if non-zero, GameInit bails out.
    bool initComplete;          // step 10

    // +0x114: copy of g_GameData+0x54 written in step 10.
    // TODO: identify g_GameData+0x54 semantics (RE gap).
    void* pAppState_x54;        // step 10

    // +0x64: List<SliceEffect>* allocated in step 9.
    // TODO: type properly once SliceEffect pool is fully ported.
    void* pSliceEffectList;     // step 9

    // +0xbc: SmartPtr<Model> for "slice_fx.mmd" loaded in step 8.
    SmartPtr<Mortar::Model> sliceFxMesh;       // step 8

    // +0xc0: SmartPtr<Model> for "slice_fx_crit.mmd" loaded in step 8.
    SmartPtr<Mortar::Model> sliceFxCritMesh;   // step 8

    // +0xc8: MemoryPool<...>* for SliceEffect nodes allocated in step 9.
    // TODO: type properly once SliceEffect pool is fully ported.
    void* pSliceEffectPool;     // step 9

    // +0xfc: background texture (loaded in GameInit)
    SmartPtr<Mortar::Texture> pBackgroundTexture;

    GameTaskState()
        : totalTime(0), prevStateDt(0), prevState(0), initialized(false),
          pPauseScreen(nullptr), firstFrame(false), field_0x111(false),
          initComplete(false), pAppState_x54(nullptr),
          pSliceEffectList(nullptr), pSliceEffectPool(nullptr) {}
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
