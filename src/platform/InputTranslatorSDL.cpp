//
// InputTranslatorSDL -- converts SDL events to Mortar InputEvents AND feeds
// Mortar::Touch directly (poll-based binary path).
//
// Port specific: binary is a strict 1:1 input->update->draw tick.
// This file splits touch dispatch into two phases:
//   DrainSDLEvent()  -- accumulate per-frame (no dispatch); called from pollInput()
//   FlushForSimTick() -- dispatch once per sim tick; called from stepUpdate()
// This ensures m_PointCount only advances inside a tick that also runs
// UpdatePoints (which reconciles the head-cap vertex), so DrawSlice never draws
// a stale head-cap to origin (#168 bridge-to-origin fix).
//

#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include <cstring>

#ifdef FN_DEBUG_TOUCH
#  include "debug/Logger.h"
#  define TLOG(fmt, ...) LOG_DEBUG("TOUCH", fmt, ##__VA_ARGS__)
#else
#  define TLOG(...) ((void)0)
#endif

// StringHash is provided by src/engine/util/StringHash.h (the binary-faithful
// Jenkins lookup3 with case-folding). Earlier this file had a local DJB2-like
// definition that produced different hashes for the same string -- causing
// SlashEntity event-driven dispatch to silently fail. Single source of truth now.

InputTranslatorSDL::InputTranslatorSDL() : hashTouchScreen(0) {
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(fingerMap, 0xFF, sizeof(fingerMap));
    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
}

void InputTranslatorSDL::Init() {
    char buf[32];

    // Pre-compute hashes matching original action names from game_input.txt
    for (int i = 0; i < 16; i++) {
        sprintf(buf, "TouchDown_%d", i);
        hashTouchDown[i] = StringHash(buf);

        sprintf(buf, "TouchMove_X%d", i);
        hashTouchMoveX[i] = StringHash(buf);

        sprintf(buf, "TouchMove_Y%d", i);
        hashTouchMoveY[i] = StringHash(buf);

        // TouchUp not in original config but we need it
        sprintf(buf, "TouchUp_%d", i);
        hashTouchUp[i] = StringHash(buf);
    }

    hashTouchScreen = StringHash("TouchScreen");
}

// Transform normalized SDL touch coords -> binary-centred ortho coords.
// Ortho: X in [-240, +240] (horizontal), Y in [-160, +160] (vertical, +up).
// SDL touch Y is top-down (0 at top), so we flip.
// Port convention: TOP = +160, BOTTOM = -160 (Y-up). Bada binary uses
// Y-down (TOP=-160); the discrepancy is handled locally by ScrollingMenu
// (its drag formula negates currY before applying the binary-faithful
// math). Other touch consumers (MenuButton hit-tests, SlashEntity blade
// tracking) work in port's Y-up convention directly.
//
// DIFFERS: original = GlesForm::TransformTouchPos @0x001f1038 (v1.6.1):
//   out.x=(int)(rawDeviceX*480.0/800.0); out.y=(int)(rawDeviceY*320.0/480.0)
//   -- pure anisotropic down-scale of raw portrait device pixels (480x800),
//   NO rotation, NO centring, top-left Y-down. Port takes normalized SDL [0,1]
//   coords -> centred Y-up ortho because the SDL pipeline works in centred
//   Y-up throughout.
void InputTranslatorSDL::TransformTouchNormalized(float nx, float ny,
                                                   float& gx, float& gy) {
    gx = nx * (float)FN_SCREEN_W - (float)(FN_SCREEN_W / 2);
    gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
    TLOG("TransformTouchNormalized raw=(%g,%g) -> game=(%g,%g)\n", nx, ny, gx, gy);
}

// DIFFERS: binary caps touch at 8 channels at the source (Mortar::Touch 8 slots,
// Touch::FindTouch @0x002429a8 loops i<8; GlesForm::OnTouch* gate GetPointId()<8
// @0x001f1128/0x001f11c4/0x001f10a0). Port uses 16 SDL channels then clamps to
// MAX_SLOTS=8 before Mortar::Touch -- benign superset.
int InputTranslatorSDL::MapFingerId(SDL_FingerID id) {
    // Check if already mapped
    for (int i = 0; i < 16; i++) {
        if (fingerActive[i] && fingerMap[i] == id)
            return i;
    }
    // Find free slot
    for (int i = 0; i < 16; i++) {
        if (!fingerActive[i]) {
            fingerMap[i] = id;
            fingerActive[i] = true;
            return i;
        }
    }
    return -1;  // all 16 channels busy
}

void InputTranslatorSDL::ReleaseFingerId(SDL_FingerID id) {
    for (int i = 0; i < 16; i++) {
        if (fingerActive[i] && fingerMap[i] == id) {
            fingerActive[i] = false;
            return;
        }
    }
}

// Port specific: legacy wrapper -- retained so any callers that invoke
// BeginFrame() are not broken. Now delegates to FlushForSimTick() which
// contains all the logic that was previously in PollHeldFingers().
void InputTranslatorSDL::BeginFrame() {
    FlushForSimTick();
}

// Port specific: synthesize TouchUp for every held finger and clear all channels.
// Called when the SDL window loses focus or is minimized so no blade stays armed
// across a background/restore cycle. Mirrors the per-finger release in PollHeldFingers
// and SDL_FINGERUP handling, applied to all active channels at once (#162).
void InputTranslatorSDL::ReleaseAllFingers() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    for (int ch = 0; ch < 16; ++ch) {
        if (!fingerActive[ch]) continue;

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
        }

        if (mgr) {
            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.actionHash  = hashTouchUp[ch];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId    = ch;
            ie.x = fingerX[ch];
            ie.y = fingerY[ch];
            mgr->DispatchEvent(&ie);
        }

        fingerActive[ch] = false;
    }

    // Also clear any pending drain state so the next flush doesn't re-fire.
    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
}

// Port specific: flush accumulated touch state to InputManager for one sim tick.
// Binary cadence: input->update->draw happens exactly once per tick.
// This function provides the "input" phase that runs at the start of each sim tick
// (before GameTaskUpdate), matching the binary's strict 1:1 tick ordering.
//
// Per tick:
//   1. PollHeldFingers reconcile (#154): synthesize TouchUp for any SDL-dropped fingers.
//   2. For each channel with a pending TouchDown edge: dispatch TouchScreen +
//      TouchMove + TouchDown (with INPUT_ACTION_DOWN_EDGE on press-edge).
//      Consumes and clears the pending edge so subsequent catch-up steps do not
//      re-fire the edge.
//   3. For each channel with a pending TouchUp: dispatch TouchUp, mark inactive.
//   4. For each still-active channel (no pending up): dispatch one TouchMove at
//      current position (held, phase 0) -- mirrors the binary's per-tick held poll.
//
// Catch-up (steps>=2 in one display frame): pending edges were set once in the drain
// (per display frame). The first FlushForSimTick consumes the edges. The second+ flush
// in the same display frame sees pendingDown=false but fingerActive=true, so it
// dispatches a held TouchMove at the same position -- one trail point per tick,
// matching the binary's once-per-tick poll cadence.
void InputTranslatorSDL::FlushForSimTick() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    if (!mgr) return;

    // Step 1: stuck-blade reconcile -- release any SDL-dropped fingers (#154).
    PollHeldFingers();

    // Step 2+3+4: dispatch pending edges and held moves.
    for (int ch = 0; ch < 16; ++ch) {
        if (pendingDown[ch]) {
            // A FINGERDOWN (or first touch) was drained this display frame.
            // Consume the edge.
            bool isEdge = pendingEdge[ch];
            pendingDown[ch] = false;
            pendingEdge[ch] = false;

            // Update Touch ring-buffer with the new press.
            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnPressed(ch + 1, fingerX[ch], fingerY[ch]);
            }

            TLOG("FlushForSimTick FINGERDOWN ch=%d edge=%d game=(%g,%g)\n",
                 ch, (int)isEdge, fingerX[ch], fingerY[ch]);

            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.fingerId = ch;
            ie.x = fingerX[ch];
            ie.y = fingerY[ch];

            ie.actionHash  = hashTouchScreen;
            ie.actionFlags = INPUT_ACTION_DOWN;
            mgr->DispatchEvent(&ie);

            // Synthesize TouchMove_X/Y before TouchDown_n so SlashEntity sees
            // fresh pos at press-edge (binary Bada platform fires moves first).
            ie.actionHash  = hashTouchMoveX[ch];
            ie.actionFlags = INPUT_ACTION_MOVE;
            mgr->DispatchEvent(&ie);
            ie.actionHash  = hashTouchMoveY[ch];
            mgr->DispatchEvent(&ie);

            ie.actionHash  = hashTouchDown[ch];
            ie.actionFlags = INPUT_ACTION_DOWN | (isEdge ? INPUT_ACTION_DOWN_EDGE : 0u);
            mgr->DispatchEvent(&ie);

        } else if (pendingUp[ch]) {
            // A FINGERUP was drained this display frame.
            pendingUp[ch] = false;

            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnReleased(ch + 1);
            }

            TLOG("FlushForSimTick FINGERUP ch=%d game=(%g,%g)\n", ch, fingerX[ch], fingerY[ch]);

            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.actionHash  = hashTouchUp[ch];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId    = ch;
            ie.x = fingerX[ch];
            ie.y = fingerY[ch];
            mgr->DispatchEvent(&ie);

            ReleaseFingerId(fingerMap[ch]);

        } else if (fingerActive[ch]) {
            // Held finger, no new edge this tick: emit one TouchMove at current pos.
            // This is the per-tick equivalent of the binary's OS touch poll.
            // isActive=true + existing slot -> OnMoved only updates currX/Y, phase stays 0.
            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnMoved(ch + 1, fingerX[ch], fingerY[ch]);
            }

            TLOG("FlushForSimTick HELD ch=%d game=(%g,%g)\n", ch, fingerX[ch], fingerY[ch]);

            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.fingerId = ch;
            ie.x = fingerX[ch];
            ie.y = fingerY[ch];

            ie.actionHash  = hashTouchMoveX[ch];
            ie.actionFlags = INPUT_ACTION_MOVE;
            mgr->DispatchEvent(&ie);

            ie.actionHash  = hashTouchMoveY[ch];
            mgr->DispatchEvent(&ie);

            ie.actionHash  = hashTouchDown[ch];
            ie.actionFlags = INPUT_ACTION_DOWN;
            mgr->DispatchEvent(&ie);
        }
    }
}

// Port specific: stuck-blade reconcile (#154).
// Check SDL's live finger state and synthesize TouchUp for any held channel whose
// fingerId is no longer present. Runs inside FlushForSimTick (once per sim tick).
// SDL_TOUCH_MOUSEID channels are checked against mouse button state.
// On desktop, SDL_FINGERUP fires reliably so this is a no-op in the common case.
// On web with real touchscreens, a dropped touchend/touchcancel leaves the channel
// stuck; this reconcile releases it (SDL_GetTouchFinger reflects hardware state
// synchronously after the event pump runs in pollInput).
void InputTranslatorSDL::PollHeldFingers() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    if (!mgr) return;

    // Build live fingerId set from all touch devices.
    SDL_FingerID liveIds[64];
    int liveCount = 0;

    int numDevices = SDL_GetNumTouchDevices();
    for (int di = 0; di < numDevices && liveCount < 64; ++di) {
        SDL_TouchID tid = SDL_GetTouchDevice(di);
        int nf = SDL_GetNumTouchFingers(tid);
        for (int fi = 0; fi < nf && liveCount < 64; ++fi) {
            SDL_Finger* f = SDL_GetTouchFinger(tid, fi);
            if (f) {
                liveIds[liveCount++] = f->id;
            }
        }
    }

    // Check for the mouse button being held (covers SDL_TOUCH_MOUSEID channel).
    Uint32 mouseButtons = SDL_GetMouseState(NULL, NULL);
    bool mouseDown = (mouseButtons & SDL_BUTTON_LMASK) != 0;

    for (int ch = 0; ch < 16; ++ch) {
        if (!fingerActive[ch]) continue;

        SDL_FingerID fid = fingerMap[ch];

        // SDL_TOUCH_MOUSEID channel: managed by mouse button state.
        if (fid == (SDL_FingerID)SDL_TOUCH_MOUSEID) {
            if (!mouseDown) {
                if (ch < Mortar::Touch::MAX_SLOTS) {
                    Mortar::Touch::GetInstance().OnReleased(ch + 1);
                }
                InputEvent ie;
                memset(&ie, 0, sizeof(ie));
                ie.actionHash  = hashTouchUp[ch];
                ie.actionFlags = INPUT_ACTION_UP;
                ie.fingerId    = ch;
                ie.x = fingerX[ch];
                ie.y = fingerY[ch];
                mgr->DispatchEvent(&ie);
                ReleaseFingerId(fid);
                // Clear any stale pending state for this channel.
                pendingDown[ch] = false;
                pendingUp[ch]   = false;
                pendingEdge[ch] = false;
            }
            continue;
        }

        // Real touch finger: check against SDL live set.
        // Port specific: only reconcile when SDL reports at least one real
        // touch device. When numDevices==0 there is no hardware touch state to
        // query -- synthetic SDL_PushEvent injections (test harnesses, etc.) do
        // not register fingers with SDL's touch tracking, so an absent-finger
        // check against an empty live set would incorrectly release every active
        // channel. With real touch hardware (numDevices>0) the reconcile is
        // meaningful: a FINGERUP dropped by the OS leaves the channel stuck, and
        // PollHeldFingers synthesizes the release (#154).
        if (numDevices == 0) {
            // No touch hardware: skip live-set reconcile for this channel.
            continue;
        }
        bool found = false;
        for (int li = 0; li < liveCount; ++li) {
            if (liveIds[li] == fid) { found = true; break; }
        }
        if (!found) {
            // SDL no longer reports this finger as down -- FINGERUP was dropped.
            // Synthesize the release so the blade latch can decay.
            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnReleased(ch + 1);
            }
            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.actionHash  = hashTouchUp[ch];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId    = ch;
            ie.x = fingerX[ch];
            ie.y = fingerY[ch];
            mgr->DispatchEvent(&ie);
            ReleaseFingerId(fid);
            pendingDown[ch] = false;
            pendingUp[ch]   = false;
            pendingEdge[ch] = false;
        }
    }
}

// Port specific: drain one SDL event into per-channel pending state.
// TOUCH events (FINGERDOWN/MOTION/UP, MOUSEBUTTONUP) are accumulated in
// pendingDown/pendingUp + fingerX/Y; they are NOT dispatched to InputManager.
// Non-touch events (WINDOW/FOCUS/keyboard) are handled inline as before.
// Called from pollInput() for every SDL_PollEvent result.
void InputTranslatorSDL::DrainSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    switch (ev.type) {

    // === Touch (multitouch, up to 16 fingers) ===
    // Mouse events arrive here too -- SDL_HINT_MOUSE_TOUCH_EVENTS=1 set
    // in mainSDL.cpp before SDL_Init synthesizes SDL_FINGER* from
    // SDL_MOUSE* with finger id = SDL_TOUCH_MOUSEID. Single touch path.

    case SDL_FINGERDOWN: {
        TLOG("SDL_FINGERDOWN fingerId=%lld nx=%.3f ny=%.3f pressure=%.3f\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.pressure);
        int ch = MapFingerId(ev.tfinger.fingerId);
        if (ch < 0) { TLOG("  -> MapFingerId returned -1 (all 16 channels busy)\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        TLOG("FINGERDOWN (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

        // Set pending down edge for this channel; will be dispatched on FlushForSimTick.
        pendingDown[ch] = true;
        pendingEdge[ch] = true;  // first-press edge
        pendingUp[ch]   = false; // cancel any stale up for the same channel
        break;
    }

    case SDL_FINGERMOTION: {
        TLOG("SDL_FINGERMOTION fingerId=%lld nx=%.3f ny=%.3f d(%.3f,%.3f)\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.dx, ev.tfinger.dy);
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) { TLOG("  no channel mapped for fingerId, skipping\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        // Update current position; FlushForSimTick will dispatch one move per tick.
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        TLOG("MOVE (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);
        break;
    }

    case SDL_FINGERUP: {
        TLOG("SDL_FINGERUP fingerId=%lld nx=%.3f ny=%.3f\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) { TLOG("  no channel mapped for fingerId, skipping\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        TLOG("FINGERUP (drain) ch=%d game=(%g,%g)\n", ch, gx, gy);

        // Set pending up; will be dispatched on FlushForSimTick.
        pendingUp[ch]   = true;
        pendingDown[ch] = false; // cancel any pending down for same channel
        pendingEdge[ch] = false;
        break;
    }

    // Port specific: safety-net for desktop/web -- SDL_HINT_MOUSE_TOUCH_EVENTS=1
    // synthesizes SDL_FINGERDOWN/MOTION but the UP sometimes arrives as
    // SDL_MOUSEBUTTONUP only, leaving fingerActive set for SDL_TOUCH_MOUSEID.
    // Belt-and-suspenders: handle it here too; PollHeldFingers also checks mouse
    // button state as a fallback in case this event is missed.
    case SDL_MOUSEBUTTONUP: {
        // Find any active finger mapped to SDL_TOUCH_MOUSEID (the synthetic ID
        // SDL uses when converting mouse events to touch events).
        SDL_FingerID mouseId = (SDL_FingerID)SDL_TOUCH_MOUSEID;
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == mouseId) {
                ch = i; break;
            }
        }
        if (ch < 0) break;  // not our mouse-as-touch finger, ignore

        TLOG("MOUSEBUTTONUP (drain) ch=%d game=(%g,%g)\n", ch, fingerX[ch], fingerY[ch]);

        // Treat the same as FINGERUP: pend the release for FlushForSimTick.
        pendingUp[ch]   = true;
        pendingDown[ch] = false;
        pendingEdge[ch] = false;
        break;
    }

    default:
        break;
    }
    (void)window;
}
