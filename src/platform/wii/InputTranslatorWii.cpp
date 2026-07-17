// Port specific: Wii WPAD IR -> Mortar::Touch translator -- SCAFFOLDING ONLY.
// See InputTranslatorWii.h for the 4-remote -> 4-finger-channel mapping
// rationale. Bodies are no-op placeholders; every real WPAD/Touch call is a
// TODO(wii) marker.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/InputTranslatorWii.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"

// TODO(wii): #include <wiiuse/wpad.h> once the real IR struct is threaded
// through DrainWiimoteIR's signature instead of the opaque (x, y) floats.

InputTranslatorWii::InputTranslatorWii()
    : hashTouchScreen(0)
{
    for (int i = 0; i < MAX_REMOTES; ++i) {
        hashTouchDown[i]  = 0;
        hashTouchMoveX[i] = 0;
        hashTouchMoveY[i] = 0;
        hashTouchUp[i]    = 0;
        channelActive[i]  = false;
        channelX[i]       = 0.0f;
        channelY[i]       = 0.0f;
        prevActive[i]     = false;
    }
}

void InputTranslatorWii::Init() {
    // TODO(wii): compute hashTouchDown/Move/Up[chan] and hashTouchScreen via
    // Mortar::StringHash, same "TouchDown_N"/"TouchMove_XN"/"TouchMove_YN"/
    // "TouchUp_N"/"TouchScreen" action-name convention InputTranslatorSDL::Init
    // uses (src/platform/InputTranslatorSDL.cpp) -- channels 0-3 only.
}

void InputTranslatorWii::DrainWiimoteIR(int chan, bool irValid, float x, float y) {
    (void)chan; (void)irValid; (void)x; (void)y;
    // TODO(wii): mirror InputTranslatorSDL::DrainSDLEvent's FINGERDOWN/
    // MOTION/UP handling:
    //   - irValid transitions false->true: TransformIRNormalized then
    //     Mortar::Touch::GetInstance().OnPressed(chan, gx, gy).
    //   - irValid true, position changed: OnMoved(chan, gx, gy).
    //   - irValid transitions true->false: OnReleased(chan).
    // No out-of-window concept applies here (IR is either valid or not --
    // there's no windowed viewport to cross, unlike the SDL desktop/web
    // mouse-drag case InputTranslatorSDL.h documents at length).
}

void InputTranslatorWii::DispatchForSimTick() {
    // TODO(wii): mirror InputTranslatorSDL::DispatchForSimTick -- call
    // Mortar::Touch::GetInstance().Update(0.0f) to drain the ring buffer,
    // then read states1[0..3] and emit hashTouchDown/Move/Up via
    // Mortar::InputManager::GetInstance() for each of the 4 remote channels.
}

void InputTranslatorWii::ReleaseAllFingers() {
    // TODO(wii): mirror InputTranslatorSDL::ReleaseAllFingers -- synthesize
    // OnReleased for every channelActive[] remote, flush the Touch ring.
    for (int i = 0; i < MAX_REMOTES; ++i) {
        channelActive[i] = false;
    }
}

void InputTranslatorWii::TransformIRNormalized(float nx, float ny, float& gx, float& gy) {
    // TODO(wii): same centred-ortho transform InputTranslatorSDL::
    // TransformTouchNormalized performs (see docs/engine/coordinate-system.md).
    gx = nx;
    gy = ny;
}

#endif // FRUIT_PLATFORM_WII
