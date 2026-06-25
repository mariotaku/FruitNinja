#ifndef GAME_H
#define GAME_H

//
// Game : MortarGame (singleton)
//
// =============================================================================
// BINARY LAYOUT NOTE
// =============================================================================
//
// In the original binary, "the game" is split across TWO independent globals:
//
//   1. g_MortarGame  (Ghidra: `Game`, ~260 bytes) -- engine singleton.
//      Holds vtable, m_versionString / m_formattedVersion, locale string,
//      hardware string, m_bFastHardware, m_licensedState, m_bInitialized.
//      Boot-time init, persistent across gameplay sessions.
//
//   2. game_work / g_GameData (Ghidra: `GameContext`, 0x608 / 1544 bytes) --
//      per-session gameplay state. See src/game/GameWork.h.
//      Reset on every Quit-to-menu. Accessed via raw GOT offset from gameplay
//      code; does NOT inherit from MortarGame in the binary.
//
// They live at DIFFERENT addresses with DIFFERENT lifetimes. The binary keeps
// them split because gameplay code has no business knowing the engine's locale
// strings, and engine code has no business knowing the player's score.
//
// The port's Stage-2 refactor honours this split: this `Game` class holds
// only the engine-singleton fields (MortarGame base + the three Game-specific
// extensions m_bSlowHardware / m_bLanguageSet / m_appState) plus port-only
// SDL plumbing. All gameplay-state fields live in the `game_work` global
// (src/game/GameWork.h) and are accessed directly as `game_work.X`.
//
// =============================================================================
//

// Analysed: 2026-04-25T12:00

#include <string>
#include <cstdint>
#include <cstddef>
#include "core/MortarGame.h"
#include "render/Renderer.h"
#include "input/InputManager.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

// Opaque platform handles (SDL types live behind void* in headers).
// The SDL backend (mainSDL.cpp / GameSDL.cpp) casts these to SDL_Window* / SDL_GLContext.
// InputTranslatorSDL is forward-declared so this header doesn't pull <SDL.h>;
// it's heap-allocated and stored as a pointer (see GameSDL.cpp).
class InputTranslatorSDL;
namespace Mortar { class ActorManager; }

struct Game : public Mortar::MortarGame {
    // === Game-specific fields beyond MortarGame base (original +0xFC..+0x103) ===
    uint8_t m_bSlowHardware;       // +0xFC: set by RenderAtHalfFrames when device matches old-iOS list
    uint8_t m_bLanguageSet;        // +0xFD: set to 1 at end of Game::Init
    // +0xFE..+0xFF: padding
    int m_appState;                // +0x100: app lifecycle state, init=0, no read xrefs found

    // +0xF4: splash logo texture (HB_logo.tex), loaded on demand in GameUpdate.
    // Released when splashFadeTimer reaches 0.
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
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetHighResolutionScale @ 0x0011fbd0
    const char* GetOpenFeintProductKey() override;          // slot 4
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetOpenFeintProductKey @ 0x0011fbf4
    const char* GetOpenFeintSecret() override;              // slot 5
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetOpenFeintSecret @ 0x0011fc10
    const char* GetOpenDisplayName() override;              // slot 6
    // Defunct: Playhaven -- no-op stub; v1.6.1 Game::GetOpenDisplayName @ 0x0011fc2c
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

    // Non-virtual -- mirrors Game_SetLanguage @ 0x0010b140 (not a vtable override;
    // binary's slot 21 still points to MortarGame::SetLanguage base impl).
    void SetLanguage(const char* lang);

    // === Methods ===
    Game();
    ~Game();

    bool init(void* win, void* gl);   // win = SDL_Window*, gl = SDL_GLContext (opaque to header)
    void shutdown();
    void run();
    void frameTick();               // Port specific: one game tick; thin wrapper for the three below
    void pollInput();               // Port specific: SDL event loop + BeginFrame (once per display frame)
    void stepUpdate();              // Port specific: dt=0 + SystemManager::Update + GameTaskUpdate
    // Port specific: alpha = fractional sim residual [0,1) for render interpolation.
    // steps = number of sim steps advanced this display frame (0 = pure interp frame,
    // >=1 = at least one sim step ran).  On steps==0 the Draw-path dt is zeroed so
    // particle/HUD/SliceEffect integrators don't over-advance on high-refresh displays
    // (#171).  Callers pass the loop's step count; frameTick() and runFrames() pass 1.
    void renderFrame(float alpha = 0.0f, int steps = 1);
    void runFrames(int frameCount);
    // Port specific: update the FPS value shown by DebugFps_Draw.
    // Called by platform main loops that compute FPS independently (e.g. mainEmscripten).
    void setCurrentFps(float fps);
};

// Field-offset assertions for Game (binary @ g_MortarGame, ARM32).
// Only the Game-specific fields that extend MortarGame in the binary are
// checked here; game_work fields have their own asserts in GameWork.h.
#ifdef __bada__
static_assert(offsetof(Game, m_bSlowHardware) == 0xFC, "Game::m_bSlowHardware must be at +0xFC");
static_assert(offsetof(Game, m_bLanguageSet)  == 0xFD, "Game::m_bLanguageSet must be at +0xFD");
static_assert(offsetof(Game, m_appState)      == 0x100, "Game::m_appState must be at +0x100");
#endif

// Forward declarations for lifecycle functions (src/game/)
void GamePreInitialise();
// DIFFERS: original passes the Bada window/config @0x0011d22c; SDL port owns its window, args unused.
void GameInitialise(void* window, const char* config);
void GameDestroy();

#endif
