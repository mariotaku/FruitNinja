// GameTaskInitInput -- v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c).

#include "game/GameTaskInput.h"
#include "Game.h"
#include "input/InputManager.h"
#include "util/StringHash.h"
#include "math/_Vector3.h"
#include "entities/ActorManager.h"
#include "screens/PauseScreen.h"
#include <cstdio>
#include "game/GameWork.h"

// ASM-spec v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c): pending re-verification

// 16-slot touch zone position table.
// Binary: g_TaskState+0xa0..0xa8 region, 12-byte stride (Vec3).
// GOT[+0x77cc] supplies the zero-vec3 default used in the loop.
static _Vector3<float> g_TouchZoneTable[16];

// 16-slot entity pointer table — one Mortar::ActorManager type-3 entity per touch zone.
// Binary: stored at g_TaskState+0x24..+0x60 (4 bytes each, 16 entries).
static Mortar::Entity* g_TouchEntities[16];

// Forward declarations for input callbacks (bodies below).
bool PointerMoveCallback(InputEvent* ev);
bool PointerDownCallback(InputEvent* ev);
bool PointerUpCallback(InputEvent* ev);
bool PointerDownXboxCallback(InputEvent* ev);
static bool PauseGameCallback(InputEvent* ev);
bool RegressMenuCallback(InputEvent* ev);
bool ShowPauseMenuCallback(InputEvent* ev);

// TouchDownCallback -- registered for "TouchDown_<i>" in the per-zone loop.
// v1.6.1 TouchDownCallback @ 0x001cbf18 dispatches InputSink::TouchDown /
// SlashEntity::TouchDown for key codes 0x89+i (distinct from PointerDownCallback
// at 0x001ca2bc which handles global "PointerPressed").
// TODO: v1.6.1 0x001cbf18 (TouchDownCallback) -- port full body once
// InputSink/key-code-0x89+i dispatch is RE'd.
bool TouchDownCallback(InputEvent* ev);


// GameTaskInitInput() -- v1.6.1 @ 0x001cae0c (thunk @ 0x0011512c)
// Initialises per-session input bindings: config load, 16 touch zones,
// and 6 global action callbacks.
void GameTaskInitInput() {
    // --- Section A: Config load @ 0x16967e ---
    // TODO: implement Mortar::InputManager::LoadConfigFile (binary @ 0x1969d8)
    // Binary: InputManager::GetInstance()->LoadConfigFile("Input/Input.txt");
    // Port specific: LoadConfigFile not ported; SDL2 input does not use
    // an action-mapper config file. Call site preserved as comment so
    // call-graph shape matches binary.

    Mortar::InputManager* im = Mortar::InputManager::GetInstance();
    if (!im) return;

    // --- Section B: 16-zone loop, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    //
    // DIFFERS: binary creates 16 pooled SlashEntity instances here (one per
    //   touch zone). Port owns SlashEntity as a singleton (g_pSlashEntity)
    //   and EntityFactory returns nullptr for type 3, so Add(3) returns
    //   nullptr. Skip the per-zone Mortar::Entity creation + Init for now and leave
    //   g_TouchEntities[i] = nullptr; PointerMoveCallback dispatch must
    //   null-check downstream. Full fix requires SlashEntity to be poolable
    //   (R5+) or a dedicated TouchZoneEntity stub.
    _Vector3<float> defaultPos(0.0f, 0.0f, 0.0f);  // GOT[+0x77cc]
    for (int i = 0; i < 16; ++i) {
        g_TouchZoneTable[i] = defaultPos;

        Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(3, true);
        g_TouchEntities[i] = e;

        _Vector3<float> initPos = defaultPos;
        // Binary: Mortar::Entity::vtable[+0x08] called as (nullptr, 0, &initPos).
        // Port-specific null-guard: skip Init when factory refused type 3.
        if (e) e->Init(nullptr, 0, &initPos);

        char nameDown[16], nameMove[16], nameUp[20];
        snprintf(nameDown, sizeof(nameDown), "TouchDown_%d", i);
        snprintf(nameMove, sizeof(nameMove), "TouchMove_X%d", i);
        snprintf(nameUp,   sizeof(nameUp),   "TouchReleased_%d", i);

        // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags).
        im->RegisterInputCallback(StringHash(nameMove), PointerMoveCallback);
        nameMove[10] = 'Y';  // binary in-place byte patch X->Y (local_7a=0x59 @ 0x001caf68)
        im->RegisterInputCallback(StringHash(nameMove), PointerMoveCallback);
        im->RegisterInputCallback(StringHash(nameDown), TouchDownCallback);

        // nameUp ("TouchReleased_<i>") is snprintf'd for stack-layout fidelity
        // only -- v1.6.1 GameTaskInitInput @ 0x001cae0c never hashes/registers
        // it (confirmed by disassembly: no bl StringHash/RegisterInputCallback
        // follows the 3rd snprintf at 0x001caee4).
        (void)nameUp;
    }

    // --- Section C: 6 global named callbacks, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    // Binary RegisterInputCallback(hash, fnPtr) is 2-arg (no actionFlags param).
    //
    // What actually raises each of these in v1.6.1 (via Data/input/input.txt ->
    // InputManager::LoadConfigFile @0x002442fc -> InputActionMapper):
    //   PointerMove      <- MouseAxisX/Y (0x74/0x75), action "move"     -- LIVE
    //   PointerPressed   <- MouseButton1 (0x6c), action "pressed"       -- LIVE
    //   PointerReleased  <- MouseButton1 (0x6c), action "released"      -- LIVE
    //   PointerPressedX  <- X360_A, action "down"        -- DEAD (ParseKey has no
    //                       X360_* name -> returns 0 -> no mapper is created)
    //   RegressMenu      <- escape, action "released"    -- DEAD (ParseKey has no
    //   ShowPauseMenu    <- AppMenu/escape, "released"      keyboard names either)
    // All three LIVE ones come from InputDeviceBada::Update @0x00242f40; see the
    // header comment in src/engine/input/InputDeviceBada.cpp for the full chain
    // and for the port's substitute emitter.
    im->RegisterInputCallback(StringHash("PointerMove"),     PointerMoveCallback);       // v1.6.1 @ 0x001cbfcc
    im->RegisterInputCallback(StringHash("PointerPressed"),  PointerDownCallback);       // v1.6.1 @ 0x001ca2bc
    im->RegisterInputCallback(StringHash("PointerReleased"), PointerUpCallback);         // v1.6.1 @ 0x001ca2e4
    im->RegisterInputCallback(StringHash("PointerPressedX"), PointerDownXboxCallback);   // v1.6.1 @ 0x001cbec8
    im->RegisterInputCallback(StringHash("RegressMenu"),     RegressMenuCallback);       // v1.6.1 @ 0x001ca350
    im->RegisterInputCallback(StringHash("ShowPauseMenu"),   ShowPauseMenuCallback);     // v1.6.1 @ 0x001ca310
    // TODO: v1.6.1 PauseScreen::Update @0x001a5f1c -- verify PauseGameCallback
    // (body @ 0x001a5978, thunk @ 0x0010d2ec) wiring site; not registered here
    // in v1.6.1 GameTaskInitInput -- PauseScreen::Update wires it directly.
}

// --- Input callbacks ---
// PointerMoveCallback / PointerDownCallback / PointerUpCallback /
// PointerDownXboxCallback below were re-decompiled against v1.6.1.
// TODO: v1.6.1 0x001cbf18 (TouchDownCallback) -- body still not re-decompiled
// against v1.6.1; re-verify before trusting the pass-through stub at the bottom
// of this file.

// ASM-spec v1.6.1 PointerMoveCallback @ 0x001cbfcc:
//   uint  kc  = ev->m_KeyCode;          // binary InputEvent +0x06 (ushort)
//   float v   = ev->m_Value;            // binary InputEvent +0x08 (float)
//   Vector2 dim = DisplayManager::GetInstance()->vtable[+0x40](&rect);  // screen w/h
//   float px  =   v - dim.x * 0.5f;
//   float py  = -(v - dim.y * 0.5f);
//   if      (kc == 0x74) game_work.m_WorldPos.x = px;   // MouseAxisX
//   else if (kc == 0x75) game_work.m_WorldPos.y = py;   // MouseAxisY
//   if ((ushort)(kc - 0x99) < 0x10) {                   // TouchAxisX1..16
//       n = kc - 0x99;
//       if (game_work.m_pActiveTouchSink) InputSink::TouchMoveX(sink, ev, &m_FingerSpawnPos[n]);
//       else                              SlashEntity::TouchMoveX(inputEnts[n], ev);
//       game_work.m_FingerSpawnPos[n].x = px;
//   }
//   if ((ushort)(kc - 0xa9) < 0x10) { ... same for TouchMoveY, .y = py; }
//   return 1;
//
// Registered on BOTH "PointerMove" (the global pointer, keycodes 0x74/0x75) and
// the per-finger "TouchMove_X<i>"/"TouchMove_Y<i>" hashes -- one function, three
// keycode ranges.
//
// DIFFERS: the per-finger half is not reproduced here. The port binds
//   SlashEntity::TouchMoveX/Y directly to the TouchMove_X<i>/Y<i> hashes in
//   SlashEntity::Init, and InputTranslatorSDL::DispatchForSimTick writes
//   game_work.m_FingerSpawnPos[] from the drained Mortar::Touch state. The
//   per-finger dispatch stamps m_KeyCode with TouchAxisX/Y<i> (0x99..0xb8), so
//   neither MouseAxis branch below fires on that path.
//
// DIFFERS: the binary re-centres the raw device pixel coordinate here
//   (px = v - W/2, py = H/2 - v, from GlesForm's top-left Y-down 480x320
//   space). The port's touch coordinates are already centred and Y-up by the
//   time they reach Mortar::Touch (Layout::TouchToGame -- see
//   InputTranslatorSDL::TransformTouchNormalized), so re-applying the centring
//   would double-transform. The value is assigned straight through.
//
// MUST return false. Unlike the binary -- where this function IS the per-finger
// TouchMoveX/Y dispatcher and returning 1 is correct -- the port has
// SlashEntity::TouchMoveX/Y registered on the same TouchMove_X<i>/Y<i> hashes,
// AFTER this one. InputDeviceBada::DispatchEvent stops the chain on a true
// return, so returning true here consumes the move event and kills slicing
// outright.
bool PointerMoveCallback(InputEvent* ev) {
    if (!ev) return false;

    // Binary InputEvent +0x08 -- the single axis-value word. See
    // InputDevice::AxisEvent @0x0027582c and the emitter in InputDeviceBada.cpp.
    const float value = ev->m_Value;

    if (ev->m_KeyCode == INPUT_KEY_MOUSEAXISX) {
        game_work.worldPos.x = value;
    } else if (ev->m_KeyCode == INPUT_KEY_MOUSEAXISY) {
        game_work.worldPos.y = value;
    }

    return false;  // see the MUST-return-false note above
}

// ASM-spec v1.6.1 PointerDownCallback @ 0x001ca2bc -- 7 instructions:
//   ldr r3,[GOT]; mov r0,#1; strb r0,[r3,#0xa2]; strb r0,[r3,#0xa0]; bx lr
// No Game::GetInstance, no null test, and the ev arg is never read. Returns 1
// (r0 still holds the stored 1).
//
// Raised on the frame the global pointer is acquired: InputDeviceBada::Update
// @0x00242f40 -> ButtonPressed(MouseButton1 0x6c, mask 1) -> input.txt's
// `PointerPressed: MouseButton1; pressed` mapper.
//
// m_bTouchDownThisFrame (+0xa0) is a per-frame edge flag, cleared at the top of
// GameUpdate @0x001cf644 and consumed by IsSingleTouchPressed @0x001ca6f8.
// m_bPointerActive (+0xa2) is a level flag, cleared by PointerUpCallback.
//
// "PointerPressed" has exactly one registered handler (this one), so returning
// true consumes nothing else -- no chain-consume hazard.
bool PointerDownCallback(InputEvent* /*ev*/) {
    game_work.m_bTouchDownThisFrame = 1;   // +0xa0
    game_work.m_bPointerActive = 1;        // +0xa2
    return true;
}

// ASM-spec v1.6.1 PointerUpCallback @ 0x001ca2e4 -- 8 instructions:
//   ldr r3,[GOT]; mov r0,#1; mov r2,#0; strb r0,[r3,#0xa1]; strb r2,[r3,#0xa2]; bx lr
// No Game::GetInstance, no null test. Returns 1.
//
// Raised on the frame the global pointer is lost: InputDeviceBada::Update
// @0x00242f40 -> ButtonPressed(MouseButton1 0x6c, mask 4) -> input.txt's
// `PointerReleased: MouseButton1; released` mapper.
//
// "PointerReleased" has exactly one registered handler (this one) -- no
// chain-consume hazard.
bool PointerUpCallback(InputEvent* /*ev*/) {
    game_work.m_bTouchUpThisFrame = 1;     // +0xa1
    game_work.m_bPointerActive = 0;        // +0xa2
    return true;
}

// ASM-spec v1.6.1 PointerDownXboxCallback @ 0x001cbec8: game_work from the GOT,
// `strb r4,[r2,#0xa2]` / `strb r4,[r2,#0xa0]`, then
// SlashEntity::TouchDown(inputEnts[ev->word1], ev) and `cpy r0,r4` (returns 1).
// No Game::GetInstance, no null test. Port covers the TouchDown dispatch via the
// per-finger SlashEntity callbacks bound in SlashEntity::Init, so only the
// game_work writes remain here.
//
// DEAD IN v1.6.1 -- registered but never dispatched, and the port keeps it that
// way. Data/input/input.txt binds `PointerPressedX: X360_A; down`, but
// InputManager::ParseKey @0x002438c8 knows only 61 key names (MouseButton1..8,
// MouseAxisX/Y, Touch1..16, TouchAxisX/Y1..16, AccelAxisX/Y/Z) -- no X360_* name
// -- so it returns 0 and InputManager::LoadConfigFile @0x002442fc skips the
// mapper on its `if (key != 0 && action != 0)` guard. Bada has no gamepad, so
// nothing ever raises this. The registration in GameTaskInitInput is kept for
// call-graph fidelity; InputDeviceBada.cpp deliberately does NOT emit
// "PointerPressedX".
bool PointerDownXboxCallback(InputEvent* /*ev*/) {
    game_work.m_bTouchDownThisFrame = 1;   // +0xa0
    game_work.m_bPointerActive = 1;        // +0xa2
    return true;
}

// v1.6.1 PauseScreen::PauseGameCallback @ 0x001a5978 (thunk @ 0x0010d2ec).
// Wired from PauseScreen::Update (@ 0x001a5f1c/0x001a5f24), not from
// GameTaskInitInput -- see TODO above Section C.
// Binary: if (ev != NULL) { if (g_GameData[+2] == 0) PauseGame(); else UnpauseGame(); }
// g_GameData[+2] = pausedFlag in port (false=running, true=paused).
// TODO: v1.6.1 0x001a5978 (PauseGameCallback) -- Ghidra types this as
// `__thiscall PauseGameCallback(PauseScreen* this)`, and the body is nothing like
// the port's: it gates on this+0xb4 == 0.0f, branches on the this+0xd8 state
// (0 -> arm the pause SFX path and set state 2; 3 -> set this+0xd0 = 2.0f, clear
// this->[+0x98]+0x149, set state 4), and reads game_work+0x18c (mGameSound) from
// the GOT with no null test. Re-RE the whole body against 0x001a5978; the pause /
// unpause dispatch below is a port-side approximation.
static bool PauseGameCallback(InputEvent* ev) {
    if (!ev) return true;
    if (!game_work.bM_Mode) {
        PauseGame();
    } else {
        UnpauseGame();
    }
    return true;
}

// ASM-spec v1.6.1 RegressMenuCallback @ 0x001ca350 -- 6 instructions:
// game_work from the GOT, `strb r0,[r3,#0x610]` = 1, `bx lr` (returns 1).
// Unconditional: no Game::GetInstance, no null test, ev never read.
// +0x610 is m_bFrameDirty in the port (same slot ShowPauseMenuCallback writes
// when its gate passes -- both actions flip the same "menu input pending"
// latch consumed downstream).
bool RegressMenuCallback(InputEvent* ev) {
    (void)ev;
    game_work.m_bFrameDirty = 1;   // +0x610
    return true;
}

// ASM-spec v1.6.1 ShowPauseMenuCallback @ 0x001ca310 -- 14 instructions:
// game_work from the GOT, then
//   if (game_work[+0x0c] == 0.0f && game_work[+0x02] == 0) game_work[+0x610] = 1;
//   return 1;
// No Game::GetInstance, no null test, ev never read.
// +0x610 is m_bFrameDirty -- same field as RegressMenuCallback.
bool ShowPauseMenuCallback(InputEvent* ev) {
    (void)ev;
    if (game_work.m_PauseAmount == 0.0f && !game_work.bM_Mode) {
        game_work.m_bFrameDirty = 1;   // +0x610
    }
    return true;
}

// TouchDownCallback -- registered for "TouchDown_<i>" actions in the zone
// loop. v1.6.1 TouchDownCallback @ 0x001cbf18 dispatches InputSink::TouchDown
// / SlashEntity::TouchDown for key codes 0x89+i. Port covers this via
// per-finger TouchDown_n callbacks bound in SlashEntity::Init directly --
// so this global hook is a no-op pass-through.
bool TouchDownCallback(InputEvent* /*ev*/) {
    return false;  // pass-through; per-finger TouchDown_n handlers do the work
}
