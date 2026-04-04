#ifndef GAME_H
#define GAME_H

//
// Game singleton — matches original Game struct (0x608 bytes)
// Port uses the same field layout where possible.
//

#include <SDL.h>
#include <string>
#include <cstdint>
#include "render/Renderer.h"
#include "input/InputManager.h"
#include "platform/SDLInputTranslator.h"

class HUD;
class ActorManager;
class MainScreen;

struct Game {
    // === Original Game struct fields ===

    // +0x00: current state index (0=Splash, 1=Frontend, 2=Game)
    uint8_t state;
    // +0x02: game running flag (0=paused in original)
    uint8_t field02;
    // +0x04: gameMode (0=Classic, 1=Arcade, 2=Zen)
    uint8_t gameMode;
    // +0x05: quit/pause flag
    uint8_t quitFlag;
    // +0x0c: transition timer
    float transitionTimer;
    // +0x10: bomb hit timer
    float bombHitTimer;
    // +0x38: current frame dt
    float dt;
    // +0x3c: HUD control manager
    HUD* hud;
    // +0x160: MainScreen pointer (for direct access)
    MainScreen* mainScreen;
    // +0x194: frame timer (ms accumulator)
    int frameTimer;

    // === Port-specific fields (SDL replacements for Bada OS) ===

    SDL_Window* window;
    SDL_GLContext gl_context;
    Renderer renderer;
    InputManager* inputManager;
    SDLInputTranslator inputTranslator;
    ActorManager* actorManager;

    // Shared textures (loaded in GameInitialise)
    GLuint bg_tex;
    GLuint hb_logo_tex;
    GLuint title_tex;

    // Global textures (loaded lazily by MainScreen)
    GLuint blurry_backing_tex;
    GLuint fruit_text_tex;
    GLuint ninja_text_tex;

    // Audio toggle state
    bool soundEnabled;
    bool musicEnabled;

    // Data directory path
    std::string data_dir;

    // Port control
    bool running;

    // === Singleton ===
    static Game* s_instance;
    static Game* GetInstance() { return s_instance; }

    // === Methods ===
    Game();
    ~Game();

    bool init(SDL_Window* win, SDL_GLContext gl);
    void shutdown();
    void run();

    GLuint load_texture(const char* name, TexImage& img);
};

// Forward declarations for lifecycle functions (src/game/)
void GamePreInitialise();
void GameInitialise();
void GameDestroy();

#endif
