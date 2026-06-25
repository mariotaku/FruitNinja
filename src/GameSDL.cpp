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

// Port specific: FPS measurement for the g_ShowFps overlay.
// Updated every render frame in run(); read in renderFrame() for the draw call.
// Uses a ~0.5s sliding window: accumulate frame count + elapsed seconds, recompute
// once the window fills, then reset.  Decoupled from the sim rate (60Hz fixed); this
// measures the actual display-frame interval including any vsync stall.
static float  s_currentFps      = 0.0f;
static double s_fpsWindowSecs   = 0.0;
static int    s_fpsWindowFrames = 0;
static const double kFpsWindowTarget = 0.5;  // recompute every ~0.5 seconds

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
    GameInitialise(nullptr, nullptr);  // boot all engine singletons + load shared data

    // Start in Splash state (will auto-transition to Game)
    game_work.taskStateIndex = 0;
    running = true;
    return true;
}

// Port specific: drain SDL events into per-channel pending state (no touch dispatch).
// Called ONCE per display frame (before accumulator drain).
// Touch events (FINGERDOWN/MOTION/UP, MOUSEBUTTONUP) are accumulated into the
// translator's pending state; actual dispatch to InputManager happens in
// stepUpdate() via DispatchForSimTick() so m_PointCount only advances inside a
// tick that also runs UpdatePoints (head-cap reconcile). This is the #173 fix.
// Focus-loss / WINDOW events still fire ReleaseAllFingers() immediately (#162).
// Non-touch keyboard/debug events are handled inline as before (#163 fidelity).
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
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F3) {
            // Port specific: toggle FPS counter overlay.
            FN::g_ShowFps = !FN::g_ShowFps;
            LOG_DEBUG("Debug", "FPS overlay %s", FN::g_ShowFps ? "ON" : "OFF");
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
            // ReleaseAllFingers fires immediately in pollInput (not deferred to
            // DispatchForSimTick) so the release takes effect on this display frame
            // regardless of whether steps==0 (#162 semantics preserved).
            if (!game_work.bM_Mode) {
                PauseGame();
                LOG_INFO("GameSDL", "focus-loss pause (SDL_WINDOWEVENT %d)", (int)ev.window.event);
            }
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
        } else if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
            // Port specific: SDL focus-loss maps to the binary's Bada app-deactivate
            // pause; clears touch so no blade stays held (pairs with #154).
            // SDL_APP_WILLENTERBACKGROUND fires on mobile (iOS/Android) before the
            // app is backgrounded -- equivalent to Bada OnBackground.
            if (!game_work.bM_Mode) {
                PauseGame();
                LOG_INFO("GameSDL", "app-background pause (SDL_APP_WILLENTERBACKGROUND)");
            }
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
        } else {
            // Port specific: accumulate touch events into pending state; dispatch
            // happens in stepUpdate()->DispatchForSimTick() (#173 fix).
            if (inputTranslator) inputTranslator->DrainSDLEvent(ev, static_cast<SDL_Window*>(window));
        }
    }

}

// Port specific: one simulation step.
// Matches FruitNinja::Draw (0x1824e0): dt=0 -> SystemManager::Update(&dt)
// writes fixed 1/60 -> GameTaskUpdate(dt).  g_DebugTimeScale scales dt for
// the slow-motion debug path only; game logic always sees 1/60 at 1.0x scale.
//
// Port specific: DispatchForSimTick() is called BEFORE GameTaskUpdate so that
// touch dispatch (AddPoint -> m_PointCount advance) and geometry reconcile
// (UpdatePoints inside GameTaskUpdate) happen in the same tick, matching the
// binary's strict 1:1 input->update ordering (#173 bridge-to-origin fix).
// On steps==0 (pure interpolated frame), stepUpdate() does not run, so neither
// DispatchForSimTick nor AddPoint runs -> m_PointCount unchanged -> DrawSlice
// draws the already-reconciled buffer, no stale head-cap.
// DispatchForSimTick contains NO SDL live-finger queries; this makes it safe to
// call from stepUpdate on web.
void Game::stepUpdate() {
    // Flush deferred touch dispatch for this sim tick (before update).
    if (inputTranslator) inputTranslator->DispatchForSimTick();

    game_work.dt = 0.0f;
    SystemManager::GetInstance().Update(&game_work.dt);
    game_work.dt *= FN::g_DebugTimeScale;
    GameTaskUpdate(game_work.dt);
}

// Port specific: allow platform main loops (mainEmscripten) to feed the FPS value
// that DebugFps_Draw reads inside renderFrame.
void Game::setCurrentFps(float fps) {
    s_currentFps = fps;
}

// Port specific: one render pass (no simulation).
// Per-frame GL setup mirrors DisplayManagerBada::BeginFrame (0x0019dfec).
// glViewport is re-applied each call so window resizes are picked up immediately.
// alpha = fractional sim residual [0,1) for render interpolation; 0 = no interp.
// steps = sim steps advanced this display frame; 0 on pure-interp frames (120 Hz
// second render with no new sim tick).
void Game::renderFrame(float alpha, int steps) {
    int ww, wh;
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
    glViewport(0, 0, ww, wh);
    Mortar::DisplayManager::GetInstance().BeginFrame();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().ApplyForDraw(alpha);
#endif
    // Port specific: #168 -- Draw-path effects integrate by dt; on interpolated
    // frames (no sim step) zero the Draw dt so they don't outrun the 60Hz sim.
    // Affects: particleDt in GameDraw, HUD anim timers, SliceEffect timer,
    // ShopScreen dial-alpha.  On steps>=1 leave dt unchanged (particles already
    // advanced in the sim step(s); scaling by steps would over-advance).
    float savedDt = game_work.dt;
    if (steps == 0) game_work.dt = 0.0f;
    GameTaskDraw(game_work.dt);   // UNMODIFIED: binary-faithful, asm-verified
    game_work.dt = savedDt;
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().RestoreAfterDraw();
#endif
    // Port specific: FPS counter overlay -- additive, after all game draw calls.
    // Intentionally outside the dt-zero block: DebugFps_Draw does not use game_work.dt.
    FN::DebugFps_Draw(s_currentFps);
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
    renderFrame(0.0f, 1);
}

// Port specific: main game loop with fixed-step accumulator.
// The accumulator decouples simulation rate (60 ticks/s) from display refresh
// so the game runs at correct wall-clock speed on 60/120/144 Hz panels.
// vsync (SDL_GL_SetSwapInterval(1) in mainSDL.cpp) paces the render; the
// accumulator owns the sim rate.
// Port specific: vsync (SDL_GL_SetSwapInterval) isn't guaranteed; when inactive,
// pace render to the display refresh so we don't render unbounded.
void Game::run() {
    fn::FixedStepDriver driver;
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    // Detect actual vsync state and display refresh target once at loop entry.
    // SDL_GL_SetSwapInterval(1) is a request that drivers may ignore; check
    // the actual interval rather than assuming the request was honoured.
    bool vsyncOn = (SDL_GL_GetSwapInterval() != 0);
    double targetMs = 1000.0 / 60.0;
    {
        SDL_DisplayMode dm;
        int win = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(window));
        if (SDL_GetCurrentDisplayMode(win < 0 ? 0 : win, &dm) == 0 && dm.refresh_rate > 0) {
            targetMs = 1000.0 / dm.refresh_rate;
        }
    }

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double ms = static_cast<double>(now - last) * 1000.0 / freq;
        last = now;

        // Port specific: accumulate render-frame intervals for the FPS overlay.
        // Only active when the overlay is on to avoid redundant work.
        if (FN::g_ShowFps) {
            s_fpsWindowSecs   += ms * 0.001;
            s_fpsWindowFrames += 1;
            if (s_fpsWindowSecs >= kFpsWindowTarget) {
                s_currentFps      = static_cast<float>(s_fpsWindowFrames / s_fpsWindowSecs);
                s_fpsWindowSecs   = 0.0;
                s_fpsWindowFrames = 0;
            }
        }

        // frameStart reuses `now` (already read at loop top) so the cap
        // accounts for the full iteration including any vsync-blocked SwapWindow.
        Uint64 frameStart = now;

        pollInput();
        if (!running) break;

        int steps = driver.advance(ms);
        for (int i = 0; i < steps && running; ++i) {
            stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
            fn::RenderInterp::Get().SnapshotAfterStep();
#endif
        }
        renderFrame(static_cast<float>(driver.alpha()), steps);

        if (!vsyncOn) {
            double frameMs = static_cast<double>(SDL_GetPerformanceCounter() - frameStart) * 1000.0 / freq;
            if (frameMs < targetMs) {
                SDL_Delay(static_cast<Uint32>(targetMs - frameMs));
            }
        } else if (steps == 0) {
            SDL_Delay(1);   // vsync on but not blocking (e.g. minimized) -- don't peg a core
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
        renderFrame(0.0f, 1);
    }
}
