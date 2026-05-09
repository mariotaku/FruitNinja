// Analysed: 2026-05-02T00:00
//
// PauseScreen — Tier-1 implementation.
// Binary: PauseScreen::PauseScreen @ 0x00155460 (ctor), Update @ 0x00154468.
// See docs/engine/pausescreen-deep-re.md for full spec.
//
// Tier-1 scope: SP-only path, states 0/2/3/4/5/6, three P1 buttons.
//
// Tier-2 items pending split-screen MP and BombFlashFull port:
// - L154/L168 P2 buttons (binary +0x9c/+0xa4/+0xb0): P2-side resume/retry/quit slots.
// - State 1 bomb-flash (binary @ 0x00168f24 BombFlashFull): full-screen white-flash on
//   menu-bomb hit; gates state 6 dismissal.
// - HitMenuBomb in state 6 (binary @ 0x0016b234): when game state != 1, plays "MenuBomb"
//   SFX, sets g_GameData[0xf8]=1, writes hit position to g_GameData[0xcc/d0/d4].
// All three depend on subsystems not yet ported. Specs are RE-complete.

#include "screens/PauseScreen.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "game/PowerUpManager.h"
#include "game/BombHit.h"
#include "game/GameTaskState.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include "asset/TextureManager.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "engine/render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "math/Colour.h"
#include "engine/audio/GameSound.h"
#include "Game.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

// -------------------------------------------------------------------------
// Constants from binary (see docs section 4 DAT table)
// -------------------------------------------------------------------------
static const float FADE_DECAY        = 0.75f;     // state 0 / 4 / 5 / 6 alpha decay
static const float FADE_CLAMP        = 0.01f;     // DAT_00154fb4
static const float FADE_IN_RATE      = 0.25f;     // state 2 ease-in: (1-alpha)*0.25
static const float ACTIVE_THRESHOLD  = 0.999f;    // DAT_00154fbc  state 2->3
static const float EXIT_THRESHOLD   = 0.001f;    // DAT_00154fc0  states 4/5/6
// ASM-verified: 2026-05-08 binary @ DAT_00154fc4/d0/c8 (re-analyst).
// Previous port values (-2.0f / 240.0f) were wrong by orders of magnitude.
// DAT_00154fd0 = 160.0f is the SCREEN-TOP-EDGE constant, reused as the base
// for both this->pos.y (title slide-in) and quit.pos.y. DAT_00154fc8 =
// 240.0f is the SCREEN-RIGHT-EDGE constant, used as quit.pos.x base.
static const float TITLE_SLIDE_MUL  = -130.0f;   // DAT_00154fc4 (was -2.0f, WRONG)
static const float TITLE_SLIDE_BASE = 160.0f;    // DAT_00154fd0 (was 240.0f, WRONG)
static const float SCREEN_RIGHT_X   = 240.0f;    // DAT_00154fc8 (quit.pos.x base)

// flash.tex overlay: alpha = clamp(m_Alpha * 1000, 0, 128); scale = m_Alpha * 10000
// Binary: PreDraw @ 0x0016bda0
static const float FLASH_ALPHA_MUL  = 1000.0f;
static const float FLASH_ALPHA_MAX  = 128.0f;
static const float FLASH_SCALE_MUL  = 10000.0f;

// Lazy-loaded flash.tex (shared with DrawBombHit)
static Mortar::SmartPtr<Mortar::Texture> s_FlashTex;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static inline Mortar::SmartPtr<Mortar::Texture> LoadTex(const char* name,
                                                         int* outW = nullptr,
                                                         int* outH = nullptr) {
    Mortar::SmartPtr<Mortar::Texture> t = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (t.IsValid()) {
        if (outW) *outW = t->m_Width;
        if (outH) *outH = t->m_Height;
    } else {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
    }
    return t;
}

// -------------------------------------------------------------------------
// PauseGame / UnpauseGame (binary @ 0x00168f80 / 0x00168fb0)
// -------------------------------------------------------------------------

// Binary @ 0x00168f80 PauseGame():
//   gameObj+0x02 (byte) = 1   -- timer-running flag on gameObj
//   TaskState+0x0C (byte) = 0 -- isPaused = 0 (transition entering pause)
//   TaskState+0x08 (float) = 0.25f -- pause transition timer
void PauseScreen::PauseGame() {
    Game* game = Game::GetInstance();
    if (!game) return;
    GameTaskState* ts = GetTaskState();
    game->gameActiveFlag = 1;
    ts->isPaused = 0;
    ts->pauseTransitionTimer = 0.25f;
}

// ASM-verified: 2026-05-03 binary @ 0x00168fb0 (re-analyst)
// Binary @ 0x00168fb0 UnpauseGame():
//   TaskState+0x10 (float) = 0.4f  -- post-unpause grace window
//   TaskState+0x0C (byte) = 1      -- isPaused = 1 (resumed)
void PauseScreen::UnpauseGame() {
    GameTaskState* ts = GetTaskState();
    ts->pauseBombHitTimer = 0.4f;   // DAT_00168fcc
    ts->isPaused = 1;
}

// -------------------------------------------------------------------------
// IsEnabled (binary @ 0x00153e4c)
// -------------------------------------------------------------------------

// ASM-verified: 2026-05-09 binary @ 0x00153e4c (re-analyst)
// Returns TRUE when pause overlay is available -- transition timer at rest
// (|t| < 0.001), no bomb-hit pause, and pauseFlag == 0. Earlier port had
// the comparison inverted -- the binary uses `bpl` after vcmpe which means
// "branch if |val| >= epsilon -> return false". Inversion was masked while
// m_TransitionTimer sat permanently at -1.0f in the port; once MainScreen
// started mirroring the camera transition (settles to 0 during gameplay),
// the inversion hid the pause button entirely.
bool PauseScreen::IsEnabled() {
    Game* g = Game::GetInstance();
    if (!g) return false;
    if (fabsf(g->m_TransitionTimer) >= 0.001f) return false;  // [+0xc] epsilon
    if (g->bombHitTimer > 0.0f)                return false;  // [+0x10]
    return (g->pauseFlag ^ 1) != 0;                           // [+0x05] XOR 1
}

// -------------------------------------------------------------------------
// QuitToMenu / RetryLevel stubs
// (binary 0x00169e50 / 0x0016a25c -- full flow not yet ported)
// -------------------------------------------------------------------------
static void QuitToMenu() {
    // Binary: WaveManager::ResetGlobalDt(1.0), then MainScreen state change,
    // NetworkManager kick, etc. Tier-1: just thaw the wave timer.
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    Game* game = Game::GetInstance();
    if (game) {
        game->pauseFlag = 1;
    }
}

static void RetryLevel() {
    // Binary: WaveManager::ResetGlobalDt(1.0), then resets per-fruit timers,
    // plays game-start SFX, etc. Tier-1: thaw wave timer + clear flags.
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    Game* game = Game::GetInstance();
    if (game) {
        game->retryFlag = 1;
    }
}

// -------------------------------------------------------------------------
// ctor
// -------------------------------------------------------------------------
PauseScreen::PauseScreen()
    : m_Alpha(0.0f),
      m_TitleSize(0.0f, 0.0f, 0.0f),
      m_ButtonOriginPos(0.0f, 0.0f, 0.0f),
      m_ResumeButton(nullptr),
      m_P2ResumeButton(nullptr),
      m_QuitButton(nullptr),
      m_P2QuitButton(nullptr),
      m_RetryButton(nullptr),
      m_P2RetryButton(nullptr),
      m_ButtonFadeAlpha(1.0f),
      m_LastHitButton(-1),
      m_PressIndex(0),
      m_RevealTimer(0.0f),
      m_State(0),
      m_TitleTexW(0.0f), m_TitleTexH(0.0f),
      m_PauseButtonTexW(0.0f), m_PauseButtonTexH(0.0f),
      m_QuitTitleTexW(0.0f), m_QuitTitleTexH(0.0f),
      m_RetryButtonTexW(0.0f), m_RetryButtonTexH(0.0f)
{
    m_LayerFlags = 8;

    // Load textures -- strings resolved from GOT in ctor (doc section 6 asset table)
    // pause_title.tex goes into inherited m_SecondaryTex (+0x78 in binary / +0x74 in port)
    // Port: m_SecondaryTex is the inherited GLuint; m_Texture (the primary display tex)
    // is set to the title texture so HUDControl3d::Draw renders it.
    {
        int w = 0, h = 0;
        Mortar::SmartPtr<Mortar::Texture> tex = LoadTex("pause_title.tex", &w, &h);
        m_Texture      = tex;     // primary: HUDControl3d::Draw uses this
        m_SecondaryTex = tex;     // also fill inherited secondary slot (binary stores here)
        m_TitleTexW = (float)w;
        m_TitleTexH = (float)h;
    }

    {
        int w = 0, h = 0;
        m_PauseButtonTex = LoadTex("pause_button.tex", &w, &h);
        m_PauseButtonTexW = (float)w;
        m_PauseButtonTexH = (float)h;
    }

    {
        m_PlayButtonTex = LoadTex("play_button.tex");
    }

    {
        int w = 0, h = 0;
        m_QuitTitleTex = LoadTex("quit_title.tex", &w, &h);
        m_QuitTitleTexW = (float)w;
        m_QuitTitleTexH = (float)h;
    }

    {
        int w = 0, h = 0;
        m_RetryButtonTex = LoadTex("retry_button.tex", &w, &h);
        m_RetryButtonTexW = (float)w;
        m_RetryButtonTexH = (float)h;
    }

    // Title size stored in m_TitleSize for slide-in math (doc section 4 #6)
    m_TitleSize = Vec3(m_TitleTexW, m_TitleTexH, 0.0f);

    // Initial pos: centered along Y by texture height (doc section 2 notes)
    // pos = (0, (320 - sizeY) * 0.5, 0)
    pos = Vec3(0.0f, (320.0f - m_TitleTexH) * 0.5f, 0.0f);

    // size for HUDControl3d::Draw quad
    size = Vec3(m_TitleTexW, m_TitleTexH, 0.0f);
}

PauseScreen::~PauseScreen() {}

// -------------------------------------------------------------------------
// vtable[2]: Init -- forwards to Reset (HUDControl::Reset)
// Binary: 0x00153e28 -- 5-instr thunk: (*vtable[4])(this)
// -------------------------------------------------------------------------
void PauseScreen::Init() {
    Reset();
}

// -------------------------------------------------------------------------
// vtable[3]: Release -- nulls 5 SmartPtr<Texture> slots.
// ASM-verified: 2026-05-08T00:00 binary @ 0x0015408C (re-analyst)
//
// Slots nulled (5x SmartPtrNull_Tex calls at 0x00154054):
//   +0x74 m_Texture (HUDControl3d primary)
//   +0xb8 m_PlayButtonTex
//   +0xbc m_QuitTitleTex
//   +0xc0 m_RetryButtonTex
//   +0xc4 m_RetryHighlightTex  (the 5th slot, was mis-named _pad_c4)
//
// Slots NOT nulled by binary:
//   +0x78 m_SecondaryTex (the slot pause_title.tex actually lives in)
//   +0xa8 m_PauseButtonTex (kept live across Release calls)
// -------------------------------------------------------------------------
void PauseScreen::Release() {
    m_Texture.SetNull();
    m_PlayButtonTex.SetNull();
    m_QuitTitleTex.SetNull();
    m_RetryButtonTex.SetNull();
    m_RetryHighlightTex.SetNull();
}

// -------------------------------------------------------------------------
// vtable[4]: Reset -- restores SP-mode tex assignments on resume/retry buttons
// ASM-verified: 2026-05-08T00:00 binary @ 0x00154024 (re-analyst)
//
// Two if-blocks, no other PauseScreen field is touched:
//   if (m_RetryButton) {
//       retry->m_bHighlighted = 1;            // +0x131
//       retry->m_SecondaryTex = m_RetryHighlightTex;   // src is +0xc4, NOT +0xc0
//   }
//   if (m_ResumeButton) resume->m_SecondaryTex = m_PlayButtonTex;
//
// Inverse of SetToMultiplayerState: re-enables RetryButton and restores
// SecondaryTex assignments so SP layout is correct after MP session ends.
// -------------------------------------------------------------------------
void PauseScreen::Reset() {
    if (m_RetryButton) {
        m_RetryButton->m_bHighlighted = 1;
        m_RetryButton->m_SecondaryTex = m_RetryHighlightTex;
    }
    if (m_ResumeButton) {
        m_ResumeButton->m_SecondaryTex = m_PlayButtonTex;
    }
}

// -------------------------------------------------------------------------
// vtable[5]: BeginDraw -- asserts m_LayerFlags = 8
// Binary: 0x00153e44
// -------------------------------------------------------------------------
void PauseScreen::BeginDraw(float dt) {
    (void)dt;
    m_LayerFlags = 8;
}

// -------------------------------------------------------------------------
// vtable[6]: PreDraw -- full-screen black-tinted flash.tex overlay
// Binary: 0x0016bda0
// Only runs when m_LayerFlags == 8 (asserted by BeginDraw each frame).
// alpha = clamp(m_Alpha * 1000.0, 0, 128); tint = (0,0,0,alpha)
// scale = m_Alpha * 10000.0
// -------------------------------------------------------------------------
void PauseScreen::PreDraw(const Vec3& hudScale) {
    (void)hudScale;

    if (m_Alpha <= 0.0f) return;

    if (!s_FlashTex.IsValid()) {
        s_FlashTex = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!s_FlashTex.IsValid()) return;
    }

    float alphaF = m_Alpha * FLASH_ALPHA_MUL;
    if (alphaF < 0.0f) alphaF = 0.0f;
    if (alphaF > FLASH_ALPHA_MAX) alphaF = FLASH_ALPHA_MAX;
    const uint8_t alpha = (uint8_t)(int)alphaF;

    const float scale = m_Alpha * FLASH_SCALE_MUL;
    const Colour tint(0, 0, 0, alpha);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    // Centered at origin (full-screen coverage)
    mat.GlobalTranslate44(Vec3(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    s_FlashTex->Set();
    Game* game = Game::GetInstance();
    if (game) {
        game->renderer.DrawQuad(tint);
    }
    s_FlashTex->UnSet();
}

// -------------------------------------------------------------------------
// vtable[9]: DrawOrder -- gate on m_Alpha > 0 (Tier-1: skip online-MP branch)
// Binary: 0x00153e98
// -------------------------------------------------------------------------
void PauseScreen::DrawOrder(const Vec3& hudScale, int layerMask) {
    if (m_Alpha > 0.0f) {
        HUDControl3d::Draw(hudScale, layerMask);
    }
}

// -------------------------------------------------------------------------
// vtable[11]: SetToMultiplayerState -- Tier-2 stub
// Binary: 0x00154060
// -------------------------------------------------------------------------
bool PauseScreen::SetToMultiplayerState() {
    // Tier-2 deferred (binary @ 0x00154060): vtable[11], called from PauseScreen::Reset on MP entry.
    // Body (3 stores):
    //   1. m_RetryButton->m_SecondaryTex = SmartPtr::Null;     // retry+0x74
    //   2. m_RetryButton->m_bHighlighted = 0;                  // retry+0x131 -- disable interactability
    //   3. m_ResumeButton->m_SecondaryTex = m_RetryButtonTex;  // resume+0x74 -- show retry icon on resume btn
    // Activates only when split-screen MP is enabled. Trivial 3-line port -- RE complete.
    return HUDControl::SetToMultiplayerState();
}

// -------------------------------------------------------------------------
// Button delegate callbacks
// -------------------------------------------------------------------------

// PauseGameCallback (binary 0x001542d0)
// Resume button press-action:
//   State 0 -> 2 (pause from gameplay)
//   State 3 -> 4 (resume)
void PauseScreen::PauseGameCallback() {
    Game* game = Game::GetInstance();
    if (m_State == 0) {
        m_State = 2;
        PauseGame();
        // SFX "Pause"
        if (game && game->pGameSound) {
            game->pGameSound->SFXPlay("Pause", 1.0f);
        }
    } else if (m_State == 3) {
        m_State = 4;
        // SFX "Unpause"
        if (game && game->pGameSound) {
            game->pGameSound->SFXPlay("Unpause", 1.0f);
        }
    }
}

// ASM-verified: 2026-05-02 binary @ 0x00154400 (asm-inspector)
void PauseScreen::PauseGameCallback2() {
    bool wasIdle = (m_ButtonFadeAlpha == 0.0f) && (m_State == 0);
    PauseGameCallback();   // drives state 0->2 or 3->4
    if (wasIdle) {
        m_PressIndex = 1;
    }
    // state 5 is unreachable from this callback in single-player
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153ebc (re-analyst)
// Binary @ 0x00153ebc QuitGameCallback():
//   if (m_State != 3) return;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   g->m_bTutorialShown = 0; m_LastHitButton = 0; m_State = 6;
// NOTE: m_Alpha *= 0.5 and SaveCurrentData happen in Update case-6 entry, NOT here.
// NOTE: SFX "MenuQuit" also happens in Update state-6 path, not this callback.
void PauseScreen::QuitGameCallback() {
    if (m_State != 3) return;
    Game* game = Game::GetInstance();
    if (game && game->pSaveData) game->pSaveData->ClearTotals();
    if (game && game->pSaveData) game->pSaveData->ClearCombo();
    if (game) game->m_bTutorialShown = 0;
    m_LastHitButton = 0;
    m_State = 6;
}

// Binary @ 0x00153ef8 QuitGameCallback2():
//   QuitGameCallback(); m_LastHitButton = 1; g->field_0x85 = 0;
// Used by P2-Quit button in MP path.
// ASM-verified: 2026-05-08T00:00 binary @ 0x00153ef8 (re-analyst)
// Defunct: multiplayer Quit2 path -- single-player port still wires the
// tutorial-clear write so the cb retains its post-call observable state.
void PauseScreen::QuitGameCallback2() {
    QuitGameCallback();
    m_LastHitButton = 1;
    Game* game = Game::GetInstance();
    if (game) game->m_bTutorialShown = 0;
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153f68 (re-analyst)
// Binary @ 0x00153f68 RetryGameCallback():
//   if (m_State != 3) return;
//   if (g->m_AchievementProgressTimer >= 10.5f)
//       FruitSaveData::AddToTotal("retries_in_a_row", hash, 1, true, true);
//   Math::SeedGlobalRng(g->m_FrameTimer);  // binary @ 0x00153f20
//   g->m_bTutorialShown = 0;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   m_State = 5;
void PauseScreen::RetryGameCallback() {
    if (m_State != 3) return;
    Game* game = Game::GetInstance();
    if (game && game->m_AchievementProgressTimer >= 10.5f && game->pSaveData) {
        // String resolved from binary DAT_00153fe4 -> 0x001ba98f.
        const char* kKey = "retries_in_a_row";
        game->pSaveData->AddToTotal(kKey, ::StringHash(kKey),
                                    1, true, true);
    }
    // Binary @ 0x00153f20: re-seed Mortar::Random g_Random with frame
    // counter so retried runs are deterministic-from-frame-state rather
    // than boot-clock-seeded. Re-analyst confirmed g_Random @ 0x0026C8B0.
    if (game) Math::SeedGlobalRng((uint32_t)game->m_FrameTimer);
    if (game) game->m_bTutorialShown = 0;
    if (game && game->pSaveData) game->pSaveData->ClearTotals();
    if (game && game->pSaveData) game->pSaveData->ClearCombo();
    m_State = 5;
}

// -------------------------------------------------------------------------
// vtable[10]: Update -- state machine + lazy button creation
// Binary: 0x00154468 (569 lines)
// Tier-1: SP path only (IsSameScreenMultiplayer() branch skipped)
// -------------------------------------------------------------------------
void PauseScreen::Update(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // --- Lazy button creation (SP path only) ---
    // ASM-verified: 2026-05-06T00:00 binary @ 0x00154468..0x001545fc (asm-inspector)
    // Each of the three create blocks is gated only on null-on-self
    // (`+0x98 m_ResumeButton`, `+0xa0 m_QuitButton`, `+0xac m_RetryButton`).
    // No `IsEnabled()` / `pauseFlag` / `m_State` / `m_TransitionTimer` test
    // wraps the allocations — binary creates eagerly on first Update().
    // Visibility on non-gameplay screens is an alpha/draw-time concern,
    // handled by m_ButtonFadeAlpha -> m_DrawColour.a propagation below.
    // ASM-spec for binary @ 0x001544e8..0x001545fc (re-analyst):
    //   MenuButton(this, texSP&, &pos, &clickDelegate, fruitType=-1,
    //              &globalCenterVec, &deletedDelegate)
    //   then SetSingular() and AddControl().
    // For PauseScreen toggles fruitType is hard-coded -1 (no fruit entity
    // spawned); globalCenterVec is HUD::g_GlobalCenterVec = (0,0,0);
    // deletedDelegate is HUD::g_DeleteControlDelegate (HUD-side cleanup
    // when the button is removed -- left empty in port until that
    // global delegate is exposed).
    //
    // Port routes through MenuButton::Init(pos, clickCb, fruitType,
    // hitBounds, deletedCb) -- the fruitType=-1 branch skips entity
    // creation but still sets m_bVisible/Interactive/Enabled and the
    // anim defaults. Texture / m_LayerFlags / m_TargetSize are set
    // before/after Init since Init doesn't manage those slots.
    if (!m_ResumeButton) {
        m_ResumeButton = new MenuButton();
        m_ResumeButton->size       = Vec3(m_PauseButtonTexW, m_PauseButtonTexH, 0.0f);
        m_ResumeButton->m_Texture  = m_PauseButtonTex;
        m_ResumeButton->m_LayerFlags = 0x100;
        m_ResumeButton->Init(
            Vec3(240.0f, -160.0f, 0.0f),  // initial pos; overwritten each frame
            Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback),
            /*fruitType=*/-1,             // toggle -- no fruit entity spawned
            Vec3(0.0f, 0.0f, 0.0f),       // globalCenterVec = HUD::g_GlobalCenterVec
            Mortar::Delegate0<void>()     // TODO: bind HUD::g_DeleteControlDelegate
        );

        if (game->hud) {
            game->hud->AddControl(m_ResumeButton);
            m_ResumeButton->SetSingular();
        }

        // ASM-verified post-Init override (binary @ 0x001545d8..0x00154604):
        //   m_TargetSize = (Vector3::One @ GOT+0x77CC) * 64.0 * 1.0 = (64,64,64)
        //   m_ButtonOriginPos := m_TargetSize  (one-shot capture for OX in
        //   the per-frame position formulas).
        m_ResumeButton->m_TargetSize = Vec3(64.0f, 64.0f, 64.0f);
        m_ButtonOriginPos            = m_ResumeButton->m_TargetSize;
    }

    if (!m_QuitButton) {
        m_QuitButton = new MenuButton();
        m_QuitButton->size       = Vec3(m_QuitTitleTexW, m_QuitTitleTexH, 0.0f);
        m_QuitButton->m_Texture  = m_QuitTitleTex;
        m_QuitButton->m_LayerFlags = 0x100;
        m_QuitButton->Init(
            Vec3(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::QuitGameCallback),
            /*fruitType=*/-1,
            Vec3(0.0f, 0.0f, 0.0f),
            Mortar::Delegate0<void>()
        );

        if (game->hud) {
            game->hud->AddControl(m_QuitButton);
            m_QuitButton->SetSingular();
        }
    }

    if (!m_RetryButton) {
        m_RetryButton = new MenuButton();
        m_RetryButton->size       = Vec3(m_RetryButtonTexW, m_RetryButtonTexH, 0.0f);
        m_RetryButton->m_Texture  = m_RetryButtonTex;
        m_RetryButton->m_LayerFlags = 0x100;
        m_RetryButton->Init(
            Vec3(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback),
            /*fruitType=*/-1,
            Vec3(0.0f, 0.0f, 0.0f),
            Mortar::Delegate0<void>()
        );

        if (game->hud) {
            game->hud->AddControl(m_RetryButton);
            m_RetryButton->SetSingular();
        }
    }

    // --- State machine ---
    switch (m_State) {

    case 0: // Hidden / idle
        // Alpha decay toward 0
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < FADE_CLAMP) m_Alpha = 0.0f;

        // Reveal timer countdown (Tier-2: gates Resume re-enable)
        m_RevealTimer -= dt;
        if (m_RevealTimer <= 0.0f) {
            m_RevealTimer = 0.0f;
            // Re-arm the Resume button. Binary @ 0x00154d24 unconditionally
            // writes `m_ResumeButton->m_bHighlighted = 1` here (re-analyst
            // confirmed no `= 0` write to +0x131 exists anywhere in
            // PauseScreen::Update). Mirror that exactly.
            if (m_ResumeButton) m_ResumeButton->m_bHighlighted = 1;
        }
        break;

    case 1: // Bomb-flash poll (Tier-2 -- skip directly to 0)
        // DIFFERS: Tier-2 will add real BombFlashFull() poll here.
        // Tier-1: immediately clear and return to hidden.
        m_Alpha = 0.0f;
        m_ButtonFadeAlpha = 1.0f;
        m_State = 0;
        break;

    case 2: // Entry fade-in
        m_Alpha += (1.0f - m_Alpha) * FADE_IN_RATE;

        // Force game pause flag each frame while fading in (SP path only)
        game->gameActiveFlag = 1;

        if (m_Alpha > ACTIVE_THRESHOLD) {
            m_Alpha = 1.0f;
            m_State = 3;
        }
        break;

    case 3: // Active menu -- buttons interactable
        // Force game pause flag each frame (SP path only)
        game->gameActiveFlag = 1;

        // Enable hit detection on Resume and Retry
        if (m_ResumeButton) m_ResumeButton->m_bHighlighted = 1;
        if (m_RetryButton)  m_RetryButton->m_bHighlighted  = 1;
        break;

    case 4: // Resume exit-fade
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            m_Alpha = 0.0f;
            m_State = 0;
            m_RevealTimer = 2.0f;
            UnpauseGame();
        }
        break;

    case 5: // Retry exit-fade
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            FruitNinja_SaveCurrentData(false);
            m_Alpha = 0.0f;
            m_ButtonFadeAlpha = 0.0f;
            m_State = 0;
            m_RevealTimer = 2.0f;
            RetryLevel();
            UnpauseGame();
        }
        break;

    case 6: // Quit confirm exit
        // 0.5 multiplier was applied once on state-3->6 transition in QuitGameCallback.
        // Per-frame: standard 0.75 decay only (binary @ 0x00154468 case 6).
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            QuitToMenu();
            // Tier-2: HitMenuBomb at quit button position (state 6 effect)
            // if (m_LastHitButton >= 0) { ... HitMenuBomb(pos) ... }
            m_ButtonFadeAlpha = 0.0f;
            m_LastHitButton   = -1;
            // Tier-2: transition to state 1 (bomb-flash). Tier-1: skip to 0.
            m_State = 0;
            m_Alpha = 0.0f;
            FruitNinja_SaveCurrentData(false);
            UnpauseGame();
        }
        break;

    default:
        break;
    }

    // --- Post-switch unconditional math (doc section 4 items 1-7) ---

    // 1. m_ButtonFadeAlpha decay / restore
    // IsEnabled() reads g_GameData fields (binary @ 0x00153e4c).
    const bool isEnabled = IsEnabled();
    if (isEnabled) {
        m_ButtonFadeAlpha *= FADE_DECAY;
        if (m_ButtonFadeAlpha < 0.0f) m_ButtonFadeAlpha = 0.0f;
    } else {
        m_ButtonFadeAlpha += (1.0f - m_ButtonFadeAlpha) * FADE_IN_RATE;
    }

    // 2. State 6 special: snap alpha = 1.0, buttonFadeAlpha = 0
    if (m_State == 6) {
        m_Alpha = 1.0f;
        m_ButtonFadeAlpha = 0.0f;
    }

    // 3. Resume button texture swap based on m_Alpha
    if (m_ResumeButton) {
        if (m_Alpha <= 0.5f) {
            m_ResumeButton->m_Texture = m_PauseButtonTex;
        } else {
            m_ResumeButton->m_Texture = m_PlayButtonTex;
        }
    }

    // 4. Title slide-in: pos.x = 0, pos.y = 240 + sizeY + (-2 * m_Alpha)
    {
        const float sizeY = m_TitleSize.y;
        pos.x = 0.0f;
        pos.y = TITLE_SLIDE_BASE + sizeY + TITLE_SLIDE_MUL * m_Alpha;
    }

    // 5. Quit button position (only when m_Alpha > 0.01 and m_PressIndex < 2).
    // ASM-verified: 2026-05-08 binary @ ~0x00154f7a..0x00154fae (re-analyst).
    //   quit.pos.y = -((160 - sizeY*0.5 - 5) + (1-alpha)*(sizeY + 10))
    //   quit.pos.x =  240 - sizeX*0.5
    // Previous port had 240/0 instead of 160/240 -- positioned the quit
    // button 80 units further off-screen and centered on x=0 instead of
    // the right edge.
    if (m_QuitButton && m_Alpha > FADE_CLAMP && m_PressIndex < 2) {
        const float qSizeY = m_QuitTitleTexH;
        const float qSizeX = m_QuitTitleTexW;
        float quitY = -((TITLE_SLIDE_BASE - qSizeY * 0.5f - 5.0f)
                        + (1.0f - m_Alpha) * (qSizeY + 10.0f));
        float quitX = SCREEN_RIGHT_X - qSizeX * 0.5f;
        m_QuitButton->pos.x = quitX;
        m_QuitButton->pos.y = quitY;
    }

    // 6. Resume + Retry button position recomputation.
    // ASM-verified: 2026-05-06T00:00 binary @ 0x00154f8a..0x001550d6 (re-analyst+asm-inspector)
    //
    // PauseScreen post-switch tail. Three RE passes converged on this:
    //   - m_ButtonOriginPos.x is a per-session constant set ONCE at
    //     lazy-create from m_ResumeButton->m_TargetSize.x. Binary builds
    //     m_TargetSize via (Vector3::One @ GOT+0x77CC) * 64.0 * 1.0
    //     => (64, 64, 64). So OX = 64.
    //   - HUD/HUDControl3d does NO parent-transform composition (binary
    //     HUD::Draw @ 0x00144a90: flat std::list, each control resets
    //     matrix and translates by its own pos). MenuButtons are top-
    //     level siblings of PauseScreen; PauseScreen.pos does NOT
    //     offset child buttons.
    //   - Resume formula writes pos.x AND pos.y; pos.z untouched.
    //   - Retry formula writes pos.x, pos.y, pos.z.
    //
    // ASM-verified: 2026-05-08 binary @ 0x00154fea..0x001551d2 (re-analyst).
    //
    // Two-phase Resume + Retry layout:
    //   Phase 1 (lines below): write the off-screen base positions.
    //   Phase 2 (after the m_Alpha > 0 gate): lerp toward an on-screen
    //     to_pos by m_Alpha. WITHOUT the lerp the buttons stay at their
    //     base positions (off-screen left/right) for the entire pause
    //     overlay -- which is what the user observed visually.
    //
    // Resume scale: m_Alpha * 1.25 + 0.75 (binary @ 0x001550da).
    if (m_ResumeButton) {
        const float resumeScale = m_Alpha * 1.25f + 0.75f;
        m_ResumeButton->size.x = m_PauseButtonTexW * resumeScale;
        m_ResumeButton->size.y = m_PauseButtonTexH * resumeScale;
    }

    const float OX = m_ButtonOriginPos.x;  // = 64

    // Phase 1: write the base positions.
    // Resume base (binary @ 0x00154fea..0x00155106):
    //   pos.x = -((244 - 0.5*OX) + |fade|*(10 + 0.75*OX))
    //   pos.y = 0.375*OX - 165   (y unchanged by phase-2 lerp)
    // (Previous port had 0.375*OX in term1 -- WRONG; binary has 0.5*OX.)
    if (m_ResumeButton) {
        const float absFade = std::fabs(m_ButtonFadeAlpha);
        const float term1 = 244.0f - 0.5f * OX;
        const float term2 = absFade * (10.0f + 0.75f * OX);
        m_ResumeButton->pos.x = -(term1 + term2);
        m_ResumeButton->pos.y = 0.375f * OX - 165.0f;
    }
    // Retry base (binary @ 0x00155076..0x00155096):
    //   pos = (240 + 0.5*OX, -20, 0)
    if (m_RetryButton) {
        m_RetryButton->pos = Vec3(240.0f + 0.5f * OX, -20.0f, 0.0f);
        m_RetryButton->m_bActive = (m_Alpha > 0.0f) ? 1 : 0;
    }

    // Phase 2: lerp toward on-screen target by m_Alpha.
    // Binary @ 0x00155106..0x001551d2:
    //   button.pos += (to_pos - button.pos) * m_Alpha
    //   Resume target = (-OX, -20, 0) -- inside-left
    //   Retry  target = (+OX, -20, 0) -- inside-right
    // (Online-MP path substitutes (0,0,0) for both -- defunct.)
    if (m_Alpha > 0.0f) {
        if (m_ResumeButton) {
            const Vec3 target(-OX, -20.0f, 0.0f);
            m_ResumeButton->pos += (target - m_ResumeButton->pos) * m_Alpha;
        }
        if (m_RetryButton) {
            const Vec3 target(OX, -20.0f, 0.0f);
            m_RetryButton->pos += (target - m_RetryButton->pos) * m_Alpha;
        }
    }

    // 7. P2 buttons inactive (Tier-2 stub -- P2 buttons are nullptr in Tier-1)
    // m_P2ResumeButton / m_P2RetryButton are always nullptr in Tier-1.
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153e34 (re-analyst)
// Binary @ 0x00153e34: external entry — force overlay fully visible and
// jump to state 3. Used by the Bada-app-side "skip intro" handler.
void PauseScreen::SkipTo() {
    m_State = 3;
    m_Alpha = 1.0f;
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153fe8 (re-analyst)
// Binary @ 0x00153fe8: external entry (no in-screen button binds it).
// Likely call site: shop/tutorial popup-dismiss handler. Advances state
// 3 -> 4 and clears the tutorial-shown flag.
void PauseScreen::ContinueGameCallback() {
    if (m_State != 3) return;
    m_State = 4;
    Game* g = Game::GetInstance();
    if (!g) return;
    if (g->m_bTutorialShown != 0) {
        // Binary @ 0x00153f20: re-seed g_Random; see RetryGameCallback notes.
        Math::SeedGlobalRng((uint32_t)g->m_FrameTimer);
    }
    g->m_bTutorialShown = 0;
}
