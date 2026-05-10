// Analysed: 2026-05-02T00:00
// GameOverScreen -- binary ctor 0x00142900, Initialise 0x00142674, Update 0x00141b34
// PreDrawOrder 0x0014171c, DrawOrder 0x00141448.
// No per-class Draw -- inherits HUDControl3d::Draw (0x0014428c).

#include "GameOverScreen.h"
#include "BonusScreen.h"
#include "Game.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "game/AchievementManager.h"
#include "game/BonusManager.h"
#include "entities/ActorManager.h"
#include "entities/FruitInfo.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/FruitFactControl.h"
#include "asset/TextureManager.h"
#include "math/MathUtil.h"
#include "math/Vec3.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdlib>

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// Helpers — local to this TU (match binary static helpers)
// ---------------------------------------------------------------------------

static int GetCurrentScore(int playerIdx) {
    if (playerIdx != 0) return 0;
    Game* g = Game::GetInstance();
    return g ? g->currentScore : 0;
}

static int GetCurrentModeHighscore() {
    Game* g = Game::GetInstance();
    if (!g || !g->pSaveData) return 0;
    int mode = g->gameMode & 0x03;
    return g->pSaveData->m_ModeHighScores[mode];
}

// SetTerminate: game[+0x33] = 1. Reuses this->SetTerminate().
// The free function wrapper is used from within Update.
static void DoSetTerminate() {
    // game[+0x33] is m_bPendingRemoval in the port's Game struct.
    // Binary: *(uint8_t*)(game + 0x33) = 1; CancelHUDProgressionTimer (no-op stub).
    // Port: mark the HUD screen for removal via game.pGameOverScreen->m_bPendingRemoval.
    Game* g = Game::GetInstance();
    if (g && g->pGameOverScreen) {
        g->pGameOverScreen->m_bPendingRemoval = 1;
    }
}

static void DoQuitToMenu() {
    // Binary 0x00169e50 -- full flow not yet ported.
    // Port: thaw wave timer, set pauseFlag.
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    Game* g = Game::GetInstance();
    if (g) g->pauseFlag = 1;
}

// ---------------------------------------------------------------------------
// Static content load/unload (binary: gated by static guard)
// ---------------------------------------------------------------------------

void GameOverScreen::LoadContent() {}
void GameOverScreen::UnLoadContent() {}

// ---------------------------------------------------------------------------
// Default ctor (binary shape)
// ---------------------------------------------------------------------------

GameOverScreen::GameOverScreen()
    : HUDControl3d(),
      field_0x7c(0.0f),
      m_State(0),
      m_Timer(0.0f),
      m_TitleSizeX(0.0f),
      m_TitleSizeY(0.0f),
      m_TitleSizeZ(0.0f),
      field_0x94(0),
      m_pRetryBtn(nullptr),
      m_pSlot9c(nullptr),
      field_0xa0(0),
      m_pQuitBtn(nullptr),
      m_pSlotA8(nullptr),
      m_AnimCounter(0),
      m_OffsetPosX(0.0f),
      m_OffsetPosY(0.0f),
      m_OffsetPosZ(0.0f),
      m_pFruitFact(nullptr),
      m_pSlotC0(nullptr),
      m_pBonusScreen(nullptr),
      m_pNoticeCtrl(nullptr),
      m_PostOk(0),
      m_PostInProgress(0),
      m_ProgressCounter(0),
      m_GameOverTex(0),
      field_0x118(0),
      m_MostFruitCount(-1),
      m_bScoreSubmitted(0),
      m_ExpressionIdx(0),
      m_BgPatternIdx(0),
      m_PomCount(0),
      m_StarCount(0),
      m_bIsClassic(0),
      m_FruitFactAlpha(0.0f)
{
    memset(m_CoinsEarnedLabel, 0, sizeof(m_CoinsEarnedLabel));
}

// ---------------------------------------------------------------------------
// Reset — empty in binary (0x00140554, single bx lr)
// ---------------------------------------------------------------------------

void GameOverScreen::Reset() {}

// ---------------------------------------------------------------------------
// IsAllowedToExit — always 1 in binary (0x0014061c)
// ---------------------------------------------------------------------------

bool GameOverScreen::IsAllowedToExit() { return true; }

// (Per re-analyst 2026-05-08: the asm-verify pipeline reports
// `_ZN14GameOverScreen12PreDrawOrderEPfi` (Pf,i = float*, int) but the
// binary at 0x0014171c actually takes (HUDControl* parent, int layer)
// -- the float* mangling is Ghidra noise from a missing prototype. The
// real impl is the (const Vec3&, int) overload at the bottom of the
// file. The previous duplicate float*-stub overloads were deleted.)

// ---------------------------------------------------------------------------
// GameOverScreen constructor (0x00142900) -- thin wrapper over Initialise
// ---------------------------------------------------------------------------

GameOverScreen::GameOverScreen(const char* modeName, int param2, float param3,
                               int expressionIdx, int bgPatternIdx,
                               int pomCount, int starCount)
    : HUDControl3d(),
      field_0x7c(0.0f),
      m_State(0),
      m_Timer(0.0f),
      m_TitleSizeX(0.0f),
      m_TitleSizeY(0.0f),
      m_TitleSizeZ(0.0f),
      field_0x94(0),
      m_pRetryBtn(nullptr),
      m_pSlot9c(nullptr),
      field_0xa0(0),
      m_pQuitBtn(nullptr),
      m_pSlotA8(nullptr),
      m_AnimCounter(0),
      m_OffsetPosX(0.0f),
      m_OffsetPosY(0.0f),
      m_OffsetPosZ(0.0f),
      m_pFruitFact(nullptr),
      m_pSlotC0(nullptr),
      m_pBonusScreen(nullptr),
      m_pNoticeCtrl(nullptr),
      m_PostOk(0),
      m_PostInProgress(0),
      m_ProgressCounter(0),
      m_GameOverTex(0),
      field_0x118(0),
      m_MostFruitCount(-1),
      m_bScoreSubmitted(0),
      m_ExpressionIdx(expressionIdx),
      m_BgPatternIdx(bgPatternIdx),
      m_PomCount(pomCount),
      m_StarCount(starCount),
      m_bIsClassic(0),
      m_FruitFactAlpha(0.0f)
{
    memset(m_CoinsEarnedLabel, 0, sizeof(m_CoinsEarnedLabel));
    Initialise(modeName, param2, param3, expressionIdx, bgPatternIdx, pomCount, starCount);
}

GameOverScreen::~GameOverScreen() {
    // Release called by HUD via vtable
}

// ---------------------------------------------------------------------------
// Initialise (0x00142674)
// ---------------------------------------------------------------------------

// Binary @ 0x00142674
void GameOverScreen::Initialise(const char* modeName, int param2, float param3,
                                int expressionIdx, int bgPatternIdx,
                                int pomCount, int starCount)
{
    // One-shot LoadContent (gated in binary by static guard; stub no-ops)
    LoadContent();

    // Defunct: NetworkManager.InvalidatePublishTextCallback -- no-op stub; binary @ 0x0014268c
    // Single call, no return value used; safe to skip on the SDL port.

    m_pNoticeCtrl    = nullptr; // +0xC8
    m_PomCount       = pomCount;
    m_Timer          = 0.0f;
    m_MostFruitCount = -1;
    field_0x118      = 0;
    m_StarCount      = starCount;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game->gameMode : 0;

    // Load mode-specific game-over background texture into m_SecondaryTex.
    // Texture names confirmed from binary @ 0x00140bb0 (re-analyst):
    //   gameMode 2 (Arcade) -> arcade_time_up.tex
    //   gameMode 3 (Zen)    -> time_up.tex
    //   default (Classic)   -> gameover.tex
    // TODO: 0x00140bb0 — confirm exact mode->variant mapping; the slot
    //   identification is correct but the mode->slot index mapping needs
    //   tracing through DAT_001428ec/f0/f4 GOT slots in Initialise.
    {
        Mortar::SmartPtr<Mortar::Texture> bgTex;
        if (gameMode == 2)
            bgTex = TextureManager::LoadLocalisedTexture("arcade_time_up.tex");
        else if (gameMode == 3)
            bgTex = TextureManager::LoadLocalisedTexture("time_up.tex");
        else
            bgTex = TextureManager::LoadLocalisedTexture("gameover.tex");
        // Store the SmartPtr in m_SecondaryTex (matches binary type).
        m_SecondaryTex = bgTex;
        // Record title size from texture dimensions
        if (bgTex) {
            m_TitleSizeX = (float)bgTex->m_Width;
            m_TitleSizeY = (float)bgTex->m_Height;
        } else {
            m_TitleSizeX = 256.0f; // DIFFERS: placeholder if tex unavailable
            m_TitleSizeY = 128.0f;
        }
        m_TitleSizeZ = 0.0f;
    }

    m_State          = 0;
    m_LayerFlags     = Mortar::HUD_LAYER_NONE; // binary: m_LayerFlags = 0 in Initialise (BeginDraw sets it each frame)
    m_GameOverTex    = 0; // field_0x114.SetNull()
    m_AnimCounter    = 0;
    m_bScoreSubmitted = 0;
    m_BgPatternIdx   = bgPatternIdx;
    field_0x94       = 0;
    m_pBonusScreen   = nullptr;
    m_FruitFactAlpha = game ? game->m_TransitionTimer : 0.0f; // game[+0xC] = game.alpha (m_TransitionTimer)
    m_ExpressionIdx  = expressionIdx;
    field_0xa0       = 0;
    m_pSlot9c        = nullptr;
    m_bIsClassic     = (gameMode == 0) ? 1 : 0;
    m_pQuitBtn       = nullptr;
    m_pSlotA8        = nullptr;
    m_pRetryBtn      = nullptr;
    field_0x7c       = 0.0f;

    // Randomise expression when caller passed -1
    if (expressionIdx < 1) {
        m_ExpressionIdx = 1;
        FindMostOfFruit();
        int score = GetCurrentScore(0);
        int hi    = GetCurrentModeHighscore();
        if (score > hi / 2) {
            // 2 or 3 (better expressions)
            m_ExpressionIdx = (rand() % 2) + 2;
        }
    }
    if (bgPatternIdx < 1) {
        m_BgPatternIdx = (rand() % 3) + 1;  // 1..3
    }

    pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
    // Initial off-screen offset (DAT_001428d0=184.0, DAT_001428d4=75.0)
    m_OffsetPosX = 184.0f;
    m_OffsetPosY = 75.0f;
    m_OffsetPosZ = 0.0f;

    m_ProgressCounter  = 0;
    m_pFruitFact       = nullptr;
    m_pSlotC0          = nullptr;
    m_PostOk           = 0;
    m_PostInProgress   = 0;

    // Format the coin-earned label.
    // ASM-verified: 2026-05-08 binary @ 0x00142810 (re-analyst):
    //   sprintf(m_CoinsEarnedLabel, "YOU JUST EARNT %i COINS",
    //           game->m_CoinsBalance - game->m_CoinsAtGameStart)
    // The "X days left" placeholder string was a mis-guess; the real
    // format string at DAT_001428fc / 0x001bb926 is "YOU JUST EARNT %i
    // COINS". Note: CoinsEnabled() @ 0x0010a428 returns 0 in shipping
    // builds so the label is computed but never displayed -- still
    // load-bearing for the binary's call shape.
    {
        const int coinsEarned = game
            ? (game->m_CoinsBalance - game->m_CoinsAtGameStart) : 0;
        snprintf(m_CoinsEarnedLabel, sizeof(m_CoinsEarnedLabel),
                 "YOU JUST EARNT %d COINS", coinsEarned);
    }

    // FAST-PATH: caller passed valid score/state and we're past wave 5 with running progress
    if (param3 >= 0.0f && param2 >= 0) {
        FindMostOfFruit();
        // ASM-spec for binary @ 0x001428a0..0x001428bc (re-analyst):
        //   gate is `wave.alpha > 0.99899f` (DAT_001428d8 = 0x3F7FBE77)
        //   write is `wave.alpha = 0.99982f` (DAT_001428dc = 0x3F7FF2E5)
        //   NOT 0.999/1.0 as previously assumed.
        const float kWaveAlphaGate  = 0.99899f;  // DAT_001428d8
        const float kWaveAlphaSet   = 0.99982f;  // DAT_001428dc
        float waveAlpha = game ? game->m_TransitionTimer : 0.0f;
        if (param2 > 5 && waveAlpha > kWaveAlphaGate) {
            if (game) game->m_TransitionTimer = kWaveAlphaSet;
            m_State           = 6;
            m_bScoreSubmitted = 1;
            m_FruitFactAlpha  = 1.0f;
            // Immediate state-6 invocation
            Update(0.0f);
        }
        m_State = param2;
        m_Timer = param3;
    }
}

// ---------------------------------------------------------------------------
// Init (vtable slot 2, 0x00140548) — trivial pass-through
// ---------------------------------------------------------------------------

// Binary @ 0x00140548
void GameOverScreen::Init() {
    // TODO: 0x00140548 -- Init invokes vtable+0x10 (Update/slot 10), not vtable+0x18 -- fix misleading comment
    // Binary: (*vtable[10])(this) calls Update once with dt=0.
    // Port: no-op (Update not safe to call from Init without state ready).
}

// ---------------------------------------------------------------------------
// BeginDraw (vtable slot 5, 0x00140590)
// ---------------------------------------------------------------------------

// Binary @ 0x00140590
void GameOverScreen::BeginDraw(float /*dt*/) {
    // Binary: m_LayerFlags = (m_State != 0) ? 0x81 : 1
    // 0x81 = HUD_LAYER_POST_ACTOR | HUD_LAYER_DEFAULT -- Draw runs in BOTH
    // the post-actor (0x80) and default (0x01) HUD::Draw passes during
    // animations / transitions; once settled (m_State == 0) only the
    // default pass renders, suppressing the second draw.
    m_LayerFlags = (m_State != 0)
        ? (int)(Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT)
        : (int)Mortar::HUD_LAYER_DEFAULT;
}

// ---------------------------------------------------------------------------
// Release (vtable slot 3, 0x00140d98)
// ---------------------------------------------------------------------------

// Binary @ 0x00140d98
void GameOverScreen::Release() {
    // TODO: 0x00140d98 -- pSaveData[+0x12C] write should be byte 0, not int -1 (minor divergence)
    m_GameOverTex = 0; // field_0x114.SetNull()

    Game* game = Game::GetInstance();
    if (game && game->pGameOverScreen == this) {
        game->pGameOverScreen = nullptr;
        // ASM-verified: 2026-05-02 binary @ 0x00140d98 -- clear 5 cached slots
        if (FruitSaveData* sd = game->pSaveData) {
            int* base = reinterpret_cast<int*>(sd);
            base[0x11C / 4] = -1;
            base[0x120 / 4] = 0;
            base[0x124 / 4] = -1;
            base[0x128 / 4] = -1;
            base[0x12C / 4] = -1;
        }
    }

    // Remove and free 4 aux HUDControls.
    // ASM-verified: 2026-05-02 binary @ 0x00140e14..0x00140e58 -- order: +0x9C, +0xBC, +0xC0, +0xA8
    if (game && game->hud) {
        HUDControl* slots[4] = {
            m_pSlot9c,
            (HUDControl*)m_pFruitFact,
            (HUDControl*)m_pSlotC0,
            m_pSlotA8
        };
        for (int i = 0; i < 4; ++i) {
            if (slots[i]) game->hud->RemoveControl(slots[i]);
        }
        for (int i = 0; i < 4; ++i) {
            if (slots[i]) {
                slots[i]->m_bNoDestructor = 0;
                delete slots[i];
            }
        }
    }
    m_pFruitFact = nullptr;
    m_pSlotC0    = nullptr;
    m_pSlot9c    = nullptr;
    m_pSlotA8    = nullptr;

    BonusManager::GetInstance()->ClearBestBonuses();
}

// ---------------------------------------------------------------------------
// SetTerminate (0x00140604)
// ---------------------------------------------------------------------------

// Binary @ 0x00140604
void GameOverScreen::SetTerminate() {
    // Binary: *(uint8_t*)(game + 0x33) = 1; CancelHUDProgressionTimer (no-op stub)
    m_bPendingRemoval = 1;
}

// ---------------------------------------------------------------------------
// SetStateWait (0x00140688)
// ---------------------------------------------------------------------------

// Binary @ 0x00140688
void GameOverScreen::SetStateWait() {
    // Binary: checks if leaderboard sign-in dialog needed; if not, state = 6.
    // Port: always go to state 6 (online services defunct).
    m_State = 6;
}

// ---------------------------------------------------------------------------
// ProgressionTimer no-op stubs (empty in binary)
// ---------------------------------------------------------------------------

// Defunct: ProgressionTimer -- empty in binary @ 0x001405fc
void GameOverScreen::StartProgressionTimer() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140600
void GameOverScreen::CancelHUDProgressionTimer() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140614
void GameOverScreen::OnProgressionTimerUp() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140618
void GameOverScreen::HandleProgressionTimerExpiration() {}

// ---------------------------------------------------------------------------
// Social share callbacks
// ---------------------------------------------------------------------------

// Defunct: Facebook share -- no-op stub; binary @ 0x0014083c (NetworkManager::PublishText)
void GameOverScreen::FacebookCallback() {}

// Defunct: Twitter share -- empty in binary @ 0x001405f8
void GameOverScreen::TwitterCallback() {}

// Binary @ 0x001405e8 -- PostCallback(result): m_bPostInProgress=0; m_bPostOk=(result==0)
void GameOverScreen::PostCallback(int result) {
    m_PostInProgress = false;
    m_PostOk = (result == 0);
}

// ---------------------------------------------------------------------------
// LeaderboardsCallback (Binary @ 0x001405a0)
// ---------------------------------------------------------------------------

// Binary @ 0x001405a0 -- LeaderboardsCallback: state-0/6 + alpha>0.999 -> m_State=10
//                       (launches NetworkManager dashboard, defunct)
void GameOverScreen::LeaderboardsCallback() {
    if (m_State == 0 || m_State == 6) {
        Game* game = Game::GetInstance();
        if (game && game->m_TransitionTimer > 0.999f) {
            m_Timer = 0.0f;
            m_State = 10;
        }
    }
}

// ---------------------------------------------------------------------------
// DeletedControl (Binary @ 0x00140558)
// ---------------------------------------------------------------------------

// Binary @ 0x00140558 -- wired as remove-callback on m_pBonusScreen/m_pSlot9c/m_pNoticeCtrl.
// On removal, clears the slot and (for bonusScreen+noticeCtrl) forces state=6.
void GameOverScreen::DeletedControl(HUDControl* ctrl) {
    if (ctrl == (HUDControl*)m_pBonusScreen) { m_pBonusScreen = nullptr; m_State = 6; }
    if (ctrl == m_pSlot9c)                   { m_pSlot9c = nullptr; }
    if (ctrl == m_pNoticeCtrl)               { m_pNoticeCtrl = nullptr; m_State = 6; }
}

// ---------------------------------------------------------------------------
// FindMostOfFruit (0x00141a18)
// ---------------------------------------------------------------------------

// Binary @ 0x00141a18
void GameOverScreen::FindMostOfFruit() {
    Game* game = Game::GetInstance();
    FruitSaveData* save = game ? game->pSaveData : nullptr;
    if (!save) return;

    int count = FruitInfo_GetCount();
    if (count <= 0) return;

    uint8_t gameMode = game->gameMode;

    // Step 1: build candidate index list, filtering power-fruits in Arcade mode
    int candidates[FRUIT_INFO_MAX];
    int numCandidates = 0;
    for (int i = 0; i < count && i < FRUIT_INFO_MAX; ++i) {
        const FruitInfo* fi = FruitInfo_Get(i);
        if (!fi) continue;
        // In Arcade (gameMode==2), skip fruits with power-ups (special fruits)
        if (gameMode == 2 && fi->m_bSpecial) continue;
        candidates[numCandidates++] = i;
    }

    if (numCandidates == 0) return;

    // Step 2: shuffle candidates (random fruit order eliminates ties bias)
    for (int i = numCandidates - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int tmp = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = tmp;
    }

    // Step 3+4: fetch GetTotal for each candidate, track max
    int bestCount = 0;
    int bestIdx   = -1;
    for (int k = 0; k < numCandidates; ++k) {
        int i = candidates[k];
        const FruitInfo* fi = FruitInfo_Get(i);
        if (!fi) continue;
        int c = save->GetTotal(fi->m_TotalStatHash);
        if (c > bestCount) {
            bestCount = c;
            bestIdx   = i;
        }
    }

    // Step 5: write result
    if (bestCount > 0) {
        field_0x118      = bestIdx;   // most-eaten fruit type index
        m_MostFruitCount = bestCount;
    }
}

// ---------------------------------------------------------------------------
// CreateRetryButton (0x00141188)
// ---------------------------------------------------------------------------

// Binary @ 0x00141188
void GameOverScreen::CreateRetryButton() {
    if (m_pRetryBtn != nullptr) return;

    Game* game = Game::GetInstance();
    if (!game || !game->hud) return;

    // ASM-spec for binary @ 0x00141188 (re-analyst):
    //   pos               = (-80, -96, 0)              [DAT_001412c4..cc]
    //   tex               = g_RetryTexSP @ GOT+0x7310  [retry.tex, loaded in LoadContent]
    //   clickDelegate     = &RetryCallback
    //   fruitType         = 0 (literal apple in the call site, NOT a DAT lookup)
    //   globalCenterVec   = HUD::g_GlobalCenterVec @ 0x001f4328 (singleton)
    //   deletedDelegate   = HUD::g_DeleteControlDelegate (HUD-wide remove cb)
    // Port maps:
    //   - texture: TextureManager::LoadLocalisedTexture("retry.tex") --
    //     port-side single-shot since LoadContent is a no-op stub.
    //   - globalCenterVec / deletedDelegate: route via MenuButton::Init's
    //     hitBounds / deletedCb args (port's Init takes 5 args matching
    //     the binary's ctor minus the texture, which is set via the
    //     m_Texture SmartPtr field).
    //   - HUD-wide deleted delegate: TODO -- routes to
    //     HUD::DeleteControl(removed); placeholder uses self-bound
    //     DeletedControl (slightly different semantic, but functional).
    Vec3 btnPos(-80.0f, -96.0f, 0.0f);
    Vec3 globalCenter(0.0f, 0.0f, 0.0f);  // HUD::g_GlobalCenterVec; HUD::Init sets to (0,0,0)
    Mortar::SmartPtr<Mortar::Texture> tex =
        TextureManager::LoadLocalisedTexture("retry.tex");

    m_pRetryBtn = new MenuButton();
    m_pRetryBtn->m_Texture    = tex;
    m_pRetryBtn->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
    m_pRetryBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnRetryClicked),
        /*fruitType=*/0,
        globalCenter,
        // TODO: 0x00141030 — bind HUD::g_DeleteControlDelegate so the
        //   button's removal triggers HUD-side cleanup. Port currently
        //   leaves the deletedCb empty (default-constructed Delegate0).
        Mortar::Delegate0<void>()
    );

    game->hud->AddControl(m_pRetryBtn, false);
}

// Binary @ 0x0014105c
void GameOverScreen::RetryCallback() {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (m_State != 0 && m_State != 6 && m_State != 14 && m_State != 10) return;
    if (game->m_TransitionTimer <= 0.989945f) return;
    CancelHUDProgressionTimer();
    // TODO: 0x0014105c -- session-stat reset block at GOT+DAT_00141164 (out of scope)
    // TODO: FruitSaveData::ClearCombo (port may lack the API)
    if (game->pSaveData) {
        // game->pSaveData->ClearCombo();   // gate if missing
    }
    m_State = 7;
    // TODO: GameSound::SFXPlay menu_retry sound
}

void GameOverScreen::OnRetryClicked() {
    RetryCallback();
}

// ---------------------------------------------------------------------------
// CreateQuitButton (0x001412e4)
// ---------------------------------------------------------------------------

// Binary @ 0x001412e4
void GameOverScreen::CreateQuitButton() {
    Game* game = Game::GetInstance();
    if (!game || !game->hud) return;

    // ASM-spec for binary @ 0x001412e4 (re-analyst):
    //   pos               = (80, -96, 0)               [DAT_00141428..30]
    //   tex               = g_QuitTexSP @ GOT+0x73fc   [quit.tex, loaded in LoadContent]
    //   clickDelegate     = &QuitCallback
    //   fruitType         = g_FruitInfo[0].m_FruitType (RUNTIME — first row of FRUIT_INFO)
    //   globalCenterVec   = HUD::g_GlobalCenterVec
    //   deletedDelegate   = HUD::g_DeleteControlDelegate
    // Plus 3 quit-only post-init steps (binary 0x00141420..0x00141438):
    //   - copy retry->[+0x124..+0x12C] (TutorialControl text slots) to quit
    //   - set quit->[+0xE2] = 1 (singular/right-flag)
    //   - call TutorialControl::ResetTutePos(hud->pTutorialCtrl, m_pQuitBtn)
    // The 3 post-init steps require fields not yet ported; deferred TODO.
    Vec3 btnPos(80.0f, -96.0f, 0.0f);
    Vec3 globalCenter(0.0f, 0.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex =
        TextureManager::LoadLocalisedTexture("quit.tex");

    m_pQuitBtn = new MenuButton();
    m_pQuitBtn->m_Texture    = tex;
    m_pQuitBtn->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
    m_pQuitBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnQuitClicked),
        /*fruitType=*/0,  // TODO: 0x00141440 -- read g_FruitInfo[0].m_FruitType
        globalCenter,
        Mortar::Delegate0<void>()  // TODO: 0x00141030 -- bind HUD::g_DeleteControlDelegate
    );

    // TODO: 0x00141420 — quit-only post-init:
    //   memcpy(quit+0x124..+0x12C, retry+0x124..+0x12C);  // TutorialControl text slots
    //   quit->[+0xE2] = 1;                                 // singular/right-flag byte
    //   TutorialControl::ResetTutePos(game->pTutorialCtrl, m_pQuitBtn);

    game->hud->AddControl(m_pQuitBtn, false);
}

// Binary @ 0x00140620
void GameOverScreen::QuitCallback() {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (m_State != 0 && m_State != 6 && m_State != 14 && m_State != 10) return;
    CancelHUDProgressionTimer();
    // TODO: FruitSaveData::ClearCombo
    m_State = 9;
    // TODO: HitMenuBomb(Vec3(-0.0625f, 163.0f, -96.0f))
}

void GameOverScreen::OnQuitClicked() {
    QuitCallback();
}

// ---------------------------------------------------------------------------
// Update (vtable slot 10, 0x00141b34) — main state machine (529 lines)
// ---------------------------------------------------------------------------

// Binary @ 0x00141b34
//
// Re-analyst 2026-05-08 corrections to legacy TODOs:
//   - "fast-skip path m_TransitionTimer = 0.0f" — no such write exists in
//     Update; deleted (was a port-time speculation). The fast-path lives
//     in Initialise (now handled, see 0.99899/0.99982 constants there).
//   - pSaveData[+0x12C] is a uint8_t = (uint8_t)SetCurrentModeHighscore(score)
//     — port wrote -1 (int) on a different code path; corrected below.
//   - state 6 ComboStarAchievement gate is
//       (m_pBonusScreen != null && gameMode == 3 &&
//        (int8_t)bonus[+0xE0] in [0, 25)).
//     Combo length is bonus[+0xD0] (uint64_t); star type is bonus[+0xE0]
//     (int8_t); hash via StringHash(GetComboName(starType)).
void GameOverScreen::Update(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Default m_LayerFlags for the frame (binary @ 0x00141b40, before
    // the state switch). Case 0 overrides to 1 for the entry animation.
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Advance animation counter (millisecond-resolution circular)
    // Binary: field_0xac = (float)(int)(field_0xac) + dt*1000.0, then mod 1000
    m_AnimCounter = (int)(((float)m_AnimCounter + dt * 1000.0f));
    if (m_AnimCounter >= 1000) m_AnimCounter -= 1000;

    switch (m_State) {

    // -----------------------------------------------------------------------
    // State 0: entry animation — sin-eased scale-in over 1.9s
    // -----------------------------------------------------------------------
    case 0: {
        // First frame: force game.processing=1 based on game mode + entity count
        if (m_bScoreSubmitted == 0) {
            uint8_t gm = game->gameMode;
            Mortar::ActorManager* am = game->actorManager;
            if ((uint8_t)(gm - 2) < 2) { // Arcade (2) or Zen (3)
                if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0)
                    game->m_bSlowMotion = 1; // game[+0x35] = m_bProcessing
            } else {
                game->m_bSlowMotion = 1;
            }
        }

        m_LayerFlags = Mortar::HUD_LAYER_DEFAULT; // single-layer during entry

        m_Timer += dt;
        const float ENTRY_DURATION = 1.9f;   // DAT_00141dac
        const float SIN_FULL       = 20000.0f; // DAT_00141da8

        if (m_Timer < ENTRY_DURATION) {
            float t = (m_Timer / ENTRY_DURATION) * SIN_FULL;
            uint16_t idx;
            if (t > SIN_FULL) idx = 0x4E34;
            else              idx = (uint16_t)(int)t;
            float curr = SinIdx(idx);
            float full = SinIdx(0x4E34);
            float scaleF = (full != 0.0f) ? (curr / full) : 0.0f;
            size.x = m_TitleSizeX * scaleF * 2.0f;
            size.y = m_TitleSizeY * scaleF * 2.0f;
            size.z = m_TitleSizeZ * scaleF * 2.0f;
        } else {
            size.x = m_TitleSizeX * 2.0f;
            size.y = m_TitleSizeY * 2.0f;
            size.z = m_TitleSizeZ * 2.0f;
        }

        if (m_Timer > ENTRY_DURATION) {
            if (game->gameMode == 2) { // Arcade
                m_State = 1;
                m_Timer = -0.333f; // DAT_00141db0
            } else {
                SetStateWait(); // goes to state 6 or pops sign-in dialog
            }
        }
        pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
        break;
    }

    // -----------------------------------------------------------------------
    // State 1: bonus phase (Arcade only) — BonusScreen creation + slide
    // -----------------------------------------------------------------------
    case 1: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0) {
            if (!m_pBonusScreen) {
                FindMostOfFruit();
                m_pBonusScreen = new BonusScreen();
                m_pBonusScreen->pos = Vec3(0.0f, -20.0f, 0.0f);
                // TODO: install m_RemoveCallback delegate to GameOverScreen::OnBonusRemoved
                if (game->hud) game->hud->AddControl(m_pBonusScreen, false);
                BonusManager::GetInstance()->SetUpBonusScreen(m_pBonusScreen);
            } else {
                // GO screen tracks bonus position for layout alignment.
                // ASM-spec for binary @ 0x00141bd0..0x00141bf0 (re-analyst):
                //   ny = bonus.pos.y + bonus[+0xC0] + 135.0f   (DAT_00141DC8)
                //   then m_OffsetPosY = max(m_OffsetPosY, ny)
                //   plus a size_factor = pos.y / -224.0f + 1.0f rescale
                //   (DAT_00141DCC) on the row above.
                // bonus[+0xC0] is a per-phase rolling Y delta, NOT m_PhaseTimer
                // (+0xB8). Port reads m_PhaseTimer as a stand-in until the
                // real field is RE'd into BonusScreen; constant updated to 135.0f.
                // TODO: 0x00141bd0 — wire bonus[+0xC0] (per-phase Y delta)
                //   and the -224.0 size rescale.
                float ny = m_pBonusScreen->pos.y + m_pBonusScreen->m_PhaseTimer + 135.0f;
                m_OffsetPosY = std::max(m_OffsetPosY, ny);

                // Advance bonus phase timer from our own timer.
                m_Timer += dt;
                m_pBonusScreen->m_PhaseTimer = m_Timer;

                // When BonusScreen sets m_bPendingRemoval, transition to main display.
                if (m_pBonusScreen->m_bPendingRemoval) {
                    SetStateWait();
                }
            }
            // Binary @ 0x00141ce8: per-frame in this branch, force
            // game.m_bSlowMotion = 1 (suppress entity processing during
            // the bonus phase).
            game->m_bSlowMotion = 1;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 6: main display + score submission
    // -----------------------------------------------------------------------
    case 6: {
        // 1) Create FruitFactControl on first entry
        if (m_pFruitFact == nullptr) {
            m_pFruitFact = new FruitFactControl();
            // Position: DAT_00142120=183.0, y=12.0, z=DAT_00142118=0.0
            m_pFruitFact->pos.x = 183.0f + m_OffsetPosX;
            m_pFruitFact->pos.y = 12.0f  + m_OffsetPosY;
            m_pFruitFact->pos.z = 0.0f;
            m_pFruitFact->m_PomCount  = (uint8_t)m_PomCount;
            m_pFruitFact->m_StarType  = (uint8_t)m_StarCount;
            if (game->hud) game->hud->AddControl(m_pFruitFact, false);
            m_pFruitFact->Init();
        }

        // 2) Pop-in animation: game.alpha (m_TransitionTimer) ramps toward 1.0
        float& alpha = game->m_TransitionTimer; // game[+0xC] = alpha
        const float ALPHA_THRESH = 0.999f; // DAT_00142124
        if (alpha < ALPHA_THRESH) {
            m_ProgressCounter = 0;
            alpha += (1.0f - alpha) * 0.125f;
            if (alpha < 0.75f) game->m_bSlowMotion = 1; // suppress entity processing
            if (alpha >= ALPHA_THRESH) alpha = 1.0f;
            m_FruitFactAlpha = alpha;
        } else {
            if (m_FruitFactAlpha < 1.0f)
                m_FruitFactAlpha += (1.0f - m_FruitFactAlpha) * 0.125f;
            if (m_ProgressCounter < 11) m_ProgressCounter++;
        }

        // 3) On frame 10 (single-shot via m_bScoreSubmitted): commit scores
        if (m_ProgressCounter == 10) {
            m_ProgressCounter = 11; // latch
            if (m_bScoreSubmitted == 0) {
                int score = GetCurrentScore(0);
                m_bScoreSubmitted = 1;

                // Score submission tail (§8): most calls are no-ops until subsystems are ported
                FruitSaveData* save = game->pSaveData;
                if (save) {
                    // Binary @ 0x00142092: pSaveData[+0x12D] = 0 hoists to
                    // the FIRST write in the m_bScoreSubmitted == 0 block,
                    // before any AddToTotal / Achievement calls.
                    save->secondaryFlag = 0;

                    // Lifetime totals
                    save->AddToTotal("FruitsCollected", 1);
                    save->AddToTotal("TotalScore", score);
                    save->UnlockTotals();  // no-op stub until AchievementManager is ported

                    AchievementManager::GetInstance()->UnlockScoreAchievement(score);
                    AchievementManager::GetInstance()->UnlockTotalFruitAchievement(0);
                    AchievementManager::GetInstance()->UnlockEndScoreAchievement(score, 0);

                    // Note: LeaderboardManager::RefreshLeaderboard -- defunct (online-services-audit).
                    // Note: FNHighscoreList::AddPlayerScore -- defunct (online-services-audit).

                    // Binary @ 0x00141fbc..0x00142010 (re-analyst):
                    //   gate = (m_pBonusScreen != null && gameMode == 3 &&
                    //           (int8_t)bonus[+0xE0] in [0, 25))
                    //   args = (bonus[+0xD0] /*uint64_t combo*/,
                    //           StringHash(GetComboName(starType)))
                    // bonus[+0xE0] / bonus[+0xD0] are unported BonusScreen
                    // fields; until they're added the gate fails and the
                    // achievement is still credited via the placeholder
                    // call below for compatibility.
                    // TODO: 0x00141fbc — wire bonus[+0xE0]/[+0xD0] + GetComboName.
                    AchievementManager::GetInstance()->UnlockComboStarAchievement(0, 0);

                    int hi = GetCurrentModeHighscore();
                    if (hi / 2 < score) {
                        // Binary @ 0x00142228:
                        //   pSaveData[+0x12C] = (uint8_t)SetCurrentModeHighscore(score);
                        // The write captures the bool return of the highscore update.
                        save->SetCurrentModeHighscore(score);
                        save->newBestThisGame = 1;  // bool result is true when hi/2 < score
                    }

                    // Note: NetworkManager::SetLeaderboardScore -- defunct (online-services-audit).

                    // Arcade-only post-game achievement
                    if (game->gameMode == 2) {
                        // Binary @ 0x0014230c -- calls BonusManager::UnlockPostGameAchievements.
                        // Port previously called AchievementManager::UnlockPostGameAchievements
                        // in error; corrected to match binary.
                        BonusManager::GetInstance()->UnlockPostGameAchievements();
                    }

                    save->FinishedGame();
                    save->ClearTotals();
                    FruitNinja_SaveCurrentData(false);
                }

                // Load localised "Game Over" text texture
                Mortar::SmartPtr<Mortar::Texture> govTex =
                    TextureManager::LoadLocalisedTexture("gameover.tex");
                m_GameOverTex = govTex ? govTex->m_TexId : 0;
            }

            game->m_TransitionTimer = 1.0f;

            // Spawn retry/quit buttons only once and only when allowed
            if (IsAllowedToExit()) {
                CreateRetryButton();
                if (m_pQuitBtn == nullptr) CreateQuitButton();
            }
        }

        // 6) Vertical "settle" — slide content based on alpha
        if (pos.y < 0.0f) {
            float a = game->m_TransitionTimer;
            // size = TitleSize * lerp(2.0, 1.0, alpha)
            float sf = 2.0f + (1.0f - 2.0f) * a; // lerp(2,1,a)
            size.x = m_TitleSizeX * sf;
            size.y = m_TitleSizeY * sf;
            // pos.y = lerp(-85.0, 0.0, alpha) per DAT_00142618/0014261c
            pos.x = 0.0f;
            pos.y = -85.0f + (-85.0f * -1.0f) * a; // = -85*(1-a) → 0 as a→1
            pos.z = 0.0f;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 7: retry — guard entities & reset wave & flag pause
    // -----------------------------------------------------------------------
    case 7: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) != 0 && m_pSlot9c == nullptr) {
            // Entities still on screen — snap alpha and stay in state 6
            game->m_TransitionTimer = 1.0f;
            m_State = 6;
            break;
        }
        // Binary: game[+0x28] = game[+0x20] (fruitConsumed = fruitTotal)
        // Port: no direct equivalent yet; TODO: wire when wave fields confirmed
        WaveManager::GetInstance()->Reset(false);
        game->pauseFlag = 1; // will be cleared in state 8
        m_State = 8;
        break;
    }

    // -----------------------------------------------------------------------
    // State 8: camera fade-out for retry
    // -----------------------------------------------------------------------
    case 8: {
        float& alpha = game->m_TransitionTimer;
        alpha *= 0.75f;
        m_FruitFactAlpha = alpha;

        const float ALPHA_LOW = 0.001f; // DAT_00142114
        if (alpha < ALPHA_LOW) {
            WaveManager::GetInstance()->Reset(false);
            alpha = 0.0f;
            game->pauseFlag = 0;
            m_FruitFactAlpha = 0.0f;
            WaveManager::NewGame();
            SetTerminate();
        }

        // Slide up off-screen as alpha decays
        if (pos.y < 0.0f) {
            // pos.y = 0.0 + (1-m_FruitFactAlpha) * 224.0
            pos.x = 0.0f;
            pos.y = (1.0f - m_FruitFactAlpha) * 224.0f; // DAT_0014211c = 224.0
            pos.z = 0.0f;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 9: quit path — wait for entities, then QuitToMenu
    // -----------------------------------------------------------------------
    case 9: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) != 0) break; // wait
        DoQuitToMenu();
        m_State = 11;
        break;
    }

    // -----------------------------------------------------------------------
    // State 10: online leaderboard launch (defunct) — no-op, back to state 6
    // -----------------------------------------------------------------------
    case 10: {
        // Note: NetworkManager::LaunchDashboard() -- defunct (online-services-audit).
        m_ProgressCounter = 0;
        m_pQuitBtn        = nullptr;
        m_pRetryBtn       = nullptr;
        field_0xa0        = 0;
        m_State           = 6;
        break;
    }

    // -----------------------------------------------------------------------
    // State 11: final fade-out
    // -----------------------------------------------------------------------
    case 11: {
        // Binary: if (game.alpha < 0.0f) SetTerminate()
        if (game->m_TransitionTimer < 0.0f) SetTerminate();
        break;
    }

    // -----------------------------------------------------------------------
    // State 14: quick-restart hot path (no observable caller in stock build)
    // -----------------------------------------------------------------------
    case 14: {
        m_State = 6;
        m_Timer = 2.0f;
        break;
    }

    default:
        // Unhandled states (2..5, 12, 13, 15+): do nothing (matches binary)
        break;
    }

    // -----------------------------------------------------------------------
    // Common layout block (runs every frame after switch).
    //
    // ASM-spec for binary @ 0x001424dc..0x00142516 (re-analyst):
    //   DAT_00142624 =   75.0f   bonus.x base
    //   DAT_00142628 =  480.0f   bonus.x alpha-coeff (multiplied by 1-alpha)
    //   DAT_0014262c = -102.0f   bonus.y
    //   DAT_00142630 = -386.0f   classic OffsetX alpha-coeff / bonus FruitFact x-offset
    //   DAT_00142634 =  368.0f   classic OffsetX base
    //   DAT_00142638 =   55.0f   classic OffsetY
    //   DAT_0014263c =  183.0f   classic FruitFact x-offset
    //   DAT_00142640 = -5000.0f  unused / sentinel
    //   DAT_00142644 =   65.0f   retryBtn y base
    //   DAT_00142648 =  300.0f   retryBtn y alpha-coeff
    //   DAT_0014264c =  190.0f   retryBtn x
    //   DAT_00142650 =  -50.0f   shake.x scale
    //   DAT_00142654 =  120.0f   shake magnitude
    //   DAT_00142658 = -125.0f   quitBtn y base
    //   DAT_00142670 -> &g_JitterVec3 (per-frame jitter source)
    //
    // The previous port-side numeric guesses (-204 / -193 / 75 / 130 / -50 /
    // 240 / -56) didn't come from these DATs and produced noticeably wrong
    // layout vs the binary. Constants below are now binary-faithful.
    // -----------------------------------------------------------------------
    // ASM-verified: 2026-05-09 binary @ 0x00141b34..0x00142613 (re-analyst).
    // DAT constants:
    //   0x00142620 = -20.0  (BonusScreen.y / Arcade-Zen FruitFact base.y)
    //   0x00142624 =  75.0  (BonusScreen.x base)
    //   0x00142628 = 480.0  (BonusScreen.x slide coeff)
    //   0x0014262c = -102.0 (Arcade-Zen FruitFact x-offset; NOT bonus.y)
    //   0x00142630 = -386.0 (Classic FruitFact slide coeff)
    //   0x00142634 = 368.0  (Classic m_OffsetPosX base)
    //   0x00142638 =  55.0  (Classic m_OffsetPosY)
    //   0x0014263c = 183.0  (Classic FruitFact +x offset)
    //   0x0014264c = 190.0  (Retry/Quit X)
    //   0x00142650 = -50.0  (Retry Y base)
    //   0x00142654 = 120.0  (jitter magnitude)
    //   0x00142658 = -125.0 (Quit Y base)
    //   0x00142670 = &g_JitterVec3 (shake source — port-side stub returns 0)
    uint8_t gm = game->gameMode;
    if ((uint8_t)(gm - 2) < 2 && m_pBonusScreen != nullptr) {
        // Arcade or Zen with BonusScreen
        const float bx = 75.0f + (1.0f - m_FruitFactAlpha) * 480.0f;
        const float by = -20.0f;  // DAT_00142620
        m_pBonusScreen->pos.x = bx;
        m_pBonusScreen->pos.y = by;
        m_pBonusScreen->pos.z = 0.0f;
        if (m_pFruitFact) {
            // FruitFact bonus offset: bx + DAT_0014262c (-102), by + 4, 0
            m_pFruitFact->pos.x = bx + (-102.0f);
            m_pFruitFact->pos.y = by + 4.0f;
            m_pFruitFact->pos.z = 0.0f;
        }
    } else {
        // Classic / single-player layout
        m_OffsetPosX = 368.0f + (-386.0f) * m_FruitFactAlpha;
        m_OffsetPosY = 55.0f;
        m_OffsetPosZ = 0.0f;
        if (m_pFruitFact) {
            m_pFruitFact->pos.x = m_OffsetPosX + 183.0f;
            m_pFruitFact->pos.y = m_OffsetPosY + 12.0f;
            m_pFruitFact->pos.z = 0.0f;
        }
        // Retry button (m_pSlot9c, field12_0x9c).
        // Binary: pos = (190, -50, 0) + (1-α) * 120 * g_JitterVec3.
        // Jitter source (DAT_00142670) not yet wired -- port treats it as 0.
        if (m_pSlot9c) {
            m_pSlot9c->pos.x = 190.0f;
            m_pSlot9c->pos.y = -50.0f;
            m_pSlot9c->pos.z = 0.0f;
        }
        // Quit button (m_pQuitBtn, field15_0xa8).
        // Binary: pos = (190, -125, 0) + (1-α) * 120 * g_JitterVec3.
        if (m_pQuitBtn) {
            m_pQuitBtn->pos.x = 190.0f;
            m_pQuitBtn->pos.y = -125.0f;
            m_pQuitBtn->pos.z = 0.0f;
        }
        // TODO: 0x00142670 — wire g_JitterVec3 read for per-frame shake on
        //   retry/quit buttons; offset = (1-α) * 120 * jitter.
    }
}

// ---------------------------------------------------------------------------
// PreDrawOrder (vtable slot 8, 0x0014171c)
// ---------------------------------------------------------------------------

// Binary @ 0x0014171c
void GameOverScreen::PreDrawOrder(const Vec3& hudScale, int layerMask) {
    if (layerMask & 0x80) {
        // Layer 0x80: draw "days remaining" text + extra texture
        // TODO: wire Font::DrawString once font rendering is confirmed for this screen
        // Binary reads m_CoinsEarnedLabel and draws via pFont at a fixed screen position.
        // For now this is a no-op placeholder.
        (void)hudScale;
    }

    if (layerMask & 1) {
        // Layer 1: expression + pattern overlays, then HUDControl3d::Draw
        // TODO: expression/pattern overlay quads (m_ExpressionIdx, m_BgPatternIdx)
        // Gated by m_bIsClassic per binary.
        HUDControl3d::Draw(hudScale, layerMask);
    }
}

// ---------------------------------------------------------------------------
// DrawOrder (vtable slot 9, 0x00141448)
// ---------------------------------------------------------------------------

// Binary @ 0x00141448
void GameOverScreen::DrawOrder(const Vec3& hudScale, int layerMask) {
    if (!(layerMask & 1)) return; // cached check

    // Rotating starburst halo: 48-vertex triangle list, time-pulsed colour.
    // Binary: 48 verts, rotation driven by m_AnimCounter, colour pulses RGBA.
    // TODO: implement GL vertex buffer / immediate-mode equivalent when
    //       render pipeline is confirmed for this screen.
    // For now this is a no-op placeholder.
    (void)hudScale;
}
