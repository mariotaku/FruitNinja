// Analysed: 2026-05-02T00:00
//
// PauseScreen — Tier-1 implementation.
// Binary: PauseScreen::PauseScreen @ 0x00155460 (ctor), Update @ 0x00154468.
// See docs/engine/pausescreen-deep-re.md for full spec.
//
// Tier-1 scope: SP-only path, states 0/2/3/4/5/6, three P1 buttons.
// Tier-2 deferred: P2 buttons (+0x9c/+0xa4/+0xb0), state 1 (bomb-flash),
//   HitMenuBomb in state 6, SetSingular, reveal-timer grace, retry slide-in.

#include "screens/PauseScreen.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "game/PowerUpManager.h"
#include "game/BombHit.h"
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
static SmartPtr<Mortar::Texture> s_FlashTex;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static inline GLuint LoadTex(const char* name, int* outW = nullptr, int* outH = nullptr) {
    SmartPtr<Mortar::Texture> t = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (!t.IsValid()) return 0;
    if (outW) *outW = t->m_Width;
    if (outH) *outH = t->m_Height;
    return t->m_TexId;
}

// -------------------------------------------------------------------------
// PauseGame / UnpauseGame (binary @ 0x00168f80 / 0x00168fb0)
// -------------------------------------------------------------------------

// Matches PauseGame (0x00168f80):
//   *(byte*)(game+0x02) = 1;
//   *(byte*)(game+0x0c) = 0;
//   *(float*)(game+0x08) = 0.25f;
void PauseScreen::PauseGame() {
    Game* game = Game::GetInstance();
    if (!game) return;
    game->gameActiveFlag = 1;
    // +0x0c = m_TransitionTimer flag byte; binary stores 0 here
    // DIFFERS: port's m_TransitionTimer is a float at +0x0c; writing the
    // "paused indicator byte" documented at +0x0c -- use retryFlag as proxy
    // for the byte written at +0x0c in the binary (game+0x0c is actually the
    // float m_TransitionTimer in the port struct; the binary byte at game+0x0c
    // is the "timer active" flag). We write game->retryTimer (float at +0x08)
    // to 0.25 and clear the byte at +0x0c via a cast.
    //
    // Binary writes:
    //   strb r1, [r0, #2]   -- gameActiveFlag = 1
    //   strb r2, [r0, #0xc] -- byte at +0xc = 0
    //   vstr s15, [r0, #8]  -- float at +0x8 = 0.25
    //
    // Port field mapping: +0x08 = retryTimer (float), +0x0c = m_TransitionTimer (float).
    // The byte-write at +0xc is a flag byte in the binary that the port
    // doesn't have a named field for. Safe to cast:
    game->retryTimer = 0.25f;
    reinterpret_cast<uint8_t*>(game)[0x0c] = 0;
}

// Matches UnpauseGame (0x00168fb0):
//   *(undefined4*)(game+0x10) = DAT_00168fcc;  -- restore timer
//   *(byte*)(game+0x0c) = 1;
void PauseScreen::UnpauseGame() {
    Game* game = Game::GetInstance();
    if (!game) return;
    // game+0x10 = bombHitTimer (float); binary restores it from a DAT constant.
    // DIFFERS: DAT_00168fcc value not fully resolved. Port writes 0.0f (timer cleared).
    game->bombHitTimer = 0.0f;
    reinterpret_cast<uint8_t*>(game)[0x0c] = 1;
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

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
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
// Binary: 0x00154060 swaps Resume button tex to retry tex
// -------------------------------------------------------------------------
void PauseScreen::SetToMultiplayerState() {
    // Tier-2: swap Resume button texture to m_RetryButtonTex (MP fallback)
    // TODO: implement when P2 buttons are ported (Tier-2 item 11).
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

// QuitGameCallback (binary 0x00153ebc)
// Quit button press-action:
//   State 3 -> 6; m_LastHitButton = 0; clear save totals
void PauseScreen::QuitGameCallback() {
    if (m_State == 3) {
        m_State = 6;
        m_LastHitButton = 0;
        // Binary: clear save totals / combo here (SaveCurrentData path)
        // Tier-1: just transition state.
    }
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
            Mortar::Delegate<void()>::Make(this, &PauseScreen::PauseGameCallback);
        m_ResumeButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_ResumeButton);
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
            Mortar::Delegate<void()>::Make(this, &PauseScreen::QuitGameCallback);
        m_QuitButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_QuitButton);
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
            Mortar::Delegate<void()>::Make(this, &PauseScreen::PauseGameCallback2);
        m_RetryButton->m_bHighlighted = 1;
        if (game->hud) {
            game->hud->AddControl(m_RetryButton);
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
        // Extra half-step decay on entry (doc: "extra m_Alpha *= 0.5 once entering")
        // Binary applies this once; since we're in the switch each frame we
        // apply it continuously here -- binary does it pre-switch. Port
        // mirrors by applying the extra decay then the standard decay.
        m_Alpha *= 0.5f;
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
    // IsEnabled() (binary 0x00153e4c) reads MainScreen state.
    // Tier-1: treat as always-enabled (m_ButtonFadeAlpha decays toward 0).
    // TODO: wire real IsEnabled() when MainScreen state machine is complete.
    const bool isEnabled = true;
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
