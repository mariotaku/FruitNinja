//
// InputTranslatorSDL — converts SDL events to Mortar InputEvents AND feeds
// Mortar::Touch directly (poll-based binary path). The InputManager dispatch
// is kept for keyboard/gamepad actions only — step 7 of the touch rewrite
// will drop the TouchDown_N / TouchMove_XN / TouchUp_N bindings.
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
// SlashEntity event-driven dispatch to silently fail. Single source of truth
// now.

InputTranslatorSDL::InputTranslatorSDL() : hashTouchScreen(0) {
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(fingerMap, 0xFF, sizeof(fingerMap));
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
void InputTranslatorSDL::TransformTouchNormalized(float nx, float ny,
                                                   float& gx, float& gy) {
    gx = nx * (float)FN_SCREEN_W - (float)(FN_SCREEN_W / 2);
    gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
    TLOG("TransformTouchNormalized raw=(%g,%g) -> game=(%g,%g)\n", nx, ny, gx, gy);
}

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

void InputTranslatorSDL::BeginFrame() {
    PollHeldFingers();
}

// Port specific: SDL is event-driven; Bada polled touch every frame.
// Re-dispatch TouchDown_N each frame for held fingers so
// SlashEntity::OnTouchActive emits a blade point per frame (binary cadence)
// -- fixes slow-slice blade dashing. Press-edge (IsTouchDown==2) stays
// first-frame-only: the Touch ring-buffer promotes phase -1->0 via StateUpdate
// after the first frame, so subsequent OnMoved calls keep phase==0 (held,
// IsTouchDown==1), never re-triggering the press-edge.
void InputTranslatorSDL::PollHeldFingers() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    if (!mgr) return;

    // Port specific: web/emscripten drops SDL_FINGERUP for real touches;
    // reconcile held fingers vs SDL touch state so the blade latch can decay
    // (binary relies on OS-guaranteed lift events).
    //
    // On web with a real touchscreen (MOUSE_TOUCH_EVENTS=0), physical touch
    // fingers carry a real Touch.identifier (not SDL_TOUCH_MOUSEID). If the
    // browser drops a touchend/touchcancel (finger leaves canvas, mobile
    // browser cancels, etc.) the matching SDL_FINGERUP is never received and
    // fingerActive[ch] stays true forever -> blade re-arms every frame.
    //
    // Fix: query SDL's real-time finger state (SDL_GetNumTouchFingers /
    // SDL_GetTouchFinger) and release any held channel whose fingerId is no
    // longer present in SDL's live set. SDL_GetTouchFinger reflects the true
    // hardware state and is updated synchronously by the event pump -- a finger
    // that has lifted is immediately absent from this list even if the FINGERUP
    // event was dropped. SDL_TOUCH_MOUSEID channels are exempt: they are managed
    // by the mouse-button-down state and released via SDL_MOUSEBUTTONUP; they
    // never appear in the touch-device finger list.
    //
    // Desktop behaviour is unchanged: on desktop SDL_FINGERUP fires reliably for
    // both real touch hardware and the MOUSE_TOUCH_EVENTS=1 synthetic path
    // (SDL_TOUCH_MOUSEID), so the reconcile loop finds no stale channels.
    {
        // Build live fingerId set from all touch devices.
        // We use a small flat array (max 16 channels) to avoid allocation.
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
                    // Mouse released without SDL_MOUSEBUTTONUP reaching us;
                    // synthesize the release now.
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
                }
                continue;
            }

            // Real touch finger: check against SDL live set.
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
            }
        }
    }

    for (int ch = 0; ch < 16; ch++) {
        if (!fingerActive[ch]) continue;

        // Keep Touch ring-buffer current for this frame (move = held, not new press).
        // isActive=true + existing slot -> only updates currX/Y, phase stays 0 (held).
        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnMoved(ch + 1, fingerX[ch], fingerY[ch]);
        }

        // Re-dispatch position then TouchDown_N for this held finger.
        // SlashEntity::OnTouchActive checks TouchDown_N every frame; without this
        // poll, frames with no SDL_FINGERMOTION emit no TouchDown_N -> gaps -> dashing.
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

void InputTranslatorSDL::ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    if (!mgr) return;

    InputEvent ie;
    memset(&ie, 0, sizeof(ie));

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

        float nx = ev.tfinger.x, ny = ev.tfinger.y;
        float gx, gy;
        TransformTouchNormalized(nx, ny, gx, gy);
        fingerX[ch] = gx; fingerY[ch] = gy;
        TLOG("FINGERDOWN ch=%d raw=(%g,%g) game=(%g,%g)\n", ch, nx, ny, gx, gy);

        // Poll-based path. Mortar::Touch has 8 slots; clamp or drop extras.
        // extId is ch+1 so 0 stays reserved as Touch's "free slot" sentinel
        // (binary @ 0x00195314 ___UpdateInternal).
        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
        }

        TLOG("dispatch TouchDown slot=%d game=(%g,%g)\n", ch, gx, gy);
        ie.actionHash = hashTouchScreen;
        ie.actionFlags = INPUT_ACTION_DOWN;
        ie.fingerId = ch;
        ie.x = gx; ie.y = gy;
        mgr->DispatchEvent(&ie);

        // Synthesize TouchMove_X/Y before TouchDown_n so SlashEntity sees
        // fresh pos at press-edge (binary Bada platform fires moves first).
        ie.actionHash = hashTouchMoveX[ch];
        ie.actionFlags = INPUT_ACTION_MOVE;
        mgr->DispatchEvent(&ie);
        ie.actionHash = hashTouchMoveY[ch];
        mgr->DispatchEvent(&ie);

        ie.actionHash = hashTouchDown[ch];
        ie.actionFlags = INPUT_ACTION_DOWN;
        mgr->DispatchEvent(&ie);
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
        float dx = gx - fingerX[ch];
        float dy = gy - fingerY[ch];
        fingerX[ch] = gx; fingerY[ch] = gy;
        TLOG("MOVE ch=%d raw=(%g,%g) game=(%g,%g) delta=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy, dx, dy);

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnMoved(ch + 1, gx, gy);  // extId +1
        }

        ie.actionHash = hashTouchMoveX[ch];
        ie.actionFlags = INPUT_ACTION_MOVE;
        ie.fingerId = ch;
        ie.x = gx; ie.y = gy;
        ie.deltaX = dx; ie.deltaY = dy;
        mgr->DispatchEvent(&ie);

        ie.actionHash = hashTouchMoveY[ch];
        mgr->DispatchEvent(&ie);

        // Binary @ 0x00169670: Bada delivers TouchDown_n events for both
        // press AND every move; re-fire here on SDL_FINGERMOTION to match.
        ie.actionHash = hashTouchDown[ch];
        ie.actionFlags = INPUT_ACTION_DOWN;
        mgr->DispatchEvent(&ie);
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
        TLOG("FINGERUP ch=%d game=(%g,%g)\n", ch, gx, gy);

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);  // extId +1
        }

        ie.actionHash = hashTouchUp[ch];
        ie.actionFlags = INPUT_ACTION_UP;
        ie.fingerId = ch;
        ie.x = gx; ie.y = gy;
        mgr->DispatchEvent(&ie);

        ReleaseFingerId(ev.tfinger.fingerId);
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

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
        }

        ie.actionHash = hashTouchUp[ch];
        ie.actionFlags = INPUT_ACTION_UP;
        ie.fingerId = ch;
        ie.x = fingerX[ch];
        ie.y = fingerY[ch];
        mgr->DispatchEvent(&ie);

        ReleaseFingerId(mouseId);
        break;
    }

    default:
        break;
    }
}
