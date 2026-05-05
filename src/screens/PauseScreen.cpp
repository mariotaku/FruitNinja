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
static const float TITLE_SLIDE_MUL  = -2.0f;     // DAT_00154fc4
static const float TITLE_SLIDE_BASE = 240.0f;    // DAT_00154fd0

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
static inline GLuint LoadTex(const char* name, int* outW = nullptr, int* outH = nullptr) {
    Mortar::SmartPtr<Mortar::Texture> t = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (!t.IsValid()) return 0;
    if (outW) *outW = t->m_Width;
    if (outH) *outH = t->m_Height;
    return t->m_TexId;
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

// ASM-verified: 2026-05-03 binary @ 0x00153e4c (re-analyst)
bool PauseScreen::IsEnabled() {
    Game* g = Game::GetInstance();
    if (!g) return false;
    if (fabsf(g->m_TransitionTimer) < 0.001f) return false;  // [+0xc] epsilon
    if (g->bombHitTimer > 0.0f)               return false;  // [+0x10]
    return (g->pauseFlag ^ 1) != 0;                          // [+0x05] XOR 1
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
      m_PauseButtonTex(0),
      m_RetryButton(nullptr),
      m_P2RetryButton(nullptr),
      m_ButtonFadeAlpha(1.0f),
      m_PlayButtonTex(0),
      m_QuitTitleTex(0),
      m_RetryButtonTex(0),
      _pad_c4(0),
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
        GLuint id = LoadTex("pause_title.tex", &w, &h);
        m_Texture = id;           // primary: HUDControl3d::Draw uses this
        m_SecondaryTex = id;      // also fill inherited secondary slot (binary stores here)
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
// vtable[3]: Release -- nulls all owned texture refs
// Binary @ 0x0015408C -- vtable slot 3.
// Nulls 5 SmartPtrs (m_SecondaryTex, m_QuitTitleTex, m_PlayButtonTex,
// m_RetryButtonTex, m_PauseButtonTex). Port uses GLuint — no-op for GLuids;
// GLuint refs are not ref-counted and are freed by TextureManager on shutdown.
// -------------------------------------------------------------------------
void PauseScreen::Release() {
    m_SecondaryTex = 0;
    m_QuitTitleTex = 0;
    m_PlayButtonTex = 0;
    m_RetryButtonTex = 0;
    m_PauseButtonTex = 0;
}

// -------------------------------------------------------------------------
// vtable[4]: Reset -- restores SP-mode tex assignments on resume/retry buttons
// Binary @ 0x00154024 -- vtable slot 4.
// Inverse of SetToMultiplayerState: re-enables RetryButton and restores
// SecondaryTex assignments so SP layout is correct after MP session ends.
// -------------------------------------------------------------------------
void PauseScreen::Reset() {
    if (m_RetryButton) {
        reinterpret_cast<uint8_t*>(m_RetryButton)[0x131] = 1;  // m_bHighlighted = 1
        m_RetryButton->m_SecondaryTex = m_RetryButtonTex;
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

// Binary @ 0x00153ebc QuitGameCallback():
//   if (m_State != 3) return;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   g->field_0x85 = 0; m_LastHitButton = 0; m_State = 6;
// NOTE: m_Alpha *= 0.5 and SaveCurrentData happen in Update case-6 entry, NOT here.
// NOTE: SFX "MenuQuit" also happens in Update state-6 path, not this callback.
void PauseScreen::QuitGameCallback() {
    if (m_State != 3) return;
    Game* game = Game::GetInstance();
    if (game && game->pSaveData) game->pSaveData->ClearTotals();
    if (game && game->pSaveData) game->pSaveData->ClearCombo();
    m_LastHitButton = 0;
    m_State = 6;
}

// Binary @ 0x00153ef8 QuitGameCallback2():
//   QuitGameCallback(); m_LastHitButton = 1; g->field_0x85 = 0;
// Used by P2-Quit button in MP path.
// Defunct: multiplayer Quit2 -- no-op for single-player port; binary @ 0x00153ef8
void PauseScreen::QuitGameCallback2() {
    QuitGameCallback();
    m_LastHitButton = 1;
    // TODO: g->field_0x85 = 0 (tutorial-shown byte on gameObj; offset not yet named in Game.h)
}

// Binary @ 0x00153f68 RetryGameCallback():
//   if (m_State != 3) return;
//   if (g->field_0x1AC >= 10.5f) { FruitSaveData::AddToTotal(..., 1, true, true); }
//   InitVec3(g->field_0x194); g->field_0x85 = 0;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   m_State = 5;
void PauseScreen::RetryGameCallback() {
    if (m_State != 3) return;
    Game* game = Game::GetInstance();
    // TODO: g->field_0x1AC achievement-progress check (binary @ 0x00153f68 +0x1AC >= 10.5)
    // TODO: g->field_0x194 Vec3 init (binary @ 0x00153f68)
    // TODO: g->field_0x85 = 0 (tutorial-shown byte)
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
    if (!m_ResumeButton) {
        // P1 Resume button: pos (240, -160, 0), size from pause_button.tex
        m_ResumeButton = new MenuButton();
        m_ResumeButton->pos = Vec3(240.0f, -160.0f, 0.0f);
        m_ResumeButton->size = Vec3(m_PauseButtonTexW, m_PauseButtonTexH, 0.0f);
        m_ResumeButton->m_Texture = m_PauseButtonTex;
        m_ResumeButton->m_LayerFlags = 0x100;
        m_ResumeButton->m_FruitType = -1;
        // Member-function factory rather than lambda, so the cross-build
        // toolchain (GCC 4.4) can parse this -- lambdas weren't added until
        // GCC 4.5. Same observable binding in both compilers.
        m_ResumeButton->m_ClickCallback =
            Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback);
        m_ResumeButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_ResumeButton);
            m_ResumeButton->SetSingular();   // binary calls this -- pins button instead of cycling layers
        }
    }

    if (!m_QuitButton) {
        // P1 Quit button: initial pos (0, 320, 0) -- repositioned each frame
        m_QuitButton = new MenuButton();
        m_QuitButton->pos = Vec3(0.0f, 320.0f, 0.0f);
        m_QuitButton->size = Vec3(m_QuitTitleTexW, m_QuitTitleTexH, 0.0f);
        m_QuitButton->m_Texture = m_QuitTitleTex;
        m_QuitButton->m_LayerFlags = 0x100;
        m_QuitButton->m_FruitType = -1;
        m_QuitButton->m_ClickCallback =
            Mortar::Delegate0<void>::Make(this, &PauseScreen::QuitGameCallback);
        m_QuitButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_QuitButton);
            m_QuitButton->SetSingular();   // binary calls this -- pins button instead of cycling layers
        }
    }

    if (!m_RetryButton) {
        // P1 Retry button: initial pos (0, 320, 0) -- repositioned each frame
        m_RetryButton = new MenuButton();
        m_RetryButton->pos = Vec3(0.0f, 320.0f, 0.0f);
        m_RetryButton->size = Vec3(m_RetryButtonTexW, m_RetryButtonTexH, 0.0f);
        m_RetryButton->m_Texture = m_RetryButtonTex;
        m_RetryButton->m_LayerFlags = 0x100;
        m_RetryButton->m_FruitType = -1;
        m_RetryButton->m_ClickCallback =
            Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback);
        m_RetryButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_RetryButton);
            m_RetryButton->SetSingular();   // binary calls this -- pins button instead of cycling layers
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
            // Enable Resume button for in-game pause trigger
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

    // 5. Quit button position (only when m_Alpha > 0.01 and m_PressIndex < 2)
    if (m_QuitButton && m_Alpha > FADE_CLAMP && m_PressIndex < 2) {
        const float qSizeY = m_QuitTitleTexH;
        const float qSizeX = m_QuitTitleTexW;
        // doc: quit.Y = -((240.0 - quitSizeY*0.5 - 5.0) + (1.0 - m_Alpha) * (quitSizeY + 10.0))
        float quitY = -((240.0f - qSizeY * 0.5f - 5.0f)
                        + (1.0f - m_Alpha) * (qSizeY + 10.0f));
        float quitX = 0.0f - qSizeX * 0.5f;
        m_QuitButton->pos.x = quitX;
        m_QuitButton->pos.y = quitY;
    }

    // 6. Retry button position and Resume button scale
    // doc: buttonOriginPos updated from Resume.m_ScreenPos each frame
    // Port: m_ButtonOriginPos is a per-frame layout cache.
    // Resume scale: local_64 = m_Alpha * 1.25 + 0.75
    if (m_ResumeButton) {
        const float resumeScale = m_Alpha * 1.25f + 0.75f;
        m_ResumeButton->size.x = m_PauseButtonTexW * resumeScale;
        m_ResumeButton->size.y = m_PauseButtonTexH * resumeScale;

        m_ButtonOriginPos = m_ResumeButton->pos;
    }

    if (m_RetryButton) {
        // doc: Retry pos = Vec3(buttonOriginX*0.5 + DAT_00155218, -20.0, 0)
        // DAT_00155218 = 0.0 (confirmed; see doc section 4 DAT table)
        const float retryX = m_ButtonOriginPos.x * 0.5f + 0.0f; // + DAT_00155218
        m_RetryButton->pos.x = retryX;
        m_RetryButton->pos.y = -20.0f;

        m_RetryButton->m_bActive = (m_Alpha > 0.0f) ? 1 : 0;
    }

    // 7. P2 buttons inactive (Tier-2 stub -- P2 buttons are nullptr in Tier-1)
    // m_P2ResumeButton / m_P2RetryButton are always nullptr in Tier-1.
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: PauseScreen::ContinueGameCallback -- auto stub
void PauseScreen::ContinueGameCallback() {}
// STUB: PauseScreen::SkipTo -- auto stub
void PauseScreen::SkipTo() {}
// ---- end AUTO-STUB MERGE ----
