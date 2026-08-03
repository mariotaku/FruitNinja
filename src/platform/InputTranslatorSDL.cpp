//
// InputTranslatorSDL -- converts SDL events to Mortar::Touch ring buffer
// entries AND InputManager hash events.
//
// Refresh-rate-independent fix for #175 (120Hz slashing bug):
// DrainSDLEvent pushes each touch event IMMEDIATELY into Mortar::Touch's
// ring buffer (OnPressed/OnMoved/OnReleased) for channels 0-7. This means
// a FINGERDOWN followed by a FINGERUP arriving in two consecutive drains
// before the next sim tick both land in the ring -- neither edge is lost.
// DispatchForSimTick calls Touch::Update(0.0f) (v1.6.1 Mortar::Touch::Update
// @0x00242d14: dt==0 skips the timestamp guard, drains the ENTIRE ring),
// then reads the drained states1 to drive InputManager hash events.
//
// At 120Hz: two drains per tick -> two events in ring -> both applied in
// one DispatchForSimTick call. At 60Hz: one drain per tick -> same as
// before. Refresh-rate-independent by construction.
//

#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include "debug/DebugFlags.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "render/Layout.h"
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
// SlashEntity event-driven dispatch to silently fail. Single source of truth now.

InputTranslatorSDL::InputTranslatorSDL() : hashTouchScreen(0) {
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(prevActive, 0, sizeof(prevActive));
    memset(fingerMap, 0xFF, sizeof(fingerMap));
    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
    memset(motionSinceDown, 0, sizeof(motionSinceDown));
    memset(fingerSuspended, 0, sizeof(fingerSuspended));
}

// Port specific: see the out-of-window release/re-press header comment block
// above. nx/ny are the raw normalized SDL touch coords (window/canvas-local,
// BEFORE Layout::TouchToGame's viewport math) -- exactly what SDL delivers
// for both a captured desktop mouse-drag and a native web canvas touch.
bool InputTranslatorSDL::IsOutOfWindow(float nx, float ny) {
    return nx < 0.0f || nx > 1.0f || ny < 0.0f || ny > 1.0f;
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

        // DIFFERS: original = "TouchReleased_%d" (Data/input/input.txt lines 49-64,
        //   bound to Touch<i+1> action "up"). The name "TouchUp_%d" appears NOWHERE
        //   in the binary or its data. Nothing in the game binds it any more -- the
        //   blade has no release handler (v1.6.1 registers no per-finger release
        //   callback at all), so these events are emitted and dropped. Kept only
        //   because tests still pin the release edge on this hash. Rename to
        //   "TouchReleased_%d" when InputManager::LoadConfigFile @0x002442fc lands.
        sprintf(buf, "TouchUp_%d", i);
        hashTouchUp[i] = StringHash(buf);
    }

    hashTouchScreen = StringHash("TouchScreen");

#ifdef FN_DEBUG_TOUCH
    // SDL suppresses DEBUG-priority logs by default; lower the cutoff so the
    // TLOG drain/dispatch trace reaches stdout / the browser console.
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_DEBUG);
    TLOG("FN_DEBUG_TOUCH active -- touch drain/dispatch trace enabled\n");
#endif
}

// Transform normalized SDL touch coords -> binary-centred ortho coords.
// Ortho: X in [-HalfWidth, +HalfWidth] (horizontal, 240 when not widescreen),
// Y in [-160, +160] (vertical, +up). SDL touch Y is top-down (0 at top), so
// we flip.
// Port convention: TOP = +160, BOTTOM = -160 (Y-up). Bada binary uses
// Y-down (TOP=-160); the discrepancy is handled locally by ScrollingMenu
// (its drag formula negates currY before applying the binary-faithful
// math). Other touch consumers (MenuButton hit-tests, SlashEntity blade
// tracking) work in port's Y-up convention directly.
//
// DIFFERS: original = GlesForm::TransformTouchPos @0x001f1038 (v1.6.1):
//   out.x=(int)(rawDeviceX*480.0/800.0); out.y=(int)(rawDeviceY*320.0/480.0)
//   -- pure anisotropic down-scale of raw portrait device pixels (480x800),
//   NO rotation, NO centring, top-left Y-down. Port takes normalized SDL [0,1]
//   coords -> centred Y-up ortho because the SDL pipeline works in centred
//   Y-up throughout.
//
// Pass 3: delegates to Layout::TouchToGame, which maps through the same
// pillarbox/letterbox viewport rect Game::renderFrame applies via
// Layout::ComputeViewport/SetActiveViewport (GameSDL.cpp) -- single source
// of truth so a widened (opt-in widescreen) field's edges, and the
// centred viewport window/GL apply, both track together. When widescreen
// is off, Layout::TouchToGame reduces exactly to the original
// nx*480-240 / 160-ny*320 mapping (see Layout.cpp).
void InputTranslatorSDL::TransformTouchNormalized(float nx, float ny,
                                                   float& gx, float& gy) {
    Layout::TouchToGame(nx, ny, &gx, &gy);
    TLOG("TransformTouchNormalized raw=(%g,%g) -> game=(%g,%g)\n", nx, ny, gx, gy);
}

// DIFFERS: binary caps touch at 8 channels at the source (Mortar::Touch 8 slots,
// v1.6.1 Touch::FindTouch @0x002429a8 loops i<8; GlesForm::OnTouch* gate GetPointId()<8
// @0x001f1128/0x001f11c4/0x001f10a0). Port uses 16 SDL channels then clamps to
// MAX_SLOTS=8 before Mortar::Touch -- benign superset.
//
// Port specific: MOUSE_CHANNEL is carved out of the 16-channel space for the
// mouse only (see InputTranslatorSDL.h). The mouse always maps to
// MOUSE_CHANNEL regardless of press/release history, so re-pressing a mouse
// button deterministically returns to the same blade instead of drifting
// onto whatever channel happens to be free. Touch fingers search only the
// remaining channels, so a touch can never claim MOUSE_CHANNEL and the mouse
// can never claim a touch's channel.
int InputTranslatorSDL::MapFingerId(SDL_FingerID id) {
    if (id == (SDL_FingerID)SDL_TOUCH_MOUSEID) {
        if (fingerActive[MOUSE_CHANNEL] && fingerMap[MOUSE_CHANNEL] == id)
            return MOUSE_CHANNEL;
        if (!fingerActive[MOUSE_CHANNEL]) {
            fingerMap[MOUSE_CHANNEL] = id;
            fingerActive[MOUSE_CHANNEL] = true;
            return MOUSE_CHANNEL;
        }
        return -1;  // mouse channel already busy (shouldn't happen -- one mouse device)
    }

    // Check if this touch id is already mapped (skip the reserved mouse channel).
    for (int i = 0; i < 16; i++) {
        if (i == MOUSE_CHANNEL) continue;
        if (fingerActive[i] && fingerMap[i] == id)
            return i;
    }
    // Find a free touch channel (skip the reserved mouse channel).
    for (int i = 0; i < 16; i++) {
        if (i == MOUSE_CHANNEL) continue;
        if (!fingerActive[i]) {
            fingerMap[i] = id;
            fingerActive[i] = true;
            return i;
        }
    }
    return -1;  // all touch channels busy
}

void InputTranslatorSDL::ReleaseFingerId(SDL_FingerID id) {
    for (int i = 0; i < 16; i++) {
        if (fingerActive[i] && fingerMap[i] == id) {
            fingerActive[i] = false;
            return;
        }
    }
}

// Port specific: MOTION MODE -- FINGERDOWN-equivalent for MOUSE_CHANNEL.
// MOUSE_CHANNEL (15) is in the 8-15 overflow range (no Mortar::Touch slot),
// so this mirrors the SDL_FINGERDOWN "ch >= 8" branch exactly: mark the
// channel active, re-arm the press-vs-motion gate, and set pendingDown/Edge
// for DispatchForSimTick to pick up on the next sim tick. No-op if the
// channel is already pressed (repeated hover moves just update position).
void InputTranslatorSDL::PointerPressMouseChannel(float gx, float gy) {
    if (fingerActive[MOUSE_CHANNEL]) return;

    fingerMap[MOUSE_CHANNEL]    = (SDL_FingerID)SDL_TOUCH_MOUSEID;
    fingerActive[MOUSE_CHANNEL] = true;
    fingerX[MOUSE_CHANNEL]      = gx;
    fingerY[MOUSE_CHANNEL]      = gy;
    motionSinceDown[MOUSE_CHANNEL] = false;
    // MOTION MODE has its own inside-the-window gate (SDL_MOUSEMOTION's
    // `inside` check + SDL_WINDOWEVENT_LEAVE) -- it never uses the
    // out-of-window suspend/resume path, so keep it clear here.
    fingerSuspended[MOUSE_CHANNEL] = false;

    pendingDown[MOUSE_CHANNEL] = true;
    pendingEdge[MOUSE_CHANNEL] = true;
    pendingUp[MOUSE_CHANNEL]   = false;
    TLOG("MOTION press ch=%d game=(%g,%g)\n", MOUSE_CHANNEL, gx, gy);
}

// Port specific: MOTION MODE -- FINGERUP-equivalent for MOUSE_CHANNEL.
// Mirrors the SDL_FINGERUP "ch >= 8" branch. No-op if the channel is not
// currently active.
void InputTranslatorSDL::PointerReleaseMouseChannel() {
    if (!fingerActive[MOUSE_CHANNEL]) return;

    fingerActive[MOUSE_CHANNEL] = false;
    ReleaseFingerId((SDL_FingerID)SDL_TOUCH_MOUSEID);

    pendingUp[MOUSE_CHANNEL]   = true;
    pendingDown[MOUSE_CHANNEL] = false;
    pendingEdge[MOUSE_CHANNEL] = false;
    TLOG("MOTION release ch=%d\n", MOUSE_CHANNEL);
}

// legacy wrapper -- no-op. Dispatch is now via DispatchForSimTick().
// Retained so any callers that invoke BeginFrame() are not broken.
void InputTranslatorSDL::BeginFrame() {
    // No-op: the drain/flush split moves all dispatch to DispatchForSimTick().
}

// legacy wrapper -- calls DrainSDLEvent internally.
// Retained for scene_slash / scene_slash_blade and any other direct callers
// that forward SDL events without going through Game::pollInput.
// Scene code that calls this should also call DispatchForSimTick() once per tick.
void InputTranslatorSDL::ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    DrainSDLEvent(ev, window);
}

// synthesize TouchUp for every held finger and clear all channels.
// Flushes the Mortar::Touch ring buffer (Touch::Update(0.0f) drains it)
// then releases all states. Called when the SDL window loses focus or is
// minimized so no blade stays armed across a background/restore cycle (#162).
void InputTranslatorSDL::ReleaseAllFingers() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    for (int ch = 0; ch < 16; ++ch) {
        if (!fingerActive[ch]) continue;

        // Port specific: a suspended (out-of-window) channel already fired
        // its release to the engine on the IN->OUT crossing -- don't
        // double-release it here.
        if (!fingerSuspended[ch]) {
            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnReleased(ch + 1);
            }

            if (mgr) {
                InputEvent ie;
                FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch,
                                        fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
            }
        }

        fingerActive[ch]    = false;
        fingerSuspended[ch] = false;
        prevActive[ch]      = false;
    }

    // Drain any ring-buffered events that accumulated before the release,
    // so the next DispatchForSimTick starts with a clean Touch state.
    if (mgr) {
        Mortar::Touch::GetInstance().Update(0.0f);
    }

    // Clear pending bools for ch >= 8 (the non-Touch fallback channels).
    memset(pendingDown, 0, sizeof(pendingDown));
    memset(pendingUp, 0, sizeof(pendingUp));
    memset(pendingEdge, 0, sizeof(pendingEdge));
    memset(motionSinceDown, 0, sizeof(motionSinceDown));
}

// drain one SDL event into Mortar::Touch ring buffer (channels 0-7) and
// into per-channel position state (all 16 channels).
//
// For channels 0-7: each FINGERDOWN/MOTION/UP is IMMEDIATELY pushed into
// the Mortar::Touch ring buffer (OnPressed/OnMoved/OnReleased). This is the
// core fix for the 120Hz slashing bug (#175): both the DOWN and UP from a
// fast flick within one tick interval enter the ring and are applied in
// order by DispatchForSimTick's Touch::Update(0.0f) drain -- no edge lost.
//
// For channels 8-15: falls back to the old pending-bool model since these
// channels have no Mortar::Touch slot.
//
// fingerX/Y always tracks the latest position for InputManager hash events.
// fingerActive tracks whether the SDL layer considers the finger down.
//
// Non-touch events (WINDOW/FOCUS/keyboard) are handled inline as before.
// Called from pollInput() for every SDL_PollEvent result.
void InputTranslatorSDL::DrainSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    switch (ev.type) {

    // === Touch (multitouch, up to 16 fingers) ===
    // Mouse events arrive here too -- SDL_HINT_MOUSE_TOUCH_EVENTS=1 set
    // in mainSDL.cpp before SDL_Init synthesizes SDL_FINGER* from
    // SDL_MOUSE* with finger id = SDL_TOUCH_MOUSEID. Single touch path.

    case SDL_FINGERDOWN: {
        TLOG("SDL_FINGERDOWN fingerId=%lld nx=%.3f ny=%.3f pressure=%.3f\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.pressure);
        // Port specific: motion mode drives MOUSE_CHANNEL from raw
        // SDL_MOUSE* events instead -- suppress the SDL-synthesized
        // finger event so the two paths don't double-drive the channel.
        // Real touch fingers (fingerId != SDL_TOUCH_MOUSEID) pass through.
        if (FN::g_MotionMode && ev.tfinger.fingerId == (SDL_FingerID)SDL_TOUCH_MOUSEID) {
            TLOG("  motion mode: suppressing synthesized mouse FINGERDOWN\n");
            break;
        }
        int ch = MapFingerId(ev.tfinger.fingerId);
        if (ch < 0) { TLOG("  -> MapFingerId returned -1 (all 16 channels busy)\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        // Port specific: re-arm the press-vs-motion gate. Until a real
        // FINGERMOTION drains for this finger, no TouchMove is dispatched
        // (a tap alone never moves the blade -- v1.6.1 semantics).
        motionSinceDown[ch] = false;
        // Port specific: a fresh press always starts in-bounds (SDL only
        // ever raises FINGERDOWN for a coordinate inside the window/canvas).
        fingerSuspended[ch] = false;
        TLOG("FINGERDOWN (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

        if (ch < Mortar::Touch::MAX_SLOTS) {
            // Push into Touch ring buffer immediately -- preserves the DOWN
            // edge even if a UP arrives before the next DispatchForSimTick.
            Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
            // Mark first-press edge for the InputManager DOWN_EDGE flag.
            pendingEdge[ch] = true;
        } else {
            // ch >= 8: no Touch slot; use pending-bool model.
            pendingDown[ch] = true;
            pendingEdge[ch] = true;
            pendingUp[ch]   = false;
        }
        break;
    }

    case SDL_FINGERMOTION: {
        TLOG("SDL_FINGERMOTION fingerId=%lld nx=%.3f ny=%.3f d(%.3f,%.3f)\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y,
             ev.tfinger.dx, ev.tfinger.dy);
        // Port specific: motion mode suppresses the synthesized mouse
        // finger event -- see SDL_FINGERDOWN above.
        if (FN::g_MotionMode && ev.tfinger.fingerId == (SDL_FingerID)SDL_TOUCH_MOUSEID) {
            TLOG("  motion mode: suppressing synthesized mouse FINGERMOTION\n");
            break;
        }
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) { TLOG("  no channel mapped for fingerId, skipping\n"); break; }

        // Port specific: out-of-window release/re-press (see header comment
        // block). Test the RAW normalized coord (pre-viewport) so this works
        // identically for a captured desktop mouse-drag and a native web
        // canvas touch dragged past the canvas edge.
        bool wasOut = fingerSuspended[ch];
        bool nowOut = IsOutOfWindow(ev.tfinger.x, ev.tfinger.y);

        if (!wasOut && nowOut) {
            // IN -> OUT: release at the LAST in-bounds position (fingerX/Y
            // still holds it -- not yet overwritten with the out-of-bounds
            // coord below). Keep the SDL finger-id mapping (fingerActive
            // stays true) so this physical finger can't be stolen by a new
            // press while suspended; just stop feeding the engine.
            TLOG("OUT-OF-WINDOW ch=%d raw=(%g,%g) -- synthesizing release at last in-bounds (%g,%g)\n",
                 ch, ev.tfinger.x, ev.tfinger.y, fingerX[ch], fingerY[ch]);

            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnReleased(ch + 1);
                pendingEdge[ch] = false;
            } else {
                pendingUp[ch]   = true;
                pendingDown[ch] = false;
                pendingEdge[ch] = false;
            }
            fingerSuspended[ch] = true;
            break;  // do not update fingerX/Y to the out-of-bounds coord
        }

        if (wasOut && !nowOut) {
            // OUT -> IN: synthesize a fresh press (new blade stroke) at the
            // new in-bounds position -- mirrors SDL_FINGERDOWN exactly.
            float gx, gy;
            TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
            fingerX[ch] = gx;
            fingerY[ch] = gy;
            motionSinceDown[ch] = false;
            fingerSuspended[ch] = false;

            TLOG("RE-ENTER-WINDOW ch=%d raw=(%g,%g) game=(%g,%g) -- synthesizing fresh press\n",
                 ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

            if (ch < Mortar::Touch::MAX_SLOTS) {
                Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
                pendingEdge[ch] = true;
            } else {
                pendingDown[ch] = true;
                pendingEdge[ch] = true;
                pendingUp[ch]   = false;
            }
            break;
        }

        if (wasOut) {
            // Still OUT: keep tracking the physical position (for when it
            // re-enters) but do NOT feed Touch/pending -- the channel is
            // suspended from the engine's POV.
            TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, fingerX[ch], fingerY[ch]);
            TLOG("MOVE (suspended, still out) ch=%d raw=(%g,%g)\n", ch, ev.tfinger.x, ev.tfinger.y);
            break;
        }

        // Still IN: normal move dispatch (unchanged).
        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        // Port specific: a real motion event arrived -- open the TouchMove
        // gate for this finger (sticky until the next FINGERDOWN).
        motionSinceDown[ch] = true;
        TLOG("MOVE (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

        if (ch < Mortar::Touch::MAX_SLOTS) {
            // Push move into Touch ring buffer immediately.
            Mortar::Touch::GetInstance().OnMoved(ch + 1, gx, gy);
        }
        // Do NOT touch pendingDown/pendingUp for ch < 8.
        // Do NOT modify pending state for ch >= 8 (motion only updates position).
        break;
    }

    case SDL_FINGERUP: {
        TLOG("SDL_FINGERUP fingerId=%lld nx=%.3f ny=%.3f\n",
             (long long)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
        // Port specific: motion mode suppresses the synthesized mouse
        // finger event -- see SDL_FINGERDOWN above.
        if (FN::g_MotionMode && ev.tfinger.fingerId == (SDL_FingerID)SDL_TOUCH_MOUSEID) {
            TLOG("  motion mode: suppressing synthesized mouse FINGERUP\n");
            break;
        }
        int ch = -1;
        for (int i = 0; i < 16; i++) {
            if (fingerActive[i] && fingerMap[i] == ev.tfinger.fingerId) {
                ch = i; break;
            }
        }
        if (ch < 0) { TLOG("  no channel mapped for fingerId, skipping\n"); break; }

        // Port specific: if this channel is currently suspended (out of the
        // window -- see SDL_FINGERMOTION above), the engine already saw a
        // release on the IN->OUT crossing. Just clear the SDL-side mapping
        // here; do NOT fire a second Touch::OnReleased/pendingUp (double
        // release).
        bool wasSuspended = fingerSuspended[ch];

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        if (!wasSuspended) {
            fingerX[ch] = gx;
            fingerY[ch] = gy;
        }
        TLOG("FINGERUP (drain) ch=%d game=(%g,%g) suspended=%d\n", ch, gx, gy, (int)wasSuspended);

        // Mark SDL-layer inactive immediately so MapFingerId can reuse this slot.
        fingerActive[ch] = false;
        fingerSuspended[ch] = false;
        ReleaseFingerId(ev.tfinger.fingerId);

        if (wasSuspended) {
            break;  // already released to the engine on the OUT crossing
        }

        if (ch < Mortar::Touch::MAX_SLOTS) {
            // Push release into Touch ring buffer immediately.
            // The DOWN (if any) is already in the ring ahead of this UP,
            // so Touch::Update(0.0f) will apply them in order. No edge lost.
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
            pendingEdge[ch] = false;
        } else {
            // ch >= 8: pending-bool model.
            pendingUp[ch]   = true;
            pendingDown[ch] = false;
            pendingEdge[ch] = false;
        }
        break;
    }

    // Port specific: MOTION MODE -- raw mouse press LIFTS the blade (so the
    // user can reposition without cutting). Only meaningful while motion
    // mode is ON; otherwise the button-down carries no special handling
    // here (the synthesized SDL_FINGERDOWN drives the channel as usual).
    case SDL_MOUSEBUTTONDOWN: {
        if (!FN::g_MotionMode) break;
        TLOG("MOTION MOUSEBUTTONDOWN -- lifting blade\n");
        PointerReleaseMouseChannel();
        break;
    }

    // Port specific: MOTION MODE -- raw mouse motion drives MOUSE_CHANNEL
    // directly (the pointer blade tracks the cursor continuously; whether a
    // cut actually registers is decided by SlashEntity's speed gate). Only
    // while motion mode is ON and no button is currently held (ev.motion.state
    // is the button mask AT this motion event) and the cursor is inside the
    // window. Off: SDL_MOUSEMOTION is ignored here as before -- the
    // synthesized SDL_FINGERMOTION path (only emitted while a button is
    // held) drives the blade instead.
    case SDL_MOUSEMOTION: {
        if (!FN::g_MotionMode) break;
        if (ev.motion.state != 0) break;  // a button is held -- not hovering

        int ww = 0, wh = 0;
        if (window) SDL_GetWindowSize(window, &ww, &wh);
        bool inside = (ww > 0 && wh > 0 &&
                       ev.motion.x >= 0 && ev.motion.x < ww &&
                       ev.motion.y >= 0 && ev.motion.y < wh);
        if (!inside) break;

        float nx = (float)ev.motion.x / (float)ww;
        float ny = (float)ev.motion.y / (float)wh;
        float gx, gy;
        TransformTouchNormalized(nx, ny, gx, gy);

        // Ensure pressed (no-op if already active), then apply the move --
        // same same-tick DOWN+MOTION merge the touch path already relies on
        // (see the FINGERDOWN comment in DispatchForSimTick).
        PointerPressMouseChannel(gx, gy);
        fingerX[MOUSE_CHANNEL] = gx;
        fingerY[MOUSE_CHANNEL] = gy;
        motionSinceDown[MOUSE_CHANNEL] = true;
        TLOG("MOTION MOUSEMOTION ch=%d game=(%g,%g)\n", MOUSE_CHANNEL, gx, gy);
        break;
    }

    // safety-net for desktop/web -- SDL_HINT_MOUSE_TOUCH_EVENTS=1
    // synthesizes SDL_FINGERDOWN/MOTION but the UP sometimes arrives as
    // SDL_MOUSEBUTTONUP only, leaving fingerActive set for SDL_TOUCH_MOUSEID.
    // Handle it here as the event-driven fallback for the mouse channel.
    // Port specific: the mouse is always MOUSE_CHANNEL (see MapFingerId), so
    // no channel scan is needed here.
    case SDL_MOUSEBUTTONUP: {
        // Port specific: MOTION MODE -- releasing the last held button
        // re-presses the blade at the current position (if the cursor is
        // still inside the window). This replaces the plain release
        // fallback below while motion mode is ON (PointerReleaseMouseChannel
        // on button-down already lifted the blade, so fingerActive is false
        // here and the fallback code below would be a no-op anyway).
        if (FN::g_MotionMode) {
            int ww = 0, wh = 0;
            if (window) SDL_GetWindowSize(window, &ww, &wh);
            bool inside = (ww > 0 && wh > 0 &&
                           ev.button.x >= 0 && ev.button.x < ww &&
                           ev.button.y >= 0 && ev.button.y < wh);
            Uint32 heldMask = SDL_GetMouseState(nullptr, nullptr);
            if (inside && heldMask == 0) {
                float nx = (float)ev.button.x / (float)ww;
                float ny = (float)ev.button.y / (float)wh;
                float gx, gy;
                TransformTouchNormalized(nx, ny, gx, gy);
                PointerPressMouseChannel(gx, gy);
                TLOG("MOTION MOUSEBUTTONUP -- re-press ch=%d game=(%g,%g)\n",
                     MOUSE_CHANNEL, gx, gy);
            }
            break;
        }

        SDL_FingerID mouseId = (SDL_FingerID)SDL_TOUCH_MOUSEID;
        int ch = MOUSE_CHANNEL;
        if (!fingerActive[ch] || fingerMap[ch] != mouseId) break;

        TLOG("MOUSEBUTTONUP (drain) ch=%d game=(%g,%g)\n", ch, fingerX[ch], fingerY[ch]);

        fingerActive[ch] = false;
        ReleaseFingerId(mouseId);

        if (ch < Mortar::Touch::MAX_SLOTS) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
            pendingEdge[ch] = false;
        } else {
            pendingUp[ch]   = true;
            pendingDown[ch] = false;
            pendingEdge[ch] = false;
        }
        break;
    }

    // Port specific: MOTION MODE -- the cursor leaving the window releases
    // MOUSE_CHANNEL (the blade shouldn't stay armed off-screen).
    case SDL_WINDOWEVENT: {
        if (FN::g_MotionMode && ev.window.event == SDL_WINDOWEVENT_LEAVE) {
            TLOG("MOTION WINDOWEVENT_LEAVE -- releasing blade\n");
            PointerReleaseMouseChannel();
        }
        break;
    }

    default:
        break;
    }
    (void)window;
}

// Drain the Mortar::Touch ring buffer and dispatch InputManager hash events
// for one sim tick.
//
// Binary cadence (v1.6.1 Mortar::Touch::Update @0x00242d14): each game tick calls
// Touch::Update(0.0) which, because dt==0, skips the timestamp guard and
// pops the ENTIRE ring buffer in order via ___UpdateInternal into states2,
// then _Update() snapshots states2->states1 and advances phase state
// (-1 just-pressed -> 0 held; phase==1 -> free slot).
//
// This function mirrors that cadence:
//   1. Touch::Update(0.0f) -- drain all queued events, advance state machine.
//   2. For each channel 0-7: read drained states1[slot] (extId == ch+1) to
//      determine phase, then emit InputManager hash events accordingly.
//   3. For channels 8-15: read pending-bool model (unchanged from before).
//
// Phase -> InputManager hash mapping:
//   phase == -1 (just-pressed, one tick): emit TouchScreen + TouchMove + TouchDown
//      with INPUT_ACTION_DOWN | INPUT_ACTION_DOWN_EDGE.
//   phase == 0 (held): emit TouchMove + TouchDown (held, no DOWN_EDGE).
//   phase == 1 (released/free) and prevActive[ch]: emit TouchUp.
//
// prevActive[ch] tracks whether the channel was active (phase < 1) on the
// previous DispatchForSimTick so that a release can be detected exactly once.
void InputTranslatorSDL::DispatchForSimTick() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();

    // Port specific: settings modal captures input -- don't feed the slice
    // blade (TouchDown_N / TouchMove_XN / TouchMove_YN) while it's open. See
    // HUD::SetInputModal (src/hud/HUD.h). This is the per-finger dispatch site
    // to InputManager; GameTaskInput.cpp's TouchDownCallback @0x001cbf18 and
    // PointerMoveCallback @0x001cbfcc are the handlers on the far side and they
    // are what forward each event to g_pSlashEntities[n]. Null out mgr rather than early-
    // returning so the Touch ring buffer still drains this tick (below) and
    // prevActive/pending bookkeeping stays correct -- otherwise events queue
    // up while the modal is open and burst-apply once it closes.
    if (game_work.mHud && game_work.mHud->GetInputModal()) mgr = nullptr;

    // Drain the entire Touch ring buffer for this tick. Binary-faithful:
    // v1.6.1 Mortar::Touch::Update(dt=0.0) @0x00242d14 with dt==0 skips the
    // timestamp guard and pops every queued TEvnt in order.
    // This is safe to call even when mgr is null (headless): Touch state
    // still advances correctly; InputManager dispatch below is gated on mgr.
    Mortar::Touch& touch = Mortar::Touch::GetInstance();
    touch.Update(0.0f);

    // --- Channels 0-7: derive state from drained states1 ---
    for (int ch = 0; ch < Mortar::Touch::MAX_SLOTS; ++ch) {
        // Find the states1 slot whose extId matches this channel.
        // extId = ch+1 (1-based) was assigned by ___UpdateInternal on press.
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

        // DIFFERS: original writes game_work.m_FingerSpawnPos from the touch
        //   callbacks themselves -- TouchDownCallback @0x001cbf18 stamps .z and
        //   PointerMoveCallback @0x001cbfcc stores .x/.y, both indexed by the
        //   ACTION CHANNEL. Those two are ported now and do exactly that. This
        //   write stays anyway, because the port's action channel is extId-1
        //   while the binary's is the Mortar::Touch::states1 SLOT index (see
        //   Touch::SendIndividualTouchCallbacks @0x00242bc4), and every UI widget
        //   (UiCheckbox/UiSlider/UiDropdown, CheckBox/SliderControl/ComboBox/
        //   VerticalScroller) reads m_FingerSpawnPos at the slot TouchInRegion
        //   handed it. Drop this block once DispatchForSimTick dispatches per
        //   states1 slot -- then channel == slot and the callbacks cover it.
        // .z is the spawn-anim age counter, independently owned and decremented
        // by GameInit.cpp's per-frame loop (2 -> 0 -> -1); only stamp it on the
        // press edge (mirroring the binary's fresh-spawn write) and leave it
        // alone while held so the aging isn't clobbered every tick.
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

            // Port specific: a tap emits no blade move; only real finger
            // motion moves the blade (v1.6.1 semantics; the SDL synth
            // previously moved the blade on press). motionSinceDown[ch] is
            // set ONLY when an SDL_FINGERMOTION drains for this finger --
            // never by the press itself -- so a stationary TAP (DOWN..UP,
            // no MOTION) emits TouchScreen + TouchDown and NO TouchMove:
            // the blade never receives a tap's position and consecutive
            // taps cannot bridge into a slash (including when SlashEntity's
            // bomb-hit latch blocks Reset). A fast swipe whose DOWN and
            // first MOTION drain within the same tick still emits the move
            // here (the flag is already true by dispatch time).
            // NB: do NOT gate this on a curr-vs-prev position compare of
            // touch.states1 -- prevX/Y holds the PREVIOUS stroke's position
            // on a fresh press, so every tap at a new location compares
            // unequal and the gate never closes (the old broken heuristic).

            TLOG("DispatchForSimTick FINGERDOWN ch=%d slot=%d edge=%d moved=%d game=(%g,%g)\n",
                 ch, slot, (int)isEdge, (int)motionSinceDown[ch], gx, gy);

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

            TLOG("DispatchForSimTick HELD ch=%d slot=%d game=(%g,%g)\n",
                 ch, slot, gx, gy);

            if (mgr) {
                InputEvent ie;

                // Port specific: same press-vs-motion gate as the press
                // frame -- a held-but-never-moved finger (long-press tap)
                // emits TouchDown only (re-arms the blade latch at the
                // stroke's existing position) and no TouchMove.
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

            TLOG("DispatchForSimTick FINGERUP ch=%d game=(%g,%g)\n", ch, gx, gy);

            if (mgr) {
                InputEvent ie;
                FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch, gx, gy);
                mgr->DispatchEvent(&ie);
            }
        }

        prevActive[ch] = nowActive;
    }

    // --- Channels 8-15: pending-bool model (no Mortar::Touch slot) ---
    for (int ch = Mortar::Touch::MAX_SLOTS; ch < 16; ++ch) {
        if (pendingDown[ch]) {
            bool isEdge = pendingEdge[ch];
            pendingDown[ch] = false;
            pendingEdge[ch] = false;

            if (!mgr) continue;

            TLOG("DispatchForSimTick FINGERDOWN ch=%d (overflow) edge=%d game=(%g,%g)\n",
                 ch, (int)isEdge, fingerX[ch], fingerY[ch]);

            InputEvent ie;

            FN_MakeTouchButtonEvent(ie, hashTouchScreen, INPUT_ACTION_DOWN, ch,
                                    fingerX[ch], fingerY[ch]);
            mgr->DispatchEvent(&ie);

            // Port specific: press-vs-motion gate (see channels 0-7).
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

            TLOG("DispatchForSimTick FINGERUP ch=%d (overflow) game=(%g,%g)\n",
                 ch, fingerX[ch], fingerY[ch]);

            if (mgr) {
                InputEvent ie;
                FN_MakeTouchButtonEvent(ie, hashTouchUp[ch], INPUT_ACTION_UP, ch,
                                        fingerX[ch], fingerY[ch]);
                mgr->DispatchEvent(&ie);
            }

        } else if (fingerActive[ch]) {
            // Held overflow finger: emit one TouchMove at current pos.
            if (!mgr) continue;

            TLOG("DispatchForSimTick HELD ch=%d (overflow) game=(%g,%g)\n",
                 ch, fingerX[ch], fingerY[ch]);

            InputEvent ie;

            // Port specific: press-vs-motion gate (see channels 0-7).
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
}
