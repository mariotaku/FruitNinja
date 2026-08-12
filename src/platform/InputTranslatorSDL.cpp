//
// InputTranslatorSDL -- feeds SDL touch/mouse events into the Mortar::Touch
// ring buffer. It raises NO action events of its own: the mapper chain
// (InputManager::LoadConfigFile -> InputActionMapper -> the registered
// callbacks) does that, driven by the binary's own per-frame poll. See the
// header for the full edge-latch / poll-replay model.
//
// Refresh-rate-independent fix for #175 (120Hz slashing bug):
// DrainSDLEvent pushes each touch event IMMEDIATELY into the ring
// (OnPressed/OnMoved/OnReleased). A FINGERDOWN followed by a FINGERUP arriving
// in two consecutive drains before the next sim tick both land in the ring --
// neither edge is lost. DispatchForSimTick calls Touch::Update(0.0f) (v1.6.1
// Mortar::Touch::Update @0x00242d14: dt==0 skips the timestamp guard and drains
// the ENTIRE ring).
//
// At 120Hz: two drains per tick -> two events in ring -> both applied in
// one DispatchForSimTick call. At 60Hz: one drain per tick -> same as
// before. Refresh-rate-independent by construction.
//

#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "render/Layout.h"
#include <cstring>

#ifdef FN_DEBUG_TOUCH
#  define TLOG(fmt, ...) LOG_DEBUG("TOUCH", fmt, ##__VA_ARGS__)
#else
#  define TLOG(...) ((void)0)
#endif

// Port specific: SDL_HINT_MOUSE_TOUCH_EVENTS (mouse -> synthetic SDL_FINGER*)
// was added in SDL 2.0.10; on any older SDL the hint string does not exist in
// the library at all, so setting it (mainSDL.cpp, before SDL_Init) is a
// silent no-op and no SDL_FINGER* ever arrives for a mouse device -- only
// SDL_MOUSEBUTTONDOWN/UP/MOTION. This must be a RUNTIME check: the app
// COMPILES against SDL 2.26.5 headers, but the webOS TV (lk5900) LOADS the
// platform's own much older LG-customized libSDL2-2.0.so.0 (observed SONAME
// libSDL2-2.0.so.0.4.1 -- somewhere in the 2.0.4-2.0.8 range: the string
// "SDL_MOUSE_TOUCH_EVENTS" is absent from the .so, and SDL_GameControllerRumble
// (added 2.0.9) is also missing). A compile-time SDL_VERSION_ATLEAST would see
// 2.26.5 and wrongly assume the hint works. Confirmed on-device: only
// SDL_MOUSEBUTTONDOWN/UP arrive (button=1, a real mouse id), never
// SDL_FINGER*. Computed once, lazily, on first use.
static bool MouseTouchFallbackActive() {
    static bool s_bMouseTouchFallbackComputed = false;
    static bool s_bMouseTouchFallback = false;
    if (!s_bMouseTouchFallbackComputed) {
        SDL_version v;
        SDL_GetVersion(&v);
        s_bMouseTouchFallback =
            SDL_VERSIONNUM(v.major, v.minor, v.patch) < SDL_VERSIONNUM(2, 0, 10);
        LOG_INFO("TOUCH", "SDL runtime %d.%d.%d detected -- mouse-touch fallback %s\n",
                 (int)v.major, (int)v.minor, (int)v.patch,
                 s_bMouseTouchFallback ? "ACTIVE (pre-2.0.10, no mouse->touch synthesis)"
                                       : "inactive");
        s_bMouseTouchFallbackComputed = true;
    }
    return s_bMouseTouchFallback;
}

InputTranslatorSDL::InputTranslatorSDL()
    : motionModeWasOn(FN::g_MotionMode)
{
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(fingerMap, 0xFF, sizeof(fingerMap));
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
#ifdef FN_DEBUG_TOUCH
    // SDL suppresses DEBUG-priority logs by default; lower the cutoff so the
    // TLOG drain trace reaches stdout / the browser console.
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_DEBUG);
    TLOG("FN_DEBUG_TOUCH active -- touch drain trace enabled\n");
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
// Port specific: MOUSE_CHANNEL and HOVER_CHANNEL are carved out of the
// 16-channel space for the mouse only (see InputTranslatorSDL.h). The mouse
// always maps to MOUSE_CHANNEL regardless of press/release history, so
// re-pressing a mouse button deterministically returns to the same blade
// instead of drifting onto whatever channel happens to be free. Touch fingers
// search only the remaining channels, so a touch can never claim either
// reserved channel and the mouse can never claim a touch's channel.
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

    // Check if this touch id is already mapped (skip the reserved channels).
    for (int i = 0; i < 16; i++) {
        if (i == MOUSE_CHANNEL || i == HOVER_CHANNEL) continue;
        if (fingerActive[i] && fingerMap[i] == id)
            return i;
    }
    // Find a free touch channel (skip the reserved channels).
    for (int i = 0; i < 16; i++) {
        if (i == MOUSE_CHANNEL || i == HOVER_CHANNEL) continue;
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

// Port specific: MOTION MODE -- FINGERDOWN-equivalent for HOVER_CHANNEL.
// Mirrors the SDL_FINGERDOWN branch: mark the channel active and push the press
// into the Mortar::Touch ring so the cursor claims a real slot (without one it
// would raise no Touch<n> action at all, and the pointer blade would be dead).
// No-op if the channel is already pressed (repeated hover moves just update
// position).
//
// HOVER_CHANNEL deliberately keeps its fingerMap entry unset: it is never
// reached through MapFingerId/ReleaseFingerId (which key on SDL_TOUCH_MOUSEID,
// and that id belongs to MOUSE_CHANNEL). Only this pair of helpers drives it.
void InputTranslatorSDL::PointerPressHoverChannel(float gx, float gy) {
    if (fingerActive[HOVER_CHANNEL]) return;

    fingerActive[HOVER_CHANNEL] = true;
    fingerX[HOVER_CHANNEL]      = gx;
    fingerY[HOVER_CHANNEL]      = gy;
    // MOTION MODE has its own inside-the-window gate (SDL_MOUSEMOTION's
    // `inside` check + SDL_WINDOWEVENT_LEAVE) -- it never uses the
    // out-of-window suspend/resume path, so keep it clear here.
    fingerSuspended[HOVER_CHANNEL] = false;

    Mortar::Touch::GetInstance().OnPressed(HOVER_CHANNEL + 1, gx, gy);
    TLOG("MOTION press ch=%d game=(%g,%g)\n", HOVER_CHANNEL, gx, gy);
}

// Port specific: MOTION MODE -- FINGERUP-equivalent for HOVER_CHANNEL.
// Mirrors the SDL_FINGERUP branch. No-op if the channel is not currently active.
void InputTranslatorSDL::PointerReleaseHoverChannel() {
    if (!fingerActive[HOVER_CHANNEL]) return;

    fingerActive[HOVER_CHANNEL] = false;

    Mortar::Touch::GetInstance().OnReleased(HOVER_CHANNEL + 1);
    TLOG("MOTION release ch=%d\n", HOVER_CHANNEL);
}

// legacy wrapper -- no-op. The ring drain is DispatchForSimTick().
// Retained so any callers that invoke BeginFrame() are not broken.
void InputTranslatorSDL::BeginFrame() {
}

// legacy wrapper -- calls DrainSDLEvent internally.
// Retained for scene_slash / scene_slash_blade and any other direct callers
// that forward SDL events without going through Game::pollInput.
// Scene code that calls this should also call DispatchForSimTick() once per tick.
void InputTranslatorSDL::ProcessSDLEvent(const SDL_Event& ev, SDL_Window* window) {
    DrainSDLEvent(ev, window);
}

// Release every held finger and clear all channels. Queues a Touch release per
// active channel then drains the ring (Touch::Update(0.0f)) so the state is
// settled immediately. Called when the SDL window loses focus or is minimized
// so no blade stays armed across a background/restore cycle (#162).
void InputTranslatorSDL::ReleaseAllFingers() {
    for (int ch = 0; ch < 16; ++ch) {
        if (!fingerActive[ch]) continue;

        // Port specific: a suspended (out-of-window) channel already fired
        // its release to the engine on the IN->OUT crossing -- don't
        // double-release it here.
        if (!fingerSuspended[ch]) {
            Mortar::Touch::GetInstance().OnReleased(ch + 1);
        }

        fingerActive[ch]    = false;
        fingerSuspended[ch] = false;
    }

    // Drain the releases we just queued so the next sim tick starts clean.
    Mortar::Touch::GetInstance().Update(0.0f);
}

// Drain one SDL event into the Mortar::Touch ring buffer and into per-channel
// SDL bookkeeping (all 16 channels).
//
// Every FINGERDOWN/MOTION/UP is pushed into the ring IMMEDIATELY
// (OnPressed/OnMoved/OnReleased) with extId = channel + 1. This is the core fix
// for the 120Hz slashing bug (#175): both the DOWN and UP from a fast flick
// within one tick interval enter the ring and are applied in order by
// DispatchForSimTick's Touch::Update(0.0f) drain -- no edge lost.
//
// All 16 channels go through the ring, including the reserved MOUSE_CHANNEL.
// Mortar::Touch has 8 slots and claims them by rotation, so the channel number
// is only an SDL-side identity: the SLOT a finger lands in is what the game
// sees, and it is that slot index that becomes the Touch<n> action channel.
// A 9th concurrent pointer finds no free slot and is dropped -- binary-faithful
// (Bada caps point ids at 8, GlesForm::OnTouch* @0x0018334c).
//
// fingerX/Y tracks the latest position for the out-of-window logic and motion
// mode. fingerActive tracks whether the SDL layer considers the finger down.
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
        // Port specific: the SDL-synthesized mouse finger drives MOUSE_CHANNEL
        // in BOTH modes -- that is the UI channel and a click must mean the
        // same thing with motion mode on or off. Motion mode's cursor-tracking
        // blade lives on its own HOVER_CHANNEL (raw SDL_MOUSE* cases below),
        // so the two paths never drive the same channel.
        int ch = MapFingerId(ev.tfinger.fingerId);
        if (ch < 0) { TLOG("  -> MapFingerId returned -1 (all 16 channels busy)\n"); break; }

        float gx, gy;
        TransformTouchNormalized(ev.tfinger.x, ev.tfinger.y, gx, gy);
        fingerX[ch] = gx;
        fingerY[ch] = gy;
        // Port specific: a fresh press always starts in-bounds (SDL only
        // ever raises FINGERDOWN for a coordinate inside the window/canvas).
        fingerSuspended[ch] = false;
        TLOG("FINGERDOWN (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

        // Push into the Touch ring buffer immediately -- preserves the DOWN
        // edge even if an UP arrives before the next DispatchForSimTick.
        Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
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

            Mortar::Touch::GetInstance().OnReleased(ch + 1);
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
            fingerSuspended[ch] = false;

            TLOG("RE-ENTER-WINDOW ch=%d raw=(%g,%g) game=(%g,%g) -- synthesizing fresh press\n",
                 ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

            Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
            break;
        }

        if (wasOut) {
            // Still OUT: keep tracking the physical position (for when it
            // re-enters) but do NOT feed the Touch ring -- the channel is
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
        TLOG("MOVE (drain) ch=%d raw=(%g,%g) game=(%g,%g)\n",
             ch, ev.tfinger.x, ev.tfinger.y, gx, gy);

        Mortar::Touch::GetInstance().OnMoved(ch + 1, gx, gy);
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

        // Port specific: if this channel is currently suspended (out of the
        // window -- see SDL_FINGERMOTION above), the engine already saw a
        // release on the IN->OUT crossing. Just clear the SDL-side mapping
        // here; do NOT fire a second Touch::OnReleased (double
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

        // Push release into the Touch ring buffer immediately. The DOWN (if
        // any) is already in the ring ahead of this UP, so Touch::Update(0.0f)
        // applies them in order. No edge lost.
        Mortar::Touch::GetInstance().OnReleased(ch + 1);
        break;
    }

    // Port specific: MOTION MODE -- raw mouse press LIFTS the hover blade so
    // the two mouse roles are never live at once (the synthesized
    // SDL_FINGERDOWN presses MOUSE_CHANNEL in the same drain, which is what
    // makes the click land). The click itself cannot cut regardless -- the
    // FN::MOTION_CLICK_ONLY_CHANNEL gate in src/game/GameTaskInput.cpp keeps
    // that slot away from the blade -- so while a button is held there is no
    // blade at all. Only meaningful while motion mode is ON; otherwise
    // HOVER_CHANNEL is never pressed and this is a no-op.
    //
    // Port specific: webOS TV delivers SDL_MOUSEBUTTONDOWN/UP for a real
    // pointer device (button=1, a genuine mouse id, not SDL_TOUCH_MOUSEID) --
    // SDL_HINT_MOUSE_TOUCH_EVENTS synthesis does not raise SDL_FINGERDOWN
    // there because the TV's runtime SDL predates 2.0.10, the version that
    // introduced the hint (see MouseTouchFallbackActive above). Bada hardware
    // is touch-only and has no mouse device, so there is no binary semantic
    // to match here -- this is the same event-driven fallback shape as the
    // SDL_MOUSEBUTTONUP case below, mirrored onto the press edge. Gated on
    // MouseTouchFallbackActive() so SDL >= 2.0.10 (desktop/web) never runs
    // this path; the fingerActive[MOUSE_CHANNEL] guard is kept as a second,
    // belt-and-suspenders idempotency check.
    case SDL_MOUSEBUTTONDOWN: {
        if (FN::g_MotionMode) {
            TLOG("MOTION MOUSEBUTTONDOWN -- lifting blade\n");
            PointerReleaseHoverChannel();
        }

        if (MouseTouchFallbackActive() &&
            ev.button.button == SDL_BUTTON_LEFT && !fingerActive[MOUSE_CHANNEL]) {
            int ww = 0, wh = 0;
            if (window) SDL_GetWindowSize(window, &ww, &wh);
            if (ww > 0 && wh > 0) {
                float nx = (float)ev.button.x / (float)ww;
                float ny = (float)ev.button.y / (float)wh;

                int ch = MapFingerId((SDL_FingerID)SDL_TOUCH_MOUSEID);
                if (ch >= 0) {
                    float gx, gy;
                    TransformTouchNormalized(nx, ny, gx, gy);
                    fingerX[ch] = gx;
                    fingerY[ch] = gy;
                    fingerSuspended[ch] = false;
                    TLOG("MOUSEBUTTONDOWN (fallback) ch=%d raw=(%g,%g) game=(%g,%g)\n",
                         ch, nx, ny, gx, gy);

                    Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
                }
            }
        }
        break;
    }

    // Port specific: MOTION MODE -- raw mouse motion drives HOVER_CHANNEL
    // directly (the pointer blade tracks the cursor continuously; whether a
    // cut actually registers is decided by SlashEntity's speed gate). Only
    // while motion mode is ON and no button is currently held (ev.motion.state
    // is the button mask AT this motion event) and the cursor is inside the
    // window. While a button IS held the hover blade stays lifted and the
    // synthesized SDL_FINGERMOTION drives MOUSE_CHANNEL instead, so a drag
    // scrolls/scrubs exactly as it does with motion mode off.
    case SDL_MOUSEMOTION: {
        // Port specific: pre-2.0.10 mouse-touch fallback -- drag edge. No
        // synthetic SDL_FINGERMOTION arrives on these SDL builds, so MOUSE_
        // CHANNEL would otherwise stay frozen at its press position for the
        // whole drag (dead sliders / dead Settings-plate scroll on webOS TV).
        // Mirrors the SDL_FINGERMOTION case above for MOUSE_CHANNEL only,
        // including the out-of-window suspend/resume handling, gated on a
        // fallback press actually being live (fingerActive[MOUSE_CHANNEL]).
        // Independent of the motion-mode hover-cursor block below -- MOUSE_
        // CHANNEL (pressed drag) and HOVER_CHANNEL (unpressed cursor) are
        // separate channels and both run in the same event when applicable.
        if (MouseTouchFallbackActive() && fingerActive[MOUSE_CHANNEL]) {
            int ww = 0, wh = 0;
            if (window) SDL_GetWindowSize(window, &ww, &wh);
            if (ww > 0 && wh > 0) {
                float nx = (float)ev.motion.x / (float)ww;
                float ny = (float)ev.motion.y / (float)wh;

                bool wasOut = fingerSuspended[MOUSE_CHANNEL];
                bool nowOut = IsOutOfWindow(nx, ny);

                if (!wasOut && nowOut) {
                    TLOG("MOUSEMOTION (fallback) OUT-OF-WINDOW ch=%d -- synthesizing release at last in-bounds (%g,%g)\n",
                         MOUSE_CHANNEL, fingerX[MOUSE_CHANNEL], fingerY[MOUSE_CHANNEL]);
                    Mortar::Touch::GetInstance().OnReleased(MOUSE_CHANNEL + 1);
                    fingerSuspended[MOUSE_CHANNEL] = true;
                } else if (wasOut && !nowOut) {
                    float gx, gy;
                    TransformTouchNormalized(nx, ny, gx, gy);
                    fingerX[MOUSE_CHANNEL] = gx;
                    fingerY[MOUSE_CHANNEL] = gy;
                    fingerSuspended[MOUSE_CHANNEL] = false;
                    TLOG("MOUSEMOTION (fallback) RE-ENTER-WINDOW ch=%d game=(%g,%g) -- synthesizing fresh press\n",
                         MOUSE_CHANNEL, gx, gy);
                    Mortar::Touch::GetInstance().OnPressed(MOUSE_CHANNEL + 1, gx, gy);
                } else if (wasOut) {
                    TransformTouchNormalized(nx, ny, fingerX[MOUSE_CHANNEL], fingerY[MOUSE_CHANNEL]);
                    TLOG("MOUSEMOTION (fallback) MOVE (suspended, still out) ch=%d\n", MOUSE_CHANNEL);
                } else {
                    float gx, gy;
                    TransformTouchNormalized(nx, ny, gx, gy);
                    fingerX[MOUSE_CHANNEL] = gx;
                    fingerY[MOUSE_CHANNEL] = gy;
                    TLOG("MOUSEMOTION (fallback) MOVE ch=%d game=(%g,%g)\n", MOUSE_CHANNEL, gx, gy);
                    Mortar::Touch::GetInstance().OnMoved(MOUSE_CHANNEL + 1, gx, gy);
                }
            }
        }

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
        PointerPressHoverChannel(gx, gy);
        fingerX[HOVER_CHANNEL] = gx;
        fingerY[HOVER_CHANNEL] = gy;
        Mortar::Touch::GetInstance().OnMoved(HOVER_CHANNEL + 1, gx, gy);
        TLOG("MOTION MOUSEMOTION ch=%d game=(%g,%g)\n", HOVER_CHANNEL, gx, gy);
        break;
    }

    // safety-net for desktop/web -- SDL_HINT_MOUSE_TOUCH_EVENTS=1
    // synthesizes SDL_FINGERDOWN/MOTION but the UP sometimes arrives as
    // SDL_MOUSEBUTTONUP only, leaving fingerActive set for SDL_TOUCH_MOUSEID.
    // Handle it here as the event-driven fallback for the mouse channel.
    // Port specific: the mouse is always MOUSE_CHANNEL (see MapFingerId), so
    // no channel scan is needed here. Deliberately NOT gated on
    // MouseTouchFallbackActive() -- desktop SDL (>= 2.0.10) can still hit
    // this path per the comment above, so it must keep running unconditionally.
    // It also pairs correctly with a press that came from the pre-2.0.10
    // SDL_MOUSEBUTTONDOWN fallback: that path maps the mouse through the same
    // MapFingerId(SDL_TOUCH_MOUSEID), so fingerActive[MOUSE_CHANNEL] and
    // fingerMap[MOUSE_CHANNEL] are set identically either way.
    case SDL_MOUSEBUTTONUP: {
        // Port specific: MOTION MODE -- releasing the last held button
        // re-presses the HOVER blade at the current position (if the cursor is
        // still inside the window), resuming cursor tracking. This does NOT
        // replace the MOUSE_CHANNEL release below: that channel now carries
        // the real click in both modes, so the fallback must still run.
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
                PointerPressHoverChannel(gx, gy);
                TLOG("MOTION MOUSEBUTTONUP -- re-press ch=%d game=(%g,%g)\n",
                     HOVER_CHANNEL, gx, gy);
            }
        }

        SDL_FingerID mouseId = (SDL_FingerID)SDL_TOUCH_MOUSEID;
        int ch = MOUSE_CHANNEL;
        if (!fingerActive[ch] || fingerMap[ch] != mouseId) break;

        TLOG("MOUSEBUTTONUP (drain) ch=%d game=(%g,%g)\n", ch, fingerX[ch], fingerY[ch]);

        fingerActive[ch] = false;
        ReleaseFingerId(mouseId);

        Mortar::Touch::GetInstance().OnReleased(ch + 1);
        break;
    }

    // Port specific: MOTION MODE -- the cursor leaving the window releases
    // HOVER_CHANNEL (the blade shouldn't stay armed off-screen).
    case SDL_WINDOWEVENT: {
        if (FN::g_MotionMode && ev.window.event == SDL_WINDOWEVENT_LEAVE) {
            TLOG("MOTION WINDOWEVENT_LEAVE -- releasing blade\n");
            PointerReleaseHoverChannel();
        }
        break;
    }

    default:
        break;
    }
    (void)window;
}

// Drain the Mortar::Touch ring buffer for one sim tick.
//
// This is the whole of the port's per-tick input work now. Everything past the
// drain is the binary's own path and runs later in the same tick:
//   GameUpdate @0x001cf644 -> InputManager::Update @0x00243838
//     -> InputDeviceBada::Update @0x00242f40   (global pointer, keys 0x6c/0x74/0x75)
//        -> Touch::SendIndividualTouchCallbacks @0x00242bc4  (per-slot 0x89..0x90)
//           -> InputDevice::AxisEvent / ButtonPressed -> CheckActions
//              -> InputActionMapper::ProcessEvent -> the registered callback.
// The mappers come from InputManager::LoadConfigFile parsing Input/Input.txt at
// GameTaskInitInput time. The SDL side no longer synthesises action events at
// all -- it only feeds Mortar::Touch, and the poll model replays them.
//
// Ordering is load-bearing: Game::stepUpdate calls this BEFORE GameTaskUpdate,
// so states1 is fresh by the time InputDeviceBada::Update polls it.
//
// Binary cadence (v1.6.1 Mortar::Touch::Update @0x00242d14): dt == 0 skips the
// timestamp guard and pops the ENTIRE ring in order via ___UpdateInternal into
// states2, then _Update() snapshots states2 -> states1 and advances the phase
// state machine (-1 just-pressed -> 0 held; phase 1 frees the slot). Because
// _Update copies BEFORE it runs State::Update on states2, a released slot keeps
// a non-zero extId/touchId in states1 for exactly one tick -- that one tick is
// what makes SendIndividualTouchCallbacks' mask-4 release a true edge.
void InputTranslatorSDL::DispatchForSimTick() {
    // Port specific: FN::g_MotionMode is written directly by SettingsScreen /
    // the F5 hotkey -- no SDL event announces the flip. Poll it here: on any
    // transition, release the hover blade. Turning motion OFF stops feeding
    // HOVER_CHANNEL, so without this the channel would stay held forever with
    // no release ever queued (the slot never frees, and the blade never dies).
    if (FN::g_MotionMode != motionModeWasOn) {
        motionModeWasOn = FN::g_MotionMode;
        PointerReleaseHoverChannel();
    }

    Mortar::Touch::GetInstance().Update(0.0f);
}
