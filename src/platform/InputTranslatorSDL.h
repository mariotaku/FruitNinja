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
// Port specific: binary is a strict 1:1 input->update->draw tick (the Bada
// OS polls touch once per frame). The SDL port separates this into two phases:
//
//   DrainSDLEvent() -- called once per display frame from pollInput().
//     Accumulates FINGERDOWN/MOTION/UP into per-channel pending state
//     (down/up edges + latest position). Does NOT dispatch to InputManager
//     so m_PointCount does not advance outside a sim tick.
//     Focus-loss / WINDOW events still fire ReleaseAllFingers() immediately
//     from pollInput() (#162).
//
//   FlushForSimTick() -- called once per sim tick from stepUpdate(), BEFORE
//     GameTaskUpdate. Dispatches the accumulated pending state: TouchDown/Up
//     edges + one TouchMove per active channel (mirrors the binary's
//     once-per-tick poll). Also runs the PollHeldFingers stuck-blade
//     reconcile (#154) which now runs per tick (more correct than per-frame).
//     Catch-up frames (steps>=2): pending edges are applied on the FIRST step
//     only; subsequent steps deliver one TouchMove at the channel's current
//     position (or nothing if no finger is down).
//
// Invariant: m_PointCount (SlashEntity::AddPoint) only advances inside a
// tick that also runs UpdatePoints (which reconciles the head-cap vertex),
// so DrawSlice never draws a stale head-cap to origin.
//

#include <SDL.h>
#include "input/InputManager.h"
#include "render/Renderer.h"

class InputTranslatorSDL {
public:
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

    // Port specific: drain one SDL event into per-channel pending state.
    // TOUCH events (FINGERDOWN/MOTION/UP + mouse-as-touch) accumulate into
    // pendingDown/pendingUp/pendingPos; they are NOT dispatched to InputManager
    // here.  Non-touch events (WINDOW/FOCUS/keyboard/mouse-button) are handled
    // inline as before.  Called from pollInput() for every SDL_PollEvent result.
    void DrainSDLEvent(const SDL_Event& ev, SDL_Window* window);

    // Port specific: flush accumulated touch state to InputManager for one sim tick.
    // Dispatches pending TouchDown/Up edges for each channel, then one TouchMove
    // at the channel's current position. Runs the PollHeldFingers stuck-blade
    // reconcile (#154). Called from stepUpdate() BEFORE GameTaskUpdate.
    // On the first flush after pending edges arrive, edges are consumed (cleared).
    // On subsequent flushes in the same display frame (catch-up steps >= 2),
    // only a held-position TouchMove is dispatched (edges already consumed).
    void FlushForSimTick();

    // Port specific: legacy wrapper -- calls FlushForSimTick(). Retained so
    // existing callers that invoke BeginFrame() directly are not broken.
    void BeginFrame();

    // Port specific: synthesize TouchUp for every held finger and release all
    // channels. Called on SDL focus-loss / minimize to clear blade state before
    // the frame that runs while the app is backgrounded (#162).
    void ReleaseAllFingers();

private:
    // Per-channel pending state, set by DrainSDLEvent, consumed by FlushForSimTick.
    // Port specific: these fields have no binary equivalent; they exist solely to
    // stage the once-per-tick dispatch that mirrors the binary's per-frame poll.
    bool pendingDown[16];   // a FINGERDOWN arrived for this channel since last flush
    bool pendingUp[16];     // a FINGERUP arrived for this channel since last flush
    bool pendingEdge[16];   // was this a first-press edge (INPUT_ACTION_DOWN_EDGE)?

    // Port specific: run the stuck-blade (#154) reconcile: check SDL live-finger
    // set and synthesize TouchUp for any held channel SDL no longer reports.
    // Moved from BeginFrame into FlushForSimTick so the reconcile happens at the
    // same cadence as the dispatch (once per sim tick).
    // Guard: when SDL_GetNumTouchDevices()==0 (no real touch hardware, e.g. test
    // harnesses using SDL_PushEvent), skip the live-set check for non-MOUSEID
    // channels -- synthetic injections do not register fingers with SDL's touch
    // tracking, so checking the empty live set would incorrectly release them.
    void PollHeldFingers();

    // Convert normalized SDL touch coords to game coords (centred ortho).
    void TransformTouchNormalized(float nx, float ny, float& gx, float& gy);

    // Map SDL finger ID to channel (0-15)
    int MapFingerId(SDL_FingerID id);
    void ReleaseFingerId(SDL_FingerID id);

    SDL_FingerID fingerMap[16];

#ifdef FN_TEST
public:
    // Test-seam: expose pending state for unit-test assertions.
    // Only compiled in when FN_TEST is defined (test targets only).
    bool TestGetPendingDown(int ch) const { return ch >= 0 && ch < 16 ? pendingDown[ch] : false; }
    bool TestGetPendingUp(int ch)   const { return ch >= 0 && ch < 16 ? pendingUp[ch]   : false; }
    bool TestGetPendingEdge(int ch) const { return ch >= 0 && ch < 16 ? pendingEdge[ch] : false; }
    bool TestGetFingerActive(int ch) const { return ch >= 0 && ch < 16 ? fingerActive[ch] : false; }
    float TestGetFingerX(int ch) const { return ch >= 0 && ch < 16 ? fingerX[ch] : 0.0f; }
    float TestGetFingerY(int ch) const { return ch >= 0 && ch < 16 ? fingerY[ch] : 0.0f; }
#endif
};

#endif
