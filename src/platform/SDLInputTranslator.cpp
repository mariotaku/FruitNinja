//
// SDLInputTranslator — converts SDL events to Mortar InputEvents
//

#include "SDLInputTranslator.h"
#include <cstdio>
#include <cstring>

// Simple StringHash implementation (matches original Jenkins lookup3)
// For now, a basic DJB2 hash — replace with exact original when scoring.cpp is implemented
uint32_t StringHash(const char* str) {
    uint32_t hash = 0x805 + (uint32_t)strlen(str);
    uint32_t a = 0xDEADBEEF + hash;
    uint32_t b = a, c = a;
    while (*str) {
        a += (uint8_t)*str++;
        a -= c; a ^= (c << 4) | (c >> 28); c += b;
        b -= a; b ^= (a << 6) | (a >> 26); a += c;
        c -= b; c ^= (b << 8) | (b >> 24); b += a;
    }
    // Final mix
    c ^= b; c -= (b << 14) | (b >> 18);
    a ^= c; a -= (c << 11) | (c >> 21);
    b ^= a; b -= (a << 25) | (a >> 7);
    return c;
}

SDLInputTranslator::SDLInputTranslator()
    : hashTouchScreen(0), mouseDown(false), mouseX(0), mouseY(0) {
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(fingerMap, 0xFF, sizeof(fingerMap));
}

void SDLInputTranslator::Init() {
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

void SDLInputTranslator::TransformTouch(SDL_Window* window, int px, int py,
                                         float& gx, float& gy) {
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    gx = (float)px * FN_SCREEN_W / ww;
    gy = FN_SCREEN_H - (float)py * FN_SCREEN_H / wh;
}

void SDLInputTranslator::TransformTouchNormalized(float nx, float ny,
                                                   float& gx, float& gy) {
    gx = nx * FN_SCREEN_W;
    gy = FN_SCREEN_H - ny * FN_SCREEN_H;
}

int SDLInputTranslator::MapFingerId(SDL_FingerID id) {
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

void SDLInputTranslator::ReleaseFingerId(SDL_FingerID id) {
    for (int i = 0; i < 16; i++) {
        if (fingerActive[i] && fingerMap[i] == id) {
            fingerActive[i] = false;
            return;
        }
    }
}

void SDLInputTranslator::ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    InputManager* mgr = InputManager::GetInstance();
    if (!mgr) return;

    InputEvent ie;
    memset(&ie, 0, sizeof(ie));

    switch (ev.type) {

    // === Mouse (emulates finger 0 for desktop) ===

    case SDL_MOUSEBUTTONDOWN:
        if (ev.button.button == SDL_BUTTON_LEFT) {
            mouseDown = true;
            TransformTouch(window, ev.button.x, ev.button.y, mouseX, mouseY);
            fingerX[0] = mouseX;
            fingerY[0] = mouseY;

            // TouchScreen global
            ie.actionHash = hashTouchScreen;
            ie.actionFlags = INPUT_ACTION_DOWN;
            ie.fingerId = 0;
            ie.x = mouseX; ie.y = mouseY;
            mgr->DispatchEvent(&ie);

            // TouchDown_0
            ie.actionHash = hashTouchDown[0];
            ie.actionFlags = INPUT_ACTION_DOWN;
            mgr->DispatchEvent(&ie);
        }
        break;

    case SDL_MOUSEMOTION:
        if (mouseDown) {
            float gx, gy;
            TransformTouch(window, ev.motion.x, ev.motion.y, gx, gy);
            float dx = gx - mouseX;
            float dy = gy - mouseY;
            mouseX = gx; mouseY = gy;
            fingerX[0] = gx; fingerY[0] = gy;

            // TouchMove_X0
            ie.actionHash = hashTouchMoveX[0];
            ie.actionFlags = INPUT_ACTION_MOVE;
            ie.fingerId = 0;
            ie.x = gx; ie.y = gy;
            ie.deltaX = dx; ie.deltaY = dy;
            mgr->DispatchEvent(&ie);

            // TouchMove_Y0
            ie.actionHash = hashTouchMoveY[0];
            mgr->DispatchEvent(&ie);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (ev.button.button == SDL_BUTTON_LEFT && mouseDown) {
            mouseDown = false;
            TransformTouch(window, ev.button.x, ev.button.y, mouseX, mouseY);

            ie.actionHash = hashTouchUp[0];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId = 0;
            ie.x = mouseX; ie.y = mouseY;
            mgr->DispatchEvent(&ie);
        }
        break;

    // === Touch (multitouch, up to 16 fingers) ===

    case SDL_FINGERDOWN: {
        int ch = MapFingerId(ev.tfinger.fingerId);
        if (ch < 0) break;
        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx; fingerY[ch] = gy;

        // TouchScreen global
        ie.actionHash = hashTouchScreen;
        ie.actionFlags = INPUT_ACTION_DOWN;
        ie.fingerId = ch;
        ie.x = gx; ie.y = gy;
        mgr->DispatchEvent(&ie);

        // TouchDown_N
        ie.actionHash = hashTouchDown[ch];
        mgr->DispatchEvent(&ie);
        break;
    }

    case SDL_FINGERMOTION: {
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) break;

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        float dx = gx - fingerX[ch];
        float dy = gy - fingerY[ch];
        fingerX[ch] = gx; fingerY[ch] = gy;

        ie.actionHash = hashTouchMoveX[ch];
        ie.actionFlags = INPUT_ACTION_MOVE;
        ie.fingerId = ch;
        ie.x = gx; ie.y = gy;
        ie.deltaX = dx; ie.deltaY = dy;
        mgr->DispatchEvent(&ie);

        ie.actionHash = hashTouchMoveY[ch];
        mgr->DispatchEvent(&ie);
        break;
    }

    case SDL_FINGERUP: {
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) break;

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);

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
