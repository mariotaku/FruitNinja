//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game -> GamePreInitialise -> GameInitialise ->
//   [GameTaskUpdate loop] -> GameDestroy
//
// Analysed: 2026-05-04T00:00

#include "Game.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "engine/audio/SoundManager.h"
#include "engine/audio/GameSound.h"
#include "game/WaveManager.h"
#include "engine/asset/FileManager.h"
#include "engine/asset/FileSystem_Direct.h"
#include "screens/PauseScreen.h"
#include "engine/core/SystemManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>
// SDL-bound bits (init / run / runFrames) live in GameSDL.cpp.

// Matches Game_ctor (0x0010dab0): calls MortarGame ctor, clears 3 fields
Game::Game()
    : Mortar::MortarGame(),
      m_bSlowHardware(0), m_bLanguageSet(0), m_appState(0),
      window(nullptr), gl_context(nullptr),
      inputManager(nullptr), inputTranslator(nullptr), actorManager(nullptr),
      soundEnabled(true), musicEnabled(true),
      running(false)
{
    // s_instance already set by MortarGame ctor
    // game_work fields are zero-initialised at BSS load time (C-linkage global)
}

Game::~Game() {
    shutdown();
}

// --- Vtable overrides ---

// slot 2 @ 0x0010dcf4 — checks old iOS hardware list; sets m_bSlowHardware
void Game::RenderAtHalfFrames(const char* hwName, const char* model) {
    if (hwName) {
        if (strcmp("iPhone 1G", hwName) == 0     // binary @ 0x001b9989
         || strcmp("iPhone 3G", hwName) == 0     // binary @ 0x001b9993
         || strcmp("iPod Touch 1G", hwName) == 0 // binary @ 0x001b999d
         || (strcmp("iPod Touch 2G", hwName) == 0 && model && (unsigned char)*model < '4')) {
            m_bSlowHardware = 1;
        }
    }
    Mortar::MortarGame::RenderAtHalfFrames(hwName, model);
}

// slot 3 @ 0x0010d9e4
float Game::GetHighResolutionScale() { return 2.0f; }

// slot 4 @ 0x0011fbd0
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetHighResolutionScale @ 0x0011fbd0
const char* Game::GetOpenFeintProductKey() { return "7rJ4RGXLEkwDnRr9QuNIQ"; }

// slot 5 @ 0x0011fbf4
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetOpenFeintProductKey @ 0x0011fbf4
const char* Game::GetOpenFeintSecret() { return "B8DmWmQ4pX3aHCtYIpu6g8rWBEkY29mlQkDjUcprKE"; }

// slot 6 @ 0x0011fc10
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetOpenFeintSecret @ 0x0011fc10
const char* Game::GetOpenDisplayName() { return "Fruit Ninja"; }

// slot 7 @ 0x0011fc2c
// Defunct: Playhaven — no-op stub; v1.6.1 Game::GetOpenDisplayName @ 0x0011fc2c
const char* Game::GetPlayhavenToken() { return "FIX!"; }

// slot 9 @ 0x0010dca8 — sets up FileSystem_Direct; args unused (root path from rodata)
void Game::CreateFileSystems(const char* a, const char* b) {
    (void)a; (void)b;
    // Binary @ 0x0010dca8: instantiate FileSystem_Direct(0x14 bytes),
    // call Initialise(g_DataRoot, /*writable*/false), register with
    // FileManager::AddSystem(fs, 0, 0). Port resolves data root via cwd
    // (empty string -> SDL backend cwd handling), matching binary intent.
    Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
    fs->Initialise(/*root*/ "", /*writable*/ false);
    FileManager::GetInstance().AddSystem(fs, 0, 0);
}

// slot 10 @ 0x0010dc80 — sets HUD multiplayer state, resets WaveManager
void Game::TellGameToStart(int multiplayer) {
    (void)multiplayer;
    if (game_work.mHud) {
        game_work.mHud->SetToMultiplayerState();
        WaveManager::GetInstance()->Reset(true);
    }
}

// slot 11 @ 0x0010dc78
void Game::Update(float dt_) { GameTaskUpdate(dt_); }

// slot 12 @ 0x0010dc70
void Game::Draw(float dt_) { GameTaskDraw(dt_); }

// slot 13 @ 0x0010dbe4 — GamePreInitialise + SetHardware + GameInitialise + m_bLanguageSet
void Game::Init(int argc, char** argv) {
    // Port specific: CombineCommandLine(argv) not ported; SDL port resolves data
    // path via working directory at launch, not via argv.
    (void)argc; (void)argv;
    GamePreInitialise();
    game_work.languageFlag = 0;         // g_GameData[3] = 0
    SetHardware("BADA", false);
    GameInitialise(nullptr, nullptr);
    m_bLanguageSet = 1;
}

// slot 14 @ 0x0010db84 — exit + destroy, returns this
Mortar::MortarGame* Game::End() {
    GameTaskExit();
    GameDestroy();
    return this;
}

// slot 15 v1.6.1 Game::Paused @0x001202ec — pause: reset input, pause sound, HUD::OnPause, skip to pause overlay, save
void Game::Paused() {
    // Port specific: LoadingJob::CanBoot() always-true on Bada (no
    // async loading screen). InputManager::ResetDevices() not yet ported.
    if (game_work.mGameSound) {
        game_work.mGameSound->Pause();
    }
    Mortar::SoundManager::GetInstance().BeginInterruption();
    if (game_work.mHud) {
        game_work.mHud->OnPause();
    }
    SkipToPause(false);
    GameTaskSaveOnExit();
}

// slot 16 v1.6.1 Game::UnPaused @0x00120270 — resume: end interruption, unpause sound, unpause game
void Game::UnPaused() {
    // Port specific: LoadingJob::CanBoot() always-true on Bada (no
    // async loading screen). Skipped -- no port equivalent needed.
    if (game_work.mGameSound) {
        Mortar::SoundManager::GetInstance().EndInterruption();
        game_work.mGameSound->Unpause();
    }
    // Binary @ 0x0010dae8: gate UnpauseGame on m_GameDt != 0.0f
    // (the camera transition isn't mid-fade). UnpauseGame
    // sets gs->m_GameDt=0.4f, gs->bM_Mode=1.
    if (game_work.m_GameDt != 0.0f) {
        UnpauseGame();
    }
}

// slot 17 @ 0x0010d9ec — TODO: re-verify v1.6.1 Game::SelfVersion address
const char* Game::SelfVersion() { return "1.6.1"; }

// slot 18 @ 0x0010dae0 — tail call to GameTaskSaveOnExit
void Game::SaveOnExit() {
    GameTaskSaveOnExit();
}

// slot 19 @ 0x0010da68 — reads/writes g_GameData+0x18C
void Game::SetAppLicensed(bool licensed) {
    if (licensed) {
        game_work.m_gameDataLicensedState = 1;
    } else if (game_work.m_gameDataLicensedState != 1) {
        game_work.m_gameDataLicensedState = 2;
    }
}

// slot 20 @ 0x0010da94 — returns g_GameData+0x18C
int Game::GetAppLicensedState() { return game_work.m_gameDataLicensedState; }

// Non-virtual — mirrors Game_SetLanguage @ 0x0010b140
// NOT a vtable override; binary slot 21 still uses MortarGame::SetLanguage base impl.
void Game::SetLanguage(const char* lang) {
    (void)lang;
    game_work.languageFlag = 0;
}

// --- Port-specific SDL methods ---

// init / run / runFrames — see GameSDL.cpp (SDL-bound).

// Matches: GameDestroy (0x10b7ec) + FruitNinja::OnAppTerminating.
// Idempotent: main() calls shutdown() explicitly and Game::~Game() chains
// to it again; without the guard GameDestroy frees its resources twice.
void Game::shutdown() {
    static bool s_shutdownDone = false;
    if (s_shutdownDone) return;
    s_shutdownDone = true;
    GameDestroy();
    renderer.shutdown();
    // inputTranslator is deleted in GameSDL.cpp via the dedicated SDL teardown
    // path; we forward-declare InputTranslatorSDL here so we can't delete it.
}

// --- Pause state accessors (free functions, binary global s_pauseScreen maps to GetTaskState()->pPauseScreen) ---

// ASM-spec v1.6.1 GetPauseScreen @0x1ca298
PauseScreen* GetPauseScreen() {
    return GetTaskState()->pPauseScreen;
}

// ASM-spec v1.6.1 ClearPause @0x1ca3bc
// Deactivates the pause overlay: resets m_State then clears bM_Mode.
// Does NOT restore m_GameDt (that happens in the unpause/settle path).
void ClearPause() {
    if (game_work.bM_Mode) {
        PauseScreen* ps = GetPauseScreen();
        if (ps) ps->m_State = 0;    // PAUSE_STATE_HIDDEN; binary str #0 [r2,#0xd8]
        game_work.bM_Mode = 0;
    }
}

// ASM-spec v1.6.1 GetPausedBy @0x1ca594
// Returns true if a player finger triggered the pause (m_PressIndex > 0).
bool GetPausedBy() {
    PauseScreen* ps = GetPauseScreen();
    return ps && ps->m_PressIndex > 0;  // +0xcc; binary ldr r0,[r0,#0xcc]
}

// ASM-spec v1.6.1 GetPauseAmount @0x001ca528
// Binary calls ps->GetTime() twice (compiler didn't cache); port calls once (same observable result).
float GetPauseAmount() {
    PauseScreen* ps = GetPauseScreen();
    if (!ps) return 0.0f;
    float t = ps->GetTime();
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t;
}

// ASM-spec v1.6.1 GetStartupTexture @0x0011f570
// DIFFERS: binary dispatches via Game vtable +0x70/+0x74 (SetStartupTexture/GetStartupTexture
// virtual slots); port accesses pSplashTex directly (vtable slots not yet declared).
// TODO: extend Game vtable with slots 24-29 (separate task) and move pSplashTex to
// MortarGame::m_StartupTexture at +0xFC after MortarGame sizeof fix.
Mortar::SmartPtr<Mortar::Texture> GetStartupTexture() {
    Game* g = Game::GetInstance();
    if (!g->pSplashTex) {
        isStartupTexturePortrait = false;
        g->pSplashTex = Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex");
    }
    return g->pSplashTex;
}

// ASM-spec v1.6.1 ReleaseStartupTexture @0x0011f64c
// DIFFERS: binary dispatches via Game vtable slot +0x70 (SetStartupTexture); port accesses directly.
void ReleaseStartupTexture() {
    Game::GetInstance()->pSplashTex = Mortar::SmartPtr<Mortar::Texture>();
}
