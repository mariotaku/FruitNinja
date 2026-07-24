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

    // Data directory path (read-only asset tree -- see FileSystem_Direct
    // Initialise(data_dir, /*writable=*/false) in GameInitialise.cpp).
    std::string data_dir;

    // Port specific: writable save directory, separate from data_dir so the
    // (read-only) asset tree can become a disc/archive later without taking
    // save files with it. Every save-path helper (GetSavePath() /
    // GetSettingsSavePath() / BuildItemSaveFullPath()) joins this with the
    // filename -- no platform branches live in those functions themselves.
    // Set on Wii directly (FN_SAVE_DIR, GameWii.cpp -- GX backend, no SDL);
    // every SDL-backed platform (host/webOS/Emscripten) resolves it through
    // the single shared Mortar_ResolveSaveDir() (src/platform/SaveDirSDL.h),
    // called identically from both mainSDL.cpp (pre-init) and Game::init
    // (GameSDL.cpp) so the two calls can't drift apart.
    std::string save_dir;

    // Port control
    bool running;

    // Port specific: OS-timer-cancel equivalent. Binary FruitNinja::OnBackground
    // @0x001ef660 cancels m_pTimer (halts the whole tick); OnForeground @0x001ef6cc
    // restarts it. Desktop has no cancelable timer, so we gate stepUpdate() on this
    // instead. Separate from bM_Mode/PauseScreen overlay (Paused()/UnPaused() above
    // are the faithfully-ported unconditional-call semantics; this flag is the
    // additional freeze that binary gets for free via the cancelled OS timer).
    bool m_bBackgrounded;

    // === Singleton ===
    static Game* GetInstance() { return static_cast<Game*>(s_instance); }

    // === MortarGame vtable overrides (TODO: re-verify v1.6.1 Game vtable address) ===
    // Inherited (not overridden): GetHardwareString(0), IsFastHardware(1),
    //   GetCacheDataArchive(8), SetLanguage(21), AllowOrientationChange(22), OrientationDidChange(23).
    void RenderAtHalfFrames(const char* hwName, const char* model) override;  // slot 2; v1.6.1 Game::RenderAtHalfFrames @0x001207f0
    float GetHighResolutionScale() override;                // slot 3; v1.6.1 Game::GetHighResolutionScale @0x0011fbd0 returns 2.0f
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetHighResolutionScale @ 0x0011fbd0
    const char* GetOpenFeintProductKey() override;          // slot 4; v1.6.1 Game::GetOpenFeintProductKey @0x0011fbf4
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetOpenFeintProductKey @ 0x0011fbf4
    const char* GetOpenFeintSecret() override;              // slot 5; v1.6.1 Game::GetOpenFeintSecret @0x0011fc10
    // Defunct: OpenFeint -- no-op stub; v1.6.1 Game::GetOpenFeintSecret @ 0x0011fc10
    const char* GetOpenDisplayName() override;              // slot 6; v1.6.1 Game::GetOpenDisplayName @0x0011fc2c
    // Defunct: Playhaven -- no-op stub; v1.6.1 Game::GetOpenDisplayName @ 0x0011fc2c
    const char* GetPlayhavenToken() override;               // slot 7; v1.6.1 Game::GetPlayhavenToken @0x0011fc48
    void CreateFileSystems(const char* a, const char* b) override;  // slot 9; v1.6.1 Game::CreateFileSystems @0x00120704
    void TellGameToStart(int multiplayer) override;         // slot 10; v1.6.1 Game::TellGameToStart @0x001206c8
    void Update(float dt) override;                         // slot 11; TODO: re-verify v1.6.1 Game::Update address (no named symbol)
    void Draw(float dt) override;                           // slot 12; TODO: re-verify v1.6.1 Game::Draw address (no named symbol)
    void Init(int argc, const char** argv) override;        // slot 13; v1.6.1 Game::Init @0x00120374
    MortarGame* End() override;                             // slot 14; TODO: re-verify v1.6.1 Game::End address (no named symbol)
    void Paused() override;                                 // slot 15; v1.6.1 Game::Paused @0x001202ec
    void UnPaused() override;                               // slot 16; v1.6.1 Game::UnPaused @0x00120270
    const char* SelfVersion() override;                     // slot 17; v1.6.1 Game::SelfVersion @0x0011fbd8 returns "1.6.1"
    void SaveOnExit() override;                             // slot 18; v1.6.1 Game::SaveOnExit @0x0012026c
    void SetAppLicensed(bool licensed) override;            // slot 19; v1.6.1 Game::SetAppLicensed @0x0011fc7c
    int GetAppLicensedState() override;                     // slot 20; v1.6.1 Game::GetAppLicensedState @0x0011fcbc

    // Implicit override of virtual MortarGame::SetLanguage (TODO: re-verify v1.6.1 Game::SetLanguage address;
    // binary's slot 21 still points to MortarGame::SetLanguage base impl).
    void SetLanguage(const char* lang) override;

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

    // Port specific: no binary counterpart. Walks game_work.mHud's control
    // list (HUD::UpdateRealtime, see hud/HUD.h) and calls
    // HUDControl::UpdateRealtime(dtSeconds) (see hud/HUDControl.h) on every
    // active control -- covers ScrollingMenu (shop list) and SettingsScreen
    // alike, since both are AddControl'd to game_work.mHud. Default no-op
    // per control. Called once per PRESENTED frame (native display refresh,
    // or the FPS-capped ~60Hz rate when FN::g_FpsCap60 is set) by both
    // platform main loops -- GameSDL.cpp's Game::renderFrame() and
    // mainEmscripten.cpp's EmscriptenFrame's `if (doPresent)` block -- NOT
    // the fixed 60Hz sim step (stepUpdate()). dtSeconds is real elapsed
    // wall-clock time since the last call, so motion tracks the display's
    // actual present rate (looks identical at 60 and 120+ fps) instead of
    // the sim tick rate.
    void tickRealtimeUi(float dtSeconds);
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
// DIFFERS: original passes the Bada window/config (v1.6.1 GameInitialise @0x0011d22c); SDL port owns its window, args unused.
void GameInitialise(void* window, const char* config);
void GameDestroy();

// String table init helpers (src/game/GameInitialise.cpp).
// v1.6.1 InitialiseStrings @0x11c1c8, UnloadRings @0x11cdc8, GetLanguage @0x1eebec
void InitialiseStrings();
void UnloadRings();
const char* GetLanguage(int& outLang);

// Pause state accessors (src/Game.cpp; binary globals map to GetTaskState()->pPauseScreen).
// v1.6.1 GetPauseScreen @0x1ca298, ClearPause @0x1ca3bc, GetPausedBy @0x1ca594
class PauseScreen;
PauseScreen* GetPauseScreen();
void ClearPause();
bool GetPausedBy();

// IsSingleTouchPressed v1.6.1 @ 0x001ca6f8 — returns true iff exactly one finger is currently down
// (IsTouchDown loop over slots 0..15) AND m_bTouchDownThisFrame edge flag fired this frame.
// Used to gate quickener recovery in GameUpdate.
bool IsSingleTouchPressed();

// v1.6.1 GetPauseAmount @0x001ca528: clamped PauseScreen::GetTime(), or 0.0 if no pause screen active.
// Returns the current pause blend amount in [0,1]: 0=not paused, 1=fully paused.
float GetPauseAmount();

// v1.6.1 GetStartupTexture @0x0011f570: returns the startup splash texture (HB_logo.tex),
// lazy-loading it on first call. Also clears isStartupTexturePortrait to false on load.
// DIFFERS: binary dispatches via Game vtable +0x70/+0x74 (SetStartupTexture/GetStartupTexture
// virtual slots); port accesses pSplashTex directly (vtable slots not yet declared).
// TODO: extend Game vtable with slots 24-29 (separate task) and move pSplashTex to
// MortarGame::m_StartupTexture at +0xFC after MortarGame sizeof fix.
Mortar::SmartPtr<Mortar::Texture> GetStartupTexture();

// v1.6.1 ReleaseStartupTexture @0x0011f64c: clears the startup texture reference (null SmartPtr).
// DIFFERS: binary dispatches via Game vtable slot +0x70 (SetStartupTexture); port accesses directly.
void ReleaseStartupTexture();

// Device/orientation query stubs (src/game/DeviceQuery.cpp).
// Binary: Bada accelerometer / OS version queries. Port: SDL fixed-landscape constants.
int CurrentOrientation();         // v1.6.1 @0x0011f4c4 — reads theGame+0x104
const char* GetHardwareString();  // v1.6.1 @0x0011f4e4 — free fn; distinct from MortarGame::GetHardwareString()
const char* GetSoftwareString();  // v1.6.1 @0x0011f504 — reads theGame+0x208 (Bada OS version)
int DeviceUpsideDown();           // v1.6.1 @0x0011a14c — accelerometer; dead in binary, returns 0
bool IsDeviceUpsideDown();        // v1.6.1 @0x0011a154 — game_work.m_UpsideDownTimer > 0
bool UpdateUpsideDown(float dt);  // v1.6.1 @0x0011a184 — drives upside-down timer

#endif
