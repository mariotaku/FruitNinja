//
// Game — singleton, SDL entry, main loop
// Matches original lifecycle: Game::Game → GamePreInitialise → GameInitialise →
//   [GameTaskUpdate loop] → GameDestroy
//

#include "Game.h"
#include "game/GameTaskState.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include <cstdio>
// SDL-bound bits (init / run / runFrames) live in GameSDL.cpp.

// Matches Game_ctor (0x0010dab0): calls MortarGame ctor, clears 3 fields
Game::Game()
    : Mortar::MortarGame(),
      field_0xfc(0), field_0xfd(0), field_0x100(0),
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
      inputManager(nullptr), actorManager(nullptr),
      soundEnabled(true), musicEnabled(true),
      running(false)
{
    // s_instance already set by MortarGame ctor
}

Game::~Game() {
    shutdown();
}

// Matches 0x0010d9ec
const char* Game::SelfVersion() {
    return "1.5.1";
}

// Matches 0x0010dae0 — calls GameTaskSaveOnExit()
void Game::SaveOnExit() {
    GameTaskExit();
}

// Matches 0x0010b140 — writes 0 to languageFlag (g_GameData+0x03)
void Game::SetLanguage(const char* lang) {
    (void)lang;
    languageFlag = 0;
}

// Matches 0x0010da68 — reads/writes g_GameData+0x18C
void Game::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_gameDataLicensedState = 1;
    } else if (m_gameDataLicensedState != 1) {
        m_gameDataLicensedState = 2;
    }
}

// Matches 0x0010da94 — returns g_GameData+0x18C
int Game::GetAppLicensedState() const {
    return m_gameDataLicensedState;
}

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
}
