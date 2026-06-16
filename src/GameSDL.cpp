// SDL backend for Game — init / run / runFrames live here so the rest of
// Game.cpp stays portable for the asm-verify cross-build. The void* fields
// in Game.h are cast to SDL_Window* / SDL_GLContext at the SDL boundary.

#include "Game.h"
#include "game/GameWork.h"
#include <SDL.h>
#include "platform/InputTranslatorSDL.h"
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "game/GameTaskState.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "config.h"
#include "render/gl_funcs.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#if defined(_WIN32)
    #include <direct.h>      // _mkdir on Windows
#else
    #include <sys/stat.h>    // mkdir on POSIX / Emscripten
#endif

// Port specific: screenshot capture flag. Set from the F12 key handler and
// read+cleared in frameTick before SDL_GL_SwapWindow (same thread, no signal).
static bool g_takeScreenshot = false;

// Port specific: perform glReadPixels + SDL_SaveBMP when g_takeScreenshot is set.
// Called just before SDL_GL_SwapWindow so GL_BACK holds the finished frame.
static void do_screenshot_if_requested(SDL_Window* window) {
    if (!g_takeScreenshot) return;
    g_takeScreenshot = false;

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    // Read pixels bottom-up (GL convention).
    std::vector<unsigned char> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    // Flip rows vertically into a second buffer (GL gives bottom-up, BMP needs top-down).
    std::vector<unsigned char> flipped(px.size());
    const size_t row = static_cast<size_t>(w) * 4u;
    for (int y = 0; y < h; ++y) {
        memcpy(flipped.data() + static_cast<size_t>(y) * row,
               px.data() + static_cast<size_t>(h - 1 - y) * row,
               row);
    }

    // Build SDL_Surface from the top-down RGBA buffer.
    // GL_RGBA / GL_UNSIGNED_BYTE gives bytes in R,G,B,A order.
    // SDL_PIXELFORMAT_ABGR8888 interprets a 32-bit little-endian word as A<<24|B<<16|G<<8|R,
    // which matches byte order R,G,B,A in memory -- correct for GL_RGBA readback.
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), w, h, 32, w * 4,
        SDL_PIXELFORMAT_ABGR8888);
    if (!surf) {
        LOG_ERROR("Screenshot", "SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return;
    }

    // Ensure screenshots/ directory exists.
#if defined(_WIN32)
    _mkdir("screenshots");
#else
    mkdir("screenshots", 0755);
#endif

    // Build filename from monotonic counter (avoids overwriting previous shots).
    static int s_counter = 0;
    char path[64];
    snprintf(path, sizeof(path), "screenshots/shot_%04d.bmp", s_counter++);

    if (SDL_SaveBMP(surf, path) != 0) {
        LOG_ERROR("Screenshot", "SDL_SaveBMP failed: %s", SDL_GetError());
    } else {
        // Print absolute path so it's easy to find.
        char abspath[512] = {0};
#if defined(_WIN32)
        _fullpath(abspath, path, sizeof(abspath));
#else
        if (!realpath(path, abspath)) {
            strncpy(abspath, path, sizeof(abspath) - 1);
        }
#endif
        LOG_INFO("Screenshot", "saved %s", abspath);
    }

    SDL_FreeSurface(surf);
}

// Matches: FruitNinja::OnAppInitializing flow
bool Game::init(void* win, void* gl) {
    window = win;        // SDL_Window* stored as void* in the header
    gl_context = gl;     // SDL_GLContext stored as void*
    if (!inputTranslator) {
        inputTranslator = new InputTranslatorSDL();
        inputTranslator->Init();   // pre-compute action hashes; safe before GameInitialise
    }
    data_dir = FN_DATA_DIR;
    Mortar::TextureManager::SetDataDir(data_dir.c_str());

    // DisplayManager holds game-space dimensions (480×320), not SDL pixel dimensions.
    // glViewport in run() handles pixel scaling independently.
    // Constructor already sets this; explicit here to match original Bada GlesForm init.
    Mortar::DisplayManager::GetInstance().SetWindowSize(0, 0, FN_SCREEN_W, FN_SCREEN_H);

    if (!renderer.init()) {
        fprintf(stderr, "Failed to init renderer\n");
        return false;
    }

    // One-shot GL state init — matches FruitNinja::InitGL @ 0x00181e54.
    int initW, initH;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &initW, &initH);
    renderer.InitGL(initW, initH);

    // Matches original lifecycle:
    GamePreInitialise();   // zero game fields
    GameInitialise();      // boot all engine singletons + load shared data

    // Start in Splash state (will auto-transition to Game)
    game_work.taskStateIndex = 0;
    running = true;
    return true;
}

// Port specific: one complete game tick — poll events, update, render, present.
// Extracted from run() so the Emscripten main loop can call it as a callback
// (emscripten_set_main_loop_arg) without the surrounding while-loop or SDL_Delay.
// Native Game::run() below calls this each iteration; behaviour is identical.
void Game::frameTick() {
    // === SDL events -> InputManager ===
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
#ifdef FN_DEBUG_TOUCH
        // Confirm which SDL event types arrive for mouse/finger input.
        // SDL_HINT_MOUSE_TOUCH_EVENTS=1 should synthesize FINGER* from MOUSE*;
        // if MOUSEBUTTONDOWN shows here instead of FINGERDOWN, the hint is not active.
        // MOUSEMOTION is intentionally excluded -- it fires every frame the
        // cursor moves and we don't handle it (touch uses FINGER* events).
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP ||
            ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERMOTION || ev.type == SDL_FINGERUP) {
            LOG_DEBUG("TOUCH", "poll ev.type=0x%x (%s)\n", ev.type,
                ev.type == SDL_MOUSEBUTTONDOWN ? "MOUSEBUTTONDOWN" :
                ev.type == SDL_MOUSEBUTTONUP   ? "MOUSEBUTTONUP" :
                ev.type == SDL_FINGERDOWN       ? "FINGERDOWN" :
                ev.type == SDL_FINGERMOTION     ? "FINGERMOTION" :
                ev.type == SDL_FINGERUP         ? "FINGERUP" : "?");
        }
#endif
        if (ev.type == SDL_QUIT) {
            running = false;
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            running = false;
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F1) {
            FN::g_DebugHitboxes = !FN::g_DebugHitboxes;
            LOG_DEBUG("Debug", "Hitboxes %s", FN::g_DebugHitboxes ? "ON" : "OFF");
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F2) {
            // Port specific: glPolygonMode(GL_LINE) around the 3D
            // entity draw pass. Desktop GL only -- no-op under GLES.
            FN::g_DebugWireframe = !FN::g_DebugWireframe;
            LOG_DEBUG("Debug", "Wireframe %s", FN::g_DebugWireframe ? "ON" : "OFF");
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F7) {
            // Port specific: debug-only, no binary equivalent
            FN::g_DebugTimeScale = (FN::g_DebugTimeScale == 1.0f) ? 0.1f : 1.0f;
            LOG_DEBUG("Debug", "timeScale = %.1f", FN::g_DebugTimeScale);
        } else if (ev.type == SDL_KEYDOWN &&
                   ev.key.keysym.scancode == SDL_SCANCODE_F12) {
            // Port specific: screenshot on F12.
            g_takeScreenshot = true;
        } else {
            if (inputTranslator) inputTranslator->ProcessSDLEvent(ev, static_cast<SDL_Window*>(window));
        }
    }

    // Per-frame shim pump: re-dispatches TouchDown_N for held fingers.
    // Logic lives inside InputTranslatorSDL::BeginFrame (poll stays in the shim).
    if (inputTranslator) inputTranslator->BeginFrame();

    // === Game tick (matches FruitNinja::Draw at 0x1824e0) ===

    // Original: dt = 0.0; Mortar::SystemManager::Update(&dt) writes fixed 1/60;
    // then passes dt to update + draw functions
    game_work.dt = 0.0f;
    SystemManager::GetInstance().Update(&game_work.dt);

    // Port specific: debug time-scale. We scale dt so every
    // dt-integrating update (physics, velocity, acceleration)
    // slows smoothly. Per-tick lerps (alpha fades, state timer
    // decays) don't read dt, so they read FN::g_DebugTimeScale
    // directly at the lerp site -- at 1.0x this is a no-op, at
    // 0.1x the lerp advances 10x less per frame. Result: both
    // categories slow uniformly AND render every real frame, so
    // animations stay smooth at slow speed.
    game_work.dt *= FN::g_DebugTimeScale;

    // Update: 3-state dispatcher
    GameTaskUpdate(game_work.dt);

    // Per-frame GL setup. Binary calls DisplayManagerBada::BeginFrame
    // (0x0019dfec) which handles clears, depth/blend state reset, and
    // matrix stack reset. glViewport isn't touched by BeginFrame -- our
    // port re-applies it each frame so window resizes are picked up.
    int ww, wh;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
    glViewport(0, 0, ww, wh);
    Mortar::DisplayManager::GetInstance().BeginFrame();

    // Draw: state-specific rendering
    GameTaskDraw(game_work.dt);

    // Port specific: capture screenshot before swap so GL_BACK has the finished frame.
    do_screenshot_if_requested(static_cast<SDL_Window*>(window));

    // Present
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
}

// Matches: FruitNinja::Draw (the real game tick) called in a loop
// Original: OnTimerExpired fires every 10ms (100fps), dt fixed at 1/60
void Game::run() {
    static const Uint32 FRAME_MS = 10;  // original Bada timer interval = 10ms

    while (running) {
        Uint32 frameStart = SDL_GetTicks();

        frameTick();

        // Frame pacing: original Bada timer fires every 10ms (100fps)
        // All game logic uses fixed dt=1/60, tuned for this tick rate
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FRAME_MS) {
            SDL_Delay(FRAME_MS - frameTime);
        }
    }
}

// Test-only: run a fixed number of game ticks (no SDL_Delay, no input).
// Used by tests/test_screen.cpp to drive a few frames after pushing a
// screen so its Update + Draw run against a real GL context.
void Game::runFrames(int frameCount) {
    for (int i = 0; i < frameCount && running; i++) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
        }

        game_work.dt = 0.0f;
        SystemManager::GetInstance().Update(&game_work.dt);
        game_work.dt *= FN::g_DebugTimeScale;
        GameTaskUpdate(game_work.dt);

        int ww, wh;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
        glViewport(0, 0, ww, wh);
        Mortar::DisplayManager::GetInstance().BeginFrame();
        GameTaskDraw(game_work.dt);
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
    }
}
