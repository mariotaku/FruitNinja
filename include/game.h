#ifndef GAME_H
#define GAME_H

#include <SDL.h>
#include <string>
#include "renderer.h"
#include "screen.h"

struct Game {
    SDL_Window* window;
    SDL_GLContext gl_context;
    Renderer renderer;

    // Shared textures (loaded once, reused across screens)
    GLuint bg_tex;
    GLuint hb_logo_tex;
    GLuint title_tex;

    // Data directory path
    std::string data_dir;

    // Screen management
    Screen* current_screen;
    Screen* next_screen;

    bool running;

    Game();
    ~Game();

    bool init(SDL_Window* win, SDL_GLContext gl);
    void shutdown();
    void run();
    void set_screen(Screen* screen);

    // Load a texture from data_dir/textures/<name>
    GLuint load_texture(const char* name, TexImage& img);

    // Convert SDL pixel coords to game coords (320x480, origin bottom-left)
    void transform_touch(int pixel_x, int pixel_y, float& game_x, float& game_y);
};

#endif
