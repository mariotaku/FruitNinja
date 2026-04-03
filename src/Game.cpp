#include "Game.h"
#include "MainScreen.h"
#include "config.h"
#include <cstdio>

Game::Game()
    : window(NULL), gl_context(NULL), hud(NULL), actorManager(NULL), inputManager(NULL),
      bg_tex(0), hb_logo_tex(0), title_tex(0),
      blurry_backing_tex(0), fruit_text_tex(0), ninja_text_tex(0),
      soundEnabled(true), musicEnabled(true),
      running(false) {}

Game::~Game() {
    shutdown();
}

GLuint Game::load_texture(const char* name, TexImage& img) {
    std::string path = data_dir + "/textures/" + name;
    if (!tex_load(path, img)) {
        fprintf(stderr, "Failed to load texture: %s\n", path.c_str());
        return 0;
    }
    return renderer.upload_texture(img);
}

bool Game::init(SDL_Window* win, SDL_GLContext gl) {
    window = win;
    gl_context = gl;
    data_dir = FN_DATA_DIR;

    if (!renderer.init()) {
        fprintf(stderr, "Failed to init renderer\n");
        return false;
    }

    // Create HUD (matches GameInit creating HUD at Game+0x3c)
    hud = new HUD();

    // Create ActorManager (matches GameInit)
    actorManager = new ActorManager();

    // Create InputManager (matches Mortar::InputManager singleton)
    inputManager = new InputManager();
    inputTranslator.Init();

    // Touch callbacks are registered by individual HUD controls
    // (e.g., MainScreen registers for TouchScreen/TouchUp in its constructor)

    // Load shared textures
    TexImage img;
    bg_tex = load_texture("bg_fruit_ninja.tex", img);
    if (!bg_tex)
        bg_tex = load_texture("bg_fruit_ninja_sml.tex", img);

    hb_logo_tex = load_texture("hb_logo.tex", img);
    title_tex = load_texture("title_backing.tex", img);

    // Create MainScreen as HUDControl (matches GameInit adding MainScreen to HUD)
    MainScreen* mainScreen = new MainScreen(*this);
    hud->AddControl(mainScreen);

    running = true;
    return true;
}

void Game::shutdown() {
    if (inputManager) { delete inputManager; inputManager = NULL; }
    if (actorManager) { delete actorManager; actorManager = NULL; }
    if (hud) { delete hud; hud = NULL; }
    if (bg_tex) { glDeleteTextures(1, &bg_tex); bg_tex = 0; }
    if (hb_logo_tex) { glDeleteTextures(1, &hb_logo_tex); hb_logo_tex = 0; }
    if (title_tex) { glDeleteTextures(1, &title_tex); title_tex = 0; }
    if (blurry_backing_tex) { glDeleteTextures(1, &blurry_backing_tex); blurry_backing_tex = 0; }
    if (fruit_text_tex) { glDeleteTextures(1, &fruit_text_tex); fruit_text_tex = 0; }
    if (ninja_text_tex) { glDeleteTextures(1, &ninja_text_tex); ninja_text_tex = 0; }
    renderer.shutdown();
}

void Game::run() {
    Uint32 last_ticks = SDL_GetTicks();

    while (running) {
        // Events — SDL → InputManager → callbacks
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else {
                // All touch/mouse events go through InputManager
                inputTranslator.ProcessSDLEvent(ev, window);
            }
        }

        // Delta time (capped at ~33ms)
        Uint32 now = SDL_GetTicks();
        float dt = (now - last_ticks) / 1000.0f;
        if (dt > 0.033f) dt = 0.033f;
        last_ticks = now;

        // Update entities (Fruit, Bomb, etc.)
        if (actorManager)
            actorManager->Update(dt);

        // Update all HUD controls (MainScreen, buttons, etc.)
        if (hud)
            hud->Update(dt);

        // Draw
        int ww, wh;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Set up ortho projection for 2D drawing
        renderer.SetupGameOrtho();

        // Draw 3D entities (fruit, bombs)
        if (actorManager)
            actorManager->Draw(renderer);

        // Draw all HUD controls (MainScreen background+logos, then buttons on top)
        if (hud) {
            hud->BeginDraw(dt);
            hud->Draw(renderer, 0xFFFF);
        }

        SDL_GL_SwapWindow(window);
    }
}
