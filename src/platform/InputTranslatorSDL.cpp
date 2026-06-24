//
// InputTranslatorSDL -- converts SDL events to Mortar InputEvents AND feeds
// Mortar::Touch directly (poll-based binary path).
//
// Port specific: binary is a strict 1:1 input->update->draw tick.
// This file splits touch dispatch into three phases:
//   DrainSDLEvent()      -- accumulate per-frame (no dispatch); called from pollInput()
//   ReconcileTouch()     -- after drain, query SDL live-finger set for #154;
//                          called from pollInput() ONLY (web-safe: live set is valid
//                          right after the event pump, NOT from stepUpdate)
//   DispatchForSimTick() -- dispatch once per sim tick; called from stepUpdate()
// This ensures m_PointCount only advances inside a tick that also runs
// UpdatePoints (which reconciles the head-cap vertex), so DrawSlice never draws
// a stale head-cap to origin (#168 / #173 bridge fix).
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

// Port specific: legacy wrapper -- no-op. Dispatch is now via DispatchForSimTick().
// Retained so any callers that invoke BeginFrame() are not broken.
void InputTranslatorSDL::BeginFrame() {
    // No-op: the drain/flush split moves all dispatch to DispatchForSimTick().
}

// Port specific: legacy wrapper -- calls DrainSDLEvent internally.
// Retained for scene_slash / scene_slash_blade and any other direct callers
// that forward SDL events without going through Game::pollInput.
// Scene code that calls this should also call DispatchForSimTick() once per tick.
void InputTranslatorSDL::ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    DrainSDLEvent(ev, window);
}

// Port specific: synthesize TouchUp for every held finger and clear all channels.
// Called when the SDL window loses focus or is minimized so no blade stays armed
// across a background/restore cycle (#162).
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

        // Set pending down edge for this channel; will be dispatched on DispatchForSimTick.
        pendingDown[ch] = true;
        pendingEdge[ch] = true;  // first-press edge (#173)
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
        // Update current position; DispatchForSimTick will dispatch one move per tick.
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        TLOG("MOVE (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);
        // Do NOT touch pendingDown/pendingUp -- motion only updates position.
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

        // Set pending up; will be dispatched on DispatchForSimTick.
        pendingUp[ch]   = true;
        pendingDown[ch] = false; // cancel any pending down for same channel
        pendingEdge[ch] = false;
        break;
    }

    // Port specific: safety-net for desktop/web -- SDL_HINT_MOUSE_TOUCH_EVENTS=1
    // synthesizes SDL_FINGERDOWN/MOTION but the UP sometimes arrives as
    // SDL_MOUSEBUTTONUP only, leaving fingerActive set for SDL_TOUCH_MOUSEID.
    // Belt-and-suspenders: handle it here too; ReconcileTouch also checks mouse
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

        // Treat the same as FINGERUP: pend the release for DispatchForSimTick.
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

// Port specific: reconcile SDL live-finger state after all drain events are processed.
// MUST be called from pollInput() ONLY -- the SDL live-set queries
// (SDL_GetNumTouchDevices / GetTouchDevice / GetNumTouchFingers / GetTouchFinger)
// are only valid right after the SDL event pump. On emscripten, calling these from
// stepUpdate would read empty and spuriously release just-pressed fingers.
//
// #154 fix: detects fingers for which SDL dropped a FINGERUP (finger left the
// canvas, browser cancelled the touch, etc.) and marks them pendingUp so
// DispatchForSimTick will release them on the next sim tick.
//
// Guard: when SDL_GetNumTouchDevices()==0 (no real touch hardware, headless tests),
// skip the live-set check for non-MOUSEID channels -- synthetic SDL_PushEvent
// injections do not register fingers with SDL's touch tracking.
// Also guard: when liveCount==0 and numDevices>0, the hardware is present but
// currently reports no fingers. In this case we DO reconcile (any held channel
// with numDevices>0 but not in the live set was dropped by the OS).
void InputTranslatorSDL::ReconcileTouch() {
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
            if (!mouseDown && !pendingUp[ch]) {
                // Mouse released without SDL_MOUSEBUTTONUP reaching us;
                // mark pendingUp so DispatchForSimTick will release it.
                pendingUp[ch]   = true;
                pendingDown[ch] = false;
                pendingEdge[ch] = false;
                TLOG("ReconcileTouch: mouse not held, ch=%d -> pendingUp\n", ch);
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
        // ReconcileTouch marks it pendingUp (#154).
        if (numDevices == 0) {
            continue;
        }
        bool found = false;
        for (int li = 0; li < liveCount; ++li) {
            if (liveIds[li] == fid) { found = true; break; }
        }
        if (!found && !pendingUp[ch]) {
            // SDL no longer reports this finger as down -- FINGERUP was dropped.
            // Mark pendingUp so DispatchForSimTick will release it.
            pendingUp[ch]   = true;
            pendingDown[ch] = false;
            pendingEdge[ch] = false;
            TLOG("ReconcileTouch: finger %lld not in live set, ch=%d -> pendingUp\n",
                 (long long)fid, ch);
        }
    }
}

// Port specific: dispatch accumulated touch state to InputManager for one sim tick.
// Binary cadence: input->update->draw happens exactly once per tick.
// This function provides the "input" phase that runs at the start of each sim tick
// (before GameTaskUpdate), matching the binary's strict 1:1 tick ordering.
//
// Per tick:
//   1. For each channel with a pending TouchDown edge: dispatch TouchScreen +
//      TouchMove + TouchDown (with INPUT_ACTION_DOWN_EDGE on press-edge, #173).
//      Consumes and clears the pending edge so subsequent catch-up steps do not
//      re-fire the edge.
//   2. For each channel with a pending TouchUp: dispatch TouchUp, mark inactive.
//   3. For each still-active channel (no pending up): dispatch one TouchMove at
//      current position (held, phase 0) -- mirrors the binary's per-tick held poll.
//
// No SDL touch/mouse live-set queries here -- only edge-driven dispatch from the
// pending state set by DrainSDLEvent + ReconcileTouch in pollInput.
//
// Catch-up (steps>=2 in one display frame): pending edges were set once in the drain
// (per display frame). The first DispatchForSimTick consumes the edges. The second+
// dispatch in the same display frame sees pendingDown=false but fingerActive=true,
// so it dispatches a held TouchMove at the same position -- one trail point per tick,
// matching the binary's once-per-tick poll cadence.
void InputTranslatorSDL::DispatchForSimTick() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    // Note: do NOT early-return on null mgr. Pending state must be consumed
    // even in headless mode so catch-up ticks don't re-fire stale edges.
    // When mgr is null, Touch ring-buffer updates and InputManager dispatches
    // are skipped; only the pending state bookkeeping runs.

    for (int ch = 0; ch < 16; ++ch) {
        if (pendingDown[ch]) {
            // A FINGERDOWN (or first touch) was drained this display frame.
            // Consume the edge unconditionally (even when mgr is null).
            bool isEdge = pendingEdge[ch];
            pendingDown[ch] = false;
            pendingEdge[ch] = false;

            if (!mgr) continue;

            // Update Touch ring-buffer with the new press.
            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnPressed(ch + 1, fingerX[ch], fingerY[ch]);
            }

            TLOG("DispatchForSimTick FINGERDOWN ch=%d edge=%d game=(%g,%g)\n",
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
            // A FINGERUP was drained this display frame (or set by ReconcileTouch).
            // Consume unconditionally; only dispatch if mgr is available.
            pendingUp[ch] = false;

            if (mgr && ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnReleased(ch + 1);
            }

            TLOG("DispatchForSimTick FINGERUP ch=%d game=(%g,%g)\n",
                 ch, fingerX[ch], fingerY[ch]);

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

            ReleaseFingerId(fingerMap[ch]);

        } else if (fingerActive[ch]) {
            // Held finger, no new edge this tick: emit one TouchMove at current pos.
            // This is the per-tick equivalent of the binary's OS touch poll.
            // isActive=true + existing slot -> OnMoved only updates currX/Y, phase stays 0.
            if (!mgr) continue;

            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnMoved(ch + 1, fingerX[ch], fingerY[ch]);
            }

            TLOG("DispatchForSimTick HELD ch=%d game=(%g,%g)\n",
                 ch, fingerX[ch], fingerY[ch]);

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
