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
// binary is 0x118 (280) bytes / 19 fields; the port currently models only the 4 fields it
// actively uses -- expand as more callers are ported.
//

#include <cstdint>
#include <cstddef>
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "util/SmartPtr.h"
#include "math/_Vector3.h"

class PauseScreen;
class HUDControl;
namespace Mortar { class MortarSound; }

// Per-task state struct (matches original GameTaskState at ~0x120 bytes)
// Binary fields added (RE'd).
struct GameTaskState {
    // +0x00: accumulated time
    float totalTime;

    // +0x04: PauseScreen pointer — allocated in GameInit step 12.
    // Binary: g_TaskState +0x04. Step 14 AddControl batch registers this.
    PauseScreen* pPauseScreen;  // step 12

    // +0x08 (float) = pause transition timer; set to 0.25f by PauseGame
    // Written by PauseGame() / UnpauseGame() free functions
    // (binary @ 0x00168f80 / 0x00168fb0).
    float pauseTransitionTimer;

    // +0x0C (byte) = is-paused indicator; 0 = pausing, 1 = resumed; set by PauseGame/UnpauseGame
    uint8_t isPaused;
    uint8_t _pad_0d[3];

    // +0x10 (float) = post-unpause bomb-hit grace timer; set to 0.4f by UnpauseGame
    float pauseBombHitTimer;

    // Gap +0x14..+0x1B: unasserted fields fit here
    float prevStateDt;          // dt from previous state
    uint8_t prevState;          // last state index
    bool initialized;           // true after Init called
    // +0x0c: "first frame" flag — cleared in step 10; semantics TBD.
    // TODO: confirm xrefs to GameTaskState+0x0c (RE gap, step 10).
    bool firstFrame;            // step 10
    uint8_t _pad_1b;

    // +0x1C: splash fade timer, statically initialised to 1.5f in .data
    // (g_TaskState + 0x1C = 0x001F3DA0, bytes 00 00 c0 3f).
    // Drains at dt * 2.0f; reaches 0 at ~0.75 s wall time.
    float splashFadeTimer;

    // Gap +0x20..+0x63: unnamed binary fields not yet RE'd
    uint8_t _gap_20[0x44];

    // +0x64: List<SliceEffect>* allocated in step 9.
    // TODO: type properly once SliceEffect pool is fully ported.
    void* pSliceEffectList;     // step 9

    // +0x68..+0xb0: 7 fruit-spawn-parameter defaults.
    // Set by _GLOBAL__I_GameTask.cpp @ 0x0016d0dc.
    // Constants resolved from read_memory(0x0016d3e8, 16):
    //   DAT_0016d3ec = 0x3FD9999A = 1.7f
    //   DAT_0016d3f0 = 0x3E99999A = 0.3f
    //   DAT_0016d3f4 = 0x3DCCCCCD = 0.1f
    // Semantic mapping (spawn-rate / variance params) not yet RE'd -- TODO.
    _Vector3<float> spawnParam0;   // +0x68: (1.0, 1.0, 1.0)  -- likely "global scale" default
    _Vector3<float> spawnParam1;   // +0x74: (1.7, 0.3, 1.0)  -- TODO: identify
    _Vector3<float> spawnParam2;   // +0x80: (8.0, 0.1, 1.0)  -- TODO: identify
    _Vector3<float> spawnParam3;   // +0x8c: (20.0, 0.1, 1.0) -- TODO: identify
    _Vector3<float> spawnParam4;   // +0x98: (4.0, 0.1, 1.0)  -- TODO: identify
    _Vector3<float> spawnParam5;   // +0xa4: (0.1, 0.1, 0.1)  -- TODO: identify
    _Vector3<float> spawnParam6;   // +0xb0: (0.1, 0.1, 0.1)  -- TODO: identify

    // +0xbc: Mortar::SmartPtr<Model> for "slice_fx.mmd" loaded in step 8.
    Mortar::SmartPtr<Mortar::Model> sliceFxMesh;       // step 8

    // +0xc0: Mortar::SmartPtr<Model> for "slice_fx_crit.mmd" loaded in step 8.
    Mortar::SmartPtr<Mortar::Model> sliceFxCritMesh;   // step 8

    // Gap +0xC4..+0xC7: unnamed
    uint8_t _gap_c4[4];

    // +0xc8: MemoryPool<...>* for SliceEffect nodes allocated in step 9.
    // TODO: type properly once SliceEffect pool is fully ported.
    void* pSliceEffectPool;     // step 9

    // Gap +0xCC..+0xD7: unnamed binary fields not yet RE'd
    uint8_t _gap_cc[0x0C];

    // +0xD8: persistent looping "Bomb-Fuse" MortarSound* (binary
    // @ 0x0016c4c8..0x0016c5ca, GameUpdate fuse-vol block). Lazily spawned
    // on first frame with an active bomb; volume modulated every frame by
    // (Bomb::GetHeighestBomb() / 100.0) * master vol. Muted (vol=0) when
    // no bombs / paused / level transition. Never explicitly Released --
    // the silent-loop matches the binary's behaviour.
    Mortar::MortarSound* m_pBombFuseSound;

    // Gap +0xDC..+0xF7: unnamed binary fields not yet RE'd
    uint8_t _gap_dc[0xf8 - 0xdc];  // 0x1c bytes (+0xdc..+0xf7)

    // +0xf8: "Menu-bomb flash" flag. Set to 1 by HitMenuBomb (v1.6.1 @ 0x001cf42c)
    // before arming bombHitTimer=2.0; cleared to 0 by HitBomb (v1.6.1 @ 0x001cf27c)
    // before arming bombHitTimer=3.2. Gates the GameUpdate cross-1.5 GameOver
    // trigger so that Quit-from-GameOverScreen / PauseScreen quit / Zen-mode bomb
    // penalty animations don't re-fire GameOver.
    // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x0016b270 / 0x0016b154 / 0x0016c2bc (re-analyst)
    // NOTE: this is NOT the binary's layout -- the flag is a standalone file-static
    // byte (s_menuBombHit @0x0031677A), not a GameTaskState member; this offset is
    // port-local only. The cited addresses above are stale v1.5.1. Tracked separately
    // as v1.5.1-residue; do not restructure here.
    uint8_t m_bMenuBombFlashFlag;          // +0xf8
    uint8_t _gap_f9[0x03];                 // +0xf9..+0xfb pad

    // +0xfc: background texture (loaded in GameInit)
    Mortar::SmartPtr<Mortar::Texture> pBackgroundTexture;

    // +0x100: deferred HUDControl queued by HUD::Add-via-callback paths
    // when AddControl can't run inline. GameUpdate drains it once per frame.
    // TODO: writers not yet RE'd; suspect a HUD::QueueDeferredAdd helper.
    HUDControl* pDeferredControl;

    // Gap +0x104..+0x10B: unnamed binary fields not yet RE'd
    uint8_t _gap_104[8];

    // +0x10C: per-attempt timed-mode accumulator. Reset to 0 alongside TimeControl
    // (+0x180)+0x7c at EndRetryLevel (binary @ 0x0016a226) and SkipToGameOver
    // (binary @ 0x0016adba, guarded by IsTimedGame). Reader not yet RE'd -- treat
    // as write-only. ASM-verified: 2026-05-20 v1.6.1 binary @ 0x0016a226 (re-analyst).
    int32_t m_TimedModeAccumulator;

    // +0x110..+0x113: written by EndRetryLevel (binary @ 0x0016a220) as float 0.5f,
    // but individual bytes also accessed: +0x111 cleared by GameInit, +0x112 used as
    // initComplete guard. Modelled as union so both float-write and byte-read call sites compile.
    union {
        float m_ScoreStateField_0x110;  // +0x110: float write path (EndRetryLevel, 0.5f)
        struct {
            uint8_t _unused_0x110;      // byte 0 of float
            bool m_reserved111;         // +0x111: flag cleared in step 10; purpose unknown
            bool initComplete;          // +0x112: GameInit-complete / re-entry guard
            uint8_t _pad_113;           // +0x113: padding
        };
    };

    // +0x114: copy of g_GameData+0x54 written in step 10.
    // TODO: identify g_GameData+0x54 semantics (RE gap).
    void* pAppState_x54;        // step 10

    GameTaskState()
        : totalTime(0),
          pPauseScreen(nullptr),
          pauseTransitionTimer(0.0f), isPaused(0), _pad_0d(), pauseBombHitTimer(0.0f),
          prevStateDt(0), prevState(0), initialized(false), firstFrame(false), _pad_1b(0),
          splashFadeTimer(1.5f),
          _gap_20(),
          pSliceEffectList(nullptr),
          spawnParam0(1.0f,  1.0f, 1.0f),
          spawnParam1(1.7f,  0.3f, 1.0f),
          spawnParam2(8.0f,  0.1f, 1.0f),
          spawnParam3(20.0f, 0.1f, 1.0f),
          spawnParam4(4.0f,  0.1f, 1.0f),
          spawnParam5(0.1f,  0.1f, 0.1f),
          spawnParam6(0.1f,  0.1f, 0.1f),
          sliceFxMesh(), sliceFxCritMesh(),
          _gap_c4(),
          pSliceEffectPool(nullptr),
          _gap_cc(),
          m_pBombFuseSound(nullptr),
          _gap_dc(),
          m_bMenuBombFlashFlag(0),
          _gap_f9(),
          pBackgroundTexture(),
          pDeferredControl(nullptr),
          _gap_104(),
          m_TimedModeAccumulator(0),
          m_ScoreStateField_0x110(0.0f),
          pAppState_x54(nullptr) {}
};

// Field-offset assertions for GameTaskState (binary global @ 0x00231404 area, ARM32).
// Offsets are struct-relative. Guarded by __bada__ so they fire only on the
// cross-build / Bada toolchain where the struct layout must match the binary.
// If any of these fail the struct member order is wrong — fix the layout, not the assert.
#ifdef __bada__
static_assert(offsetof(GameTaskState, totalTime)             == 0x00,  "GameTaskState::totalTime must be at +0x00");
static_assert(offsetof(GameTaskState, pPauseScreen)          == 0x04,  "GameTaskState::pPauseScreen must be at +0x04");
static_assert(offsetof(GameTaskState, pauseTransitionTimer)  == 0x08,  "GameTaskState::pauseTransitionTimer must be at +0x08");
static_assert(offsetof(GameTaskState, isPaused)              == 0x0C,  "GameTaskState::isPaused must be at +0x0C");
static_assert(offsetof(GameTaskState, pauseBombHitTimer)     == 0x10,  "GameTaskState::pauseBombHitTimer must be at +0x10");
static_assert(offsetof(GameTaskState, splashFadeTimer)       == 0x1C,  "GameTaskState::splashFadeTimer must be at +0x1C");
static_assert(offsetof(GameTaskState, pSliceEffectList)      == 0x64,  "GameTaskState::pSliceEffectList must be at +0x64");
static_assert(offsetof(GameTaskState, spawnParam0)           == 0x68,  "GameTaskState::spawnParam0 must be at +0x68");
static_assert(offsetof(GameTaskState, spawnParam1)           == 0x74,  "GameTaskState::spawnParam1 must be at +0x74");
static_assert(offsetof(GameTaskState, spawnParam2)           == 0x80,  "GameTaskState::spawnParam2 must be at +0x80");
static_assert(offsetof(GameTaskState, spawnParam3)           == 0x8C,  "GameTaskState::spawnParam3 must be at +0x8C");
static_assert(offsetof(GameTaskState, spawnParam4)           == 0x98,  "GameTaskState::spawnParam4 must be at +0x98");
static_assert(offsetof(GameTaskState, spawnParam5)           == 0xA4,  "GameTaskState::spawnParam5 must be at +0xA4");
static_assert(offsetof(GameTaskState, spawnParam6)           == 0xB0,  "GameTaskState::spawnParam6 must be at +0xB0");
static_assert(offsetof(GameTaskState, sliceFxMesh)           == 0xBC,  "GameTaskState::sliceFxMesh must be at +0xBC");
static_assert(offsetof(GameTaskState, sliceFxCritMesh)       == 0xC0,  "GameTaskState::sliceFxCritMesh must be at +0xC0");
static_assert(offsetof(GameTaskState, pSliceEffectPool)      == 0xC8,  "GameTaskState::pSliceEffectPool must be at +0xC8");
static_assert(offsetof(GameTaskState, m_pBombFuseSound)      == 0xD8,  "GameTaskState::m_pBombFuseSound must be at +0xD8");
static_assert(offsetof(GameTaskState, m_bMenuBombFlashFlag)  == 0xF8,  "GameTaskState m_bMenuBombFlashFlag at +0xF8");
static_assert(offsetof(GameTaskState, pBackgroundTexture)    == 0xFC,  "GameTaskState::pBackgroundTexture must be at +0xFC");
static_assert(offsetof(GameTaskState, pDeferredControl)      == 0x100, "GameTaskState::pDeferredControl must be at +0x100");
static_assert(offsetof(GameTaskState, m_TimedModeAccumulator) == 0x10C, "GameTaskState::m_TimedModeAccumulator must be at +0x10C");
static_assert(offsetof(GameTaskState, m_ScoreStateField_0x110) == 0x110, "GameTaskState::m_ScoreStateField_0x110 must be at +0x110");
static_assert(offsetof(GameTaskState, m_reserved111)         == 0x111, "GameTaskState::m_reserved111 must be at +0x111");
static_assert(offsetof(GameTaskState, initComplete)          == 0x112, "GameTaskState::initComplete must be at +0x112");
static_assert(offsetof(GameTaskState, pAppState_x54)         == 0x114, "GameTaskState::pAppState_x54 must be at +0x114");
static_assert(sizeof(GameTaskState)                          == 0x118, "sizeof(GameTaskState) must be 0x118");
#endif

// State handler function types (match original function pointer table)
typedef void (*StateInitFn)(unsigned long);
typedef void (*StateUpdateFn)(float dt, bool active);
typedef void (*StateDrawFn)(float dt, bool active);
typedef void (*StateExitFn)(void);

// The 3-state dispatcher — matches GameTaskUpdate (0x10a5d4, 87 lines)
void GameTaskUpdate(float rawDt);
void GameTaskDraw(float dt);
void GameTaskExit();
void GameTaskSaveOnExit(); // v1.6.1 GameTaskSaveOnExit @0x001ce170

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
void GameExit(); // v1.6.1 GameExit @0x001cfed4

// DrawBackground (v1.6.1 @ 0x001ccaf4) — draws the current background texture
// quad (see src/game/GameInit.cpp). Factored out of GameDraw so isolated
// render tests (e.g. component-mode powerup screenshots) can draw the real
// wooden dojo panel behind an effect without running the full GameDraw path.
void DrawBackground();

// ASM-spec v1.6.1 s_flashTexture @0x00316790: ONE shared file-static
// SmartPtr<Texture> ("flash.tex") in GameTask.cpp's global block
// (base 0x00316700), lazily loaded by whichever consumer draws first.
// All three port-side flash overlays share this single instance:
//   - BombHit.cpp DrawCritHit      (v1.6.1 @0x001ccfa0)
//   - Bomb.cpp DrawBombHit         (v1.6.1 @0x001cd1a0)
//   - PauseScreen.cpp PreDraw      (v1.6.1 @0x001cd35c)
// Previously ported as three independent statics that never released their
// GL texture name (task #141).
extern Mortar::SmartPtr<Mortar::Texture> g_FlashTexture;

// Port specific: releases g_FlashTexture. v1.6.1 has no dedicated unload
// entry point for this global -- the binary leaves it to two
// __aeabi_atexit-registered SmartPtr dtors (global.constructors.keyed.to.
// GameTask.cpp @0x001cec1c/0x001cec2c). The port cannot rely on its own
// atexit for this because it runs after SDL_GL_DeleteContext and the GL
// texture name would leak. Called from GameDestroy, before
// MeshManager::Destroy() -- a pre-GL-teardown backstop, distinct from (and
// in addition to) the explicit release GameExit performs at 0x001cff88
// (see GameInit.cpp). No-op when no flash overlay ever drew; idempotent.
void FlashTexture_UnloadStatics();

// v1.6.1 CleanupAndReturnToMainMenu @ 0x00157620 -- bx lr (empty body in v1.6.1).
// Called from GameUpdate quit-transition timer path.
void CleanupAndReturnToMainMenu();

#endif
