// Analysed: 2026-04-30T12:00
// GameOverScreen — binary ctor 0x00141218, Update 0x00141960, Draw 0x00141da4
// First-iteration port: state 0->6->buttons->7/8->cleanup.
// Full 529-line state machine deferred; see TODOs below.

#include "GameOverScreen.h"
#include "Game.h"
#include "game/WaveManager.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include <cstring>
#include <cstdio>

// Size 0x13C — verified from spec.
// Signature maps: startState->endReason, startTimer->endScore per spec.
GameOverScreen::GameOverScreen(const char* modeName, int startState, float startTimer,
                                int expressionIdx, int bgPatternIdx, int pomCount, int starCount)
    : m_State(startState < 0 ? 0 : startState),
      m_Timer(startTimer < 0.0f ? 0.0f : startTimer),
      m_EndReason(startState),
      m_EndScore(startTimer),
      m_ExpressionIdx(expressionIdx),
      m_BgPatternIdx(bgPatternIdx),
      m_PomCount(pomCount),
      m_StarCount(starCount),
      m_pRetryBtn(nullptr),
      m_pQuitBtn(nullptr) {
    strncpy(m_ModeName, modeName ? modeName : "", sizeof(m_ModeName) - 1);
    m_ModeName[sizeof(m_ModeName) - 1] = '\0';
    m_LayerFlags = 0x100;
    m_bActive    = 1;
}

void GameOverScreen::Init() {
    // TODO: load "game-over" textures, set up result screen visuals
}

void GameOverScreen::Reset() {
    m_State     = 0;
    m_Timer     = 0.0f;
    m_pRetryBtn = nullptr;
    m_pQuitBtn  = nullptr;
}

void GameOverScreen::CreateButtons() {
    if (m_pRetryBtn || m_pQuitBtn) return;

    Game* game = Game::GetInstance();
    if (!game || !game->hud) return;

    // TODO: use real binary positions/sizes from docs/screens/game-over.md when ported.
    m_pRetryBtn = new MenuButton();
    m_pRetryBtn->pos         = Vec3(0.0f, -30.0f, 0.0f);
    m_pRetryBtn->m_LayerFlags = 0x08;
    m_pRetryBtn->m_FruitType  = -1;
    {
        GameOverScreen* self = this;
        m_pRetryBtn->m_ClickCallback = [self]() {
            if (Game* g = Game::GetInstance())
                g->retryFlag = 1;
            self->m_State = 7;
        };
    }
    game->hud->AddControl(m_pRetryBtn);

    m_pQuitBtn = new MenuButton();
    m_pQuitBtn->pos          = Vec3(0.0f, 30.0f, 0.0f);
    m_pQuitBtn->m_LayerFlags = 0x08;
    m_pQuitBtn->m_FruitType  = -1;
    {
        GameOverScreen* self = this;
        m_pQuitBtn->m_ClickCallback = [self]() {
            self->m_State = 9;
        };
    }
    game->hud->AddControl(m_pQuitBtn);
}

void GameOverScreen::Update(float dt) {
    switch (m_State) {
    case 0:
        // Entry: brief delay then transition to main display
        m_Timer += dt;
        if (m_Timer > 0.5f) {
            m_Timer = 0.0f;
            m_State = 6;
        }
        break;

    case 6:
        // Main display: create retry/quit buttons
        // TODO: proper layout + animations per binary state 6
        CreateButtons();
        m_State = 60;
        break;

    case 60:
        // Waiting for button press — handled via callbacks
        break;

    case 7:
        // Retry: reset wave without respawn
        WaveManager::GetInstance()->Reset(false);
        // TODO: EndRetryLevel (0x0016a25c) full flow
        if (Game* game = Game::GetInstance()) {
            game->retryFlag = 0;
            game->pauseFlag = 0;
        }
        m_bPendingRemoval = 1;
        break;

    case 8:
        // Quit to menu: reset wave without respawn
        WaveManager::GetInstance()->Reset(false);
        // TODO: QuitToMenu (0x00169e50) full flow
        m_bPendingRemoval = 1;
        break;

    case 9:
        // Quit path transition
        m_State = 8;
        break;

    case 11:
        // Final
        m_bPendingRemoval = 1;
        break;

    default:
        // TODO: states 1..5, 10: animations, score tally, star count
        m_State = 6;
        break;
    }
}

void GameOverScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
    // TODO: draw black overlay + "GAME OVER" + score text
    // Deferred until Font::DrawString and full asset loading is ported.
}
