#ifndef FN_INPUT_TRANSLATOR_SDL_H
#define FN_INPUT_TRANSLATOR_SDL_H

//
// InputTranslatorSDL — converts SDL touch events to Mortar InputEvents.
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

    // Process an SDL event → dispatch InputEvents via InputManager
    void ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window);

private:
    // Convert normalized SDL touch coords to game coords (centred ortho).
    void TransformTouchNormalized(float nx, float ny, float& gx, float& gy);

    // Map SDL finger ID to channel (0-15)
    int MapFingerId(SDL_FingerID id);
    void ReleaseFingerId(SDL_FingerID id);

    SDL_FingerID fingerMap[16];
};

#endif
