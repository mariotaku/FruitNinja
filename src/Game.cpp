//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game → GamePreInitialise → GameInitialise →
//   [GameTaskUpdate loop] → GameDestroy
//

#include "Game.h"
#include "asset/TextureManager.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
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
      dt(0), hud(NULL),
      pCamera(NULL),
      isFirstPlay1(false), isFirstPlay2(false),
      field_0x88(0),
      mainScreen(NULL),
      fruitTotal(0), m_gameDataLicensedState(0),
      m_FrameTimer(0), m_MenuReturnTimer(0), flag_0x1a8(0), m_bFrameDirty(0),
      window(NULL), gl_context(NULL),
      inputManager(NULL), actorManager(NULL),
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

    if (!renderer.init()) {
        fprintf(stderr, "Failed to init renderer\n");
        return false;
    }

    // Matches original lifecycle:
    GamePreInitialise();   // zero game fields
    GameInitialise();      // boot all engine singletons + load shared data

    // Start in Splash state (will auto-transition to Game)
    taskStateIndex = 0;
    running = true;
    return true;
}

// Matches: FruitNinja::Draw (the real game tick) called in a loop
void Game::run() {
    Uint32 last_ticks = SDL_GetTicks();

    while (running) {
        // === SDL events → InputManager ===
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else {
                inputTranslator.ProcessSDLEvent(ev, window);
            }
        }

        // === Delta time (matches original clamping from GameTaskUpdate) ===
        Uint32 now = SDL_GetTicks();
        float rawDt = (now - last_ticks) / 1000.0f;
        if (rawDt > 0.033f) rawDt = 0.033f;
        last_ticks = now;

        // === Game tick (matches FruitNinja::Draw at 0x1824e0) ===

        // SystemManager::Update(&dt) — stub for now

        // Update: 3-state dispatcher
        GameTaskUpdate(rawDt);

        // Draw
        int ww, wh;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Draw: state-specific rendering
        GameTaskDraw(dt);

        // Present
        SDL_GL_SwapWindow(window);
    }
}

// Matches: GameDestroy (0x10b7ec) + FruitNinja::OnAppTerminating
void Game::shutdown() {
    GameDestroy();
    renderer.shutdown();
}
