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

    default:
        break;
    }
}
