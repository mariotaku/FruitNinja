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
#include "engine/input/Touch.h"
#include "screens/PauseScreen.h"
#include "engine/core/SystemManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>
// SDL-bound bits (init / run / runFrames) live in GameSDL.cpp.

// Matches Game_ctor (0x0010dab0): calls MortarGame ctor, clears 3 fields
Game::Game()
    : Mortar::MortarGame(),
      m_bSlowHardware(0), m_bLanguageSet(0), m_Orientation(0),
      window(nullptr), gl_context(nullptr),
      inputManager(nullptr), inputTranslator(nullptr), actorManager(nullptr),
      soundEnabled(true), musicEnabled(true),
      running(false), m_bBackgrounded(false)
{
    // s_instance already set by MortarGame ctor
    // game_work fields are zero-initialised at BSS load time (C-linkage global)
}

Game::~Game() {
    shutdown();
}

// --- Vtable overrides ---

// slot 4 v1.6.1 Game::RenderAtHalfFrames @0x001207f0 — checks old iOS hardware list; sets m_bSlowHardware (Game+0x100)
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

// slot 5 v1.6.1 Game::GetHighResolutionScale @0x0011fbd0
float Game::GetHighResolutionScale() { return 2.0f; }

// slot 6
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetOpenFeintProductKey @ 0x0011fbf4
const char* Game::GetOpenFeintProductKey() { return "7rJ4RGXLEkwDnRr9QuNIQ"; }

// slot 7
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetOpenFeintSecret @ 0x0011fc10
const char* Game::GetOpenFeintSecret() { return "B8DmWmQ4pX3aHCtYIpu6g8rWBEkY29mlQkDjUcprKE"; }

// slot 8
// Defunct: OpenFeint — no-op stub; v1.6.1 Game::GetOpenDisplayName @ 0x0011fc2c
const char* Game::GetOpenDisplayName() { return "Fruit Ninja"; }

// slot 9
// Defunct: Playhaven — no-op stub; v1.6.1 Game::GetPlayhavenToken @ 0x0011fc48
const char* Game::GetPlayhavenToken() { return "FIX!"; }

// slot 12 v1.6.1 Game::CreateFileSystems @0x00120704 — sets up FileSystem_Direct; args unused (root path from rodata)
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

// slot 13 v1.6.1 Game::TellGameToStart @0x001206c8 — sets HUD multiplayer state, resets WaveManager
void Game::TellGameToStart(int multiplayer) {
    (void)multiplayer;
    if (game_work.mHud) {
        game_work.mHud->SetToMultiplayerState();
        WaveManager::GetInstance()->Reset(true);
    }
}

// slot 14 — Game::Update; TODO: re-verify v1.6.1 Game::Update address (no named symbol)
void Game::Update(float dt_) { GameTaskUpdate(dt_); }

// slot 15 — Game::Draw; TODO: re-verify v1.6.1 Game::Draw address (no named symbol)
void Game::Draw(float dt_) { GameTaskDraw(dt_); }

// slot 16 v1.6.1 Game::Init @0x00120374 — GamePreInitialise + SetHardware + GameInitialise + m_bLanguageSet (Game+0x101)
void Game::Init(int argc, const char** argv) {
    // Port specific: CombineCommandLine(argv) not ported; SDL port resolves data
    // path via working directory at launch, not via argv.
    (void)argc; (void)argv;
    GamePreInitialise();
    game_work.languageFlag = 0;         // g_GameData[3] = 0
    // v1.6.1 Game::Init @0x0010dbe4: Bada Wave (S8500, Cortex-A8 + SGX540) is
    // fast hardware -> MortarGame::m_bFastHardware (+0xF4) is true at runtime.
    // Game::RenderAtHalfFrames @0x001207f0's slow/half-frame path only trips on
    // iPhone-1G/3G-class device strings, never "BADA". Gates: Fruit::Update's
    // fruit_flight flight-trail fallback (Fruit.cpp:501), SuperFruitControl jib
    // count (25 vs 10), FruitInfo fruit_shadow load, ScreenEffect fast/slow filters.
    SetHardware("BADA", true);
    GameInitialise(nullptr, nullptr);
    m_bLanguageSet = 1;
}

// slot 17 v1.6.1 Game::End @0x0010a468 — GameTaskExit() then GameDestroy(), returns this.
// Task exit runs FIRST: GameTaskExit dispatches the live state's exit handler
// (GameExit @0x001cfed4 for the Game task) which needs the HUD/ActorManager/save
// data that GameDestroy then tears down. Reached from Game::shutdown().
Mortar::MortarGame* Game::End() {
    GameTaskExit();
    GameDestroy();
    return this;
}

// slot 18 v1.6.1 Game::Paused @0x001202ec — pause: reset input, pause sound, HUD::OnPause, skip to pause overlay, save
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

// slot 19 v1.6.1 Game::UnPaused @0x00120270 — resume: end interruption, unpause sound, unpause game
void Game::UnPaused() {
    // Port specific: LoadingJob::CanBoot() always-true on Bada (no
    // async loading screen). Skipped -- no port equivalent needed.
    if (game_work.mGameSound) {
        Mortar::SoundManager::GetInstance().EndInterruption();
        game_work.mGameSound->Unpause();
    }
    // Binary @ 0x0010dae8: gate UnpauseGame on m_PauseAmount != 0.0f
    // (the camera transition isn't mid-fade). UnpauseGame
    // sets gs->m_PauseAmount=0.4f, gs->bM_Mode=1.
    if (game_work.m_PauseAmount != 0.0f) {
        UnpauseGame();
    }
}

// slot 20 v1.6.1 Game::SelfVersion @0x0011fbd8
const char* Game::SelfVersion() { return "1.6.1"; }

// slot 21 v1.6.1 Game::SaveOnExit @0x0012026c — tail call to GameTaskSaveOnExit
void Game::SaveOnExit() {
    GameTaskSaveOnExit();
}

// slot 22 v1.6.1 Game::SetAppLicensed @0x0011fc7c — reads/writes g_GameData+0x18C
void Game::SetAppLicensed(bool licensed) {
    if (licensed) {
        game_work.m_gameDataLicensedState = 1;
    } else if (game_work.m_gameDataLicensedState != 1) {
        game_work.m_gameDataLicensedState = 2;
    }
}

// slot 23 v1.6.1 Game::GetAppLicensedState @0x0011fcbc — returns g_GameData+0x18C
int Game::GetAppLicensedState() { return game_work.m_gameDataLicensedState; }

// slot 24 — mirrors Game_SetLanguage @0x0010b140
// DIFFERS: the port overrides slot 24; the binary's Game vtable leaves it pointing at
//          MortarGame::SetLanguage @0x0022dee4 and calls Game_SetLanguage non-virtually.
void Game::SetLanguage(const char* lang) {
    (void)lang;
    game_work.languageFlag = 0;
}

// --- Port-specific SDL methods ---

// init / run / runFrames — see GameSDL.cpp (SDL-bound).

// Port specific: the platform-teardown wrapper around the binary's slot-17
// End(). The Bada framework dispatches Game::End (GameTaskExit -> GameDestroy)
// at app terminate; the SDL/emscripten/Wii entry points call shutdown()
// instead, so route it through the virtual so the TASK-EXIT half actually
// runs. Before this, every port entry point called GameDestroy() alone and
// GameExit() had zero call sites -- leaking MainScreen's instance textures,
// the MissControl pool and the per-session wave/entity-heap state on quit.
//
// Idempotent: main() calls shutdown() explicitly and Game::~Game() chains
// to it again; without the guard GameDestroy frees its resources twice.
// GameTaskExit() is separately self-guarding (GameTaskState::initialized), so
// a Game that never booted a task state runs no exit handler at all.
void Game::shutdown() {
    static bool s_shutdownDone = false;
    if (s_shutdownDone) return;
    s_shutdownDone = true;
    End();  // slot 17: GameTaskExit() then GameDestroy(), binary order
    renderer.shutdown();
    // inputTranslator is deleted in GameSDL.cpp via the dedicated SDL teardown
    // path; we forward-declare InputTranslatorSDL here so we can't delete it.
}

// Port specific: no binary counterpart -- see Game.h. Dispatches the
// per-present UI tick by walking game_work.mHud's control list and calling
// HUDControl::UpdateRealtime(dtSeconds) on each active control (default
// no-op; see HUDControl.h / HUD::UpdateRealtime). Covers ScrollingMenu
// (m_pShopList, AddControl'd to the HUD) and SettingsScreen (also
// AddControl'd to the HUD) alike -- no per-screen special case needed.
// #ifndef __bada__: HUDControl::UpdateRealtime / HUD::UpdateRealtime don't
// exist under __bada__ (see HUDControl.h) so the asm-verify cross-build's
// vtable layout stays byte-identical to the binary; this whole method is a
// no-op there instead.
void Game::tickRealtimeUi(float dtSeconds) {
#ifndef __bada__
    // Task #13: refresh per-present live finger positions BEFORE any widget's
    // UpdateRealtime runs this present, so ScrollingMenu/SettingsScreen/
    // UiDropdown's drag-delta reads (Mortar::Touch::GetLivePos) see the
    // newest ring sample for native-refresh-rate (120Hz) scroll tracking.
    // EDGE/dispatch + slicing stay on the 60Hz sim tick (Touch::Update /
    // DispatchForSimTick) -- this only refreshes the separate liveX/liveY
    // shadow fields, read-only against the ring buffer.
    Mortar::Touch::GetInstance().RefreshLivePos();
    if (game_work.mHud) {
        game_work.mHud->UpdateRealtime(dtSeconds);
    }
#else
    (void)dtSeconds;
#endif
}

// --- Pause state accessors (free functions, binary global s_pauseScreen maps to GetTaskState()->pPauseScreen) ---

// ASM-spec v1.6.1 GetPauseScreen @0x1ca298
PauseScreen* GetPauseScreen() {
    return GetTaskState()->pPauseScreen;
}

// ASM-spec v1.6.1 ClearPause @0x1ca3bc
// Deactivates the pause overlay: resets m_State then clears bM_Mode.
// Does NOT restore m_PauseAmount (that happens in the unpause/settle path).
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
// Binary: probe via vtable byte +0x74 (slot 29) into a temporary, test it for null, and on
// miss load the texture and store it back through vtable byte +0x70 (slot 28), then return
// a fresh copy from +0x74 again.
Mortar::SmartPtr<Mortar::Texture> GetStartupTexture() {
    Game* g = Game::GetInstance();
    if (!g->GetStartupTexture()) {
        isStartupTexturePortrait = false;
        g->SetStartupTexture(Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex"));
    }
    return g->GetStartupTexture();
}

// ASM-spec v1.6.1 ReleaseStartupTexture @0x0011f64c
// Binary: constructs an empty SmartPtr<Texture> and stores it through vtable byte +0x70 (slot 28).
void ReleaseStartupTexture() {
    Game::GetInstance()->SetStartupTexture(Mortar::SmartPtr<Mortar::Texture>());
}
