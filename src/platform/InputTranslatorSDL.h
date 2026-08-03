#ifndef FN_INPUT_TRANSLATOR_SDL_H
#define FN_INPUT_TRANSLATOR_SDL_H

//
// InputTranslatorSDL -- converts SDL touch events to Mortar InputEvents.
//
// Maps SDL finger IDs to the 16-channel touch system used by the original.
// Each channel has actions: TouchDown_N, TouchMove_XN, TouchMove_YN, TouchUp_N.
// Also generates global "TouchScreen" events for any touch.
//
// DIFFERS: original = 8 dispatchable fingers. Data/input/input.txt declares 16
//   TouchDown_/TouchReleased_/TouchMove_X/Y channels, but the producer side caps
//   at 8: Touch::SendIndividualTouchCallbacks @0x00242bc4 loops key codes
//   0x89..0x90 only, and Mortar::Touch itself has 8 slots (Touch::FindTouch
//   @0x002429a8 loops i<8). Channels 8-15 therefore have mappers that nothing
//   ever raises in v1.6.1. The port keeps all 16 because MOUSE_CHANNEL is carved
//   out of the 8-15 overflow range so a desktop mouse never steals a real
//   finger's Mortar::Touch slot -- a host-input nicety with no gameplay effect
//   (a Bada device could not deliver a 9th finger anyway).
//
// DIFFERS: "TouchScreen" is PORT-ONLY. No mapper for it exists in v1.6.1: the
//   name is absent from Data/input/input.txt, so InputManager::LoadConfigFile
//   @0x002442fc never creates one. Its only binary appearance is on the
//   frontend.txt / SplashInit path, which is unreachable dead code (the v1.6.1
//   dispatch table at 0x002cc130 registers no Splash/Frontend handlers -- see
//   src/game/SplashTask.cpp). Nothing in the port subscribes to it either; it is
//   dispatched purely for shape.
//
// Mouse-only desktop platforms are handled by setting
// SDL_HINT_MOUSE_TOUCH_EVENTS=1 before SDL_Init, which makes SDL synthesize
// SDL_FINGER* events from SDL_MOUSE* with finger id = SDL_TOUCH_MOUSEID.
// This file therefore only handles SDL_FINGERDOWN/MOTION/UP.
//
// Port specific: the mouse is a dedicated "finger" pinned to MOUSE_CHANNEL
// (see below) and never shares a channel with a real touch, nor vice-versa.
// The binary is touch-only Bada hardware -- there is no mouse device there,
// so this reservation is host-only SDL glue.
//
// Port specific: MOTION MODE (FN::g_MotionMode, src/debug/DebugFlags.h;
// toggle F5 / --motion; default OFF; host-only, no binary equivalent).
// When ON, MOUSE_CHANNEL is driven from RAW SDL_MOUSEMOTION /
// SDL_MOUSEBUTTONDOWN / SDL_MOUSEBUTTONUP instead of the SDL-synthesized
// SDL_FINGER* events (SDL_HINT_MOUSE_TOUCH_EVENTS=1 still synthesizes those
// alongside the raw events; DrainSDLEvent suppresses the synthesized ones
// for fingerId==SDL_TOUCH_MOUSEID while motion mode is ON so the two paths
// never double-drive the channel). Real touch fingers (channels 0-14) are
// unaffected in both modes. This file only does CURSOR TRACKING -- the
// blade always follows the cursor; the velocity gate that decides whether
// a fast-enough movement actually cuts lives in SlashEntity::Update (so the
// trail keeps animating while the user is merely aiming).
//   - Hovering with the cursor inside the window and no button held keeps
//     MOUSE_CHANNEL pressed and moving -- the blade follows the cursor.
//   - Pressing any mouse button LIFTS the blade (releases MOUSE_CHANNEL) so
//     the user can reposition without cutting. (v1 placeholder for the
//     button role -- final choice TBD, this mirrors the old always-cut mode.)
//   - Releasing the last held button re-presses MOUSE_CHANNEL at the
//     current position (if the cursor is still inside the window).
//   - The cursor leaving the window releases MOUSE_CHANNEL.
// When OFF, behaviour is identical to the pre-motion-mode synthesized-finger path.
//
// Refresh-rate-independent dispatch (Mortar::Touch::Update @0x00242d14):
//
//   DrainSDLEvent() -- called once per display frame from pollInput().
//     For channels 0-7 (Mortar::Touch slots): IMMEDIATELY pushes each event
//     into the Mortar::Touch ring buffer via OnPressed/OnMoved/OnReleased.
//     This means a DOWN followed by an UP within the same drain window both
//     enter the ring and are applied in order -- neither edge is lost.
//     For channels 8-15 (beyond Touch capacity): accumulates pendingDown/Up
//     bools as before (these channels have no binary Touch equivalent).
//     Focus-loss / WINDOW events still fire ReleaseAllFingers() immediately.
//
//   DispatchForSimTick() -- called once per sim tick from stepUpdate(), BEFORE
//     GameTaskUpdate. Calls Touch::Update(0.0f) to drain the ENTIRE ring buffer
//     accumulated since last tick (binary-faithful: Mortar::Touch::Update with
//     dt=0 drains all queued events in order). Then reads drained states1 to
//     drive InputManager hash events per channel.
//
// At 120Hz: two drains (RAF ~8.3ms each) feed one dispatch tick (~16.7ms).
// Both drains' events sit in the ring; DispatchForSimTick drains the whole ring
// in one pass -- no edge is lost regardless of arrival rate.
// At 60Hz: one drain feeds one dispatch (1:1, same behaviour as before).
//
// Press-vs-motion gate (blade events): TouchMove_XN/YN are emitted ONLY once a
// real SDL_FINGERMOTION has been drained for that finger since its FINGERDOWN
// (per-channel motionSinceDown flag). A stationary TAP (DOWN..UP with no
// MOTION) emits TouchScreen + TouchDown_N + TouchUp_N and NO TouchMove, so the
// blade (SlashEntity) never receives a tap's position and consecutive taps can
// never bridge into a slash -- v1.6.1 semantics: a tap alone never moves the
// blade; only finger motion does. Companion port change: SlashEntity::TouchDown
// seeds a NEW stroke from the TouchDown event's position (see SlashEntity.h),
// since no press-frame TouchMove delivers it anymore.
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
// DrainSDLEvent's SDL_FINGERMOTION case now detects the IN<->OUT crossing
// per channel (IsOutOfWindow on the raw normalized nx/ny SDL delivers,
// BEFORE Layout::TouchToGame's viewport math):
//   - IN -> OUT: synthesizes a release (Touch::OnReleased / pendingUp) at
//     the LAST in-bounds position, then marks the channel "suspended": the
//     SDL finger-id mapping is KEPT (fingerActive stays true so the same
//     physical finger can't be reassigned to a new channel) but no further
//     motion for it is fed to Touch/pending state while suspended.
//   - OUT -> IN: synthesizes a fresh press (Touch::OnPressed / pendingDown +
//     pendingEdge, motionSinceDown reset) at the new in-bounds position --
//     a brand-new blade stroke, mirroring a real SDL_FINGERDOWN.
//   - A real SDL_FINGERUP while suspended just clears the channel mapping
//     (no double release -- the release already fired on the OUT crossing).
// Multi-finger safe: tracked per channel (fingerSuspended[16]), independent
// of every other finger.
//
// Web (emscripten): this C++ logic is shared and applies as-is to
// canvas-native fingers (those whose touchstart hit the #canvas element --
// SDL's own listeners own their whole stream, same unclamped-coordinate
// shape as desktop). Fingers that STARTED outside the canvas are instead
// forwarded via shell.html's document-capture IIFE + fn_web_synth_touch
// (mainEmscripten.cpp) -- that JS layer used to clamp01() the forwarded
// coordinate at the canvas edge, which masked the crossing before it ever
// reached C++. It now forwards the true (unclamped, can exceed [0,1])
// coordinate instead so this same DrainSDLEvent logic sees the crossing
// uniformly for both finger populations.
//

#include <SDL.h>
#include "input/InputManager.h"
#include "render/Renderer.h"
#include "debug/DebugFlags.h"

class InputTranslatorSDL {
public:
    // Port specific: fixed channel reserved exclusively for the mouse
    // (SDL_TOUCH_MOUSEID). All mouse buttons collapse onto this one channel
    // -- one mouse == one finger. Chosen inside the 8-15 "overflow" range
    // (channels beyond Mortar::Touch::MAX_SLOTS=8) so real touch keeps its
    // full 8-finger Mortar::Touch capacity (channels 0-7) unchanged; touch
    // overflow (channels 8-15) loses one slot (8-14 remain). Channel 15
    // still reaches a real blade: DispatchForSimTick's channel 8-15 loop
    // dispatches TouchDown_15/TouchMove_X15/Y15/TouchUp_15 through
    // InputManager exactly like channels 0-7, and
    // SlashEntity::RegisterInputCallbacks subscribes g_pSlashEntities[15]
    // (GameInit's 16-blade init loop) to those same hashes.
    static const int MOUSE_CHANNEL = FN::POINTER_FINGER_CHANNEL;

    // Pre-computed action hashes for 16 touch channels
    uint32_t hashTouchDown[16];
    uint32_t hashTouchMoveX[16];
    uint32_t hashTouchMoveY[16];
    uint32_t hashTouchUp[16];
    uint32_t hashTouchScreen;

    // Track finger positions for delta calculation
    float fingerX[16];
    float fingerY[16];
    bool fingerActive[16];

    InputTranslatorSDL();

    // Initialize action hashes (call once after StringHash is available)
    void Init();

    // drain one SDL event into Mortar::Touch ring buffer (channels 0-7)
    // and into per-channel state (all 16). For channels 0-7, FINGERDOWN /
    // MOTION / UP are pushed immediately into Mortar::Touch via
    // OnPressed/OnMoved/OnReleased -- preserving all edges in order.
    // For channels 8-15, falls back to the pending-bool model (no Touch slot).
    // Non-touch events (WINDOW/FOCUS/keyboard) are handled inline.
    // Called from pollInput() for every SDL_PollEvent result.
    void DrainSDLEvent(const SDL_Event& ev, SDL_Window* window);

    // drain the Mortar::Touch ring buffer and dispatch InputManager hash
    // events for one sim tick.  Calls Touch::Update(0.0f) which drains ALL
    // queued events (binary-faithful: dt==0 skips the timestamp guard and
    // pops the entire ring). Then reads the drained states1 to emit
    // hashTouchDown/Move/Up events to InputManager.
    // Called from stepUpdate() BEFORE GameTaskUpdate.
    void DispatchForSimTick();

    // legacy wrapper -- retained so existing callers that invoke
    // BeginFrame() are not broken. No-op now; dispatch is via DispatchForSimTick.
    void BeginFrame();

    // synthesize TouchUp for every held finger and release all channels.
    // Flushes + clears the Mortar::Touch ring buffer. Called on SDL focus-loss
    // / minimize to clear blade state before the frame that runs while the
    // app is backgrounded (#162).
    void ReleaseAllFingers();

    // legacy wrapper for ProcessSDLEvent -- retained for callers
    // (scene_slash, scene_slash_blade) that forward events directly. Calls
    // DrainSDLEvent internally.
    void ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window);

private:
    // Per-channel "was active at last DispatchForSimTick" snapshot.
    // Used to detect release edges for the InputManager hash path:
    //   prevActive[ch]==true and now inactive -> emit hashTouchUp.
    // For channels 0-7 this is derived from states1 after Touch::Update(0.0f).
    // For channels 8-15 it mirrors fingerActive directly.
    bool prevActive[16];

    // Pending bools for channels 8-15 ONLY (no Mortar::Touch slot).
    // For channels 0-7 these are unused; ring buffer preserves all edges.
    bool pendingDown[16];
    bool pendingUp[16];
    bool pendingEdge[16];

    // Port specific: per-channel press-vs-motion gate. false on FINGERDOWN,
    // true once a real FINGERMOTION drains for that finger. TouchMove_XN/YN
    // are only dispatched while true -- a stationary tap emits no blade move
    // (v1.6.1: only real finger motion moves the blade).
    bool motionSinceDown[16];

    // Port specific: out-of-window release/re-press (desktop mouse-drag only;
    // the Bada touchscreen never reports a touch outside its own bounds, so
    // this has no fidelity concern -- see the header comment block above
    // DrainSDLEvent's SDL_FINGERMOTION case for the full rationale).
    // true once a MOTION for this channel lands outside the window's [0,1]
    // normalized rect. While true, the channel keeps its SDL finger-id
    // mapping (fingerActive stays true so the physical finger isn't
    // reassigned to a new channel) but is released from the engine's POV
    // (Touch::OnReleased / pendingUp already fired on the OUT crossing) --
    // further MOTION events for this channel are NOT fed to Touch/pending
    // state until it re-enters and gets a fresh synthesized press.
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
    // Mirror the FINGERDOWN / FINGERUP handling DrainSDLEvent already does
    // for overflow channels (8-15), reused here so the channel-8..15
    // pending-bool model (see DispatchForSimTick) stays the single source
    // of truth for how a press/release reaches InputManager.
    // PointerPressMouseChannel is a no-op if MOUSE_CHANNEL is already active.
    void PointerPressMouseChannel(float gx, float gy);
    // PointerReleaseMouseChannel is a no-op if MOUSE_CHANNEL is not active.
    void PointerReleaseMouseChannel();

#ifdef FN_TEST
public:
    // Test-seam: expose state for unit-test assertions.
    // Only compiled in when FN_TEST is defined (test targets only).
    // pendingDown/Up/Edge are only meaningful for ch >= 8 in the new model;
    // for ch 0-7 use TestGetTouchPhase() to read the ring-drained state.
    bool TestGetPendingDown(int ch) const { return ch >= 0 && ch < 16 ? pendingDown[ch] : false; }
    bool TestGetPendingUp(int ch)   const { return ch >= 0 && ch < 16 ? pendingUp[ch]   : false; }
    bool TestGetPendingEdge(int ch) const { return ch >= 0 && ch < 16 ? pendingEdge[ch] : false; }
    bool TestGetFingerActive(int ch) const { return ch >= 0 && ch < 16 ? fingerActive[ch] : false; }
    float TestGetFingerX(int ch) const { return ch >= 0 && ch < 16 ? fingerX[ch] : 0.0f; }
    float TestGetFingerY(int ch) const { return ch >= 0 && ch < 16 ? fingerY[ch] : 0.0f; }
    bool TestGetPrevActive(int ch) const { return ch >= 0 && ch < 16 ? prevActive[ch] : false; }
    bool TestGetMotionSinceDown(int ch) const { return ch >= 0 && ch < 16 ? motionSinceDown[ch] : false; }
    bool TestGetFingerSuspended(int ch) const { return ch >= 0 && ch < 16 ? fingerSuspended[ch] : false; }
#endif
};

#endif
