//
// GameModeScreen — mode select child screen (Classic / Zen / Arcade).
// See GameModeScreen.h for binary refs.
//
// Analysed: 2026-04-18T01:00
//

#include "GameModeScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "asset/TextureManager.h"
#include "audio/GameSound.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "debug/DebugFlags.h"
#include <cmath>
#include <cstdio>

// --- Binary constants (resolved from read_memory) ---

// Button positions (offline layout only — P2P variants skipped)
static const Vec3 POS_CLASSIC( 195.0f, -110.0f, 0.0f);  // DAT_0013ea04/08/0c
static const Vec3 POS_ZEN    ( -70.0f,   71.0f, 0.0f);  // DAT_0013ea18/1c/0c
static const Vec3 POS_ARCADE1(  88.0f,   48.0f, 0.0f);  // DAT_0013ea58/5c/60
static const Vec3 POS_ARCADE2(  19.0f,  -76.0f, 0.0f);  // offline MP button pos

// Button scale multipliers
static const float CLASSIC_TARGET_SCALE = 0.75f;  // DAT_0013e59c
static const float CLASSIC_FRUIT_SCALE  = 0.75f;
static const float ZEN_TARGET_SCALE     = 0.90f;  // DAT_0013ea20
static const float ZEN_FRUIT_SCALE      = 0.95f;  // DAT_0013ea24
static const float ZEN_HITBOUNDS_SCALE  = 0.85f;  // DAT_0013ea28
static const float ARCADE_FRUIT_SCALE   = 0.90f;  // DAT_0013ecac

// Update constants
static const float ALPHA_LERP_STEP  = 0.15f;    // DAT_0013f458
static const float ALPHA_IN_DONE    = 0.999f;   // DAT_0013f45c
static const float ALPHA_DECAY_MODE = 0.85f;    // DAT_0013f480 (states 3-7)
static const float ALPHA_DECAY_BACK = 0.75f;    // state 0xE
static const float ALPHA_OUT_DONE   = 0.001f;   // DAT_0013f484
static const float CAMERA_DECAY     = 0.75f;
static const float CAMERA_THRESH    = -0.9f;    // DAT_0013f460
static const float SECONDARY_CLAMP  = 0.1f;     // DAT_0013f474
static const float SECONDARY_RATE   = 0.25f;
static const float FRAMETIMER_RATE  = 0.15f;    // DAT_0013f48c

// Draw constants — mode_sensei sits at bottom-left, same pattern as
// DojoScreen's dojo_sensei. Slides in from left horizontally.
static const Vec3 POS_BG    (-188.0f,  -32.0f, 0.0f);    // DAT_0013fb84/88
static const Vec3 POS_BORDER(-115.0f, -130.0f, 0.0f);    // DAT_0013fb8c/90
static const Vec3 POS_CONNECT(-40.0f,  -53.0f, 0.0f);    // DAT_0013fb94/98
static const Vec3 POS_LOGO_SRC( 314.0f, 14.0f, 10.0f);   // offline
static const Vec3 POS_LOGO_DST( 314.0f, 29.0f, 10.0f);   // offline

// Fruit type name strings (binary: DAT_0013ea50 = "watermelon",
// DAT_0013ecc4 = "apple_red", DAT_0013ecd8 = "banana").
// Resolved at runtime via Fruit::FruitType() — matches binary call.
static const char* FRUIT_ZEN    = "watermelon";
static const char* FRUIT_ARCADE = "apple_red";
static const char* FRUIT_MP     = "banana";

// SinIdx scale for DrawConnectTexture pulsation
static const float SIN_SCALE   = 16380.0f;  // DAT_0013f8b4

// --- Static texture storage ---
SmartPtr<Mortar::Texture> GameModeScreen::s_TexModeSensei;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexModeSelect;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexClassic;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexMode2;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexArcadeMode;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexComingSoon;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexZenSign;
SmartPtr<Mortar::Texture> GameModeScreen::s_TexBackIcon;

static GLuint TexIdOf(const SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}
static Vec3 TexSizeOf(const SmartPtr<Mortar::Texture>& tex, float defW, float defH) {
    if (tex.IsValid())
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

// ===================================================================
// Matches GameModeScreen::LoadContent @ 0x13e330
// Binary loads 7 textures: mode_sensei, mode_select, classic, mode_2,
// arcade_mode, coming_soon, zen_sign. All module-level singletons.
// ===================================================================
void GameModeScreen::LoadContent() {
    BaseScreen::LoadContent();
    if (!s_TexModeSensei.IsValid())
        s_TexModeSensei = Mortar::TextureManager::LoadLocalisedTexture("mode_sensei.tex");
    if (!s_TexModeSelect.IsValid())
        s_TexModeSelect = Mortar::TextureManager::LoadLocalisedTexture("mode_select.tex");
    if (!s_TexClassic.IsValid())
        s_TexClassic    = Mortar::TextureManager::LoadLocalisedTexture("classic.tex");
    if (!s_TexMode2.IsValid())
        s_TexMode2      = Mortar::TextureManager::LoadLocalisedTexture("mode_2.tex");
    if (!s_TexArcadeMode.IsValid())
        s_TexArcadeMode = Mortar::TextureManager::LoadLocalisedTexture("arcade_mode.tex");
    if (!s_TexComingSoon.IsValid())
        s_TexComingSoon = Mortar::TextureManager::LoadLocalisedTexture("coming_soon.tex");
    if (!s_TexZenSign.IsValid())
        s_TexZenSign    = Mortar::TextureManager::LoadLocalisedTexture("zen_sign.tex");
    // Port specific: back_icon.tex. Binary reads this from Game+0x17c
    // (shared slot also used by DojoScreen's play/back button). Load
    // it locally here until the Game+0x17c field is ported.
    if (!s_TexBackIcon.IsValid())
        s_TexBackIcon   = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");
}

// ===================================================================
// Matches GameModeScreen::UnLoadContent @ 0x13e5a8
// ===================================================================
void GameModeScreen::UnLoadContent() {
    s_TexModeSensei.SetNull();
    s_TexModeSelect.SetNull();
    s_TexClassic.SetNull();
    s_TexMode2.SetNull();
    s_TexArcadeMode.SetNull();
    s_TexComingSoon.SetNull();
    s_TexZenSign.SetNull();
    s_TexBackIcon.SetNull();
}

// ===================================================================
// Matches GameModeScreen::GameModeScreen(bool) @ 0x0013e524
// ===================================================================
GameModeScreen::GameModeScreen(Game& g, bool isFromPause)
    : game(g)
    , m_pClassicButton(NULL)
    , m_pZenButton(NULL)
    , m_pArcadeButton(NULL)
    , m_pMultiplayerButton(NULL)
    , m_ButtonDelay(-1.0f)
    , m_SecondaryAlpha(-2.5f)   // DAT_0013e5a0
    , m_FrameTimer(0.0f)         // DAT_0013e59c
    , m_bIsFromPause(isFromPause)
    , m_bButtonsCreated(false)
{
    LoadContent();
    m_LayerFlags = 1;  // binary sets to 1 in ctor; raised to 0x80 by subclass Draw
}

GameModeScreen::~GameModeScreen() {
    RemoveButtons();
}

void GameModeScreen::Init() {
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

void GameModeScreen::Release() {
    RemoveButtons();
}

// ===================================================================
// Matches GameModeScreen::CreateControls @ 0x0013e764
// Binary creates 4 buttons. Port creates 3 (skips MP matchmaker).
// ===================================================================
void GameModeScreen::CreateControls() {
    if (m_bButtonsCreated) return;
    if (!game.hud) return;

    // --- Button 1: Classic mode ---
    // Binary texture: Game+0x17c = back_icon.tex (same global slot used
    // by DojoScreen's back/play button).
    // Binary fruit type: **(int**)(GOT+0x7060) = bomb threshold = FruitInfo_GetCount().
    m_pClassicButton = new MenuButton();
    m_pClassicButton->m_Texture = TexIdOf(s_TexBackIcon);
    m_pClassicButton->size      = TexSizeOf(s_TexBackIcon, 64.0f, 64.0f);
    m_pClassicButton->Init(POS_CLASSIC,
                           [this]() { ClassicModeCallback(); },
                           FruitInfo_GetCount(), Vec3(0, 0, 0), nullptr);
    m_pClassicButton->m_TargetSize = m_pClassicButton->m_TargetSize * CLASSIC_TARGET_SCALE;
    if (m_pClassicButton->m_pFruitPiece) {
        m_pClassicButton->m_pFruitPiece->scale =
            m_pClassicButton->m_pFruitPiece->scale * CLASSIC_FRUIT_SCALE;
    }
    m_pClassicButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pClassicButton);

    // --- Button 2: Zen mode (uses classic.tex panel, watermelon fruit) ---
    // Binary: TutorialControl::ResetTutePos(game.tutorialCtrl, zenBtn)
    m_pZenButton = new MenuButton();
    m_pZenButton->m_Texture = TexIdOf(s_TexClassic);
    m_pZenButton->size      = TexSizeOf(s_TexClassic, 64.0f, 64.0f);
    m_pZenButton->Init(POS_ZEN,
                       [this]() { ZenModeCallback(); },
                       Fruit::FruitType(FRUIT_ZEN, false), Vec3(0, 0, 0), nullptr);
    if (game.pTutorialCtrl) {
        game.pTutorialCtrl->ResetTutePos(m_pZenButton);
    }
    m_pZenButton->m_TargetSize = m_pZenButton->m_TargetSize * ZEN_TARGET_SCALE;
    if (m_pZenButton->m_pFruitPiece) {
        m_pZenButton->m_pFruitPiece->scale =
            m_pZenButton->m_pFruitPiece->scale * ZEN_FRUIT_SCALE;
    }
    m_pZenButton->m_HitBoundsScale = m_pZenButton->m_HitBoundsScale * ZEN_HITBOUNDS_SCALE;
    m_pZenButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pZenButton);

    // --- Button 3: Arcade mode (uses mode_2.tex panel, apple fruit) ---
    m_pArcadeButton = new MenuButton();
    m_pArcadeButton->m_Texture = TexIdOf(s_TexMode2);
    m_pArcadeButton->size      = TexSizeOf(s_TexMode2, 64.0f, 64.0f);
    m_pArcadeButton->Init(POS_ARCADE1,
                          [this]() { ArcadeModeCallback(); },
                          Fruit::FruitType(FRUIT_ARCADE, false), Vec3(0, 0, 0), nullptr);
    m_pArcadeButton->m_TargetSize = m_pArcadeButton->m_TargetSize * ZEN_TARGET_SCALE;
    if (m_pArcadeButton->m_pFruitPiece) {
        m_pArcadeButton->m_pFruitPiece->scale =
            m_pArcadeButton->m_pFruitPiece->scale * ARCADE_FRUIT_SCALE;
    }
    m_pArcadeButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pArcadeButton);

    // --- Button 4: Multiplayer matchmaker (DEFUNCT — OpenFeint/GameCenter) ---
    // Binary: arcade_mode.tex panel, banana fruit, RotateFacingUp((-75, 1, -75)).
    // Callback opens NetworkManager::OpenMatchmaker via state 7/8/9 flow.
    // Port: button is created to preserve the 4-button layout, but its
    // callback (MatchmakerCallback) is a no-op stub since online MP is
    // defunct. User can still see/tap it, just nothing happens.
    m_pMultiplayerButton = new MenuButton();
    m_pMultiplayerButton->m_Texture = TexIdOf(s_TexArcadeMode);
    m_pMultiplayerButton->size      = TexSizeOf(s_TexArcadeMode, 64.0f, 64.0f);
    m_pMultiplayerButton->Init(POS_ARCADE2,
                               [this]() { MatchmakerCallback(); },
                               Fruit::FruitType(FRUIT_MP, false),
                               Vec3(0, 0, 0), nullptr);
    m_pMultiplayerButton->m_TargetSize = m_pMultiplayerButton->m_TargetSize * ZEN_TARGET_SCALE;
    if (m_pMultiplayerButton->m_pFruitPiece) {
        // Binary: fruit scale *= 0.75, then RotateFacingUp(false, (-75, 1, -75))
        m_pMultiplayerButton->m_pFruitPiece->scale =
            m_pMultiplayerButton->m_pFruitPiece->scale * 0.75f;
        // TODO: Fruit::RotateFacingUp(fruit, false, Vec3(-75, 1, -75))
    }
    m_pMultiplayerButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pMultiplayerButton);

    m_bButtonsCreated = true;
}

void GameModeScreen::RemoveButtons() {
    if (m_pClassicButton)     { m_pClassicButton->SetPendingRemoval();     m_pClassicButton     = NULL; }
    if (m_pZenButton)         { m_pZenButton->SetPendingRemoval();         m_pZenButton         = NULL; }
    if (m_pArcadeButton)      { m_pArcadeButton->SetPendingRemoval();      m_pArcadeButton      = NULL; }
    if (m_pMultiplayerButton) { m_pMultiplayerButton->SetPendingRemoval(); m_pMultiplayerButton = NULL; }
    m_bButtonsCreated = false;
}

// ===================================================================
// Matches GameModeScreen::Update @ 0x0013f10c (212 lines)
// ===================================================================
void GameModeScreen::Update(float dt) {
    switch (m_State) {
    case 0: {
        // Transition in — lerp alpha, always advance (binary's
        // IsTransitionInFinished is a no-op stub returning whatever's in r0)
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP * FN::g_DebugTimeScale;

        // In binary, state advances when IsTransitionInFinished() != 0.
        // Port gates on alpha reaching the threshold.
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            m_State = 2;
            CreateControls();
            m_ButtonDelay = -1.0f;
        }
        break;
    }

    case 1: {
        // Alternate transition in (from state 9 network recovery).
        // Port: not reachable, but kept for faithful state machine.
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP * FN::g_DebugTimeScale;
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_State = 2;
            CreateControls();
        }
        break;
    }

    case 2: {
        // Idle — lerp alpha to 1.0, lerp secondaryAlpha toward 0, tick delay.
        if (m_TransitionAlpha < ALPHA_IN_DONE) {
            m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP * FN::g_DebugTimeScale;
        } else {
            m_TransitionAlpha = 1.0f;
        }

        // Binary lerps m_SecondaryAlpha toward 1.0 (NOT 0.0) at step 0.25,
        // clamped ±0.1. In Draw, `1 - secondaryAlpha` gives the slide offset,
        // so sa→1 means offset→0 (panel settles at final position).
        float step = (1.0f - m_SecondaryAlpha) * SECONDARY_RATE;
        if (step >  SECONDARY_CLAMP) step =  SECONDARY_CLAMP;
        if (step < -SECONDARY_CLAMP) step = -SECONDARY_CLAMP;
        m_SecondaryAlpha += step;

        // Tick button delay
        if (m_ButtonDelay > 0.0f) {
            m_ButtonDelay -= dt;
            if (m_ButtonDelay <= 0.0f) m_ButtonDelay = -1.0f;
        } else {
            m_ButtonDelay = -1.0f;
        }
        break;
    }

    case 3:
    case 4:
    case 5:
    case 6: {
        // Mode picked — fade out, decay camera, launch game
        // Decay scaled by debug time-scale (see MainScreen state 0xe).
        const float modeDecay = 1.0f - (1.0f - ALPHA_DECAY_MODE) * FN::g_DebugTimeScale;
        m_TransitionAlpha *= modeDecay;
        m_SecondaryAlpha = m_TransitionAlpha;

        if (game.mainScreen) {
            float camT = game.mainScreen->GetCameraTransition();
            camT *= CAMERA_DECAY;
            game.mainScreen->SetCameraTransition(camT);

            if (fabsf(camT) < ALPHA_OUT_DONE) {
                if (game.pGameSound) {
                    game.pGameSound->SFXPlay("Game-start", 1.0f, 1.0f);
                }
                game.mainScreen->SetCameraTransition(0.0f);
                game.pauseFlag = 0;
                m_bPendingRemoval = 1;
                game.mainScreen->SetState(STATE_CAMERA_FADE);
                // Binary: same-screen MP SlashEntity::ColoursChanged loop — skipped
            }
        }
        break;
    }

    case 7:
    case 8:
    case 9:
        // Online multiplayer flow — skipped (defunct network)
        m_State = 1;  // fall back to idle
        break;

    case 0xe: {
        // Back-out: quicker fade, cross 0.25 → MainScreen SLIDE_IN
        float oldAlpha = m_TransitionAlpha;
        const float backDecay = 1.0f - (1.0f - ALPHA_DECAY_BACK) * FN::g_DebugTimeScale;
        m_TransitionAlpha *= backDecay;
        m_SecondaryAlpha  = m_TransitionAlpha;

        if (oldAlpha > 0.25f && m_TransitionAlpha <= 0.25f) {
            if (game.mainScreen) game.mainScreen->SetState(STATE_SLIDE_IN);
        }
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            m_bPendingRemoval = 1;
        }
        break;
    }

    default:
        break;
    }

    // FrameTimer accumulator (drives DrawConnectTexture animation).
    // Only ticks when state > 2.
    if ((int)m_State > 2) {
        m_FrameTimer += dt / FRAMETIMER_RATE;
        if (m_FrameTimer < 0.0f) m_FrameTimer = 0.0f;
    }
}

// ===================================================================
// Matches GameModeScreen::DrawConnectTexture @ 0x0013f754
// Binary: P2P-only animation (matchmaker connection indicator).
// Guards on isP2PSupported flag — port has no P2P, always no-op.
// Texture at g_instance+0x34 (primary) / +0x38 (alt) — NOT zen_sign.
// ===================================================================
void GameModeScreen::DrawConnectTexture(const Vec3& pos) {
    (void)pos;
    // Port: no P2P network support — skip entirely.
    // Binary: if (m_FrameTimer <= 0 || !isP2PSupported) return;
}

// ===================================================================
// Matches GameModeScreen::Draw @ 0x0013f8c8
// 1. Background panel (mode_sensei.tex) with slide-in from secondaryAlpha
// 2. Borders via BaseScreen::DrawBorders (mode_select.tex)
// 3. Connect animation (zen_sign.tex pulsating)
// 4. Logo panel (mode_sensei.tex repeated at top-right)
// ===================================================================
void GameModeScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    if ((layerMask & m_LayerFlags) == 0) return;
    if (m_TransitionAlpha <= 0.0f) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // --- 1. Background panel (mode_sensei.tex) ---
    // Binary math:
    //   slideVec = (0, 1, 0)  (g_slideVec global)
    //   scaled   = slideVec * texWidth
    //   offset   = scaled * (1 - m_SecondaryAlpha)
    //   translate = offset - POS_BG_NEG
    //             = (0, texW*(1-sa), 0) - (-188, -32, 0)
    //             = (188, 32 + texW*(1-sa), 0)
    // At sa=1: panel at (188, 32). At sa=-2.5: offset is texW*3.5
    // above, so the panel slides DOWN from above.
    // Mode_sensei panel — same pattern as DojoScreen's dojo_sensei:
    // bottom-left position (-188, -32) with horizontal slide from left.
    if (s_TexModeSensei.IsValid()) {
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexModeSensei->m_Width + 1.0f,
            (float)s_TexModeSensei->m_Height + 1.0f,
            1.0f);
        const float slideX = -(float)s_TexModeSensei->m_Width * (1.0f - m_SecondaryAlpha);
        mat.GlobalTranslate44(Vec3(POS_BG.x + slideX, POS_BG.y, POS_BG.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexModeSensei->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        r->DrawQuad(Colour(255, 255, 255, 255));
        s_TexModeSensei->UnSet();
    }

    // --- 2. Borders (BaseScreen::DrawBorders with mode_select.tex) ---
    DrawBorders(s_TexModeSelect, m_TransitionAlpha, POS_BORDER);

    // --- 3. Connect texture animation ---
    DrawConnectTexture(POS_CONNECT);

    // --- 4. Logo panel (zen_sign.tex — slot 8, NOT mode_sensei) ---
    // Binary DAT_0013fbc0 = 0x76f8 → BSS slot for zen_sign.tex.
    // Lerps from SRC to DST by m_TransitionAlpha.
    if (s_TexZenSign.IsValid()) {
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexZenSign->m_Width + 1.0f,
            (float)s_TexZenSign->m_Height + 1.0f,
            1.0f);
        Vec3 logoPos = POS_LOGO_SRC + (POS_LOGO_DST - POS_LOGO_SRC) * m_TransitionAlpha;
        mat.GlobalTranslate44(logoPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexZenSign->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        r->DrawQuad(Colour(255, 255, 255, 255));
        s_TexZenSign->UnSet();
    }
}

// --- Sub-button callbacks ---

// Matches ClassicModeCallback @ 0x0013dfb4
void GameModeScreen::ClassicModeCallback() {
    m_State = 3;
    game.gameMode = 0;
}

// Matches ZenModeCallback @ 0x0013dffc
void GameModeScreen::ZenModeCallback() {
    m_State = 6;
    game.gameMode = 3;
}

// Matches ArcadeModeCallback @ 0x0013e19c
// Binary: FruitSaveData::AddToTotal("coming_soon", ..., 10) — skipped
void GameModeScreen::ArcadeModeCallback() {
    m_State = 5;
    game.gameMode = 2;
}

// Matches the 4th button callback (multiplayer matchmaker entry).
// Binary: sets m_State = 7 which begins the OpenFeint matchmaker fade
// sequence (states 7→8→9), ending with NetworkManager::OpenMatchmaker
// and NetworkManager::ConnectGameCenter.
// Port: defunct — no network backend. Stub to a no-op so the button
// exists in the layout but doesn't transition anywhere.
void GameModeScreen::MatchmakerCallback() {
    // TODO: if network is ever restored, set m_State = 7 and implement
    // states 7/8/9 in Update (currently they fall through to state 1).
}
