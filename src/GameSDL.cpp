// SDL backend for Game — init / run / runFrames live here so the rest of
// Game.cpp stays portable for the asm-verify cross-build. The void* fields
// in Game.h are cast to SDL_Window* / SDL_GLContext at the SDL boundary.

#include "Game.h"
#include <SDL.h>
#include "platform/InputTranslatorSDL.h"
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "game/GameTaskState.h"
#include "debug/DebugFlags.h"
#include "config.h"
#include <cstdio>

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
    taskStateIndex = 0;
    running = true;
    return true;
}

// Matches: FruitNinja::Draw (the real game tick) called in a loop
// Original: OnTimerExpired fires every 10ms (100fps), dt fixed at 1/60
void Game::run() {
    static const Uint32 FRAME_MS = 10;  // original Bada timer interval = 10ms

    while (running) {
        Uint32 frameStart = SDL_GetTicks();

        // === SDL events → InputManager ===
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F1) {
                FN::g_DebugHitboxes = !FN::g_DebugHitboxes;
                printf("[Debug] Hitboxes %s\n", FN::g_DebugHitboxes ? "ON" : "OFF");
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F2) {
                // Port specific: glPolygonMode(GL_LINE) around the 3D
                // entity draw pass. Desktop GL only -- no-op under GLES.
                FN::g_DebugWireframe = !FN::g_DebugWireframe;
                printf("[Debug] Wireframe %s\n", FN::g_DebugWireframe ? "ON" : "OFF");
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F7) {
                // Port specific: debug-only, no binary equivalent
                FN::g_DebugTimeScale = (FN::g_DebugTimeScale == 1.0f) ? 0.1f : 1.0f;
                printf("[debug] timeScale = %.1f\n", FN::g_DebugTimeScale);
            } else {
                if (inputTranslator) inputTranslator->ProcessSDLEvent(ev, static_cast<SDL_Window*>(window));
            }
        }

        // === Game tick (matches FruitNinja::Draw at 0x1824e0) ===

        // Original: dt = 0.0; Mortar::SystemManager::Update(&dt) writes fixed 1/60;
        // then passes dt to update + draw functions
        dt = 0.0f;
        Mortar::SystemManager::GetInstance().Update(&dt);

        // Port specific: debug time-scale. We scale dt so every
        // dt-integrating update (physics, velocity, acceleration)
        // slows smoothly. Per-tick lerps (alpha fades, state timer
        // decays) don't read dt, so they read FN::g_DebugTimeScale
        // directly at the lerp site -- at 1.0x this is a no-op, at
        // 0.1x the lerp advances 10x less per frame. Result: both
        // categories slow uniformly AND render every real frame, so
        // animations stay smooth at slow speed.
        dt *= FN::g_DebugTimeScale;

        // Update: 3-state dispatcher
        GameTaskUpdate(dt);

        // Per-frame GL setup. Binary calls DisplayManagerBada::BeginFrame
        // (0x0019dfec) which handles clears, depth/blend state reset, and
        // matrix stack reset. glViewport isn't touched by BeginFrame -- our
        // port re-applies it each frame so window resizes are picked up.
        int ww, wh;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
        glViewport(0, 0, ww, wh);
        Mortar::DisplayManager::GetInstance().BeginFrame();

        // Draw: state-specific rendering
        GameTaskDraw(dt);

        // Present
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));

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

        dt = 0.0f;
        Mortar::SystemManager::GetInstance().Update(&dt);
        dt *= FN::g_DebugTimeScale;
        GameTaskUpdate(dt);

        int ww, wh;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
        glViewport(0, 0, ww, wh);
        Mortar::DisplayManager::GetInstance().BeginFrame();
        GameTaskDraw(dt);
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
    }
}
