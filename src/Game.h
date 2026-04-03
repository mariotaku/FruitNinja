#ifndef GAME_H
#define GAME_H

#include <SDL.h>
#include <string>
#include "Renderer.h"
#include "HUD.h"
#include "ActorManager.h"
#include "InputManager.h"
#include "SDLInputTranslator.h"

struct Game {
    SDL_Window* window;
    SDL_GLContext gl_context;
    Renderer renderer;

    // Matches Game+0x3c in original — HUD control manager
    HUD* hud;

    // Entity manager (matches ActorManager singleton)
    ActorManager* actorManager;

    // Input system (replaces Mortar::InputManager + InputDeviceBada)
    InputManager* inputManager;
    SDLInputTranslator inputTranslator;

    // Shared textures (loaded once, reused across screens)
    GLuint bg_tex;
    GLuint hb_logo_tex;
    GLuint title_tex;

    // Global textures (per MainScreen docs — not on any struct)
    GLuint blurry_backing_tex;
    GLuint fruit_text_tex;
    GLuint ninja_text_tex;

    // Audio toggle state
    bool soundEnabled;
    bool musicEnabled;

    // Data directory path
    std::string data_dir;

    bool running;

    Game();
    ~Game();

    bool init(SDL_Window* win, SDL_GLContext gl);
    void shutdown();
    void run();

    // Load a texture from data_dir/textures/<name>
    GLuint load_texture(const char* name, TexImage& img);
};

#endif
