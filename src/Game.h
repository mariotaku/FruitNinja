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
namespace Mortar { class ActorManager; }
class MainScreen;
class FruitCamera;
class FruitSaveData;
class GameOverScreen;
class TimeControl;
class GameSound;

struct Game : public Mortar::MortarGame {
    // === Game-specific fields beyond MortarGame base (original +0xFC..+0x103) ===
    uint8_t m_bSlowHardware;       // +0xFC: set by RenderAtHalfFrames when device matches old-iOS list
    uint8_t m_bLanguageSet;        // +0xFD: set to 1 at end of Game::Init
    // +0xFE..+0xFF: padding
    int m_appState;                // +0x100: app lifecycle state, init=0, no read xrefs found

    // === g_GameData (0x608 bytes, kept here for port convenience) ===
    // In original binary this is a separate flat C struct accessed via GOT.
    // See docs/structs/game.md for full layout.

    uint8_t taskStateIndex;        // +0x00: 0=Splash, 1=Frontend, 2=Game
    uint8_t field_0x01;            // +0x01
    uint8_t gameActiveFlag;        // +0x02: 0=running, !=0=paused/frozen
                                   // (Bomb::Update @ 0x00162bfe: cbnz r2 -> skip
                                   //  countdown decrement when !=0; PauseScreen
                                   //  sets to 1 on pause; GameInitialise inits to 0)
    uint8_t languageFlag;          // +0x03: SetLanguage writes 0 here
    // +0x04: GAME_MODE enum (see game/GameMode.h) stored as uint8_t.
    //   0=GAME_MODE_CLASSIC (originalWaveList.xml)
    //   1=GAME_MODE_COMBO   (comboWaveList.xml; callbacks call it "Casino")
    //   2=GAME_MODE_ARCADE  (arcadeWaveList.xml)
    //   3=GAME_MODE_ZEN     (zenWaveList.xml)
    uint8_t gameMode;
    uint8_t pauseFlag;             // +0x05: set by GameOver, QuitToMenu
    uint8_t retryFlag;             // +0x06
    uint8_t field_0x07;            // +0x07
    // +0x85: tutorial-shown flag. Set by TutorialControl when intro plays;
    //   cleared to 0 by PauseScreen Quit/Retry/Continue callbacks
    //   (binary @ 0x00153ee0 / 0x00153f12 / 0x00153fc4 / 0x00153fc4) so the
    //   tutorial re-arms on the next session.
    uint8_t m_bTutorialShown;
    // +0x1AC: achievement-progress accumulator (seconds in-game).
    //   Threshold check >= 10.5f credits FruitSaveData::AddToTotal in
    //   PauseScreen::RetryGameCallback (binary @ 0x00153f86).
    //   TODO: writer not yet RE'd (likely Game::Update accumulates dt).
    float m_AchievementProgressTimer;
    float retryTimer;              // +0x08
    float m_TransitionTimer;       // +0x0C
    float bombHitTimer;            // +0x10
    uint8_t missCount;             // +0x14: combo counter
    int currentScore;              // +0x18
    uint8_t m_bUnsullied;          // +0x1C: 0=no misses yet
    // +0x20: current coin balance (signed; AddCoins(n) at binary @ 0x0010a3bc).
    int32_t m_CoinsBalance;
    // +0x24: cumulative coins earned this session (only incremented when
    //   AddCoins's n > 0; never decreases).
    int32_t m_CoinsTotalEarned;
    // +0x28: snapshot of m_CoinsBalance at game start.
    //   GameOverScreen displays "YOU JUST EARNT %i COINS" computed as
    //   m_CoinsBalance - m_CoinsAtGameStart (binary @ 0x00142810).
    //   GameOverScreen::Update case-7 (retry) re-snapshots this slot.
    //   CoinsEnabled() @ 0x0010a428 returns 0 in shipping builds, so the
    //   coin UI never displays at runtime -- but the field layout is
    //   load-bearing for the Initialise / Update reads.
    // (Was previously mis-typed as Vec3 retryPos -- 12-byte slot stomped
    //  three int fields. Re-analyst 2026-05-08 confirmed via AddCoins
    //  pure-int ldr/str + SetupGameWork str(int) vs vstr(float) opcode.)
    int32_t m_CoinsAtGameStart;
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
    Mortar::SmartPtr<Mortar::Font> pFontReserved0;    // +0x50: unused (always null)
    // DIFFERS: binary loads HD font at +0x54 when DisplayManager::ShouldUseHDFonts().
    // Port always loads SD path (font_fruit_ninja.fnt) — HD asset-check not replicated.
    Mortar::SmartPtr<Mortar::Font> pFontMain;         // +0x54: fonts/font_fruit_ninja.fnt
    // DIFFERS: binary loads HD font at +0x58 when HD flag set.
    // Port always loads SD path (fruit_ninja_numbers.fnt).
    Mortar::SmartPtr<Mortar::Font> pFontNumbers;      // +0x58: fonts/fruit_ninja_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontReserved1;    // +0x5C: unused (always null; CoinCounter::Draw checks)
    uint32_t _gap_0x60;                       // +0x60: gap (not a Font slot per GameDestroy)
    Mortar::SmartPtr<Mortar::Font> pFontReserved2;    // +0x64: unused (always null)
    Mortar::SmartPtr<Mortar::Font> pFontGreen;        // +0x68: fonts/fruit_ninja_numbers_green.fnt
    Mortar::SmartPtr<Mortar::Font> pFontArcade;       // +0x6C: fonts/arcade_results_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontGold;         // +0x70: fonts/gold_numbers.fnt (File::Exists guarded; absent in shipped assets)
    Mortar::SmartPtr<Mortar::Font> pFontSilver;       // +0x74: fonts/silver_numbers.fnt (File::Exists guarded; absent)
    Mortar::SmartPtr<Mortar::Font> pFontBronze;       // +0x78: fonts/bronze_numbers.fnt (File::Exists guarded; absent)
    Mortar::SmartPtr<Mortar::Font> pFontArcadeAlias;  // +0x7C: non-owning alias of pFontArcade
    Mortar::SmartPtr<Mortar::Font> pFontBlue2;        // +0x80: fonts/fruit_ninja_numbers_blue2.fnt

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
    uint8_t m_bGameOverActive;     // +0x190 -- cleared by GameOverScreen::Update case 0xe
    // +0x191..+0x193: padding (binary leaves 3 bytes here; port matches)
    int m_FrameTimer;              // +0x194: (int)(dt * scale) + prev
    float m_MenuReturnTimer;       // +0x1A0
    uint8_t flag_0x1a8;            // +0x1A8
    uint8_t m_bFrameDirty;         // +0x604

    // +0xF4: splash logo texture (HB_logo.tex), loaded on demand in GameUpdate.
    // Released when splashFadeTimer reaches 0. Distinct from +0xFC background.
    Mortar::SmartPtr<Mortar::Texture> pSplashTex;

    // === Port-specific fields (SDL replacements for Bada OS) ===

    void* window;          // SDL_Window* (opaque to portable headers)
    void* gl_context;      // SDL_GLContext (opaque)
    Renderer renderer;
    Mortar::InputManager* inputManager;
    InputTranslatorSDL* inputTranslator;   // heap-allocated in GameSDL.cpp
    Mortar::ActorManager* actorManager;

    // Audio toggle state
    bool soundEnabled;
    bool musicEnabled;

    // Data directory path
    std::string data_dir;

    // Port control
    bool running;

    // === Singleton ===
    static Game* GetInstance() { return static_cast<Game*>(s_instance); }

    // === MortarGame vtable overrides (Game vtable @ 0x001e8bc0) ===
    // Inherited (not overridden): GetHardwareString(0), IsFastHardware(1),
    //   GetCacheDataArchive(8), SetLanguage(21), AllowOrientationChange(22), OrientationDidChange(23).
    void RenderAtHalfFrames(const char* hwName, const char* model) override;  // slot 2 @ 0x0010dcf4
    float GetHighResolutionScale() override;                // slot 3 @ 0x0010d9e4 returns 2.0f
    // Defunct: OpenFeint — no-op stub; binary @ 0x0010da04
    const char* GetOpenFeintProductKey() override;          // slot 4
    // Defunct: OpenFeint — no-op stub; binary @ 0x0010da14
    const char* GetOpenFeintSecret() override;              // slot 5
    // Defunct: OpenFeint — no-op stub; binary @ 0x0010da24
    const char* GetOpenDisplayName() override;              // slot 6
    // Defunct: Playhaven — no-op stub; binary @ 0x0010da34
    const char* GetPlayhavenToken() override;               // slot 7
    void CreateFileSystems(const char* a, const char* b) override;  // slot 9 @ 0x0010dca8
    void TellGameToStart(int multiplayer) override;         // slot 10 @ 0x0010dc80
    void Update(float dt) override;                         // slot 11 @ 0x0010dc78
    void Draw(float dt) override;                           // slot 12 @ 0x0010dc70
    void Init(int argc, char** argv) override;              // slot 13 @ 0x0010dbe4
    MortarGame* End() override;                             // slot 14 @ 0x0010db84
    void Paused() override;                                 // slot 15 @ 0x0010db34
    void UnPaused() override;                               // slot 16 @ 0x0010dae8
    const char* SelfVersion() override;                     // slot 17 @ 0x0010d9ec returns "1.5.1"
    void SaveOnExit() override;                             // slot 18 @ 0x0010dae0
    void SetAppLicensed(bool licensed) override;            // slot 19 @ 0x0010da68
    int GetAppLicensedState() override;                     // slot 20 @ 0x0010da94

    // Non-virtual — mirrors Game_SetLanguage @ 0x0010b140 (not a vtable override;
    // binary's slot 21 still points to MortarGame::SetLanguage base impl).
    void SetLanguage(const char* lang);

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
