#ifndef GAME_H
#define GAME_H

//
// Game : MortarGame (singleton, size = 0x104 for the C++ object)
//
// In the original binary, gameplay state lives in a separate g_GameData global
// (0x608 bytes). For the port, gameplay fields are kept here for convenience.
//

// Analysed: 2026-04-25T12:00

#include <string>
#include <cstdint>
#include "core/MortarGame.h"
#include "math/Vec3.h"
#include "render/Renderer.h"
#include "input/InputManager.h"
#include "render/Font.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"

// Opaque platform handles (SDL types live behind void* in headers).
// The SDL backend (mainSDL.cpp / GameSDL.cpp) casts these to SDL_Window* / SDL_GLContext.
// InputTranslatorSDL is forward-declared so this header doesn't pull <SDL.h>;
// it's heap-allocated and stored as a pointer (see GameSDL.cpp).
class InputTranslatorSDL;

class HUD;
class ActorManager;
class MainScreen;
class FruitCamera;
class FruitSaveData;
class GameOverScreen;
class TimeControl;
class GameSound;

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
    bool m_bSoundOn;               // +0x44: InitialiseData: GetTotal("soundOff")==0; 1=sound enabled
    bool m_bMusicOn;               // +0x45: InitialiseData: GetTotal("musicOff")==0; 1=music enabled
    FruitCamera* pCamera;             // +0x48
    FruitSaveData* pSaveData;         // +0x4C: persistent save state (stub)

    // Font slots +0x50..+0x80 (g_GameData layout, 11 slots)
    // Loaded in GameInitialise; destroyed in GameDestroy.
    // See docs/engine/font.md for full per-slot spec.
    SmartPtr<Mortar::Font> pFontReserved0;    // +0x50: unused (always null)
    // DIFFERS: binary loads HD font at +0x54 when DisplayManager::ShouldUseHDFonts().
    // Port always loads SD path (font_fruit_ninja.fnt) — HD asset-check not replicated.
    SmartPtr<Mortar::Font> pFontMain;         // +0x54: fonts/font_fruit_ninja.fnt
    // DIFFERS: binary loads HD font at +0x58 when HD flag set.
    // Port always loads SD path (fruit_ninja_numbers.fnt).
    SmartPtr<Mortar::Font> pFontNumbers;      // +0x58: fonts/fruit_ninja_numbers.fnt
    SmartPtr<Mortar::Font> pFontReserved1;    // +0x5C: unused (always null; CoinCounter::Draw checks)
    uint32_t _gap_0x60;                       // +0x60: gap (not a Font slot per GameDestroy)
    SmartPtr<Mortar::Font> pFontReserved2;    // +0x64: unused (always null)
    SmartPtr<Mortar::Font> pFontGreen;        // +0x68: fonts/fruit_ninja_numbers_green.fnt
    SmartPtr<Mortar::Font> pFontArcade;       // +0x6C: fonts/arcade_results_numbers.fnt
    SmartPtr<Mortar::Font> pFontGold;         // +0x70: fonts/gold_numbers.fnt (File::Exists guarded; absent in shipped assets)
    SmartPtr<Mortar::Font> pFontSilver;       // +0x74: fonts/silver_numbers.fnt (File::Exists guarded; absent)
    SmartPtr<Mortar::Font> pFontBronze;       // +0x78: fonts/bronze_numbers.fnt (File::Exists guarded; absent)
    SmartPtr<Mortar::Font> pFontArcadeAlias;  // +0x7C: non-owning alias of pFontArcade
    SmartPtr<Mortar::Font> pFontBlue2;        // +0x80: fonts/fruit_ninja_numbers_blue2.fnt

    float field_0x88;              // +0x88
    Vec3 worldPos;                 // +0x90: light direction in GameDraw
    MainScreen* mainScreen;        // +0x160: pMainScreen
    GameOverScreen* pGameOverScreen;  // +0x164
    class TutorialControl* pTutorialCtrl;  // +0x168
    int fruitTotal;                // +0x174: last AddToTotal result
    class CoinCounter* pCoinCounter; // +0x178: step 5 in GameInit
    TimeControl* pTimeCtrl;        // +0x180
    // +0x188: GameSound* pGameSound. Port backs this with a real
    // GameSound instance, but the sound backend itself is no-op
    // (SoundManager is stubbed). Makes the SFXPlay call sites real
    // so they're easy to light up once audio is wired.
    GameSound* pGameSound;
    int m_gameDataLicensedState;   // +0x18C: game-level licensed state (separate from MortarGame)
    int m_FrameTimer;              // +0x194: (int)(dt * scale) + prev
    float m_MenuReturnTimer;       // +0x1A0
    uint8_t flag_0x1a8;            // +0x1A8
    uint8_t m_bFrameDirty;         // +0x604

    // +0xF4: splash logo texture (HB_logo.tex), loaded on demand in GameUpdate.
    // Released when splashFadeTimer reaches 0. Distinct from +0xFC background.
    SmartPtr<Mortar::Texture> pSplashTex;

    // === Port-specific fields (SDL replacements for Bada OS) ===

    void* window;          // SDL_Window* (opaque to portable headers)
    void* gl_context;      // SDL_GLContext (opaque)
    Renderer renderer;
    Mortar::InputManager* inputManager;
    InputTranslatorSDL* inputTranslator;   // heap-allocated in GameSDL.cpp
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

    bool init(void* win, void* gl);   // win = SDL_Window*, gl = SDL_GLContext (opaque to header)
    void shutdown();
    void run();
    void runFrames(int frameCount);
};

// Forward declarations for lifecycle functions (src/game/)
void GamePreInitialise();
void GameInitialise();
void GameDestroy();

#endif
