// Port specific: Emscripten entry point.
// Mirrors mainSDL.cpp but registers Game::frameTick() as the emscripten
// main-loop callback instead of calling the blocking Game::run().
// Platform divergences are marked // Port specific: throughout.

#include <emscripten.h>

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "game/GameWork.h"

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

// Port specific: called from JS once the initial syncfs(true) callback fires.
// EMSCRIPTEN_KEEPALIVE prevents dead-code elimination so the symbol is visible
// as Module._fn_idbfs_ready() in the generated JS.
extern "C" {
EMSCRIPTEN_KEEPALIVE void fn_idbfs_ready(void) {
    g_idbfs_ready = 1;
}

// Port specific: called from JS when the user taps the "TAP TO START"
// button on the overlay.  Kept for the tap-overlay path (JS still calls it);
// now also used by boot-direct path where JS calls it immediately on load.
// BootWait requires g_tap_started=1 before calling g_game.init().
EMSCRIPTEN_KEEPALIVE void fn_user_started(void) {
    g_tap_started = 1;
}
} // extern "C"

// Port specific: free function used as the emscripten main-loop callback.
// C++ lambdas with captures cannot be passed as C function pointers, so a
// plain static function is used instead.
static void EmscriptenFrame(void* arg) {
    Game* game = static_cast<Game*>(arg);
    if (!game->running) {
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

    game->pollInput();
    if (!game->running) {
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
    game->renderFrame(static_cast<float>(g_driver.alpha()));

    // Port specific: web (#73) -- fade the DOM loading splash out once the game has
    // actually rendered a few frames.  The shell keeps the splash fully opaque over
    // the canvas during runtime load + game init (when the canvas is still blank);
    // deferring the fade until real content is on screen (the game draws its own
    // in-game HB_logo splash, identical to the DOM splash) avoids the few-frame
    // blank/white flash a fade-on-load-complete produced.  One-shot.
    static int  s_renderedFrames = 0;
    static bool s_splashFaded    = false;
    if (!s_splashFaded) {
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

    // Port specific: parse URL query parameters to set debug flags on web.
    // Enables the hitbox overlay without a physical keyboard (no F1 on mobile).
    // Supported params:
    //   ?hitbox=1  or  ?hitboxes=1  -- enables g_DebugHitboxes (same as F1 on desktop)
    //   ?timescale=<float>           -- sets g_DebugTimeScale (e.g. ?timescale=0.1 for 10x slow-mo)
    {
        // hitbox / hitboxes=1
        int hitboxParam = EM_ASM_INT({
            try {
                var qs = window.location.search;
                if (!qs) return 0;
                var params = new URLSearchParams(qs);
                if (params.get('hitbox') === '1' || params.get('hitboxes') === '1') return 1;
            } catch(e) {}
            return 0;
        });
        if (hitboxParam) {
            FN::g_DebugHitboxes = true;
            LOG_INFO("Debug", "URL param: hitbox overlay ON");
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
    }

    if (!g_game.init(ba->window, ba->gl)) {
        fprintf(stderr, "Failed to init game\n");
        return;
    }

    // Port specific: web audio (#73) -- resume the suspended AudioContext on the
    // first user gesture, called SYNCHRONOUSLY inside the gesture handler.  Mobile
    // browsers (notably mobile Firefox) only honour AudioContext.resume() within the
    // gesture's transient-activation window; a resume deferred to the RAF callback
    // (the previous SDL_PeepEvents approach) works on desktop but is silently
    // rejected on mobile FF.  A one-shot JS listener that calls resume() directly in
    // the handler works on every browser.  Listeners are capture+passive so the same
    // tap still reaches the SDL canvas and registers as a slice -- input is not
    // consumed.  SDL2 stores the context at Module.SDL2.audioContext (verified in the
    // generated fruit-ninja.js).  The audio device was opened during g_game.init()
    // above, so the context already exists; resume() on a running context is a no-op,
    // so leaving the listeners installed is harmless.
    EM_ASM({
        var fnWakeAudio = function() {
            try {
                var ctx = (typeof Module !== 'undefined' && Module.SDL2 && Module.SDL2.audioContext)
                          ? Module.SDL2.audioContext
                          : (typeof SDL2 !== 'undefined' ? SDL2.audioContext : null);
                if (ctx && ctx.state === 'suspended') { ctx.resume(); }
            } catch (e) {}
        };
        var evs = 'touchstart mousedown pointerdown keydown'.split(' ');
        for (var i = 0; i < evs.length; ++i) {
            window.addEventListener(evs[i], fnWakeAudio, true);
        }
    });

    // Port specific: hand control to the browser event loop.
    // fps=0 lets the browser decide (requestAnimationFrame).
    // simulate_infinite_loop=0: we return from main() after the boot loop
    // returned from emscripten_set_main_loop_arg below; the game loop is
    // installed for future RAFs.
    emscripten_set_main_loop_arg(EmscriptenFrame, &g_game, 0, 0);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Port specific: Emscripten WebGL context.  LEGACY_GL_EMULATION injects
    // the fixed-function shim over WebGL 1; requesting an ES 2.0 context
    // (major=2, minor=0) is what emcc requires to obtain a WebGL context.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "Fruit Ninja",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "Window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fprintf(stderr, "GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Port specific: gl_load_functions() is a no-op on non-Windows;
    // under Emscripten all functions resolve via LEGACY_GL_EMULATION shim.
    if (!gl_load_functions()) {
        fprintf(stderr, "Failed to load required GL functions\n");
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
