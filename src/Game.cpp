//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game → GamePreInitialise → GameInitialise →
//   [GameTaskUpdate loop] → GameDestroy
//

#include "Game.h"
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "debug/DebugFlags.h"
#include "config.h"
#include <cstdio>

// Matches Game_ctor (0x0010dab0): calls MortarGame ctor, clears 3 fields
Game::Game()
    : Mortar::MortarGame(),
      field_0xfc(0), field_0xfd(0), field_0x100(0),
      taskStateIndex(0), field_0x01(0), gameActiveFlag(0), languageFlag(0),
      gameMode(0), pauseFlag(0), retryFlag(0), field_0x07(0),
      retryTimer(0), m_TransitionTimer(0), bombHitTimer(0),
      missCount(0), currentScore(0), m_bUnsullied(0),
      m_CritTimer(0), m_ScoreThreshold(0), field_0x34(0), m_bSlowMotion(0),
      dt(0), hud(nullptr),
      pCamera(nullptr),
      isFirstPlay1(false), isFirstPlay2(false),
      field_0x88(0),
      mainScreen(nullptr),
      pTutorialCtrl(nullptr),
      fruitTotal(0),
      pGameSound(nullptr),
      m_gameDataLicensedState(0),
      m_FrameTimer(0), m_MenuReturnTimer(0), flag_0x1a8(0), m_bFrameDirty(0),
      window(nullptr), gl_context(nullptr),
      inputManager(nullptr), actorManager(nullptr),
      soundEnabled(true), musicEnabled(true),
      running(false)
{
    // s_instance already set by MortarGame ctor
}

Game::~Game() {
    shutdown();
}

// Matches 0x0010d9ec
const char* Game::SelfVersion() {
    return "1.5.1";
}

// Matches 0x0010dae0 — calls GameTaskSaveOnExit()
void Game::SaveOnExit() {
    GameTaskExit();
}

// Matches 0x0010b140 — writes 0 to languageFlag (g_GameData+0x03)
void Game::SetLanguage(const char* lang) {
    (void)lang;
    languageFlag = 0;
}

// Matches 0x0010da68 — reads/writes g_GameData+0x18C
void Game::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_gameDataLicensedState = 1;
    } else if (m_gameDataLicensedState != 1) {
        m_gameDataLicensedState = 2;
    }
}

// Matches 0x0010da94 — returns g_GameData+0x18C
int Game::GetAppLicensedState() const {
    return m_gameDataLicensedState;
}

// Matches: FruitNinja::OnAppInitializing flow
bool Game::init(SDL_Window* win, SDL_GLContext gl) {
    window = win;
    gl_context = gl;
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
    SDL_GL_GetDrawableSize(window, &initW, &initH);
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
                // entity draw pass. Desktop GL only — no-op under GLES.
                FN::g_DebugWireframe = !FN::g_DebugWireframe;
                printf("[Debug] Wireframe %s\n", FN::g_DebugWireframe ? "ON" : "OFF");
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F7) {
                // Port specific: debug-only, no binary equivalent
                FN::g_DebugTimeScale = (FN::g_DebugTimeScale == 1.0f) ? 0.1f : 1.0f;
                printf("[debug] timeScale = %.1f\n", FN::g_DebugTimeScale);
            } else {
                inputTranslator.ProcessSDLEvent(ev, window);
            }
        }

        // === Game tick (matches FruitNinja::Draw at 0x1824e0) ===

        // Original: dt = 0.0; SystemManager::Update(&dt) writes fixed 1/60;
        // then passes dt to update + draw functions
        dt = 0.0f;
        Mortar::SystemManager::GetInstance().Update(&dt);

        // Port specific: debug time-scale. We scale dt so every
        // dt-integrating update (physics, velocity, acceleration)
        // slows smoothly. Per-tick lerps (alpha fades, state timer
        // decays) don't read dt, so they read FN::g_DebugTimeScale
        // directly at the lerp site — at 1.0× this is a no-op, at
        // 0.1× the lerp advances 10× less per frame. Result: both
        // categories slow uniformly AND render every real frame, so
        // animations stay smooth at slow speed.
        dt *= FN::g_DebugTimeScale;

        // Update: 3-state dispatcher
        GameTaskUpdate(dt);

        // Per-frame GL setup. Binary calls DisplayManagerBada::BeginFrame
        // (0x0019dfec) which handles clears, depth/blend state reset, and
        // matrix stack reset. glViewport isn't touched by BeginFrame — our
        // port re-applies it each frame so window resizes are picked up.
        int ww, wh;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        Mortar::DisplayManager::GetInstance().BeginFrame();

        // Draw: state-specific rendering
        GameTaskDraw(dt);

        // Present
        SDL_GL_SwapWindow(window);

        // Frame pacing: original Bada timer fires every 10ms (100fps)
        // All game logic uses fixed dt=1/60, tuned for this tick rate
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FRAME_MS) {
            SDL_Delay(FRAME_MS - frameTime);
        }
    }
}

// Matches: GameDestroy (0x10b7ec) + FruitNinja::OnAppTerminating
void Game::shutdown() {
    GameDestroy();
    renderer.shutdown();
}
