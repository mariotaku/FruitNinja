//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game -> GamePreInitialise -> GameInitialise ->
//   [GameTaskUpdate loop] -> GameDestroy
//
// Analysed: 2026-05-04T00:00

#include "Game.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "engine/audio/SoundManager.h"
#include "engine/audio/GameSound.h"
#include "game/WaveManager.h"
#include <cstdio>
#include <cstring>
// SDL-bound bits (init / run / runFrames) live in GameSDL.cpp.

// Matches Game_ctor (0x0010dab0): calls MortarGame ctor, clears 3 fields
Game::Game()
    : Mortar::MortarGame(),
      m_bSlowHardware(0), m_bLanguageSet(0), m_appState(0),
      taskStateIndex(0), field_0x01(0), gameActiveFlag(0), languageFlag(0),
      gameMode(0), pauseFlag(0), retryFlag(0), field_0x07(0),
      retryTimer(0), m_TransitionTimer(0), bombHitTimer(0),
      missCount(0), currentScore(0), m_bUnsullied(0),
      m_CritTimer(0), m_ScoreThreshold(0), field_0x34(0), m_bSlowMotion(0),
      dt(0), hud(nullptr),
      pCamera(nullptr),
      pSaveData(nullptr),
      m_bSoundOn(true), m_bMusicOn(true),
      field_0x88(0),
      mainScreen(nullptr),
      pTutorialCtrl(nullptr),
      fruitTotal(0),
      pGameSound(nullptr),
      m_gameDataLicensedState(0),
      m_FrameTimer(0), m_MenuReturnTimer(0), flag_0x1a8(0), m_bFrameDirty(0),
      window(nullptr), gl_context(nullptr),
      inputManager(nullptr), inputTranslator(nullptr), actorManager(nullptr),
      soundEnabled(true), musicEnabled(true),
      running(false)
{
    // s_instance already set by MortarGame ctor
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

// slot 4 @ 0x0010da04
// Defunct: OpenFeint — no-op stub; binary @ 0x0010da04
const char* Game::GetOpenFeintProductKey() { return "7rJ4RGXLEkwDnRr9QuNIQ"; }

// slot 5 @ 0x0010da14
// Defunct: OpenFeint — no-op stub; binary @ 0x0010da14
const char* Game::GetOpenFeintSecret() { return "B8DmWmQ4pX3aHCtYIpu6g8rWBEkY29mlQkDjUcprKE"; }

// slot 6 @ 0x0010da24
// Defunct: OpenFeint — no-op stub; binary @ 0x0010da24
const char* Game::GetOpenDisplayName() { return "Fruit Ninja"; }

// slot 7 @ 0x0010da34
// Defunct: Playhaven — no-op stub; binary @ 0x0010da34
const char* Game::GetPlayhavenToken() { return "FIX!"; }

// slot 9 @ 0x0010dca8 — sets up FileSystem_Direct; args unused (root path from rodata)
void Game::CreateFileSystems(const char* a, const char* b) {
    (void)a; (void)b;
    // TODO: 0x0010dca8 — FileSystem_Direct construction not yet ported
}

// slot 10 @ 0x0010dc80 — sets HUD multiplayer state, resets WaveManager
void Game::TellGameToStart(int multiplayer) {
    (void)multiplayer;
    if (hud) {
        hud->SetToMultiplayerState();
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
    languageFlag = 0;                   // g_GameData[3] = 0
    SetHardware("BADA", false);
    GameInitialise();
    m_bLanguageSet = 1;
}

// slot 14 @ 0x0010db84 — exit + destroy, returns this
Mortar::MortarGame* Game::End() {
    GameTaskExit();
    GameDestroy();
    return this;
}

// slot 15 @ 0x0010db34 — pause: reset input, pause sound, HUD::OnPause, save
void Game::Paused() {
    // TODO: 0x0010db34 — LoadingJob::CanBoot() guard not yet ported
    // Port specific: InputManager::ResetDevices() not yet ported; skipped.
    if (pGameSound) {
        pGameSound->Pause();
    }
    Mortar::SoundManager::GetInstance().BeginInterruption();
    if (hud) {
        hud->OnPause();
    }
    // Port specific: SkipToPause(false) not yet declared; skipped.
    GameTaskExit();  // Port specific: GameTaskSaveOnExit not yet declared; GameTaskExit used as placeholder.
}

// slot 16 @ 0x0010dae8 — resume: end interruption, unpause sound, unpause game
void Game::UnPaused() {
    // TODO: 0x0010dae8 — LoadingJob::CanBoot() guard not yet ported
    if (pGameSound) {
        Mortar::SoundManager::GetInstance().EndInterruption();
        pGameSound->Unpause();
    }
    // TODO: 0x0010dae8 — UnpauseGame() conditioned on m_TransitionTimer != 0.0f
}

// slot 17 @ 0x0010d9ec
const char* Game::SelfVersion() { return "1.5.1"; }

// slot 18 @ 0x0010dae0 — tail call to GameTaskSaveOnExit
void Game::SaveOnExit() {
    // TODO: 0x0010dae0 — GameTaskSaveOnExit not yet declared; using GameTaskExit as placeholder
    GameTaskExit();
}

// slot 19 @ 0x0010da68 — reads/writes g_GameData+0x18C
void Game::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_gameDataLicensedState = 1;
    } else if (m_gameDataLicensedState != 1) {
        m_gameDataLicensedState = 2;
    }
}

// slot 20 @ 0x0010da94 — returns g_GameData+0x18C
int Game::GetAppLicensedState() { return m_gameDataLicensedState; }

// Non-virtual — mirrors Game_SetLanguage @ 0x0010b140
// NOT a vtable override; binary slot 21 still uses MortarGame::SetLanguage base impl.
void Game::SetLanguage(const char* lang) {
    (void)lang;
    languageFlag = 0;
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
