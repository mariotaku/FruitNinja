//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game → GamePreInitialise → GameInitialise →
//   [GameTaskUpdate loop] → GameDestroy
//

#include "Game.h"
#include "asset/tex_loader.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "config.h"
#include <cstdio>

Game* Game::s_instance = NULL;

Game::Game()
    : state(0), field02(0), gameMode(0), quitFlag(0),
      transitionTimer(0), bombHitTimer(0), dt(0),
      hud(NULL), mainScreen(NULL), frameTimer(0),
      window(NULL), gl_context(NULL),
      inputManager(NULL), actorManager(NULL),
      bg_tex(0), hb_logo_tex(0), title_tex(0),
      blurry_backing_tex(0), fruit_text_tex(0), ninja_text_tex(0),
      soundEnabled(true), musicEnabled(true),
      running(false)
{
    s_instance = this;
}

Game::~Game() {
    shutdown();
    s_instance = NULL;
}

GLuint Game::load_texture(const char* name, TexImage& img) {
    std::string path = data_dir + "/textures/" + name;
    if (!tex_load(path, img)) {
        fprintf(stderr, "Failed to load texture: %s\n", path.c_str());
        return 0;
    }
    return renderer.upload_texture(img);
}

// Matches: FruitNinja::OnAppInitializing flow
bool Game::init(SDL_Window* win, SDL_GLContext gl) {
    window = win;
    gl_context = gl;
    data_dir = FN_DATA_DIR;

    if (!renderer.init()) {
        fprintf(stderr, "Failed to init renderer\n");
        return false;
    }

    // Matches original lifecycle:
    GamePreInitialise();   // zero game fields
    GameInitialise();      // boot all engine singletons + load shared data

    // Start in Splash state (will auto-transition to Game)
    state = 0;
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
