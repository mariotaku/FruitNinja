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
#include "entities/SlashEntity.h"
#include "input/InputSink.h"
#include "hud/HUD.h"

// Port specific: the settings modal captures input -- while it is open the
// per-finger blade must not be fed (see HUD::SetInputModal, src/hud/HUD.h).
// The gate used to sit in InputTranslatorSDL::DispatchForSimTick, back when the
// translator was the port's own dispatch site. Now that the binary mapper chain
// (LoadConfigFile -> InputActionMapper -> these callbacks) is live, the per-finger
// events arrive here instead, so the gate moved with them. UI widgets are
// unaffected: they read Mortar::Touch::states1 directly, not these events.
static bool BladeInputSuppressed() {
#if defined(__bada__)
    return false;   // no settings modal on device; folds the gate away entirely
#else
    return game_work.mHud && game_work.mHud->GetInputModal() != nullptr;
#endif
}

// ASM-spec v1.6.1 GameTaskInitInput @ 0x001cae0c (thunk @ 0x0011512c): pending re-verification

// The 16-slot finger position table this file zeroes and then feeds is
// game_work.m_FingerSpawnPos (GameWork +0xa4, 12-byte Vec3 stride, x@+0xa4
// y@+0xa8 z@+0xac). There is no second table: IsTouchDown @0x001ca69c reads
// +0xac and TouchInRegion @0x001ca754 gates on +0xac then reads +0xa4/+0xa8.
//
// The 16 pooled type-3 entities the loop allocates are the port's
// g_pSlashEntities[] -- the binary indexes the same array as inputEnts[n] from
// TouchDownCallback @0x001cbf18 and PointerMoveCallback @0x001cbfcc.

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
bool TouchDownCallback(InputEvent* ev);


// GameTaskInitInput() -- v1.6.1 @ 0x001cae0c (thunk @ 0x0011512c)
// Initialises per-session input bindings: config load, 16 touch zones,
// and 6 global action callbacks.
void GameTaskInitInput() {
    Mortar::InputManager* im = Mortar::InputManager::GetInstance();
    if (!im) return;

    // --- Section A: Config load, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    // MUST come first. LoadConfigFile @0x002442fc is the only thing that creates
    // InputActionMappers, and every RegisterInputCallback below binds by walking
    // that mapper list -- it never inserts on a miss, so a callback registered
    // before the parse would bind to nothing.
    im->LoadConfigFile("Input/Input.txt");

    // --- Section B: 16-zone loop, v1.6.1 GameTaskInitInput @ 0x001cae0c ---
    //
    // Per slot: zero the finger position, allocate one pooled type-3 entity
    // (EntityFactory case 3 -> new SlashEntity) through ActorManager::Add(3,
    // true), Init it at that zero position, then register the three per-finger
    // action callbacks. Every touch reaches a blade through those callbacks --
    // SlashEntity does not subscribe to the InputManager itself, in the binary
    // or here.
    //
    // Only slots 0..7 are ever driven in v1.6.1 (Touch::SendIndividualTouchCallbacks
    // @0x00242bc4 walks 8 states1 slots). Slots 8..15 are allocated and
    // registered and then never fire -- keep them, they are binary-faithful.
    _Vector3<float> defaultPos(0.0f, 0.0f, 0.0f);  // GOT[+0x77cc]
    for (int i = 0; i < 16; ++i) {
        game_work.m_FingerSpawnPos[i] = defaultPos;

        SlashEntity* e = static_cast<SlashEntity*>(
            Mortar::ActorManager::GetInstance()->Add(3, true));
        g_pSlashEntities[i] = e;

        // Binary: Mortar::Entity::vtable[+0x08] called as (nullptr, 0, &initPos)
        // with initPos = the same zero vec3. The port wrapper stamps the finger
        // index first (needed by the FN::g_MotionMode pointer-channel gate) and
        // then makes that 3-arg call; SlashEntity::Init ignores all three args.
        e->Init(i);

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
// All of these were re-decompiled against v1.6.1.

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
// keycode ranges. The two per-finger ranges are tested independently (NOT
// else-if); they are disjoint, so at most one fires.
//
// DIFFERS: original = px/py re-centred from raw top-left Y-down device pixels
//   (px = v - W/2, py = H/2 - v, dimensions from DisplayManager vtable +0x40)
//   (v1.6.1 PointerMoveCallback @0x001cbfcc), using ev->m_Value unchanged
//   because the port already re-centres and flips Y one layer earlier, in
//   Layout::TouchToGame (see InputTranslatorSDL::TransformTouchNormalized)
//   before the value ever reaches Mortar::Touch. Exactly one side does the
//   centring -- applying it here as well would double-transform every touch.
//
// `n` is the ACTION CHANNEL, which IS the Mortar::Touch::states1 slot index:
// Touch::SendIndividualTouchCallbacks @0x00242bc4 walks states1 and derives
// 0x89+slot / 0x99+slot / 0xa9+slot from the slot it is standing on. So
// m_FingerSpawnPos[n] here and the slot every UI widget gets back from
// TouchInRegion are the same index. (Before the mapper chain landed, the port's
// translator derived the channel from extId-1 instead, and the two disagreed.)
//
// Returns true (binary returns 1). CheckActions @0x002757fc discards the return,
// so it consumes nothing.
bool PointerMoveCallback(InputEvent* ev) {
    if (!ev) return false;

    // Binary InputEvent +0x08 -- the single axis-value word. See
    // InputDevice::AxisEvent @0x0027582c and the emitter in InputDeviceBada.cpp.
    const float px = ev->m_Value;
    const float py = ev->m_Value;

    const uint16_t kc = ev->m_KeyCode;

    if (kc == INPUT_KEY_MOUSEAXISX) {
        game_work.worldPos.x = px;
    } else if (kc == INPUT_KEY_MOUSEAXISY) {
        game_work.worldPos.y = py;
    }

    // ASM-spec v1.6.1: this sink is ALWAYS NULL, in the binary as well as here, so
    // the sink-first branches below are dead in both. That is faithful, not a gap.
    // The only writer of game_work.m_pActiveTouchSink (+0x1AC) in the whole image
    // is FruitNinjaNewsControl::StartNewsRender @0x001a2074, and that is an
    // UNREACHABLE RELIC: its sole caller is the GOT veneer @0x00102a80, whose sole
    // caller is NetworkManager::StartNewsDisplay @0x0023132c, which has no real
    // caller anywhere. The second route is closed too -- NetworkManager::Update
    // @0x002310c8 only reaches the news renderer when m_NewsRenderWanted is true,
    // and that flag is set in exactly one place: inside StartNewsDisplay itself.
    // Verified 2026-08-06; do NOT "fix" the dead branches by deleting them
    // (stub-don't-skip) or by inventing a sink installer.
    Mortar::InputSink* sink = game_work.m_pActiveTouchSink;

    // TouchAxisX1..16 (0x99..0xa8).
    unsigned int n = (unsigned int)(uint16_t)(kc - INPUT_KEY_TOUCHAXISX1);
    if (n < 16) {
        if (sink) {
            sink->TouchMoveX(ev, &game_work.m_FingerSpawnPos[n]);
        } else if (!BladeInputSuppressed()) {
            g_pSlashEntities[n]->TouchMoveX(ev);
        }
        game_work.m_FingerSpawnPos[kc - INPUT_KEY_TOUCHAXISX1].x = px;
    }

    // TouchAxisY1..16 (0xa9..0xb8). The store order really is asymmetric with
    // the X block above: the binary stores BEFORE the sink call and AFTER the
    // blade call, so the sink sees the fresh y and the blade does not. Do not
    // "tidy" this into one store.
    n = (unsigned int)(uint16_t)(kc - INPUT_KEY_TOUCHAXISY1);
    if (n < 16) {
        if (sink) {
            game_work.m_FingerSpawnPos[n].y = py;
            sink->TouchMoveY(ev, &game_work.m_FingerSpawnPos[n]);
        } else {
            if (!BladeInputSuppressed()) {
                g_pSlashEntities[n]->TouchMoveY(ev);
            }
            game_work.m_FingerSpawnPos[kc - INPUT_KEY_TOUCHAXISY1].y = py;
        }
    }

    return true;
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
// No Game::GetInstance, no null test. The blade dispatch is omitted here because
// nothing ever raises this action (see below); only the game_work writes remain.
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

// ASM-spec v1.6.1 TouchDownCallback @ 0x001cbf18 -- registered for
// "TouchDown_<i>" in the zone loop above. The finger index comes from the BUTTON
// key id at InputEvent +0x08 (m_KeyId, Touch1 = 0x89), not from the axis key
// code at +0x06.
//
//   uint n = ev->m_KeyId - 0x89;
//   if (n < 0x10) {
//       if (!sink || !InputSink::TouchDown(sink, ev, &m_FingerSpawnPos[n]))
//           SlashEntity::TouchDown(inputEnts[n], ev);
//       float& z = m_FingerSpawnPos[n].z;
//       z = (z < 0.0f) ? 2.0f : 1.0f;
//   }
//   return 1;
//
// The sink gets first refusal and the blade runs only when the sink declines
// (returns 0). The z stamp happens either way -- z is the finger's age counter
// that GameUpdate's loop ages down (2/1 -> 0 -> -1), so 2 means "press edge"
// (previous value had already aged past 0) and 1 means "still held".
//
// This fires EVERY tick a finger is held, not just on the press edge: the
// binary's ButtonPressed(Touch<n>, 2, ...) is emitted per poll from
// Touch::SendIndividualTouchCallbacks @0x00242bc4. SlashEntity::TouchDown
// @0x001ea420 relies on that -- its unconditional tail call to UpdateTouchDown
// is what feeds the trail, and its `m_BladeActive == 0` Reset gate is only
// self-clearing because the call repeats every frame.
bool TouchDownCallback(InputEvent* ev) {
    if (!ev) return false;

    unsigned int n = (unsigned int)(ev->m_KeyId - INPUT_KEY_TOUCH1);
    if (n < 16) {
        Mortar::InputSink* sink = game_work.m_pActiveTouchSink;
        if (!sink || !sink->TouchDown(ev, &game_work.m_FingerSpawnPos[n])) {
            if (!BladeInputSuppressed()) {
                g_pSlashEntities[ev->m_KeyId - INPUT_KEY_TOUCH1]->TouchDown(ev);
            }
        }
        float& z = game_work.m_FingerSpawnPos[n].z;
        z = (z < 0.0f) ? 2.0f : 1.0f;
    }
    return true;
}
