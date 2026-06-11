// Port specific: Emscripten entry point.
// Mirrors mainSDL.cpp but registers Game::frameTick() as the emscripten
// main-loop callback instead of calling the blocking Game::run().
// Platform divergences are marked // Port specific: throughout.

#include <emscripten.h>

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/Logger.h"

// Port specific: Game instance lives as a file-static so the C callback
// can reach it.  Must outlive the emscripten main loop.
static Game g_game;

// Port specific: fixed-timestep accumulator state for the Emscripten path.
// The simulation is frame-rate-coupled: SystemManager::Update writes a fixed
// dt = 1/60 s per tick (binary 0x0018ade0, DAT_0018ae84) and physics+spin both
// scale by it, so the real-time game speed equals the TICK RATE. The design
// intent is ~60 ticks/s (hardcoded FPS=59, dt=1/60 -> real-time); the native
// run()'s FRAME_MS=10 is only an upper FPS *floor*, while vsync gates the real
// native rate to the display refresh (~60 on a 60 Hz panel).
// Without accumulation, RAF fires at the display refresh (120 Hz on the test
// phone) -> sim runs 2x too fast. The accumulator must therefore target the
// DESIGN rate of 60 ticks/s (NOT 100 = FRAME_MS/10, which ran ~1.67x fast and
// left fruit trajectories visibly off while spin still "looked" right).
static const double EM_FRAME_MS    = 1000.0 / 60.0;  // target ms per sim tick (60 ticks/s)
static const int    EM_MAX_STEPS   = 5;      // spiral-of-death guard
static double       g_accumulator  = 0.0;    // unprocessed ms carried between RAFs
static double       g_lastTime     = -1.0;   // last RAF timestamp (emscripten_get_now)

// Port specific: free function used as the emscripten main-loop callback.
// C++ lambdas with captures cannot be passed as C function pointers, so a
// plain static function is used instead.
static void EmscriptenFrame(void* arg) {
    Game* game = static_cast<Game*>(arg);
    if (!game->running) {
        emscripten_cancel_main_loop();
        return;
    }

    // Port specific: fixed-timestep accumulator.
    // Accumulate real elapsed time, then drain in FRAME_MS-sized steps so
    // the simulation always advances at the same wall-clock rate regardless
    // of display refresh (60 Hz, 120 Hz, etc.).
    // Target is 60 ticks/s (EM_FRAME_MS ~= 16.667 ms), the design rate.
    // On 60 Hz: ~16.7 ms elapsed -> 1 step per RAF.
    // On 120 Hz: ~8.3 ms elapsed -> 0 steps every other RAF, 1 step the next
    //   -> 60 ticks/s, decoupled from the 120 Hz refresh.
    // Note: frameTick() bundles poll+update+render, so a multi-step callback
    // redraws N times per RAF.  This is acceptable for correctness; a future
    // optimisation could separate update from render for the catch-up steps.
    double now = emscripten_get_now();
    if (g_lastTime < 0.0) {
        g_lastTime = now;
    }
    double elapsed = now - g_lastTime;
    g_lastTime = now;

    // Clamp elapsed to guard against tab-suspend / huge gaps.
    if (elapsed > EM_FRAME_MS * EM_MAX_STEPS) {
        elapsed = EM_FRAME_MS * EM_MAX_STEPS;
    }
    g_accumulator += elapsed;

    int steps = 0;
    while (g_accumulator >= EM_FRAME_MS && steps < EM_MAX_STEPS) {
        game->frameTick();
        g_accumulator -= EM_FRAME_MS;
        ++steps;
    }
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Port specific: stdout/stderr flushing -- same rationale as mainSDL.cpp.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Port specific: synthesize SDL_FINGER* from mouse events so the
    // InputTranslator only handles the touch path.
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

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

    if (!g_game.init(window, gl)) {
        fprintf(stderr, "Failed to init game\n");
        return 1;
    }

    // Port specific: hand control to the browser event loop.
    // fps=0 lets the browser decide (requestAnimationFrame).
    // simulate_infinite_loop=1 keeps Emscripten from returning from main().
    emscripten_set_main_loop_arg(EmscriptenFrame, &g_game, 0, 1);

    // Unreachable with simulate_infinite_loop=1; kept for symmetry.
    g_game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
