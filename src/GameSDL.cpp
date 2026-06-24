// SDL backend for Game — init / run / runFrames live here so the rest of
// Game.cpp stays portable for the asm-verify cross-build. The void* fields
// in Game.h are cast to SDL_Window* / SDL_GLContext at the SDL boundary.

#include "Game.h"
#include "game/GameWork.h"
#include <SDL.h>
#include "platform/InputTranslatorSDL.h"
#include "platform/FixedStepDriver.h"
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
#include "platform/RenderInterp.h"
#endif
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "game/GameTaskState.h"
#include "screens/PauseScreen.h"
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

// Port specific: poll SDL events and pump the input translator for one display
// frame.  Called ONCE per display frame (before accumulator drain) so held-
// finger TouchDown_N is not re-dispatched per catch-up step, which would
// change slice behaviour on high-refresh displays.
void Game::pollInput() {
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
        } else if (ev.type == SDL_WINDOWEVENT &&
                   (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    ev.window.event == SDL_WINDOWEVENT_MINIMIZED  ||
                    ev.window.event == SDL_WINDOWEVENT_HIDDEN)) {
            // Port specific: SDL focus-loss maps to the binary's Bada app-deactivate
            // pause; clears touch so no blade stays held (pairs with #154).
            // Bada OnBackground/OnDeactivated triggered a pause when the OS
            // backgrounded the app mid-slice. SDL has no equivalent lifecycle
            // event, so we synthesize it from FOCUS_LOST / MINIMIZED / HIDDEN.
            // Only pause during active gameplay (bM_Mode == false means game is
            // running; true means already paused/transitioning).
            if (!game_work.bM_Mode) {
                PauseScreen::PauseGame();
                LOG_INFO("GameSDL", "focus-loss pause (SDL_WINDOWEVENT %d)", (int)ev.window.event);
            }
            // Always clear held touch channels regardless of pause state so no
            // blade stays armed across a background/restore cycle.
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
        } else if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
            // Port specific: SDL focus-loss maps to the binary's Bada app-deactivate
            // pause; clears touch so no blade stays held (pairs with #154).
            // SDL_APP_WILLENTERBACKGROUND fires on mobile (iOS/Android) before the
            // app is backgrounded -- equivalent to Bada OnBackground.
            if (!game_work.bM_Mode) {
                PauseScreen::PauseGame();
                LOG_INFO("GameSDL", "app-background pause (SDL_APP_WILLENTERBACKGROUND)");
            }
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
        } else {
            if (inputTranslator) inputTranslator->ProcessSDLEvent(ev, static_cast<SDL_Window*>(window));
        }
    }

    // Per-frame shim pump: re-dispatches TouchDown_N for held fingers.
    // Logic lives inside InputTranslatorSDL::BeginFrame (poll stays in the shim).
    if (inputTranslator) inputTranslator->BeginFrame();
}

// Port specific: one simulation step.
// Matches FruitNinja::Draw (0x1824e0): dt=0 -> SystemManager::Update(&dt)
// writes fixed 1/60 -> GameTaskUpdate(dt).  g_DebugTimeScale scales dt for
// the slow-motion debug path only; game logic always sees 1/60 at 1.0x scale.
void Game::stepUpdate() {
    game_work.dt = 0.0f;
    SystemManager::GetInstance().Update(&game_work.dt);
    game_work.dt *= FN::g_DebugTimeScale;
    GameTaskUpdate(game_work.dt);
}

// Port specific: one render pass (no simulation).
// Per-frame GL setup mirrors DisplayManagerBada::BeginFrame (0x0019dfec).
// glViewport is re-applied each call so window resizes are picked up immediately.
// alpha is the fractional sim residual [0,1) for render interpolation; 0 = no interp.
void Game::renderFrame(float alpha) {
    int ww, wh;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
    glViewport(0, 0, ww, wh);
    Mortar::DisplayManager::GetInstance().BeginFrame();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().ApplyForDraw(alpha);
#endif
    GameTaskDraw(game_work.dt);   // UNMODIFIED: binary-faithful, asm-verified
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().RestoreAfterDraw();
#endif
    do_screenshot_if_requested(static_cast<SDL_Window*>(window));
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
}

// Port specific: one complete game tick — poll, step, render.
// Kept as a thin wrapper so existing callers (legacy / external) are unaffected.
// Passes alpha=0 to renderFrame (no interpolation; frameTick is used by runFrames
// which is the deterministic headless path -- Apply/Restore must not fire there).
void Game::frameTick() {
    pollInput();
    stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().SnapshotAfterStep();
#endif
    renderFrame(0.0f);
}

// Port specific: main game loop with fixed-step accumulator.
// The accumulator decouples simulation rate (60 ticks/s) from display refresh
// so the game runs at correct wall-clock speed on 60/120/144 Hz panels.
// vsync (SDL_GL_SetSwapInterval(1) in mainSDL.cpp) paces the render; the
// accumulator owns the sim rate.  SDL_Delay(1) only fires when steps==0
// (minimised / vsync off) to avoid a busy-spin.
void Game::run() {
    fn::FixedStepDriver driver;
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double ms = static_cast<double>(now - last) * 1000.0 / freq;
        last = now;

        pollInput();
        if (!running) break;

        int steps = driver.advance(ms);
        for (int i = 0; i < steps && running; ++i) {
            stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
            fn::RenderInterp::Get().SnapshotAfterStep();
#endif
        }
        renderFrame(static_cast<float>(driver.alpha()));

        if (steps == 0) {
            SDL_Delay(1);
        }
    }
}

// Test-only: run a fixed number of game ticks (no SDL_Delay, no accumulator).
// Wall-clock-free and deterministic: each iteration drains only SDL_QUIT then
// calls stepUpdate() + renderFrame() exactly once.  No full pollInput() so
// held-finger shim and focus-loss logic don't fire during headless tests.
// Behaviour is identical to the pre-Phase-1 implementation; headless tests
// (test_screen, test_gameplay, etc.) are unaffected by the run() rewrite.
void Game::runFrames(int frameCount) {
    for (int i = 0; i < frameCount && running; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
        }
        stepUpdate();
        renderFrame();
    }
}
