#include "game.h"
#include "splash_screen.h"
#include "config.h"
#include <cstdio>

Game::Game()
    : window(NULL), gl_context(NULL),
      bg_tex(0), hb_logo_tex(0), title_tex(0),
      current_screen(NULL), next_screen(NULL),
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

    // Load shared textures
    TexImage img;
    bg_tex = load_texture("bg_fruit_ninja.tex", img);
    if (!bg_tex)
        bg_tex = load_texture("bg_fruit_ninja_sml.tex", img);

    hb_logo_tex = load_texture("hb_logo.tex", img);
    title_tex = load_texture("title_backing.tex", img);

    // Start with splash screen
    set_screen(new SplashScreen(*this));
    running = true;
    return true;
}

void Game::shutdown() {
    if (current_screen) {
        current_screen->exit();
        delete current_screen;
        current_screen = NULL;
    }
    if (next_screen) {
        delete next_screen;
        next_screen = NULL;
    }
    if (bg_tex) { glDeleteTextures(1, &bg_tex); bg_tex = 0; }
    if (hb_logo_tex) { glDeleteTextures(1, &hb_logo_tex); hb_logo_tex = 0; }
    if (title_tex) { glDeleteTextures(1, &title_tex); title_tex = 0; }
    renderer.shutdown();
}

void Game::set_screen(Screen* screen) {
    next_screen = screen;
}

void Game::transform_touch(int pixel_x, int pixel_y, float& game_x, float& game_y) {
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    game_x = (float)pixel_x * FN_SCREEN_W / ww;
    game_y = FN_SCREEN_H - (float)pixel_y * FN_SCREEN_H / wh;
}

void Game::run() {
    Uint32 last_ticks = SDL_GetTicks();

    while (running) {
        // Handle screen transitions
        if (next_screen) {
            if (current_screen) {
                current_screen->exit();
                delete current_screen;
            }
            current_screen = next_screen;
            next_screen = NULL;
            current_screen->enter();
        }

        // Events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                if (current_screen) {
                    float gx, gy;
                    transform_touch(ev.button.x, ev.button.y, gx, gy);
                    current_screen->on_touch_down(gx, gy);
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                if (current_screen) {
                    float gx, gy;
                    transform_touch(ev.button.x, ev.button.y, gx, gy);
                    current_screen->on_touch_up(gx, gy);
                }
            } else if (ev.type == SDL_FINGERDOWN) {
                if (current_screen) {
                    float gx = ev.tfinger.x * FN_SCREEN_W;
                    float gy = FN_SCREEN_H - ev.tfinger.y * FN_SCREEN_H;
                    current_screen->on_touch_down(gx, gy);
                }
            } else if (ev.type == SDL_FINGERUP) {
                if (current_screen) {
                    float gx = ev.tfinger.x * FN_SCREEN_W;
                    float gy = FN_SCREEN_H - ev.tfinger.y * FN_SCREEN_H;
                    current_screen->on_touch_up(gx, gy);
                }
            }
        }

        // Delta time (capped at ~33ms)
        Uint32 now = SDL_GetTicks();
        float dt = (now - last_ticks) / 1000.0f;
        if (dt > 0.033f) dt = 0.033f;
        last_ticks = now;

        // Update
        if (current_screen)
            current_screen->update(dt);

        // Draw
        int ww, wh;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (current_screen)
            current_screen->draw(renderer);

        SDL_GL_SwapWindow(window);
    }
}
