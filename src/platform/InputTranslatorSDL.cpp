//
// InputTranslatorSDL — converts SDL events to Mortar InputEvents AND feeds
// Mortar::Touch directly (poll-based binary path). The InputManager dispatch
// is kept for keyboard/gamepad actions only — step 7 of the touch rewrite
// will drop the TouchDown_N / TouchMove_XN / TouchUp_N bindings.
//

#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include <cstdio>
#include <cstring>

// Touch event logging — flip to 0 to silence after diagnosing crashes
// caused by stray mouse/finger events on startup.
#define TOUCH_LOG 0
#if TOUCH_LOG
#  define TLOG(...) do { printf("[TOUCH] " __VA_ARGS__); fflush(stdout); } while (0)
#else
#  define TLOG(...) do {} while (0)
#endif

// StringHash is provided by src/engine/util/StringHash.h (the binary-faithful
// Jenkins lookup3 with case-folding). Earlier this file had a local DJB2-like
// definition that produced different hashes for the same string -- causing
// SlashEntity event-driven dispatch to silently fail. Single source of truth
// now.

InputTranslatorSDL::InputTranslatorSDL()
    : hashTouchScreen(0), mouseDown(false), mouseX(0), mouseY(0) {
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

// Transform SDL window pixel coords → binary-centred ortho coords.
// Ortho: X ∈ [-240, +240] (horizontal), Y ∈ [-160, +160] (vertical, +up).
// SDL pixel Y is top-down (0 at top), so we flip.
// See docs/engine/coordinate-system.md.
void InputTranslatorSDL::TransformTouch(SDL_Window* window, int px, int py,
                                         float& gx, float& gy) {
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    // Normalise to [0, 1] then remap to [-240, 240] × [-160, 160].
    // Port convention: TOP = +160, BOTTOM = -160 (Y-up). Note the Bada
    // binary uses Y-down (TOP=-160) — the discrepancy is handled locally
    // by ScrollingMenu (its drag formula negates currY before applying the
    // binary-faithful math). Other touch consumers (MenuButton hit-tests,
    // SlashEntity blade tracking) work in port's Y-up convention directly.
    const float nx = (float)px / (float)ww;
    const float ny = (float)py / (float)wh;
    gx = nx * (float)FN_SCREEN_W - (float)(FN_SCREEN_W / 2);   // [-240, 240]
    gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;   // [+160, -160]
}

void InputTranslatorSDL::TransformTouchNormalized(float nx, float ny,
                                                   float& gx, float& gy) {
    gx = nx * (float)FN_SCREEN_W - (float)(FN_SCREEN_W / 2);
    gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
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

    // === Mouse (emulates finger 0 for desktop) ===

    case SDL_MOUSEBUTTONDOWN:
        TLOG("SDL_MOUSEBUTTONDOWN btn=%d px=(%d,%d) state=0x%x mouseDown_was=%d\n",
             (int)ev.button.button, ev.button.x, ev.button.y,
             (unsigned)ev.button.state, mouseDown ? 1 : 0);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            mouseDown = true;
            TransformTouch(window, ev.button.x, ev.button.y, mouseX, mouseY);
            fingerX[0] = mouseX;
            fingerY[0] = mouseY;
            TLOG("  → game coords (%.1f, %.1f) — pressing slot 0\n", mouseX, mouseY);

            // Poll-based path: feed Mortar::Touch directly (slot 0 = mouse).
            // extId is +1 so 0 stays reserved as Touch's "free slot" sentinel
            // (binary @ 0x00195314 ___UpdateInternal). Mouse maps to extId 1.
            Mortar::Touch::GetInstance().OnPressed(1, mouseX, mouseY);

            // Callback path (keyboard/gamepad style) — kept until step 7.
            ie.actionHash = hashTouchScreen;
            ie.actionFlags = INPUT_ACTION_DOWN;
            ie.fingerId = 0;
            ie.x = mouseX; ie.y = mouseY;
            mgr->DispatchEvent(&ie);

            // Bada platform delivers TouchMove_X/Y before TouchDown_n on a
            // new finger landing, so SlashEntity::TouchDown reads fresh
            // pos.{x,y} (set by the move handlers) and starts the trail at
            // the press point. SDL has no separate move-on-press, so we
            // synthesize one here -- otherwise the trail begins at the
            // previous swipe's release point.
            ie.actionHash = hashTouchMoveX[0];
            ie.actionFlags = INPUT_ACTION_MOVE;
            mgr->DispatchEvent(&ie);
            ie.actionHash = hashTouchMoveY[0];
            mgr->DispatchEvent(&ie);

            ie.actionHash = hashTouchDown[0];
            ie.actionFlags = INPUT_ACTION_DOWN;
            mgr->DispatchEvent(&ie);
        }
        break;

    case SDL_MOUSEMOTION:
        // Log every motion event regardless of mouseDown so we see
        // the very first SDL_MOUSEMOTION the game receives at startup
        // (which carries the mouse position from when the window was
        // created — useful for diagnosing "mouse over X" crashes).
        TLOG("SDL_MOUSEMOTION px=(%d,%d) state=0x%x mouseDown=%d\n",
             ev.motion.x, ev.motion.y, (unsigned)ev.motion.state, mouseDown ? 1 : 0);
        if (mouseDown) {
            float gx, gy;
            TransformTouch(window, ev.motion.x, ev.motion.y, gx, gy);
            float dx = gx - mouseX;
            float dy = gy - mouseY;
            mouseX = gx; mouseY = gy;
            fingerX[0] = gx; fingerY[0] = gy;
            TLOG("  → game coords (%.1f, %.1f) Δ(%.1f, %.1f) — moving slot 0\n",
                 gx, gy, dx, dy);

            Mortar::Touch::GetInstance().OnMoved(1, gx, gy);  // extId +1 (mouse=1)

            ie.actionHash = hashTouchMoveX[0];
            ie.actionFlags = INPUT_ACTION_MOVE;
            ie.fingerId = 0;
            ie.x = gx; ie.y = gy;
            ie.deltaX = dx; ie.deltaY = dy;
            mgr->DispatchEvent(&ie);

            ie.actionHash = hashTouchMoveY[0];
            mgr->DispatchEvent(&ie);

            // Binary @ 0x00169670: Bada delivers TouchDown_n events for both
            // press AND every move while held; SlashEntity::TouchDown calls
            // UpdateTouchDown unconditionally (extending the trail). SDL
            // splits press from motion, so we re-fire TouchDown_n here on
            // motion to match the binary's per-move trail-extension trigger.
            ie.actionHash = hashTouchDown[0];
            ie.actionFlags = INPUT_ACTION_DOWN;
            mgr->DispatchEvent(&ie);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        TLOG("SDL_MOUSEBUTTONUP btn=%d px=(%d,%d) state=0x%x mouseDown_was=%d\n",
             (int)ev.button.button, ev.button.x, ev.button.y,
             (unsigned)ev.button.state, mouseDown ? 1 : 0);
        if (ev.button.button == SDL_BUTTON_LEFT && mouseDown) {
            mouseDown = false;
            TransformTouch(window, ev.button.x, ev.button.y, mouseX, mouseY);
            TLOG("  → game coords (%.1f, %.1f) — releasing slot 0\n", mouseX, mouseY);

            Mortar::Touch::GetInstance().OnReleased(1);  // extId +1 (mouse=1)

            ie.actionHash = hashTouchUp[0];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId = 0;
            ie.x = mouseX; ie.y = mouseY;
            mgr->DispatchEvent(&ie);
        }
        break;

    // === Touch (multitouch, up to 16 fingers) ===

    case SDL_FINGERDOWN: {
        TLOG("SDL_FINGERDOWN fingerId=%lld nx=%.3f ny=%.3f pressure=%.3f\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.pressure);
        int ch = MapFingerId(ev.tfinger.fingerId);
        if (ch < 0) { TLOG("  → MapFingerId returned -1 (all 16 channels busy)\n"); break; }
        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx; fingerY[ch] = gy;
        TLOG("  → ch=%d game (%.1f, %.1f) — pressing slot\n", ch, gx, gy);

        // Poll-based path. Mortar::Touch has 8 slots; clamp or drop extras.
        // extId is ch+1 so 0 stays reserved as Touch's "free slot" sentinel
        // (binary @ 0x00195314 ___UpdateInternal).
        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
        }

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
        TLOG("SDL_FINGERMOTION fingerId=%lld nx=%.3f ny=%.3f Δ(%.3f,%.3f)\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.dx, ev.tfinger.dy);
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) { TLOG("  → no channel mapped for fingerId, skipping\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        float dx = gx - fingerX[ch];
        float dy = gy - fingerY[ch];
        fingerX[ch] = gx; fingerY[ch] = gy;

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
        if (ch < 0) { TLOG("  → no channel mapped for fingerId, skipping\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);

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
