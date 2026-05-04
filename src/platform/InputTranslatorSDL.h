#ifndef FN_INPUT_TRANSLATOR_SDL_H
#define FN_INPUT_TRANSLATOR_SDL_H

//
// InputTranslatorSDL — converts SDL touch/mouse events to Mortar InputEvents
//
// Maps SDL finger IDs to the 16-channel touch system used by the original.
// Each channel has actions: TouchDown_N, TouchMove_XN, TouchMove_YN, TouchUp_N
//
// Also generates global "TouchScreen" events for any touch.
//

#include <SDL.h>
#include "input/InputManager.h"
#include "render/Renderer.h"

// Forward-declare StringHash (from scoring or a utility)
uint32_t StringHash(const char* str);

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

    // Mouse emulates finger 0
    bool mouseDown;
    float mouseX, mouseY;

    InputTranslatorSDL();

    // Initialize action hashes (call once after StringHash is available)
    void Init();

    // Process an SDL event → dispatch InputEvents via InputManager
    void ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window);

private:
    // Convert SDL pixel coords to game coords
    void TransformTouch(SDL_Window* window, int px, int py, float& gx, float& gy);
    void TransformTouchNormalized(float nx, float ny, float& gx, float& gy);

    // Map SDL finger ID to channel (0-15)
    int MapFingerId(SDL_FingerID id);
    void ReleaseFingerId(SDL_FingerID id);

    SDL_FingerID fingerMap[16];
};

#endif
