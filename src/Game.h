#ifndef GAME_H
#define GAME_H

//
// Game : MortarGame (singleton, size = 0x104 for the C++ object)
//
// In the original binary, gameplay state lives in a separate g_GameData global
// (0x608 bytes). For the port, gameplay fields are kept here for convenience.
//

#include <SDL.h>
#include <string>
#include <cstdint>
#include "core/MortarGame.h"
#include "math/Vec3.h"
#include "render/Renderer.h"
#include "input/InputManager.h"
#include "platform/SDLInputTranslator.h"

class HUD;
class ActorManager;
class MainScreen;
class FruitCamera;

struct Game : public Mortar::MortarGame {
    // === Game-specific fields beyond MortarGame base (original +0xFC..+0x103) ===
    uint8_t field_0xfc;            // +0xFC
    uint8_t field_0xfd;            // +0xFD
    int field_0x100;               // +0x100

    // === g_GameData (0x608 bytes, kept here for port convenience) ===
    // In original binary this is a separate flat C struct accessed via GOT.
    // See docs/structs/game.md for full layout.

    uint8_t taskStateIndex;        // +0x00: 0=Splash, 1=Frontend, 2=Game
    uint8_t field_0x01;            // +0x01
    uint8_t gameActiveFlag;        // +0x02: 0=paused, !=0=active
    uint8_t languageFlag;          // +0x03: SetLanguage writes 0 here
    uint8_t gameMode;              // +0x04: 0=Classic, 1=Arcade, 2=Zen, 3=Attack
    uint8_t pauseFlag;             // +0x05: set by GameOver, QuitToMenu
    uint8_t retryFlag;             // +0x06
    uint8_t field_0x07;            // +0x07
    float retryTimer;              // +0x08
    float m_TransitionTimer;       // +0x0C
    float bombHitTimer;            // +0x10
    uint8_t missCount;             // +0x14: combo counter
    int currentScore;              // +0x18
    uint8_t m_bUnsullied;          // +0x1C: 0=no misses yet
    Vec3 retryPos;                 // +0x20
    float m_CritTimer;             // +0x2C
    int m_ScoreThreshold;          // +0x30
    uint8_t field_0x34;            // +0x34
    uint8_t m_bSlowMotion;         // +0x35
    float dt;                      // +0x38
    HUD* hud;                      // +0x3C: pHUD
    bool isFirstPlay1;             // +0x44
    bool isFirstPlay2;             // +0x45
    FruitCamera* pCamera;             // +0x48
    // +0x4C: FruitSaveData* pSaveData (TODO)
    // +0x50..+0x80: Font* slots (TODO)
    float field_0x88;              // +0x88
    Vec3 worldPos;                 // +0x90: light direction in GameDraw
    MainScreen* mainScreen;        // +0x160: pMainScreen
    // +0x164: GameOverScreen* pGameOverScreen (TODO)
    // +0x168: TutorialControl* pTutorialCtrl (TODO)
    int fruitTotal;                // +0x174: last AddToTotal result
    // +0x178: CoinCounter* pCoinCounter (TODO)
    // +0x180: TimeControl* pTimeCtrl (TODO)
    // +0x188: GameSound* pGameSound (TODO)
    int m_gameDataLicensedState;   // +0x18C: game-level licensed state (separate from MortarGame)
    int m_FrameTimer;              // +0x194: (int)(dt * scale) + prev
    float m_MenuReturnTimer;       // +0x1A0
    uint8_t flag_0x1a8;            // +0x1A8
    uint8_t m_bFrameDirty;         // +0x604

    // === Port-specific fields (SDL replacements for Bada OS) ===

    SDL_Window* window;
    SDL_GLContext gl_context;
    Renderer renderer;
    InputManager* inputManager;
    SDLInputTranslator inputTranslator;
    ActorManager* actorManager;

    // Audio toggle state
    bool soundEnabled;
    bool musicEnabled;

    // Data directory path
    std::string data_dir;

    // Port control
    bool running;

    // === Singleton ===
    static Game* GetInstance() { return static_cast<Game*>(s_instance); }

    // === MortarGame overrides ===
    const char* SelfVersion() override;
    void SaveOnExit() override;
    void SetLanguage(const char* lang) override;
    void SetAppLicensed(bool licensed) override;
    int GetAppLicensedState() const override;

    // === Methods ===
    Game();
    ~Game();

    bool init(SDL_Window* win, SDL_GLContext gl);
    void shutdown();
    void run();
};

// Forward declarations for lifecycle functions (src/game/)
void GamePreInitialise();
void GameInitialise();
void GameDestroy();

#endif
