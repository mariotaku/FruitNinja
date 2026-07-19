// Port specific: Wii WPAD IR -> Mortar::Touch translator.
// See InputTranslatorWii.h for the 4-remote -> 4-finger-channel mapping
// rationale. Mirrors InputTranslatorSDL.cpp's DrainSDLEvent/DispatchForSimTick
// (src/platform/InputTranslatorSDL.cpp) as closely as the WPAD IR model
// allows: each remote's IR pointer edge (valid transition) plays the role of
// a FINGERDOWN/FINGERUP, and a position change while valid plays the role of
// FINGERMOTION. There is no out-of-window / mouse-channel concept here --
// WPAD IR is either valid (pointed at the sensor bar) or not, no windowed
// viewport to cross.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/InputTranslatorWii.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include "render/Layout.h"
#include <cstdio>
#include <cstring>

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
        prevIRValid[i]    = false;
        motionSinceDown[i] = false;
        pendingEdge[i]    = false;
    }
}

// Mirrors InputTranslatorSDL::Init -- same "TouchDown_N"/"TouchMove_XN"/
// "TouchMove_YN"/"TouchUp_N"/"TouchScreen" action-name convention, just for
// channels 0-3 (the 4 remote channels) instead of 0-15.
void InputTranslatorWii::Init() {
    char buf[32];

    for (int i = 0; i < MAX_REMOTES; i++) {
        sprintf(buf, "TouchDown_%d", i);
        hashTouchDown[i] = StringHash(buf);

        sprintf(buf, "TouchMove_X%d", i);
        hashTouchMoveX[i] = StringHash(buf);

        sprintf(buf, "TouchMove_Y%d", i);
        hashTouchMoveY[i] = StringHash(buf);

        sprintf(buf, "TouchUp_%d", i);
        hashTouchUp[i] = StringHash(buf);
    }

    hashTouchScreen = StringHash("TouchScreen");
}

// Normalized IR-pointer (nx, ny in [0,1], top-left origin, y-down -- the
// same convention WPAD_SetVRes/ir_t.x,y normalize to via mainWii.cpp's
// nx = ir.x/fbWidth, ny = ir.y/xfbHeight) -> centred game-ortho coords.
// Delegates to Layout::TouchToGame, the single shared transform
// InputTranslatorSDL::TransformTouchNormalized also uses (src/engine/render/
// Layout.cpp) -- NOT SDL-specific, guarded only by __bada__. GameWii.cpp DOES
// call Layout::SetActiveViewport every frame (renderFrame), recording the
// full real-framebuffer rect (Layout::SetWideLayout(false) makes
// ComputeViewport return the whole EFB, no pillarbox/letterbox on Wii's fixed
// TV framebuffer). TouchToGame therefore takes its "invert the recorded
// viewport" branch, which for a full-window viewport reduces to the same
// gx = nx*2*HalfWidth()-HalfWidth(), gy = 160-ny*320 mapping -- i.e. the
// original nx*480-240 / 160-ny*320 mapping when widescreen is off (Wii never
// enables Layout::g_WideLayout).
void InputTranslatorWii::TransformIRNormalized(float nx, float ny, float& gx, float& gy) {
    Layout::TouchToGame(nx, ny, &gx, &gy);
}

// Mirrors InputTranslatorSDL::DrainSDLEvent's FINGERDOWN/MOTION/UP handling,
// collapsed into one call per channel per WPAD_ScanPads() (mainWii.cpp calls
// this once per remote per frame with the freshly-scanned IR state).
void InputTranslatorWii::DrainWiimoteIR(int chan, bool irValid, float x, float y) {
    if (chan < 0 || chan >= MAX_REMOTES) return;

    bool wasValid = prevIRValid[chan];

    if (!wasValid && irValid) {
        // false -> true: press (mirrors SDL_FINGERDOWN).
        float gx, gy;
        TransformIRNormalized(x, y, gx, gy);
        channelX[chan] = gx;
        channelY[chan] = gy;
        channelActive[chan] = true;
        // Port specific: re-arm the press-vs-motion gate -- a tap alone
        // never moves the blade (v1.6.1 semantics), same as SDL.
        motionSinceDown[chan] = false;

        Mortar::Touch::GetInstance().OnPressed(chan + 1, gx, gy);
        pendingEdge[chan] = true;
    } else if (wasValid && irValid) {
        // true, still true: move if position changed (mirrors
        // SDL_FINGERMOTION).
        float gx, gy;
        TransformIRNormalized(x, y, gx, gy);
        if (gx != channelX[chan] || gy != channelY[chan]) {
            channelX[chan] = gx;
            channelY[chan] = gy;
            motionSinceDown[chan] = true;
            Mortar::Touch::GetInstance().OnMoved(chan + 1, gx, gy);
        }
    } else if (wasValid && !irValid) {
        // true -> false: release (mirrors SDL_FINGERUP).
        channelActive[chan] = false;
        Mortar::Touch::GetInstance().OnReleased(chan + 1);
        pendingEdge[chan] = false;
    }
    // false, still false: nothing to do.

    prevIRValid[chan] = irValid;
}

// Mirrors InputTranslatorSDL::DispatchForSimTick's channel 0-7 loop, just
// for the 4 remote channels. Same phase -> InputManager hash mapping:
//   phase == -1 (just-pressed): TouchScreen + TouchMove (if moved) + TouchDown|DOWN_EDGE.
//   phase == 0  (held):         TouchMove (if moved) + TouchDown.
//   phase == 1 (released) and prevActive: TouchUp.
void InputTranslatorWii::DispatchForSimTick() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    Mortar::Touch& touch = Mortar::Touch::GetInstance();
    touch.Update(0.0f);

    for (int chan = 0; chan < MAX_REMOTES; ++chan) {
        int slot = -1;
        for (int s = 0; s < Mortar::Touch::MAX_SLOTS; ++s) {
            if (touch.states1[s].extId == (uint32_t)(chan + 1)) {
                slot = s;
                break;
            }
        }

        bool nowActive = (slot >= 0 && touch.states1[slot].phase < 1);
        bool wasActive = prevActive[chan];
        int  phase     = (slot >= 0) ? touch.states1[slot].phase : 1;

        if (phase == -1) {
            float gx = channelX[chan];
            float gy = channelY[chan];
            bool  isEdge = pendingEdge[chan];
            pendingEdge[chan] = false;

            if (mgr) {
                InputEvent ie;
                memset(&ie, 0, sizeof(ie));
                ie.fingerId = chan;
                ie.x = gx;
                ie.y = gy;

                ie.actionHash  = hashTouchScreen;
                ie.actionFlags = INPUT_ACTION_DOWN;
                mgr->DispatchEvent(&ie);

                if (motionSinceDown[chan]) {
                    ie.actionHash  = hashTouchMoveX[chan];
                    ie.actionFlags = INPUT_ACTION_MOVE;
                    mgr->DispatchEvent(&ie);
                    ie.actionHash  = hashTouchMoveY[chan];
                    mgr->DispatchEvent(&ie);
                }

                ie.actionHash  = hashTouchDown[chan];
                ie.actionFlags = INPUT_ACTION_DOWN | (isEdge ? INPUT_ACTION_DOWN_EDGE : 0u);
                mgr->DispatchEvent(&ie);
            }
        } else if (phase == 0) {
            float gx = channelX[chan];
            float gy = channelY[chan];

            if (mgr) {
                InputEvent ie;
                memset(&ie, 0, sizeof(ie));
                ie.fingerId = chan;
                ie.x = gx;
                ie.y = gy;

                if (motionSinceDown[chan]) {
                    ie.actionHash  = hashTouchMoveX[chan];
                    ie.actionFlags = INPUT_ACTION_MOVE;
                    mgr->DispatchEvent(&ie);

                    ie.actionHash  = hashTouchMoveY[chan];
                    mgr->DispatchEvent(&ie);
                }

                ie.actionHash  = hashTouchDown[chan];
                ie.actionFlags = INPUT_ACTION_DOWN;
                mgr->DispatchEvent(&ie);
            }
        } else if (wasActive && !nowActive) {
            float gx = channelX[chan];
            float gy = channelY[chan];

            if (mgr) {
                InputEvent ie;
                memset(&ie, 0, sizeof(ie));
                ie.actionHash  = hashTouchUp[chan];
                ie.actionFlags = INPUT_ACTION_UP;
                ie.fingerId    = chan;
                ie.x = gx;
                ie.y = gy;
                mgr->DispatchEvent(&ie);
            }
        }

        prevActive[chan] = nowActive;
    }
}

// Mirrors InputTranslatorSDL::ReleaseAllFingers -- synthesize a release for
// every remote channel the engine currently sees as held, then flush the
// Touch ring so DispatchForSimTick starts clean next tick.
void InputTranslatorWii::ReleaseAllFingers() {
    for (int chan = 0; chan < MAX_REMOTES; ++chan) {
        if (!channelActive[chan]) continue;

        Mortar::Touch::GetInstance().OnReleased(chan + 1);

        Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
        if (mgr) {
            InputEvent ie;
            memset(&ie, 0, sizeof(ie));
            ie.actionHash  = hashTouchUp[chan];
            ie.actionFlags = INPUT_ACTION_UP;
            ie.fingerId    = chan;
            ie.x = channelX[chan];
            ie.y = channelY[chan];
            mgr->DispatchEvent(&ie);
        }

        channelActive[chan] = false;
        prevIRValid[chan]   = false;
        prevActive[chan]    = false;
    }

    Mortar::Touch::GetInstance().Update(0.0f);

    for (int chan = 0; chan < MAX_REMOTES; ++chan) {
        motionSinceDown[chan] = false;
        pendingEdge[chan]     = false;
    }
}

#endif // FRUIT_PLATFORM_WII
