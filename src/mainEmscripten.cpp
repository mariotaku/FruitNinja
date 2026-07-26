// Port specific: Emscripten entry point.
// Mirrors mainSDL.cpp but registers Game::frameTick() as the emscripten
// main-loop callback instead of calling the blocking Game::run().
// Platform divergences are marked // Port specific: throughout.

#include <emscripten.h>
#include <emscripten/html5.h>

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "game/SettingsSave.h"
#include "game/GameWork.h"
#include "game/StartupEffects.h"
#include "audio/SoundManager.h"
#include "render/Layout.h"
#include <cstdio>
#include <cstring>

// Port specific: SDL's default log output function writes to stderr on every
// platform. On Emscripten, stderr maps to console.error, which prints a full
// JS call stack per line -- useless noise for routine SDL_Log/LOG_* traffic.
// Route SDL's own logs to stdout (console.log) instead; genuine JS errors/
// aborts still hit stderr/console.error since they never go through this
// callback. Format is "[NNNNNN][LEVEL][TAG] message" -- the 6-digit
// zero-padded prefix is the sim-tick counter (Debug::g_LogTick); [TAG] is
// already embedded in `message` by Debug::Log (src/debug/LoggerSDL.cpp).
static void FnSdlLogToStdout(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message) {
    const char* p = "INFO";
    switch (priority) {
        case SDL_LOG_PRIORITY_VERBOSE:  p = "VERBOSE"; break;
        case SDL_LOG_PRIORITY_DEBUG:    p = "DEBUG";   break;
        case SDL_LOG_PRIORITY_INFO:     p = "INFO";    break;
        case SDL_LOG_PRIORITY_WARN:     p = "WARN";    break;
        case SDL_LOG_PRIORITY_ERROR:    p = "ERROR";   break;
        case SDL_LOG_PRIORITY_CRITICAL: p = "CRITICAL";break;
        default: break;
    }
    fprintf(stdout, "[%06u][%s]%s\n", Debug::g_LogTick, p, message);
    fflush(stdout);
}

// Port specific: Game instance lives as a file-static so the C callback
// can reach it.  Must outlive the emscripten main loop.
static Game g_game;

// Port specific: fixed-timestep accumulator for the Emscripten path.
// Uses the shared FixedStepDriver so the desktop and web builds have a single
// source of truth for frameMs=1000/60 and maxSteps=5.
// Without accumulation, RAF fires at display refresh (120 Hz on a test phone)
// -> sim runs 2x too fast. The accumulator targets 60 ticks/s (the design rate
// hardcoded in SystemManager::Update, binary 0x0018ade0 / DAT_0018ae84).
#include "platform/FixedStepDriver.h"
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
#include "platform/RenderInterp.h"
#endif
static fn::FixedStepDriver g_driver;
static double g_lastTime = -1.0;   // last RAF timestamp from emscripten_get_now()

// Port specific: FPS measurement for the g_ShowFps overlay.
// Mirrors the logic in GameSDL.cpp run(): ~0.5s sliding window.
static float  s_emFps             = 0.0f;
static double s_emFpsWindowMs     = 0.0;
static int    s_emFpsWindowFrames = 0;

// Port specific: IDBFS boot-gate flag.
// 0 = syncfs(true) still pending; 1 = load complete (or failed), safe to init.
// Written from JS via Module._fn_idbfs_ready() (EMSCRIPTEN_KEEPALIVE below).
static volatile int g_idbfs_ready = 0;

// Port specific: web autoplay -- boot-start gate flag.
// 0 = JS has not yet signalled start; 1 = JS called fn_user_started().
// BootWait waits for BOTH g_idbfs_ready AND g_tap_started before calling
// g_game.init().  In the boot-direct shell (no tap overlay), JS calls
// fn_user_started() immediately when Module.calledRun fires, so the gate
// is cleared as soon as loading completes with no user interaction required.
// IDBFS syncfs(true) runs in parallel so it is typically done by then.
static volatile int g_tap_started = 0;

// Port specific: audio-consent overlay (splash-time cushion) -- see
// shell.html's #audio-consent-overlay. Shown AFTER g_game.init() succeeds
// (BootWait), overlaid on the game's own splash (FN::g_AudioConsentPending
// freezes StartupEffects' splashFadeTimer so the splash holds steady behind
// it -- see StartupEffects.h).
//
// The sfx/music on-off PREFERENCE *is* persisted (FruitSaveData's
// "soundOff"/"musicOff" totals, GameInitialise.cpp -- restored into
// game_work.m_bSoundOn/m_bMusicOn before this overlay ever runs). This
// overlay's only job is the browser's separate, unrelated AudioContext
// UNLOCK requirement -- a discrete user gesture some browsers demand before
// any audio can play at all, regardless of the user's sound/music choice.
// The show/skip/style decision reads BOTH the AudioContext's BORN state
// (window.FNAudio.bornSuspended, captured ONCE, SYNCHRONOUSLY, right when
// the context is created -- fnaudio_init, SoundManagerWebAudio.cpp -- NOT a
// resume()-then-poll check, which would race an unrequested resume attempt
// and only resolve asynchronously) AND window.FNAudioPrefs (published just
// below, from the already-loaded game_work flags):
//   - bornSuspended === false (already unlocked -- PWA/home-screen launch,
//     prior sticky activation, a site-level autoplay grant, etc.): SKIP the
//     overlay, nothing to unlock -- fn_audio_consent_done() un-freezes.
//   - prefs.sound === 0 && prefs.music === 0 (both channels off): SKIP the
//     overlay -- nothing to unlock either, since no audio will play; the
//     permanent capture-phase gesture listeners below (fnWakeAudio) will
//     resume() the context on whatever later tap re-enables audio in-game.
//   - prefs.hasSave (a returning user, at least one channel on): show the
//     single-button "TAP TO PLAY" unlock-only overlay -- no sound/music
//     choice offered, their saved preference is never touched. Tap does
//     ctx.resume() then fn_audio_consent_done().
//   - else (true first run, no save yet): the two-button PLAY WITH SOUND /
//     PLAY MUTED overlay -> fn_set_audio_enabled(1|0), which both unlocks
//     and sets the first-run default (safe: no saved preference exists yet
//     to clobber).
//
// game.init() has ALREADY run by the time this overlay can appear (BootWait
// triggers the check right after init succeeds), so game_work always exists
// when any of these callbacks land -- no before/after-init race here.
static volatile int g_gameInited = 0;

// Port specific: called from JS once the initial syncfs(true) callback fires.
// EMSCRIPTEN_KEEPALIVE prevents dead-code elimination so the symbol is visible
// as Module._fn_idbfs_ready() in the generated JS.
extern "C" {
EMSCRIPTEN_KEEPALIVE void fn_idbfs_ready(void) {
    g_idbfs_ready = 1;
}

// Port specific: called from JS on the FIRST-RUN two-button overlay's tap
// (shell.html #audio-consent-overlay -- shown only when no save file exists
// yet, see the g_gameInited comment block above). on=1 for "PLAY WITH
// SOUND", on=0 for "PLAY MUTED". Safe to write game_work.m_bSoundOn/
// m_bMusicOn directly here because a first run has no saved preference to
// clobber -- this call IS the first-run default. By construction the
// overlay only ever appears after g_game.init() has already run (see
// BootWait below), so game_work always exists; apply directly.
// g_gameInited is still checked defensively in case a stray/duplicate call
// somehow lands before init (should not happen). Mirrors v1.6.1
// MainScreen::SoundCallback @0x00195d14 (the same game_work.m_bSoundOn flip
// + SetSFXVolume(0.5f : 0.0f) pattern) plus the m_bMusicOn flag UpdateMusic
// (v1.6.1 @0x001cc18c) reads every frame -- no direct SetMusicVolume call
// needed, UpdateMusic's volume ramp handles that.
EMSCRIPTEN_KEEPALIVE void fn_set_audio_enabled(int on) {
    if (!g_gameInited) return;
    game_work.m_bSoundOn = (on != 0);
    game_work.m_bMusicOn = (on != 0);
    Mortar::SoundManager::GetInstance().SetSFXVolume(on != 0 ? 0.5f : 0.0f);

    // Un-freeze the splash -- StartupEffects' splashFadeTimer resumes
    // draining on the next GameUpdate tick.
    FN::g_AudioConsentPending = false;
}

// Port specific: called from JS once the audio-consent overlay's job is
// done -- either the unlock-only "TAP TO PLAY" overlay was tapped (returning
// user, saved preference already loaded and untouched), or
// _fnMaybeShowAudioConsent decided no overlay was needed at all (already
// unlocked, or both channels off). Clears ONLY the splash-freeze gate --
// never writes game_work.m_bSoundOn/m_bMusicOn (the loaded/first-run-default
// preference from GameInitialise.cpp must survive untouched). The
// SetSFXVolume call is belt-and-braces idempotence: GameInit step 23 already
// set the SFX volume from the loaded m_bSoundOn during g_game.init(), so this
// is a safe no-op repeat, not a source of truth.
EMSCRIPTEN_KEEPALIVE void fn_audio_consent_done(void) {
    if (!g_gameInited) return;
    Mortar::SoundManager::GetInstance().SetSFXVolume(game_work.m_bSoundOn ? 0.5f : 0.0f);
    FN::g_AudioConsentPending = false;
}

// Port specific: called from JS when the user taps the "TAP TO START"
// button on the overlay.  Kept for the tap-overlay path (JS still calls it);
// now also used by boot-direct path where JS calls it immediately on load.
// BootWait requires g_tap_started=1 before calling g_game.init().
EMSCRIPTEN_KEEPALIVE void fn_user_started(void) {
    g_tap_started = 1;
}

// Port specific: web-only -- inject a synthetic SDL_FINGER* event for a
// gesture that started outside the canvas (see shell.html's document-level
// capture IIFE). SDL2-emscripten only listens for touch events on the canvas
// element, so a touch whose touchstart landed outside it is bound by the
// browser to that outside element for its entire lifetime and never reaches
// SDL natively -- even after the finger slides over the canvas. Called from
// JS as Module._fn_web_synth_touch(phase, fingerId, nx, ny) once such a
// finger enters the canvas rect.
//
// phase: 0=down, 1=move, 2=up. nx/ny: normalized canvas-local coords,
// matching the SDL touch coord space DrainSDLEvent expects (same convention
// a native SDL_FINGER* event arrives in). Intentionally UNCLAMPED -- can be
// <0 or >1 once the forwarded finger is dragged past the canvas edge again,
// so InputTranslatorSDL::DrainSDLEvent's SDL_FINGERMOTION out-of-window
// crossing detector (IsOutOfWindow) sees the same shape a native out-of-
// window touch/mouse drag reports, and applies the same release/re-press
// logic (see InputTranslatorSDL.h's out-of-window header comment block).
//
// Pushed via SDL_PushEvent so it flows through the SAME path pollInput
// drains every RAF (DrainSDLEvent -> Mortar::Touch ring buffer ->
// DispatchForSimTick at 60Hz) -- no sim-tick semantics bypassed. A DOWN
// immediately followed by a MOVE in the same JS callback (both queued before
// the next drain) is fine: the ring buffer already handles same-tick
// DOWN+MOVE sequences from native input (see #175 comments above).
//
// fingerId space: reuses the browser's Touch.identifier directly, offset by
// a large constant (0x40000000) so a synthesized finger's id space can never
// collide with a native SDL_FingerID (SDL/emscripten's synthesized-from-
// browser-Touch ids and SDL_TOUCH_MOUSEID both live well below this range).
// InputTranslatorSDL::MapFingerId (src/platform/InputTranslatorSDL.cpp) maps
// any distinct SDL_FingerID to a free 0-15 channel by equality search/first-
// free-slot -- it does not interpret the id's bit pattern -- so the offset
// only needs to guarantee non-collision, which it does.
EMSCRIPTEN_KEEPALIVE void fn_web_synth_touch(int phase, int fingerId, float nx, float ny) {
    static const Sint64 kSynthFingerIdOffset = 0x40000000;

    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.tfinger.timestamp = SDL_GetTicks();
    ev.tfinger.touchId   = 0;
    ev.tfinger.fingerId  = (SDL_FingerID)(kSynthFingerIdOffset + (Sint64)fingerId);
    ev.tfinger.x         = nx;
    ev.tfinger.y         = ny;
    ev.tfinger.dx        = 0.0f;
    ev.tfinger.dy        = 0.0f;
    ev.tfinger.pressure  = 1.0f;

    switch (phase) {
        case 0:  ev.type = SDL_FINGERDOWN;   break;
        case 1:  ev.type = SDL_FINGERMOTION; break;
        case 2:  ev.type = SDL_FINGERUP;     break;
        default: return;
    }

    SDL_PushEvent(&ev);
}
} // extern "C"

// Port specific: web audio teardown on quit.  Restart is a full page reload,
// not a same-instance resume -- confirmed by reading shell.html's
// restart-btn handler (src/platform/emscripten/shell.html ~line 611-627):
// it does FS.syncfs(false, ...) then location.reload(). A full reload tears
// down the whole WASM instance/JS context, so it is safe -- and closest to
// the desktop teardown path -- to run the real g_game.shutdown() teardown
// here (GameDestroy -> delete mGameSound -> ~SoundManager ->
// SDL_CloseAudioDevice, same as the desktop path in main()/GameSDL.cpp).
//
// This call is actually load-bearing on web: emscripten_set_main_loop_arg()
// in main() below is invoked with simulate_infinite_loop=1, so main()'s
// post-loop teardown code (g_game.shutdown() at the bottom of main(), kept
// "for symmetry") is UNREACHABLE on web -- nothing else will ever call
// shutdown() unless this function does.
//
// SoundManager is a process-lifetime singleton -- GameDestroy does NOT call
// SoundManager::Destroy() (see GameInitialise.cpp step-10 note: "SoundManager
// teardown on process exit"), so its AudioContext is still open even after
// shutdown() returns.  Explicitly silence it via the real SoundManager vtable
// API (SFXPauseAll/SongStop -- these forward into the Web Audio backend's
// pauseAllSfx()/songStop(), SoundManagerWebAudio.cpp) so the mixer stops
// producing anything but silence, AND hard-stop + suspend the browser
// AudioContext directly: window.FNAudio.stopAll() stops every live
// AudioBufferSourceNode (SFX + music) and zeroes both master gain nodes as a
// second layer, then ctx.suspend() halts the context clock outright -- the
// browser's Web Audio graph runs independent of the cancelled RAF main loop,
// so silencing sources alone could still leave the graph "running" (silently)
// until the context is suspended, and the user may sit on the restart overlay
// for a while before actually reloading.
//
// Belt-and-suspenders: this EM_ASM ALSO zeroes window.FNAudio.masterSfx/music
// gain directly (not just via stopAll()) so audio is silenced even if
// window.FNAudio.stopAll somehow isn't callable (stale/mismatched JS) --
// gain=0 is a single synchronous property write, unlike ctx.suspend()'s
// async Promise, so it takes effect immediately regardless of suspend timing.
static void StopWebAudioAndShutdown(Game* game) {
    Mortar::SoundManager::GetInstance().SFXPauseAll();
    Mortar::SoundManager::GetInstance().SongStop();

    EM_ASM({
        try {
            // NOTE: window.FNAudio -- SoundManagerWebAudio.cpp's fnaudio_init
            // assigns `window.FNAudio = FN` (not a module-local var), so it is
            // visible here across this separate EM_ASM JS closure.
            var FN = window.FNAudio;
            if (FN) {
                if (typeof FN.stopAll === 'function') { FN.stopAll(); }
                try { if (FN.masterSfx) FN.masterSfx.gain.value = 0; } catch (e) {}
                try { if (FN.music)     FN.music.gain.value     = 0; } catch (e) {}
                var ctx = FN.ctx;
                if (ctx && typeof ctx.suspend === 'function') { ctx.suspend(); }
            }
        } catch (e) {}
    });

    game->shutdown();
}

// Port specific: free function used as the emscripten main-loop callback.
// C++ lambdas with captures cannot be passed as C function pointers, so a
// plain static function is used instead.
static void EmscriptenFrame(void* arg) {
    Game* game = static_cast<Game*>(arg);
    if (!game->running) {
        // Port specific: stop audio + run real teardown BEFORE showing the
        // restart overlay / cancelling the main loop -- see
        // StopWebAudioAndShutdown() above for why this is needed on web.
        StopWebAudioAndShutdown(game);
        // Port specific: show the restart overlay so the user can tap to
        // reload the page and play again.  Must be called BEFORE cancelling
        // the main loop -- the overlay is rendered in the DOM, not the canvas.
        EM_ASM({
            if (typeof window._fnShowRestart === 'function') { window._fnShowRestart(); }
        });
        emscripten_cancel_main_loop();
        return;
    }

    // Port specific: fixed-timestep accumulator (shared FixedStepDriver).
    // Poll input ONCE per RAF (not per sim step) so held-finger TouchDown_N is
    // not re-dispatched during catch-up ticks, which would change slice behaviour
    // on high-refresh displays.  N sim steps run, then ONE render pass.
    // On 60 Hz:  ~16.7 ms -> 1 step, 1 render.
    // On 120 Hz: ~8.3 ms  -> 0 or 1 step per RAF, 60 ticks/s total, 1 render/RAF.
    double now = emscripten_get_now();
    if (g_lastTime < 0.0) {
        g_lastTime = now;
    }
    double elapsed = now - g_lastTime;
    g_lastTime = now;

    // Port specific: FPS overlay measures the PRESENT rate, not the RAF rate.
    // s_emFpsWindowMs sums real wall-clock time every RAF (so the window closes
    // at a true 500ms regardless of how many presents land in it); the frame
    // COUNT is incremented only inside `if (doPresent)` below, next to
    // renderFrame(). With FN::g_FpsCap60 skipping ~2 of every 3 RAFs' presents
    // (see the doPresent gate below), this yields presents/real-second (~60
    // when capped). When uncapped, every RAF presents, so frames == RAF count
    // and the result is unchanged (~120).
    if (FN::g_ShowFps) {
        s_emFpsWindowMs += elapsed;
    }

    game->pollInput();
    if (!game->running) {
        StopWebAudioAndShutdown(game);
        EM_ASM({
            if (typeof window._fnShowRestart === 'function') { window._fnShowRestart(); }
        });
        emscripten_cancel_main_loop();
        return;
    }

    int steps = g_driver.advance(elapsed);
    for (int i = 0; i < steps && game->running; ++i) {
        game->stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
        fn::RenderInterp::Get().SnapshotAfterStep();
#endif
    }
    // Port specific: "Limit to 60 FPS" (FN::g_FpsCap60, SettingsScreen checkbox)
    // -- caps the render/PRESENT rate only. RAF stays native (fps=0 above, no
    // re-registration): pollInput()/g_driver.advance()/stepUpdate() above this
    // point already ran unconditionally for every RAF regardless of the cap,
    // so sim tick rate and input latency are identical either way. Only the
    // draw + SDL_GL_SwapWindow call below (game->renderFrame, which performs
    // both) is skipped on RAFs that don't cross the next scheduled 1/60s
    // boundary. Boundary-accumulator, not a fixed since-last-present budget:
    // s_nextPresentMs advances by exactly kCapPeriodMs (16.6667ms) each
    // present, so presents AVERAGE 60/s regardless of refresh rate --
    // alternating 2-/3-frame skips on 144Hz (~6.94ms/RAF), exactly every-2nd
    // on 120Hz, every-RAF on <=60Hz displays (a fixed since-last budget only
    // hits clean refresh-rate divisors, e.g. 48fps on 144Hz, never 60). If we
    // fall far behind the schedule (tab backgrounded, hitch, or a display
    // slower than 60Hz), rebase to now instead of burst-presenting to catch
    // up. Read g_FpsCap60 live so toggling the checkbox takes effect the next
    // RAF; s_nextPresentMs resets to "uninitialized" when uncapped so
    // re-enabling the cap re-inits cleanly on the next RAF.
    static double s_nextPresentMs = -1.0;
    const double kCapPeriodMs = 1000.0 / 60.0;
    bool doPresent;
    if (FN::g_FpsCap60) {
        if (s_nextPresentMs < 0.0) {
            s_nextPresentMs = now;
        }
        if (now >= s_nextPresentMs) {
            doPresent = true;
            s_nextPresentMs += kCapPeriodMs;
            if (s_nextPresentMs < now) {
                s_nextPresentMs = now + kCapPeriodMs;
            }
        } else {
            doPresent = false;
        }
    } else {
        doPresent = true;
        s_nextPresentMs = -1.0;
    }

    if (doPresent) {
        // Port specific: count this present toward the FPS window (see the
        // s_emFpsWindowMs accumulation above) and roll the window over once
        // it reaches real-time 500ms, independent of how many RAFs occurred.
        if (FN::g_ShowFps) {
            s_emFpsWindowFrames += 1;
            if (s_emFpsWindowMs >= 500.0) {
                s_emFps             = static_cast<float>(s_emFpsWindowFrames / (s_emFpsWindowMs * 0.001));
                s_emFpsWindowMs     = 0.0;
                s_emFpsWindowFrames = 0;
            }
        }
        // Port specific: feed the current FPS to renderFrame so DebugFps_Draw
        // (called inside renderFrame, before SDL_GL_SwapWindow) shows the right value.
        // The desktop run() loop does this via s_currentFps in GameSDL.cpp; here we
        // use the web-specific s_emFps accumulator and push it in via setCurrentFps.
        game->setCurrentFps(s_emFps);

        // Port specific: no binary counterpart. Per-PRESENT UI tick (see
        // Game.h tickRealtimeUi), mirroring GameSDL.cpp Game::run()'s
        // placement -- runs once per PRESENTED frame (inside doPresent, so it
        // naturally follows the FPS-cap gate: native RAF rate when uncapped,
        // ~60Hz when FN::g_FpsCap60 caps presents). s_lastRealtimeUiMs is a
        // separate wall-clock timestamp (ms, emscripten_get_now()) from
        // g_lastTime (which tracks RAF-to-RAF elapsed regardless of
        // doPresent) since this must measure PRESENT-to-PRESENT time.
        // Clamped in tickRealtimeUi's callee against stalls/tab-backgrounding.
        {
            static double s_lastRealtimeUiMs = -1.0;
            if (s_lastRealtimeUiMs < 0.0) {
                s_lastRealtimeUiMs = now;
            }
            double dtPresent = (now - s_lastRealtimeUiMs) * 0.001;
            s_lastRealtimeUiMs = now;
            game->tickRealtimeUi(static_cast<float>(dtPresent));
        }

        game->renderFrame(static_cast<float>(g_driver.alpha()), steps);
    }

    // Port specific: web (#73) -- fade the DOM loading splash out once the game has
    // actually rendered a few frames.  The shell keeps the splash fully opaque over
    // the canvas during runtime load + game init (when the canvas is still blank);
    // deferring the fade until real content is on screen (the game draws its own
    // in-game HB_logo splash, identical to the DOM splash) avoids the few-frame
    // blank/white flash a fade-on-load-complete produced.  One-shot.
    static int  s_renderedFrames = 0;
    static bool s_splashFaded    = false;
    if (!s_splashFaded && doPresent) {
        s_renderedFrames += steps;
        if (s_renderedFrames >= 3) {
            s_splashFaded = true;
            EM_ASM({
                if (typeof window._fnFadeSplash === 'function') { window._fnFadeSplash(); }
            });
        }
    }
}

// Port specific: boot-wait loop callback.
// Spins (yielding to the browser each frame) until g_idbfs_ready is set by
// the JS syncfs(true) callback.  Once ready it calls g_game.init() and
// re-registers the real EmscriptenFrame loop.
// The arg pointer carries both the SDL_Window* and SDL_GLContext; we pack
// them into a small struct so the single void* slot suffices.
struct BootArgs {
    SDL_Window*   window;
    SDL_GLContext gl;
};
static BootArgs g_bootArgs;

static void BootWait(void* arg) {
    if (!g_idbfs_ready || !g_tap_started) {
        // Still waiting for IDBFS syncfs(true) and/or fn_user_started() from JS.
        // With the boot-direct shell, fn_user_started() fires on load-complete
        // with no user gesture required -- typically at the same time as IDBFS.
        return;
    }
    // Both IDBFS load and start signal are done.  Cancel this boot loop before
    // calling init so that emscripten_set_main_loop below replaces it cleanly.
    emscripten_cancel_main_loop();

    BootArgs* ba = static_cast<BootArgs*>(arg);

    // Port specific: load persisted settings. Language, motion mode,
    // sensitivity, and the FPS counter are user-settable via the in-game
    // Settings UI and persisted through SettingsSave/LoadSettings. Runs
    // before g_game.init() so GameInitialise's Localisation::Load step
    // sees the right languageFlag.
    LoadSettings();

    // Port specific: mirrors mainSDL.cpp's desktop widescreen-window default
    // (see mainSDL.cpp "the widescreen setting... drives the DEFAULT window
    // aspect"). On web the canvas backing -- not a native window -- decides
    // what SDL_GL_GetDrawableSize() reports, and Layout::EffectiveAspect()
    // clamps to whatever aspect that drawable actually has (Game::renderFrame
    // already calls SetWindowAspect/ComputeViewport/glViewport off the real
    // drawable size every frame). The gap is that the canvas was created
    // 960x640 (3:2) in main() below, before LoadSettings() -- so the pref
    // wasn't known yet at SDL_CreateWindow time (game.init() below, which the
    // whole boot sequence gates on IDBFS, happens after LoadSettings, but the
    // window itself is created eagerly in main() so GL context creation isn't
    // blocked on the async IDBFS wait). Resize BOTH the SDL window and the
    // emscripten canvas element's backing store here -- right after
    // LoadSettings(), before g_game.init() ever calls renderFrame() -- so the
    // very first frame already reports a 16:9 drawable when the pref is on.
    // emscripten_set_canvas_element_size is required in addition to
    // SDL_SetWindowSize: on Emscripten, SDL's "window" is a thin wrapper
    // around the canvas element and does not itself resize the backing store
    // pixel buffer -- only the real canvas.width/height attributes do that,
    // which is what SDL_GL_GetDrawableSize ultimately reads back via
    // emscripten_get_canvas_element_size. Guarded like the desktop's
    // `#ifndef __bada__` (this whole file is web-only, but Layout::* itself
    // additionally no-ops under __bada__ per Layout.h).
#ifndef __bada__
    if (Layout::IsWideLayout()) {
        const int kWideW = 1136, kWideH = 640; // ~16:9 at 640 tall (1136/640 = 1.775; even width), matches mainSDL.cpp
        SDL_SetWindowSize(ba->window, kWideW, kWideH);
        emscripten_set_canvas_element_size("#canvas", kWideW, kWideH);
    }
    // Port specific: shell.html's resize() (src/platform/emscripten/shell.html)
    // reads the LIVE canvas.width/canvas.height to letterbox the CSS display
    // size to whatever backing aspect is actually present. It also runs once
    // at <script> PARSE time (before this point), when the canvas element
    // still has the browser's default 300x150 attributes -- not this file's
    // 960x640 fallback constant, since `canvas.width || DEFAULT_GAME_W` only
    // falls back on a falsy (zero) width, and the HTML spec default (300) is
    // truthy. That stale 300x150-aspect letterbox is never corrected by
    // itself: emscripten_set_canvas_element_size() (both the 960x640 default
    // just above, from main()'s SDL_CreateWindow, and the 1136x640 branch
    // above) changes the canvas element's width/height attributes directly
    // and does NOT dispatch a DOM `resize` event -- only actual browser
    // window/viewport changes do that (shell.html's own `resize` listener).
    // So without an explicit nudge here, the canvas CSS size (and hence the
    // visible letterbox) stays wrong for the entire session -- both on a
    // plain 3:2 boot AND after a widescreen-toggle restart -- until some
    // unrelated event happens to fire a window `resize` (e.g. a mobile
    // browser's URL-bar show/hide), which is what let this go unnoticed on
    // some devices but not on others (e.g. a TV browser, where no such
    // incidental resize ever fires). Call it unconditionally, once, right
    // after the backing size is finalised for this boot -- covers both the
    // wide and non-wide cases in one place.
    EM_ASM({
        if (typeof window._fnResize === 'function') { window._fnResize(); }
    });
#endif

    // Port specific: parse URL query parameters to set debug flags on web.
    // Enables the hitbox overlay without a physical keyboard (no F1 on mobile).
    // Supported params:
    //   ?hitbox=<0-3> or ?hitboxes=<0-3> -- sets g_DebugHitboxes level directly
    //                                        (same 4 levels F1 cycles through on desktop:
    //                                        0=off 1=entity 2=+HUD 3=+font). Mobile has
    //                                        no F1, so the level is set straight from the URL.
    //   ?timescale=<float>           -- sets g_DebugTimeScale (e.g. ?timescale=0.1 for 10x slow-mo)
    //   ?osdsfx=1                    -- per-SFX OSD readout (g_bOsdSfx, F4 on desktop)
    {
        // hitbox / hitboxes=<level>
        int hitboxLevel = EM_ASM_INT({
            try {
                var qs = window.location.search;
                if (!qs) return 0;
                var params = new URLSearchParams(qs);
                var v = params.get('hitbox');
                if (v === null) v = params.get('hitboxes');
                if (v === null) return 0;
                var n = parseInt(v, 10);
                if (n >= 0 && n <= 3) return n;
                return 0;
            } catch(e) {}
            return 0;
        });
        if (hitboxLevel > 0) {
            FN::g_DebugHitboxes = hitboxLevel;
            LOG_INFO("Debug", "URL param: hitbox overlay level %d", hitboxLevel);
        }

        // timescale=<float>
        double tsParam = EM_ASM_DOUBLE({
            try {
                var qs = window.location.search;
                if (!qs) return -1.0;
                var params = new URLSearchParams(qs);
                var v = params.get('timescale');
                if (v !== null) {
                    var f = parseFloat(v);
                    if (!isNaN(f) && f > 0.0) return f;
                }
            } catch(e) {}
            return -1.0;
        });
        if (tsParam > 0.0) {
            FN::g_DebugTimeScale = (float)tsParam;
            LOG_INFO("Debug", "URL param: timescale = %.3f", FN::g_DebugTimeScale);
        }

        // osdsfx=1 or osdsfx (bare) -- enables the per-SFX OSD readout
        // (same flag F4 toggles on desktop; mobile has no F4).
        int osdSfxParam = EM_ASM_INT({
            try {
                var qs = window.location.search;
                if (!qs) return 0;
                var params = new URLSearchParams(qs);
                var v = params.get('osdsfx');
                if (v !== null && v !== '0') return 1;
            } catch(e) {}
            return 0;
        });
        if (osdSfxParam) {
            FN::g_bOsdSfx = true;
            LOG_INFO("Debug", "URL param: SFX OSD ON");
        }

        // fps=1 or fps (bare) -- enables the FPS counter overlay
        // (same flag F3 toggles on desktop; mobile has no F3). The only way
        // to see frame timing on a TV browser with no DevTools.
        int fpsParam = EM_ASM_INT({
            try {
                var qs = window.location.search;
                if (!qs) return 0;
                var params = new URLSearchParams(qs);
                var v = params.get('fps');
                if (v !== null && v !== '0') return 1;
            } catch(e) {}
            return 0;
        });
        if (fpsParam) {
            FN::g_ShowFps = true;
            LOG_INFO("Debug", "URL param: FPS overlay ON");
        }
    }

    if (!g_game.init(ba->window, ba->gl)) {
        LOG_ERROR("mainEmscripten", "Failed to init game");
        return;
    }

    // Port specific: audio-consent overlay -- game_work now exists (post
    // GameInitialise), so fn_set_audio_enabled / fn_audio_consent_done can
    // apply directly from here on.
    g_gameInited = 1;

    // Freeze the splash (StartupEffects' splashFadeTimer, gated in
    // GameInit.cpp's GameUpdate) UNCONDITIONALLY, BEFORE the game loop's
    // first tick -- so the very first rendered frame is already the frozen
    // one -- then publish the loaded sfx/music preference (game_work is
    // already populated by GameInitialise.cpp at this point) so shell.html's
    // _fnMaybeShowAudioConsent can pick the right one of its 4 branches (see
    // the g_gameInited comment block above) without needing its own copy of
    // the preference logic.
    FN::g_AudioConsentPending = true;
    // NOTE: assign the fields one at a time -- do NOT use a JS object literal
    // here. EM_ASM is a macro and the C preprocessor balances PARENTHESES, not
    // braces, so the commas inside `{ sound: $0, music: $1 }` are parsed as
    // extra macro arguments and the build dies with a confusing "to match this
    // '('" error pointing at the NEXT EM_ASM in the file.
    EM_ASM({
        window.FNAudioPrefs = {};
        window.FNAudioPrefs.sound = $0;
        window.FNAudioPrefs.music = $1;
        window.FNAudioPrefs.hasSave = $2;
        if (typeof window._fnMaybeShowAudioConsent === 'function') { window._fnMaybeShowAudioConsent(); }
    }, game_work.m_bSoundOn ? 1 : 0, game_work.m_bMusicOn ? 1 : 0, FN::g_SaveFileExisted ? 1 : 0);

    // Port specific: web audio (#73) -- resume the suspended AudioContext on the
    // first user gesture, called SYNCHRONOUSLY inside the gesture handler.  Mobile
    // browsers (notably mobile Firefox) only honour AudioContext.resume() within the
    // gesture's transient-activation window; a resume deferred to the RAF callback
    // (the previous SDL_PeepEvents approach) works on desktop but is silently
    // rejected on mobile FF.  A one-shot JS listener that calls resume() directly in
    // the handler works on every browser.  Listeners are capture+passive so the same
    // tap still reaches the SDL canvas and registers as a slice -- input is not
    // consumed.  The Web Audio backend stores its AudioContext at window.FNAudio.ctx
    // (created in SoundManager::Init -> FNAudio.init during g_game.init() above), so
    // the context already exists; resume() on a running context is a no-op, so
    // leaving the listeners installed is harmless.
    EM_ASM({
        var fnWakeAudio = function() {
            try {
                var ctx = (window.FNAudio && window.FNAudio.ctx) ? window.FNAudio.ctx : null;
                if (ctx && ctx.state === 'suspended') { ctx.resume(); }
            } catch (e) {}
        };
        var evs = 'touchstart mousedown pointerdown keydown'.split(' ');
        for (var i = 0; i < evs.length; ++i) {
            window.addEventListener(evs[i], fnWakeAudio, true);
        }
    });

    // Port specific: hand control to the browser event loop.
    // fps=0 lets the browser decide (requestAnimationFrame) = display refresh rate.
    // simulate_infinite_loop=0: we return from main() after the boot loop
    // returned from emscripten_set_main_loop_arg below; the game loop is
    // installed for future RAFs.
    //
    // FN_WEB_FORCE_60FPS (debug isolation switch): drive the loop at a fixed
    // 60Hz setTimeout cadence instead of RAF, so pollInput()+render run at 60Hz
    // regardless of a 120/144Hz panel (1:1 drain:dispatch). Confirms whether the
    // high-refresh decoupling is what breaks touch. Default OFF (RAF).
#if defined(FN_WEB_FORCE_60FPS) && FN_WEB_FORCE_60FPS
    emscripten_set_main_loop_arg(EmscriptenFrame, &g_game, 60, 0);
#else
    emscripten_set_main_loop_arg(EmscriptenFrame, &g_game, 0, 0);
#endif
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Port specific: stdout/stderr flushing -- same rationale as mainSDL.cpp.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Port specific: synthesize SDL_FINGER* from mouse events so the
    // InputTranslator only handles the touch path. Also disable the reverse
    // (touch -> mouse, SDL default "1") so a real touch is not round-tripped
    // back into a second synthetic SDL_FINGER* event: exactly one touch per
    // physical pointer action, touch-only input path.
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    // Port specific: route SDL_Log output to stdout (see FnSdlLogToStdout above)
    // so it lands on console.log instead of console.error's noisy call-stack dump.
    // Registered as early as possible so every subsequent SDL log goes through it.
    SDL_LogSetOutputFunction(FnSdlLogToStdout, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("mainEmscripten", "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Port specific: Emscripten WebGL context.  LEGACY_GL_EMULATION injects
    // the fixed-function shim over WebGL 1; requesting an ES 2.0 context
    // (major=2, minor=0) is what emcc requires to obtain a WebGL context.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    // Port specific: opaque WebGL surface (alpha:false) -- binary used an opaque Bada EGL surface;
    // default alpha:true canvas alpha-composites semi-transparent HUD labels over the page bg -> desaturation
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    SDL_Window* window = SDL_CreateWindow(
        "Fruit Ninja",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) {
        LOG_ERROR("mainEmscripten", "Window failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        LOG_ERROR("mainEmscripten", "GL context failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Port specific: gl_load_functions() is a no-op on non-Windows;
    // under Emscripten all functions resolve via LEGACY_GL_EMULATION shim.
    if (!gl_load_functions()) {
        LOG_ERROR("mainEmscripten", "Failed to load required GL functions");
        return 1;
    }

    LOG_INFO("GL", "GL Vendor: %s", (const char*)glGetString(GL_VENDOR));
    LOG_INFO("GL", "GL Renderer: %s", (const char*)glGetString(GL_RENDERER));
    LOG_INFO("GL", "GL Version: %s", (const char*)glGetString(GL_VERSION));

    // Port specific: mount an IDBFS-backed /save directory before game init
    // so that FruitySave.xml and ItemSave.xml persist across page reloads.
    // FS.mkdir is a no-op if the directory already exists.
    // FS.syncfs(true, cb) populates /save from IndexedDB asynchronously; the
    // callback calls fn_idbfs_ready() which sets g_idbfs_ready so the boot
    // loop can proceed.  If syncfs fails we still set the flag (safe defaults
    // on first run) -- the game handles absent save files gracefully.
    // A beforeunload listener is also registered as a safety-net flush.
    EM_ASM({
        try {
            FS.mkdir('/save');
        } catch(e) {}
        FS.mount(IDBFS, {}, '/save');
        FS.syncfs(true, function(err) {
            if (err) {
                console.warn('IDBFS load error: ' + err);
            }
            Module._fn_idbfs_ready();
        });
        window.addEventListener('beforeunload', function() {
            FS.syncfs(false, function(err) {});
        });
    });

    // Port specific: stash window+gl for the boot-wait callback, then spin
    // waiting for the IDBFS load before calling g_game.init().
    // simulate_infinite_loop=1 here so main() does not return until the game
    // loop is running; BootWait cancels itself and installs EmscriptenFrame.
    g_bootArgs.window = window;
    g_bootArgs.gl     = gl;
    emscripten_set_main_loop_arg(BootWait, &g_bootArgs, 0, 1);

    // Unreachable with simulate_infinite_loop=1; kept for symmetry.
    g_game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
