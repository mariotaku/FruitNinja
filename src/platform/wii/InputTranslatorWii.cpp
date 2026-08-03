// Port specific: Wii WPAD IR + A-button -> Mortar input translator.
// See InputTranslatorWii.h for the full two-role model (per remote: a
// "press finger" on channel N feeding Mortar::Touch slots, plus a motion-mode
// "hover pointer blade" on channel 12+N using the pending-bool model).
// InputTranslatorSDL.{h,cpp}'s mouse handling is the reference
// implementation; every block below cites the SDL counterpart it mirrors.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/InputTranslatorWii.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include "render/Layout.h"
#include "debug/DebugFlags.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstring>
#include <cmath>

InputTranslatorWii::InputTranslatorWii()
    : hashTouchScreen(0)
{
    memset(hashTouchDown, 0, sizeof(hashTouchDown));
    memset(hashTouchMoveX, 0, sizeof(hashTouchMoveX));
    memset(hashTouchMoveY, 0, sizeof(hashTouchMoveY));
    memset(hashTouchUp, 0, sizeof(hashTouchUp));
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(prevActive, 0, sizeof(prevActive));
    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
    memset(motionSinceDown, 0, sizeof(motionSinceDown));
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

// Mirrors InputTranslatorSDL::Init -- same "TouchDown_N"/"TouchMove_XN"/
// "TouchMove_YN"/"TouchUp_N"/"TouchScreen" action-name convention for the
// full 16-channel space (Role 1 uses 0-3, Role 2 uses 12-15; the unused
// middle channels cost only the hash precompute).
void InputTranslatorWii::Init() {
    char buf[32];

    for (int i = 0; i < CHANNEL_COUNT; i++) {
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

// Mirrors InputTranslatorSDL::PointerPressMouseChannel, generalized to any
// hover-blade channel (12-15). ch >= 8: no Mortar::Touch slot, pending-bool
// model only. No-op if the channel is already pressed.
void InputTranslatorWii::PointerPressChannel(int ch, float gx, float gy) {
    if (fingerActive[ch]) return;

    fingerActive[ch] = true;
    fingerX[ch] = gx;
    fingerY[ch] = gy;
    motionSinceDown[ch] = false;

    pendingDown[ch] = true;
    pendingEdge[ch] = true;
    pendingUp[ch]   = false;
}

// Mirrors InputTranslatorSDL::PointerReleaseMouseChannel. No-op if the
// channel is not currently active.
void InputTranslatorWii::PointerReleaseChannel(int ch) {
    if (!fingerActive[ch]) return;

    fingerActive[ch] = false;

    pendingUp[ch]   = true;
    pendingDown[ch] = false;
    pendingEdge[ch] = false;
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
            // Re-arm the press-vs-motion gate -- a tap alone never moves the
            // blade (v1.6.1 semantics), same as SDL.
            motionSinceDown[pressCh] = false;

            Mortar::Touch::GetInstance().OnPressed(pressCh + 1, gx, gy);
            pendingEdge[pressCh] = true;
        } else if (down && wasDown) {
            // FINGERMOTION: SDL only delivers motion events when the pointer
            // actually moved; the poll-model equivalent is a position-change
            // test.
            if (gx != fingerX[pressCh] || gy != fingerY[pressCh]) {
                fingerX[pressCh] = gx;
                fingerY[pressCh] = gy;
                motionSinceDown[pressCh] = true;
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
            pendingEdge[pressCh] = false;
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
                motionSinceDown[hoverCh] = true;
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

// Mirror of InputTranslatorSDL::DispatchForSimTick (see that function for the
// binary-cadence rationale): HUD input-modal gate, Touch::Update(0.0f) full
// ring drain, slot-derived dispatch + game_work.m_FingerSpawnPos refresh for
// channels 0-7 (Role 1 lives in 0-3), pending-bool dispatch for channels
// 8-15 (Role 2 lives in 12-15).
void InputTranslatorWii::DispatchForSimTick() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    // Port specific: settings modal captures input -- don't feed the slice
    // blades while it's open (see the matching block in InputTranslatorSDL::
    // DispatchForSimTick). Null out mgr rather than early-returning so the
    // Touch ring still drains and prevActive/pending bookkeeping stays
    // correct.
    if (game_work.mHud && game_work.mHud->GetInputModal()) mgr = nullptr;

    // Drain the entire Touch ring buffer for this tick. Binary-faithful:
    // v1.6.1 Mortar::Touch::Update(dt=0.0) @0x00242d14 with dt==0 skips the
    // timestamp guard and pops every queued TEvnt in order.
    Mortar::Touch& touch = Mortar::Touch::GetInstance();
    touch.Update(0.0f);

    // --- Channels 0-7: derive state from drained states1 (Role 1) ---
    for (int ch = 0; ch < Mortar::Touch::MAX_SLOTS; ++ch) {
        // Find the states1 slot whose extId matches this channel
        // (extId = ch+1, assigned by ___UpdateInternal on press).
        int slot = -1;
        for (int s = 0; s < Mortar::Touch::MAX_SLOTS; ++s) {
            if (touch.states1[s].extId == (uint32_t)(ch + 1)) {
                slot = s;
                break;
            }
        }

        bool nowActive = (slot >= 0 && touch.states1[slot].phase < 1);
        bool wasActive = prevActive[ch];
        int  phase     = (slot >= 0) ? touch.states1[slot].phase : 1;

        // Refresh game_work.m_FingerSpawnPos[slot] from the drained Touch
        // state -- the position source HUD widgets (CheckBox/SliderControl/
        // ComboBox/VerticalScroller, UiWidget PollTouch) read. Same rationale
        // and indexing (by `slot`, not `ch`) as the SDL translator -- see the
        // long comment in InputTranslatorSDL::DispatchForSimTick. .z is the
        // spawn-anim age counter, only stamped on the press edge.
        if (slot >= 0 && phase < 1) {
            _Vector3<float>& spawnPos = game_work.m_FingerSpawnPos[slot];
            spawnPos.x = touch.states1[slot].currX;
            spawnPos.y = touch.states1[slot].currY;
            if (phase == -1) {
                spawnPos.z = 2.0f;
            }
        }

        if (phase == -1) {
            // Just-pressed this tick.
            float gx = fingerX[ch];
            float gy = fingerY[ch];
            bool  isEdge = pendingEdge[ch];
            pendingEdge[ch] = false;

            if (mgr) {
                InputEvent ie;

                FN_MakeTouchButtonEvent(ie, hashTouchScreen, INPUT_ACTION_DOWN, ch, gx, gy);
                mgr->DispatchEvent(&ie);

                if (motionSinceDown[ch]) {
                    FN_MakeTouchAxisEvent(ie, hashTouchMoveX[ch], ch, false, gx, gy);
                    mgr->DispatchEvent(&ie);
                    FN_MakeTouchAxisEvent(ie, hashTouchMoveY[ch], ch, true, gx, gy);
                    mgr->DispatchEvent(&ie);
                }

                FN_MakeTouchButtonEvent(ie, hashTouchDown[ch],
                                        INPUT_ACTION_DOWN | (isEdge ? INPUT_ACTION_DOWN_EDGE : 0u),
                                        ch, gx, gy);
                mgr->DispatchEvent(&ie);
            }
        } else if (phase == 0) {
            // Held finger: emit move + held-down.
            float gx = fingerX[ch];
            float gy = fingerY[ch];

            if (mgr) {
                InputEvent ie;

                // Press-vs-motion gate, same as the press frame.
                if (motionSinceDown[ch]) {
                    FN_MakeTouchAxisEvent(ie, hashTouchMoveX[ch], ch, false, gx, gy);
                    mgr->DispatchEvent(&ie);

                    FN_MakeTouchAxisEvent(ie, hashTouchMoveY[ch], ch, true, gx, gy);
                    mgr->DispatchEvent(&ie);
                }

                FN_MakeTouchButtonEvent(ie, hashTouchDown[ch], INPUT_ACTION_DOWN, ch, gx, gy);
                mgr->DispatchEvent(&ie);
            }
        } else if (wasActive && !nowActive) {
            // Released this tick: emit TouchUp once.
            float gx = fingerX[ch];
            float gy = fingerY[ch];

            if (mgr) {
                InputEvent ie;
                FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch, gx, gy);
                mgr->DispatchEvent(&ie);
            }
        }

        prevActive[ch] = nowActive;
    }

    // --- Channels 8-15: pending-bool model (Role 2, no Mortar::Touch slot) ---
    for (int ch = Mortar::Touch::MAX_SLOTS; ch < CHANNEL_COUNT; ++ch) {
        if (pendingDown[ch]) {
            bool isEdge = pendingEdge[ch];
            pendingDown[ch] = false;
            pendingEdge[ch] = false;

            if (!mgr) continue;

            InputEvent ie;

            FN_MakeTouchButtonEvent(ie, hashTouchScreen, INPUT_ACTION_DOWN, ch,
                                    fingerX[ch], fingerY[ch]);
            mgr->DispatchEvent(&ie);

            // Press-vs-motion gate (see channels 0-7).
            if (motionSinceDown[ch]) {
                FN_MakeTouchAxisEvent(ie, hashTouchMoveX[ch], ch, false,
                                      fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
                FN_MakeTouchAxisEvent(ie, hashTouchMoveY[ch], ch, true,
                                      fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
            }

            FN_MakeTouchButtonEvent(ie, hashTouchDown[ch],
                                    INPUT_ACTION_DOWN | (isEdge ? INPUT_ACTION_DOWN_EDGE : 0u),
                                    ch, fingerX[ch], fingerY[ch]);
            mgr->DispatchEvent(&ie);

        } else if (pendingUp[ch]) {
            pendingUp[ch] = false;
            fingerActive[ch] = false;

            if (mgr) {
                InputEvent ie;
                FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch,
                                        fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
            }

        } else if (fingerActive[ch]) {
            // Held hover blade: emit one TouchMove (if moved) + held TouchDown.
            if (!mgr) continue;

            InputEvent ie;

            // Press-vs-motion gate (see channels 0-7).
            if (motionSinceDown[ch]) {
                FN_MakeTouchAxisEvent(ie, hashTouchMoveX[ch], ch, false,
                                      fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);

                FN_MakeTouchAxisEvent(ie, hashTouchMoveY[ch], ch, true,
                                      fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
            }

            FN_MakeTouchButtonEvent(ie, hashTouchDown[ch], INPUT_ACTION_DOWN, ch,
                                    fingerX[ch], fingerY[ch]);
            mgr->DispatchEvent(&ie);
        }
    }

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

// Mirrors InputTranslatorSDL::ReleaseAllFingers -- synthesize TouchUp for
// every held channel (both roles), flush the Touch ring, clear all pending
// and per-remote edge state so the next DispatchForSimTick starts clean.
void InputTranslatorWii::ReleaseAllFingers() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    for (int ch = 0; ch < CHANNEL_COUNT; ++ch) {
        if (!fingerActive[ch]) continue;

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
        }

        if (mgr) {
            InputEvent ie;
            FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch,
                                    fingerX[ch], fingerY[ch]);
            mgr->DispatchEvent(&ie);
        }

        fingerActive[ch] = false;
        prevActive[ch]   = false;
    }

    // Drain any ring-buffered events that accumulated before the release.
    if (mgr) {
        Mortar::Touch::GetInstance().Update(0.0f);
    }

    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
    memset(motionSinceDown, 0, sizeof(motionSinceDown));
    memset(prevButtonDown, 0, sizeof(prevButtonDown));
    memset(prevIRValid, 0, sizeof(prevIRValid));
}

#endif // FRUIT_PLATFORM_WII
