#ifndef FN_INPUT_TRANSLATOR_SDL_H
#define FN_INPUT_TRANSLATOR_SDL_H

//
// InputTranslatorSDL -- feeds SDL touch/mouse events into Mortar::Touch.
//
// It raises NO action events. The binary's own poll does that, once per sim
// tick, entirely inside engine + game code:
//
//   Game::stepUpdate
//     -> InputTranslatorSDL::DispatchForSimTick   (this class: drain the ring)
//     -> GameTaskUpdate -> GameUpdate @0x001cf644
//        -> InputManager::Update @0x00243838
//           -> InputDeviceBada::Update @0x00242f40
//              global pointer:  AxisEvent(0x74/0x75), ButtonPressed(0x6c)
//              -> Touch::SendIndividualTouchCallbacks @0x00242bc4
//                 per slot i (0..7), key K = 0x89 + i:
//                   active:   AxisEvent(K+0x10, x, dx), AxisEvent(K+0x20, y, dy),
//                             ButtonPressed(K, 1) on the press edge, then
//                             ButtonPressed(K, 2)
//                   inactive: ButtonPressed(K, 4) on the release edge, then
//                             ButtonPressed(K, 8)
//              -> InputDevice::CheckActions -> InputActionMapper::ProcessEvent
//                 -> the callback GameTaskInitInput registered for that action.
//
// The mappers come from InputManager::LoadConfigFile @0x002442fc parsing
// Input/Input.txt. So the ACTION CHANNEL a finger drives is its Mortar::Touch
// SLOT index, not the SDL channel number below.
//
// EDGE-LATCH / POLL-REPLAY: the binary polls the touch hardware every frame;
// SDL pushes edges instead. Mortar::Touch's ring buffer is the adapter --
// DrainSDLEvent latches each SDL edge into the ring, and Touch::Update replays
// the accumulated edges into the polled states1 snapshot that
// SendIndividualTouchCallbacks walks. That IS the shim, and it lives entirely
// in Mortar::Touch; this class only pushes into it.
//
// Channels: this class maps SDL finger IDs onto 16 channels, but they are only
// an SDL-side identity for finger tracking / out-of-window bookkeeping. Every
// channel pushes into Mortar::Touch with extId = channel + 1, and Touch's own
// 8 slots decide what the game sees. A 9th concurrent pointer finds no free
// slot and is dropped -- binary-faithful (Mortar::Touch has 8 slots,
// Touch::FindTouch @0x002429a8 loops i<8; GlesForm::OnTouch* gate
// GetPointId()<8 @0x001f1128/0x001f11c4/0x001f10a0).
//
// Mouse-only desktop platforms are handled by setting
// SDL_HINT_MOUSE_TOUCH_EVENTS=1 before SDL_Init, which makes SDL synthesize
// SDL_FINGER* events from SDL_MOUSE*. This file therefore mostly handles
// SDL_FINGERDOWN/MOTION/UP.
//
// Port specific: MOUSE_CHANNEL is a channel reserved so a real touch and the
// mouse can never steal each other's finger-id mapping. The binary is
// touch-only Bada hardware -- there is no mouse device there, so this
// reservation is host-only SDL glue.
//
// Port specific: MOTION MODE (FN::g_MotionMode, src/debug/DebugFlags.h;
// toggle F5 / --motion; default OFF; host-only, no binary equivalent).
// When ON, MOUSE_CHANNEL is driven from RAW SDL_MOUSEMOTION /
// SDL_MOUSEBUTTONDOWN / SDL_MOUSEBUTTONUP instead of the SDL-synthesized
// SDL_FINGER* events (DrainSDLEvent suppresses the synthesized ones for
// fingerId==SDL_TOUCH_MOUSEID while motion mode is ON so the two paths never
// double-drive the channel). Real touch fingers are unaffected in both modes.
// This file only does CURSOR TRACKING -- the blade always follows the cursor;
// the velocity gate that decides whether a fast-enough movement actually cuts
// lives in SlashEntity::Update (see FN::MOTION_GATE_CHANNEL_MIN/MAX).
//   - Hovering with the cursor inside the window and no button held keeps
//     MOUSE_CHANNEL pressed and moving -- the blade follows the cursor.
//   - Pressing any mouse button LIFTS the blade (releases MOUSE_CHANNEL) so
//     the user can reposition without cutting.
//   - Releasing the last held button re-presses MOUSE_CHANNEL at the
//     current position (if the cursor is still inside the window).
//   - The cursor leaving the window releases MOUSE_CHANNEL.
// When OFF, behaviour is identical to the synthesized-finger path.
//
// Refresh-rate independence (#175):
//
//   DrainSDLEvent()      -- once per display frame from pollInput(). Pushes
//     each event into the Mortar::Touch ring IMMEDIATELY, so a DOWN followed by
//     an UP within the same drain window both land in the ring and neither edge
//     is lost. Focus-loss / WINDOW events still fire ReleaseAllFingers().
//
//   DispatchForSimTick() -- once per sim tick from stepUpdate(), BEFORE
//     GameTaskUpdate. Calls Touch::Update(0.0f), which drains the ENTIRE ring
//     accumulated since the last tick (binary-faithful: dt==0 skips the
//     timestamp guard). Ordering matters -- states1 must be fresh before
//     GameUpdate's InputManager::Update polls it.
//
// At 120Hz: two drains (RAF ~8.3ms each) feed one dispatch tick (~16.7ms).
// Both drains' events sit in the ring; the tick drains the whole ring in one
// pass. At 60Hz: one drain feeds one dispatch (1:1).
//
// Invariant: m_PointCount (SlashEntity::AddPoint) only advances inside a
// tick that also runs UpdatePoints (which reconciles the head-cap vertex),
// so DrawSlice never draws a stale head-cap to origin (#168 / #173 fix).
//
// Port specific: out-of-window release/re-press. The Bada touchscreen never
// reports a touch outside its own bounds, so this is a host/web-only input
// nicety with no fidelity concern -- it replaces what would otherwise be an
// unclamped-coordinate blade (desktop: SDL keeps delivering SDL_FINGERMOTION
// with the OS mouse captured while a button is held, so nx/ny legitimately
// go outside [0,1]; web native-canvas touch: browsers bind an entire touch
// stream to its touchstart target, so a finger dragged off the canvas keeps
// generating touchmove-driven SDL_FINGERMOTION with out-of-[0,1] coords).
// Previously these unclamped coords fed straight through TouchToGame, so the
// blade would fly to whatever off-field position they mapped to.
//
// DrainSDLEvent's SDL_FINGERMOTION case detects the IN<->OUT crossing per
// channel (IsOutOfWindow on the raw normalized nx/ny SDL delivers, BEFORE
// Layout::TouchToGame's viewport math):
//   - IN -> OUT: pushes a Touch release at the LAST in-bounds position, then
//     marks the channel "suspended": the SDL finger-id mapping is KEPT
//     (fingerActive stays true so the same physical finger can't be reassigned
//     to a new channel) but no further motion for it is fed to Touch.
//   - OUT -> IN: pushes a fresh Touch press at the new in-bounds position --
//     a brand-new blade stroke, mirroring a real SDL_FINGERDOWN.
//   - A real SDL_FINGERUP while suspended just clears the channel mapping
//     (no double release).
// Multi-finger safe: tracked per channel (fingerSuspended[16]).
//
// Web (emscripten): this C++ logic is shared and applies as-is to
// canvas-native fingers. Fingers that STARTED outside the canvas are forwarded
// via shell.html's document-capture IIFE + fn_web_synth_touch
// (mainEmscripten.cpp), which forwards the true (unclamped) coordinate so this
// same logic sees the crossing uniformly for both finger populations.
//

#include <SDL.h>
#include "input/InputManager.h"
#include "render/Renderer.h"
#include "debug/DebugFlags.h"

class InputTranslatorSDL {
public:
    // Port specific: channel reserved exclusively for the mouse
    // (SDL_TOUCH_MOUSEID) so a real touch and the mouse never share a channel
    // -- one mouse == one finger. It still gets a real Mortar::Touch slot like
    // any other channel; only the SDL-side finger-id bookkeeping is reserved.
    static const int MOUSE_CHANNEL = FN::POINTER_FINGER_CHANNEL;

    // Latest game-space position per channel. Bookkeeping only -- the position
    // the game acts on is the one in the Mortar::Touch slot.
    float fingerX[16];
    float fingerY[16];
    bool fingerActive[16];

    InputTranslatorSDL();

    // Currently only lowers the SDL log cutoff under FN_DEBUG_TOUCH. Kept as a
    // call site because every platform main calls it after StringHash is up.
    void Init();

    // Drain one SDL event into the Mortar::Touch ring buffer (all 16 channels,
    // extId = channel + 1) and into per-channel SDL bookkeeping. Non-touch
    // events (WINDOW/FOCUS/keyboard) are handled inline.
    // Called from pollInput() for every SDL_PollEvent result.
    void DrainSDLEvent(const SDL_Event& ev, SDL_Window* window);

    // Drain the Mortar::Touch ring for one sim tick: Touch::Update(0.0f), which
    // pops every queued event and advances the slot state machine. The action
    // events themselves are raised later in the same tick by GameUpdate ->
    // InputManager::Update, so this MUST run before GameTaskUpdate.
    void DispatchForSimTick();

    // legacy wrapper -- retained so existing callers that invoke
    // BeginFrame() are not broken. No-op.
    void BeginFrame();

    // Release every held finger and clear all channels, then drain the ring.
    // Called on SDL focus-loss / minimize so no blade stays armed across a
    // background/restore cycle (#162).
    void ReleaseAllFingers();

    // legacy wrapper for ProcessSDLEvent -- retained for callers
    // (scene_slash, scene_slash_blade) that forward events directly. Calls
    // DrainSDLEvent internally.
    void ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window);

private:
    // Port specific: out-of-window release/re-press (see the header comment
    // block above). true once a MOTION for this channel lands outside the
    // window's [0,1] normalized rect. While true, the channel keeps its SDL
    // finger-id mapping (fingerActive stays true so the physical finger isn't
    // reassigned) but is released from the engine's POV -- further MOTION
    // events for it are NOT fed to Mortar::Touch until it re-enters and gets a
    // fresh synthesized press.
    bool fingerSuspended[16];

    // Convert normalized SDL touch coords to game coords (centred ortho).
    void TransformTouchNormalized(float nx, float ny, float& gx, float& gy);

    // Port specific: true when normalized SDL coords (as delivered by SDL --
    // window-relative, NOT viewport-relative) fall outside [0,1]. Used to
    // detect a mouse drag leaving/re-entering the window while a button is
    // held (SDL keeps delivering motion with the OS mouse capture engaged).
    static bool IsOutOfWindow(float nx, float ny);

    // Map SDL finger ID to channel (0-15)
    int MapFingerId(SDL_FingerID id);
    void ReleaseFingerId(SDL_FingerID id);

    SDL_FingerID fingerMap[16];

    // Port specific: MOTION MODE raw-mouse-drive helpers for MOUSE_CHANNEL.
    // PointerPressMouseChannel is a no-op if MOUSE_CHANNEL is already active.
    void PointerPressMouseChannel(float gx, float gy);
    // PointerReleaseMouseChannel is a no-op if MOUSE_CHANNEL is not active.
    void PointerReleaseMouseChannel();

#ifdef FN_TEST
public:
    // Test-seam: expose state for unit-test assertions.
    // Only compiled in when FN_TEST is defined (test targets only).
    // Channel-level press/release state now lives in Mortar::Touch::states1 --
    // read it there; these only expose the SDL-side bookkeeping.
    bool TestGetFingerActive(int ch) const { return ch >= 0 && ch < 16 ? fingerActive[ch] : false; }
    float TestGetFingerX(int ch) const { return ch >= 0 && ch < 16 ? fingerX[ch] : 0.0f; }
    float TestGetFingerY(int ch) const { return ch >= 0 && ch < 16 ? fingerY[ch] : 0.0f; }
    bool TestGetFingerSuspended(int ch) const { return ch >= 0 && ch < 16 ? fingerSuspended[ch] : false; }
#endif
};

#endif
