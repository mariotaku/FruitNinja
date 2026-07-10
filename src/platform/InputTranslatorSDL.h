#ifndef FN_INPUT_TRANSLATOR_SDL_H
#define FN_INPUT_TRANSLATOR_SDL_H

//
// InputTranslatorSDL -- converts SDL touch events to Mortar InputEvents.
//
// Maps SDL finger IDs to the 16-channel touch system used by the original.
// Each channel has actions: TouchDown_N, TouchMove_XN, TouchMove_YN, TouchUp_N.
// Also generates global "TouchScreen" events for any touch.
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

#include <SDL.h>
#include "input/InputManager.h"
#include "render/Renderer.h"

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
    static const int MOUSE_CHANNEL = 15;

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

    // Convert normalized SDL touch coords to game coords (centred ortho).
    void TransformTouchNormalized(float nx, float ny, float& gx, float& gy);

    // Map SDL finger ID to channel (0-15)
    int MapFingerId(SDL_FingerID id);
    void ReleaseFingerId(SDL_FingerID id);

    SDL_FingerID fingerMap[16];

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
#endif
};

#endif
