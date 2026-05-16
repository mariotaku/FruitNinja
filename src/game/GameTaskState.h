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
#include "math/Vec3.h"

class PauseScreen;
class HUDControl;

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

    // +0xbc: Mortar::SmartPtr<Model> for "slice_fx.mmd" loaded in step 8.
    Mortar::SmartPtr<Mortar::Model> sliceFxMesh;       // step 8

    // +0xc0: Mortar::SmartPtr<Model> for "slice_fx_crit.mmd" loaded in step 8.
    Mortar::SmartPtr<Mortar::Model> sliceFxCritMesh;   // step 8

    // +0xc8: MemoryPool<...>* for SliceEffect nodes allocated in step 9.
    // TODO: type properly once SliceEffect pool is fully ported.
    void* pSliceEffectPool;     // step 9

    // +0xfc: background texture (loaded in GameInit)
    Mortar::SmartPtr<Mortar::Texture> pBackgroundTexture;

    // +0x100: deferred HUDControl queued by HUD::Add-via-callback paths
    // when AddControl can't run inline. GameUpdate drains it once per frame.
    // TODO: writers not yet RE'd; suspect a HUD::QueueDeferredAdd helper.
    HUDControl* pDeferredControl;

    // Binary @ 0x00231404 GameTaskState global pause fields.
    // These three fields are written by PauseGame() / UnpauseGame() free functions
    // (binary @ 0x00168f80 / 0x00168fb0) to a fixed-address global separate from gameObj.
    // +0x08 (float) = pause transition timer; set to 0.25f by PauseGame
    float pauseTransitionTimer;  // +0x08
    // +0x0C (byte) = is-paused indicator; 0 = pausing, 1 = resumed; set by PauseGame/UnpauseGame
    uint8_t isPaused;            // +0x0C
    uint8_t _pad_0d[3];
    // +0x10 (float) = post-unpause bomb-hit grace timer; set to 0.4f by UnpauseGame
    float pauseBombHitTimer;     // +0x10

    // +0x1C: splash fade timer, statically initialised to 1.5f in .data
    // (g_TaskState + 0x1C = 0x001F3DA0, bytes 00 00 c0 3f).
    // Drains at dt * 2.0f; reaches 0 at ~0.75 s wall time.
    float splashFadeTimer;

    // +0x68..+0xb0: 7 fruit-spawn-parameter defaults.
    // Set by _GLOBAL__I_GameTask.cpp @ 0x0016d0dc.
    // Constants resolved from read_memory(0x0016d3e8, 16):
    //   DAT_0016d3ec = 0x3FD9999A = 1.7f
    //   DAT_0016d3f0 = 0x3E99999A = 0.3f
    //   DAT_0016d3f4 = 0x3DCCCCCD = 0.1f
    // Semantic mapping (spawn-rate / variance params) not yet RE'd — TODO.
    Vec3 spawnParam0;   // +0x68: (1.0, 1.0, 1.0)  — likely "global scale" default
    Vec3 spawnParam1;   // +0x74: (1.7, 0.3, 1.0)  — TODO: identify
    Vec3 spawnParam2;   // +0x80: (8.0, 0.1, 1.0)  — TODO: identify
    Vec3 spawnParam3;   // +0x8c: (20.0, 0.1, 1.0) — TODO: identify
    Vec3 spawnParam4;   // +0x98: (4.0, 0.1, 1.0)  — TODO: identify
    Vec3 spawnParam5;   // +0xa4: (0.1, 0.1, 0.1)  — TODO: identify
    Vec3 spawnParam6;   // +0xb0: (0.1, 0.1, 0.1)  — TODO: identify

    GameTaskState()
        : totalTime(0), prevStateDt(0), prevState(0), initialized(false),
          pPauseScreen(nullptr), firstFrame(false), field_0x111(false),
          initComplete(false), pAppState_x54(nullptr),
          pSliceEffectList(nullptr), pSliceEffectPool(nullptr),
          pauseTransitionTimer(0.0f), isPaused(0), _pad_0d(), pauseBombHitTimer(0.0f),
          splashFadeTimer(1.5f),
          spawnParam0(1.0f,  1.0f, 1.0f),
          spawnParam1(1.7f,  0.3f, 1.0f),
          spawnParam2(8.0f,  0.1f, 1.0f),
          spawnParam3(20.0f, 0.1f, 1.0f),
          spawnParam4(4.0f,  0.1f, 1.0f),
          spawnParam5(0.1f,  0.1f, 0.1f),
          spawnParam6(0.1f,  0.1f, 0.1f),
          pDeferredControl(nullptr) {}
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
