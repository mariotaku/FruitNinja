// Port specific: Wii WPAD IR + A-button -> Mortar input translator.
// See InputTranslatorWii.h for the full two-role model (per remote: a
// "press finger" on channel N, plus a motion-mode "hover pointer blade" on
// channel 12+N). Both roles feed the Mortar::Touch ring with extId = channel+1;
// the translator raises NO action events of its own -- the binary's per-frame
// poll (InputDeviceBada::Update -> Touch::SendIndividualTouchCallbacks ->
// InputActionMapper) does that. InputTranslatorSDL.{h,cpp}'s mouse handling is
// the reference implementation; every block below cites the SDL counterpart it
// mirrors.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/InputTranslatorWii.h"
#include "input/Touch.h"
#include "render/Layout.h"
#include "debug/DebugFlags.h"
#include <cstring>
#include <cmath>

InputTranslatorWii::InputTranslatorWii() {
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(prevButtonDown, 0, sizeof(prevButtonDown));
    memset(prevIRValid, 0, sizeof(prevIRValid));

    memset(m_PtrGX, 0, sizeof(m_PtrGX));
    memset(m_PtrGY, 0, sizeof(m_PtrGY));
    memset(m_PtrValid, 0, sizeof(m_PtrValid));
    memset(m_PtrAHeld, 0, sizeof(m_PtrAHeld));
    memset(m_PtrSmoothedSpeed, 0, sizeof(m_PtrSmoothedSpeed));
    memset(m_PtrPrevGX, 0, sizeof(m_PtrPrevGX));
    memset(m_PtrPrevGY, 0, sizeof(m_PtrPrevGY));
}

// Nothing left to precompute: the action names live in Input/Input.txt and are
// resolved once by InputManager::LoadConfigFile. Kept as a call site because
// mainWii calls it during boot.
void InputTranslatorWii::Init() {
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

// Mirrors InputTranslatorSDL::PointerPressMouseChannel, generalized to any
// hover-blade channel (12-15). Pushes the press into the Mortar::Touch ring so
// the hover blade claims a real slot -- without one it would raise no Touch<n>
// action at all. No-op if the channel is already pressed.
void InputTranslatorWii::PointerPressChannel(int ch, float gx, float gy) {
    if (fingerActive[ch]) return;

    fingerActive[ch] = true;
    fingerX[ch] = gx;
    fingerY[ch] = gy;

    Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
}

// Mirrors InputTranslatorSDL::PointerReleaseMouseChannel. No-op if the
// channel is not currently active.
void InputTranslatorWii::PointerReleaseChannel(int ch) {
    if (!fingerActive[ch]) return;

    fingerActive[ch] = false;

    Mortar::Touch::GetInstance().OnReleased(ch + 1);
}

// One poll-model drain per remote per frame. Composes the two roles from the
// two raw signals (irValid, aPressed) -- see InputTranslatorWii.h for the
// SDL-mouse mapping rationale of each block.
void InputTranslatorWii::DrainWiimoteIR(int chan, bool irValid, bool aPressed, float x, float y) {
    if (chan < 0 || chan >= MAX_REMOTES) return;

    const int pressCh = chan;                                // Role 1: Touch-slot press finger
    const int hoverCh = FN::WII_POINTER_CHANNEL_FIRST + chan; // Role 2: hover pointer blade

    float gx = 0.0f, gy = 0.0f;
    if (irValid) {
        TransformIRNormalized(x, y, gx, gy);
    }

    // Port specific: on-screen hand-pointer state (WiiPointer overlay). No
    // binary equivalent. Stored every frame regardless of press/hover role --
    // this is purely "where is remote `chan` currently pointing", independent
    // of whether A is held. Smoothed speed is advanced once per sim tick in
    // DispatchForSimTick, not here (this runs once per display frame).
    m_PtrValid[chan] = irValid;
    m_PtrAHeld[chan] = aPressed;
    if (irValid) {
        m_PtrGX[chan] = gx;
        m_PtrGY[chan] = gy;
    }

    // ---- Role 1: A-press finger on channel `chan` (0-3), both modes ----
    // Mirrors the SDL synthesized mouse-finger path (DrainSDLEvent
    // SDL_FINGERDOWN/MOTION/UP, ch < Mortar::Touch::MAX_SLOTS branch):
    // pressed while A is held AND the IR read is valid. Feeding the
    // Mortar::Touch ring is what makes menu buttons / widgets clickable
    // (they consume Touch slots only -- see InputTranslatorWii.h Role 1).
    {
        bool down    = aPressed && irValid;
        bool wasDown = fingerActive[pressCh];

        if (down && !wasDown) {
            // FINGERDOWN: press at the current IR position.
            fingerX[pressCh] = gx;
            fingerY[pressCh] = gy;
            fingerActive[pressCh] = true;

            Mortar::Touch::GetInstance().OnPressed(pressCh + 1, gx, gy);
        } else if (down && wasDown) {
            // FINGERMOTION: SDL only delivers motion events when the pointer
            // actually moved; the poll-model equivalent is a position-change
            // test.
            if (gx != fingerX[pressCh] || gy != fingerY[pressCh]) {
                fingerX[pressCh] = gx;
                fingerY[pressCh] = gy;
                Mortar::Touch::GetInstance().OnMoved(pressCh + 1, gx, gy);
            }
        } else if (!down && wasDown) {
            // FINGERUP: A released, or the IR dot lost while held (release at
            // the last known position in that case -- gx/gy are only fresh
            // when irValid). SDL's FINGERUP also refreshes the position from
            // the up event when it has one.
            if (irValid) {
                fingerX[pressCh] = gx;
                fingerY[pressCh] = gy;
            }
            fingerActive[pressCh] = false;
            Mortar::Touch::GetInstance().OnReleased(pressCh + 1);
        }
    }

    // ---- Role 2: motion-mode hover blade on channel 12+chan ----
    // Mirrors InputTranslatorSDL's raw-mouse MOTION MODE handlers; every
    // sub-block cites its SDL case. Inert when g_MotionMode is OFF (the SDL
    // handlers early-out on !g_MotionMode the same way).
    if (FN::g_MotionMode) {
        bool wasButton = prevButtonDown[chan];

        // A down-edge LIFTS the blade (SDL_MOUSEBUTTONDOWN ->
        // PointerReleaseMouseChannel: reposition without cutting; the menu
        // click itself rides Role 1's Touch-slot press).
        if (aPressed && !wasButton) {
            PointerReleaseChannel(hoverCh);
        }

        // A up-edge re-presses at the pointer (SDL_MOUSEBUTTONUP: `inside &&
        // heldMask == 0` -> PointerPressMouseChannel; here inside==irValid
        // and heldMask==0 is !aPressed).
        if (!aPressed && wasButton && irValid) {
            PointerPressChannel(hoverCh, gx, gy);
        }

        // IR loss releases the blade -- pointing away from the screen is the
        // WPAD analogue of the cursor leaving the window
        // (SDL_WINDOWEVENT_LEAVE -> PointerReleaseMouseChannel).
        if (!irValid && prevIRValid[chan]) {
            PointerReleaseChannel(hoverCh);
        }

        // Hover tracking (SDL_MOUSEMOTION with ev.motion.state == 0 and the
        // cursor inside the window): ensure pressed, then apply the move.
        // The position-change test is the poll-model equivalent of "a motion
        // event arrived" (SDL only delivers them when the cursor moved); a
        // fresh press sets fingerX/Y to gx/gy, so the pressing frame itself
        // never opens the motion gate -- tap semantics preserved.
        if (irValid && !aPressed) {
            PointerPressChannel(hoverCh, gx, gy);
            if (gx != fingerX[hoverCh] || gy != fingerY[hoverCh]) {
                fingerX[hoverCh] = gx;
                fingerY[hoverCh] = gy;
                Mortar::Touch::GetInstance().OnMoved(hoverCh + 1, gx, gy);
            }
        }
    } else if (fingerActive[hoverCh]) {
        // Motion mode toggled OFF while the hover blade was live -- retire it
        // so the channel doesn't stay pressed forever.
        PointerReleaseChannel(hoverCh);
    }

    prevButtonDown[chan] = aPressed;
    prevIRValid[chan]    = irValid;
}

// Mirror of InputTranslatorSDL::DispatchForSimTick: drain the whole Mortar::Touch
// ring for this tick, then advance the Wii-only hand-pointer speed EMA. The
// action events are raised later in the same tick by GameUpdate ->
// InputManager::Update -> InputDeviceBada::Update ->
// Touch::SendIndividualTouchCallbacks, so this must run BEFORE Game::stepUpdate.
void InputTranslatorWii::DispatchForSimTick() {
    Mortar::Touch::GetInstance().Update(0.0f);

    // Port specific: advance the hand-pointer smoothed speed once per sim
    // tick (matching SlashEntity::m_SmoothedSpeed's cadence/units --
    // SlashEntity.cpp's EMA also runs once per sim tick, k=0.4). No binary
    // equivalent. Invalid remotes reset to 0 so a lost/regained IR dot
    // doesn't read a stale high speed from before the loss.
    for (int remote = 0; remote < MAX_REMOTES; ++remote) {
        if (m_PtrValid[remote]) {
            float dx = m_PtrGX[remote] - m_PtrPrevGX[remote];
            float dy = m_PtrGY[remote] - m_PtrPrevGY[remote];
            float spd = sqrtf(dx * dx + dy * dy);
            m_PtrSmoothedSpeed[remote] += (spd - m_PtrSmoothedSpeed[remote]) * 0.4f;
        } else {
            m_PtrSmoothedSpeed[remote] = 0.0f;
        }
        m_PtrPrevGX[remote] = m_PtrGX[remote];
        m_PtrPrevGY[remote] = m_PtrGY[remote];
    }
}

// Port specific: on-screen hand-pointer accessor (WiiPointer overlay). No
// binary equivalent.
bool InputTranslatorWii::GetPointer(int remote, float* gx, float* gy, bool* aHeld, float* speed) const {
    if (remote < 0 || remote >= MAX_REMOTES) return false;
    if (!m_PtrValid[remote]) return false;

    *gx = m_PtrGX[remote];
    *gy = m_PtrGY[remote];
    *aHeld = m_PtrAHeld[remote];
    *speed = m_PtrSmoothedSpeed[remote];
    return true;
}

// Mirrors InputTranslatorSDL::ReleaseAllFingers -- queue a Touch release for
// every held channel (both roles), drain the ring, and clear the per-remote
// edge state so the next DrainWiimoteIR starts clean.
void InputTranslatorWii::ReleaseAllFingers() {
    for (int ch = 0; ch < CHANNEL_COUNT; ++ch) {
        if (!fingerActive[ch]) continue;
        Mortar::Touch::GetInstance().OnReleased(ch + 1);
        fingerActive[ch] = false;
    }

    Mortar::Touch::GetInstance().Update(0.0f);

    memset(prevButtonDown, 0, sizeof(prevButtonDown));
    memset(prevIRValid, 0, sizeof(prevIRValid));
}

#endif // FRUIT_PLATFORM_WII
