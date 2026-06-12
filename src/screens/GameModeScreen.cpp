//
// GameModeScreen — mode select child screen (Classic / Zen / Arcade).
// See GameModeScreen.h for binary refs.
//
// Analysed: 2026-04-18T01:00
//

#include "GameModeScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "game/StartupEffects.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "audio/GameSound.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "util/StringHash.h"
#include "game/FruitSaveData.h"
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"

// Helper functor: captures {screen*, btn*} to call DeletedMenuButton(btn) with no args.
// Replaces std::bind(&GameModeScreen::DeletedMenuButton, this, btn) — 8 bytes on ARM32,
// fits the 32-byte Mortar::Delegate inline storage.
namespace {
struct BtnDeletedFn {
    GameModeScreen* m_screen;
    MenuButton*     m_btn;
    void operator()() const { m_screen->DeletedMenuButton(m_btn); }
};
} // namespace

// --- Binary constants (resolved from read_memory) ---

// Button positions (offline layout only — P2P variants skipped)
// Binary button order: back(1) / classic(2) / zen(3) / arcade(4)
static const Vec3 POS_BACK   ( 195.0f, -110.0f, 0.0f);  // DAT_0013ea04/08/0c — button 1
static const Vec3 POS_CLASSIC( -70.0f,   71.0f, 0.0f);  // DAT_0013ea18/1c/0c — button 2
static const Vec3 POS_ZEN    (  88.0f,   48.0f, 0.0f);  // DAT_0013ea58/5c/60 — button 3
static const Vec3 POS_ARCADE (  19.0f,  -76.0f, 0.0f);  // DAT_0013ea__/__   — button 4

// Button scale multipliers
static const float BACK_TARGET_SCALE    = 0.75f;  // DAT_0013e59c — button 1 m_TargetSize
static const float BACK_FRUIT_SCALE     = 0.75f;  // button 1 fruit scale
static const float CLASSIC_TARGET_SCALE = 0.90f;  // DAT_0013ea20 — button 2 m_TargetSize
static const float CLASSIC_FRUIT_SCALE  = 0.95f;  // DAT_0013ea24 — button 2 fruit scale
static const float SHARED_TARGET_SCALE  = 0.85f;  // DAT_0013ea28 — classic * 0.85 -> zen/arcade
static const float ZEN_FRUIT_SCALE      = 0.90f;  // DAT_0013ecac — button 3 fruit scale
static const float ARCADE_FRUIT_SCALE   = 0.75f;  // button 4 fruit scale

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
// Zen sign lerp endpoints (offline branch — network-gated online values
// at DAT_0013fba0/fba4 skipped).
//   SRC = resting pos at alpha=1 (DAT_0013fb9c + literals 14.0, 10.0)
//   DST = start pos at alpha=0  (DAT_0013fba8 + literals 29.0, 10.0)
// Binary lerp: pos = src + (src - dst) * alpha
static const Vec3 POS_LOGO_SRC( 314.0f, 14.0f, 10.0f);   // DAT_0013fb9c
static const Vec3 POS_LOGO_DST( 194.0f, 29.0f, 10.0f);   // DAT_0013fba8

// Fruit type name strings resolved at runtime via Fruit::FruitType() — matches binary call.
// Binary: DAT_0013ea50 = "watermelon" (button 2 classic),
//         DAT_0013ecc4 = "apple_red"  (button 3 zen),
//         DAT_0013ecd8 = "banana"     (button 4 arcade).
static const char* FRUIT_CLASSIC = "watermelon";
static const char* FRUIT_ZEN     = "apple_red";
static const char* FRUIT_ARCADE  = "banana";

// SinIdx scale for DrawConnectTexture pulsation
static const float SIN_SCALE   = 16380.0f;  // DAT_0013f8b4

// --- Static texture storage ---
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexModeSensei;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexModeSelect;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexClassic;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexMode2;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexArcadeMode;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexComingSoon;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexZenSign;
Mortar::SmartPtr<Mortar::Texture> GameModeScreen::s_TexBackIcon;

static GLuint TexIdOf(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
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
// Binary @ 0x0013e524 — initialise BaseScreen, default m_State=0,
// m_SecondaryAlpha=-2.5, online-MP slot=null, m_FrameTimer=0.
// ===================================================================
GameModeScreen::GameModeScreen(Game& g, bool isFromPause)
    : m_pBackButton(nullptr)        // +0xa0
    , m_ButtonDelay(-1.0f)          // +0xa4 (binary init)
    , field_0xa8(-1.0f)             // +0xa8 (binary: set to -1 in state-0 transition)
    , m_pClassicButton(nullptr)     // +0xac
    , m_pZenButton(nullptr)         // +0xb0
    , m_SecondaryAlpha(-2.5f)       // +0xb4 DAT_0013e5a0
    , m_bIsFromPause(isFromPause)   // +0xb8
    , field_0xb9(false)             // +0xb9 = 0
    , m_bChallenge(0)               // +0xba
    , m_ChallengeId(0)              // +0xbc
    , m_pChallengeData(nullptr)     // +0xc0
    , m_LayerFlagsAlt(0x80)         // +0xc4 DAT matches ctor write movs r2,#1; adds r2,#0x7f
    , m_FrameTimer(0.0f)            // +0xc8 DAT_0013e59c
    , m_pArcadeButton(nullptr)      // +0xcc
    , game(g)
    , m_bButtonsCreated(false)
    , m_bSetupLevelFired(false)
    , m_pOnlineMpButton(nullptr)
{
    LoadContent();
    m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;  // binary sets to 1 in ctor; raised to HUD_LAYER_POST_ACTOR by subclass Draw
    // Binary ctor @ 0x00182da0 (v1.6.1): m_State(+0x90)=0, m_TransAlpha(+0x8c)=0.0,
    // enabled-flag(+0x34)=1. Base ctors (BaseScreen, HUDControl) already cover
    // these, but we set them explicitly so the screen is active from construction
    // without requiring Init() to be called -- matching binary ctor semantics.
    m_State           = 0;
    m_TransitionAlpha = 0.0f;
    m_Active          = 1;
}

GameModeScreen::~GameModeScreen() {
    RemoveButtons();
}

// Binary Init @ 0x00181060 (v1.6.1) forwards to vtable+0x10 = Reset @ 0x00181074,
// which is bare BX LR. Both are no-ops; activation is done in the ctor.
// MainScreen does NOT call Init() on GameModeScreen (matching binary behaviour).
void GameModeScreen::Init() {}

// Binary @ 0x0013df80 — vtable slot 2 Reset(): no-op override stub
void GameModeScreen::Reset() {}

// Binary @ 0x00140498 — vtable slot 11 UpdateSpecific(): no-op (Update does all work)
void GameModeScreen::UpdateSpecific(float /*dt*/) {}

// Binary @ 0x0013df94 — vtable slot 15 IsTransitionInFinished(): bare BX LR, returns false
bool GameModeScreen::IsTransitionInFinished() { return false; }

void GameModeScreen::Release() {
    RemoveButtons();
}

// ===================================================================
// Matches GameModeScreen::CreateControls @ 0x0013e764
// Binary creates 4 buttons in this order:
//   Back(1)    : construct -> Init -> m_bRemovalPending=1 -> AddControl
//                -> m_TargetSize *= 0.75 -> fruitPiece->scale *= 0.75
//   Classic(2) : construct -> ResetTutePos -> Init -> AddControl
//                -> m_TargetSize *= 0.90 -> fruitPiece->scale *= 0.95
//                -> sharedVec = classicBtn->m_TargetSize * 0.85
//   Zen(3)     : construct -> Init -> m_TargetSize = sharedVec
//                -> fruitPiece->scale *= 0.90 -> AddControl
//   Arcade(4)  : construct -> Init -> m_TargetSize = sharedVec
//                -> fruitPiece->scale *= 0.75 -> RotateFacingUp(false,(0,1,0))
//                -> AddControl
// ===================================================================
void GameModeScreen::CreateControls() {
    // Port specific: guards not in binary.
    if (m_bButtonsCreated) return;
    if (!game_work.mHud) return;

    // --- Button 1: BACK (back_icon.tex, bomb fruit, QuitCallback) ---
    // Binary texture: Game+0x17c = back_icon.tex (same global slot used
    // by DojoScreen's back/play button).
    // Binary fruit type: FruitInfo_GetCount() (bomb threshold index).
    m_pBackButton = new MenuButton();
    m_pBackButton->m_Texture = (s_TexBackIcon);
    {
        MenuButton* btn = m_pBackButton;
        m_pBackButton->Init(POS_BACK,
                            Mortar::Delegate0<void>::Make(this, &GameModeScreen::QuitCallback),
                            FruitInfo_GetCount(), Vec3(0, 0, 0),
                            Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    // Binary @ 0x0013e86a: writes 1 to MenuButton+0x138 = m_bRespondsToBackKey.
    // Marks this button as the screen's hardware Back-key handler.
    m_pBackButton->m_bRespondsToBackKey = 1;
    game_work.mHud->AddControl(m_pBackButton);
    m_pBackButton->m_TargetSize = m_pBackButton->m_TargetSize * BACK_TARGET_SCALE;
    if (m_pBackButton->m_pFruitPiece) {
        m_pBackButton->m_pFruitPiece->scale =
            m_pBackButton->m_pFruitPiece->scale * BACK_FRUIT_SCALE;
    }

    // --- Button 2: CLASSIC (classic.tex, watermelon, ClassicModeCallback) ---
    // Binary: ResetTutePos is called on THIS button (not Zen).
    m_pClassicButton = new MenuButton();
    m_pClassicButton->m_Texture = (s_TexClassic);
    {
        MenuButton* btn = m_pClassicButton;
        m_pClassicButton->Init(POS_CLASSIC,
                               Mortar::Delegate0<void>::Make(this, &GameModeScreen::ClassicModeCallback),
                               Fruit::FruitType(FRUIT_CLASSIC, false), Vec3(0, 0, 0),
                               Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos(m_pClassicButton);
    }
    game_work.mHud->AddControl(m_pClassicButton);
    m_pClassicButton->m_TargetSize = m_pClassicButton->m_TargetSize * CLASSIC_TARGET_SCALE;
    if (m_pClassicButton->m_pFruitPiece) {
        m_pClassicButton->m_pFruitPiece->scale =
            m_pClassicButton->m_pFruitPiece->scale * CLASSIC_FRUIT_SCALE;
    }
    // Binary computes classicBtn->m_TargetSize * 0.85 and stores to a module-level
    // global. Zen and Arcade buttons receive this as an absolute assignment (NOT
    // a multiply of their own size).
    Vec3 sharedTargetSize = m_pClassicButton->m_TargetSize * SHARED_TARGET_SCALE;

    // --- Button 3: ZEN (mode_2.tex, apple_red, ZenModeCallback) ---
    // m_TargetSize = sharedTargetSize (absolute, NOT *= own size).
    m_pZenButton = new MenuButton();
    m_pZenButton->m_Texture = (s_TexMode2);
    {
        MenuButton* btn = m_pZenButton;
        m_pZenButton->Init(POS_ZEN,
                           Mortar::Delegate0<void>::Make(this, &GameModeScreen::ZenModeCallback),
                           Fruit::FruitType(FRUIT_ZEN, false), Vec3(0, 0, 0),
                           Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    m_pZenButton->m_TargetSize = sharedTargetSize;
    if (m_pZenButton->m_pFruitPiece) {
        m_pZenButton->m_pFruitPiece->scale =
            m_pZenButton->m_pFruitPiece->scale * ZEN_FRUIT_SCALE;
    }
    game_work.mHud->AddControl(m_pZenButton);

    // --- Button 4: ARCADE (arcade_mode.tex, banana, ArcadeModeCallback) ---
    // Binary: scale -> RotateFacingUp(false, Vec3(0,1,0)) -> AddControl.
    // m_TargetSize = sharedTargetSize (absolute, NOT *= own size).
    // spinVelAxis confirmed from DAT_0013ecbc=0.0f, literal 1.0, 0.0f.
    m_pArcadeButton = new MenuButton();
    m_pArcadeButton->m_Texture = (s_TexArcadeMode);
    {
        MenuButton* btn = m_pArcadeButton;
        m_pArcadeButton->Init(POS_ARCADE,
                              Mortar::Delegate0<void>::Make(this, &GameModeScreen::ArcadeModeCallback),
                              Fruit::FruitType(FRUIT_ARCADE, false),
                              Vec3(0, 0, 0),
                              Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    m_pArcadeButton->m_TargetSize = sharedTargetSize;
    if (m_pArcadeButton->m_pFruitPiece) {
        m_pArcadeButton->m_pFruitPiece->scale =
            m_pArcadeButton->m_pFruitPiece->scale * ARCADE_FRUIT_SCALE;
        m_pArcadeButton->m_pFruitPiece->RotateFacingUp(
            false,
            Vec3(0.0f, 1.0f, 0.0f));
    }
    game_work.mHud->AddControl(m_pArcadeButton);

    m_bButtonsCreated = true;
}

void GameModeScreen::RemoveButtons() {
    if (m_pBackButton)    { m_pBackButton->SetPendingRemoval();    m_pBackButton    = nullptr; }
    if (m_pClassicButton) { m_pClassicButton->SetPendingRemoval(); m_pClassicButton = nullptr; }
    if (m_pZenButton)     { m_pZenButton->SetPendingRemoval();     m_pZenButton     = nullptr; }
    if (m_pArcadeButton)  { m_pArcadeButton->SetPendingRemoval();  m_pArcadeButton  = nullptr; }
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

        if (game_work.mMainScreen) {
            float camT = game_work.mMainScreen->GetCameraTransition();
            camT *= CAMERA_DECAY;
            game_work.mMainScreen->SetCameraTransition(camT);
            // Binary @ 0x0013f2e2: vtable[18] (SetupLevel) dispatched once
            // camera transition crosses -0.9 (DAT_0013f460). camT decays
            // toward 0 from -1 (main menu zoom-in), so the actual gate is
            // "passed -0.9 toward zero" i.e. camT > -0.9 (less negative).
            // Latch keeps it one-shot per mode-pick.
            if (!m_bSetupLevelFired && camT > -0.9f) {
                LOG_INFO("MODESEL", "SetupLevel called; game_work.gameMode=%d", (int)game_work.gameMode);
                SetupLevel();
                m_bSetupLevelFired = true;
            }

            if (fabsf(camT) < ALPHA_OUT_DONE) {
                if (game_work.mGameSound) {
                    game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f);
                }
                LOG_INFO("MODESEL", "%d -> STATE_CAMERA_FADE (camT done; levelTransitionFlag=%d gameMode=%d)",
                         (int)m_State, (int)game_work.m_LevelTransitionFlag, (int)game_work.gameMode);
                // ASM-verified: 2026-05-22 binary @ 0x0013f366..0x0013f386
                // (re-analyst). Tail of GameModeScreen::Update cases 3..6:
                //   g_GameData->cameraFadeTimer = 0.0f;       // +0x0c
                //   g_GameData->byte_0x5 = 0;                 // m_LevelTransitionFlag
                //   this->m_bPendingRemoval = 1;
                //   g_GameData->pMainScreen->m_State = 0x11;  // STATE_CAMERA_FADE
                //   if (IsSameScreenMultiplayer()) for-each-Slash: ColoursChanged
                // State 0x11 is a PASSIVE camera-fade wait state -- it does
                // NOT call WaveManager::Reset(true)/NewGame() nor
                // PowerUpManager::Reset(true) anywhere. The earlier
                // misreading of [r5+0x160] as a child-object state machine
                // was wrong (r5 was the GOT base, not MainScreen*).
                //
                // The binary's arcade-start path does NOT trigger
                // PowerUpManager's m_bIsSpecial activation here. The real
                // trigger for ready_set_go is elsewhere -- likely
                // PowerUpManager::Update scanning specials with an
                // in-game gate, or some other entry point not yet RE'd.
                // TODO: find the real m_bIsSpecial activation site so
                // ready_set_go / arcade_60seconds / arcade_go fire on
                // arcade start. See Claude task #10.
                game_work.mMainScreen->SetCameraTransition(0.0f);
                game_work.m_LevelTransitionFlag = 0;
                m_bPendingRemoval = 1;
                game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
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
            if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_SLIDE_IN);
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
void GameModeScreen::DrawConnectTexture(Vec3 pos) {
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

    MatrixManager& mm = MatrixManager::GetInstance();

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
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexModeSensei->UnSet();
    }

    // --- 2. Borders (BaseScreen::DrawBorders with mode_select.tex) ---
    DrawBorders(s_TexModeSelect, m_TransitionAlpha, POS_BORDER);

    // --- 3. Connect texture animation ---
    DrawConnectTexture(POS_CONNECT);

    // --- 4. Logo panel (zen_sign.tex — slot 8, NOT mode_sensei) ---
    // Binary DAT_0013fbc0 = 0x76f8 → BSS slot for zen_sign.tex.
    // Standard slide-in lerp: at alpha=0 the sign sits off-right at
    // SRC=(314,14,10) (past +240 X edge), as alpha→1 it slides in to
    // rest at DST=(194,29,10) on-screen.
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
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexZenSign->UnSet();
    }
}

// --- Button callbacks ---

// Matches GameModeScreen::QuitCallback @ 0x0013F5E0.
// Plays "menu-bomb" SFX, sets m_State = 0xE (back-out), detaches back
// button's fruit piece and flings it up-right, then resets tutorial arrow.
void GameModeScreen::QuitCallback() {
    // 1. SFX
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // 2. Enter back-out state
    m_State = 0xe;

    // 3. Fling back button (binary: *(byte*)(piece+0x80) = 1, then random vel).
    // +0x80 aliases m_ChuckDelay (Fruit) / m_bMovement (Bomb). Port omits the
    // Fruit byte write since m_ChuckDelay is write-only here; Bomb write kept.
    if (m_pBackButton && m_pBackButton->m_pEntity) {
        Mortar::Entity* e = m_pBackButton->m_pEntity;
        float rx = (float)rand() / (float)RAND_MAX;
        float ry = (float)rand() / (float)RAND_MAX;
        if (e->entityType == 1) {
            static_cast<Bomb*>(e)->m_bMovement = 1;
        }
        e->vel = Vec3(rx + 5.0f, -ry, 0.0f);
    }

    // 4. Clear any active tutorial arrow
    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos(nullptr);
    }

    // Binary GameModeScreen::QuitCallback @0x00181884 does NOT call ClearMenuItems;
    // it flings only the back button's own piece (above). The port's prior
    // cascade-release flung all menu fruits, re-firing MainScreen menu callbacks
    // and oscillating menu<->mode-select (task #16). Removed for fidelity.
}

// vtable[18] @ 0x0013e21c
void GameModeScreen::SetupLevel() {
    FN::PrepareForLevelStart();
}

// Matches ClassicModeCallback @ 0x0013dfb4
void GameModeScreen::ClassicModeCallback() {
    LOG_INFO("MODESEL/Classic", "callback fired; setting gameMode=0; m_State=3");
    m_bSetupLevelFired = false;
    m_State = 3;
    game_work.gameMode = 0;
    LOG_INFO("MODESEL/Classic", "after writes: gameMode=%d m_State=%d m_bSetupLevelFired=%d",
             (int)game_work.gameMode, (int)m_State, (int)m_bSetupLevelFired);
}

// Matches ZenModeCallback @ 0x0013dffc
void GameModeScreen::ZenModeCallback() {
    m_bSetupLevelFired = false;
    m_State = 6;
    game_work.gameMode = 3;
}

// Matches ArcadeModeCallback @ 0x0013e19c
// Binary: FruitSaveData::AddToTotal("coming_soon", ..., 10) — skipped
void GameModeScreen::ArcadeModeCallback() {
    m_bSetupLevelFired = false;
    m_State = 5;
    game_work.gameMode = 2;
}

// Binary @ 0x0013df84 — sets m_bChallenge=true + stores id and data ptr
//                       (entry from challenge-invite flow)
void GameModeScreen::SetIsChallenge(int challengeId, void* data) {
    m_bChallenge     = 1;
    m_ChallengeId    = challengeId;
    m_pChallengeData = data;
}

// Binary @ 0x0013e124 — coming_soon tile callback: bump save-stat counter + reset tutorial.
// (typo "Commings" preserved from binary symbol)
void GameModeScreen::CommingsSoonCallback() {
    // Binary @ 0x0013e124: FruitSaveData::AddToTotal("modeS_pcoming_soon", hash, 1, true, true).
    if (game_work.m_SaveData) {
        const char* key = "modeS_pcoming_soon";
        game_work.m_SaveData->AddToTotal(key, ::StringHash(key), 1, true, true);
    }
    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos(nullptr);
    }
}

// Binary @ 0x0013f6ac — clears m_p*Button cache on MenuButton destroy;
//                       online-MP slot also flings the orphan fruit off-screen.
void GameModeScreen::DeletedMenuButton(MenuButton* btn) {
    if (btn == m_pBackButton)    { m_pBackButton    = nullptr; return; }
    if (btn == m_pClassicButton) { m_pClassicButton = nullptr; return; }
    if (btn == m_pZenButton)     { m_pZenButton     = nullptr; return; }
    if (btn == m_pArcadeButton)  { m_pArcadeButton  = nullptr; return; }
    if (btn == m_pOnlineMpButton) {
        m_pOnlineMpButton = nullptr;
        // Defunct: online-MP detached fruit fling (binary @ 0x0013f6ac)
    }
}

// Defunct: online MP (Casino) -- no-op stub; binary @ 0x0013dfdc sets m_State=4 + NetworkManager flag
void GameModeScreen::CasinoModeCallback() {
    m_State = 4;
    // Defunct: NetworkManager online-MP flag omitted
}

// Defunct: online MP (Versus) -- no-op stub; binary @ 0x0013e01c sets m_State=7 + alpha=1.0 to enter matchmaker
void GameModeScreen::VersusModeCallback() {
    m_State = 7;
    m_TransitionAlpha = 1.0f;
    // Defunct: matchmaker entry omitted
}

// Defunct: P2P connect -- no-op stub; binary @ 0x0013dfd4 sets m_State=8 (GameCenter connect)
void GameModeScreen::P2PConnectCallback() {
    m_State = 8;
    // Defunct: GameCenter/P2P connect omitted
}

// Defunct: upsell store handoff -- no-op stub; binary @ 0x0013e10c calls GotoFruitNinjaPage(1,-1) then m_State=0xd
void GameModeScreen::BuyNow() {
    // Defunct: GotoFruitNinjaPage(1,-1) omitted (online upsell)
    // Defunct: m_State=0xd transition omitted (UpsellScreen is Phantom)
}

// Defunct: upsell glue -- UpsellScreen never instantiated; binary @ 0x0013e084 sets m_State=10 + bumps modeS_p* counters
void GameModeScreen::SwitchToUpsell(int idx) {
    // Binary @ 0x0013e084: FruitSaveData::AddToTotal for the matching
    // modeS_p* counter. Per-idx key is "modeS_p<n>" where <n> is the
    // tile slot. Stat tracking happens even though the UpsellScreen
    // transition itself is defunct (UpsellScreen is Phantom in this build).
    if (game_work.m_SaveData) {
        char key[16];
        snprintf(key, sizeof(key), "modeS_p%d", idx);
        game_work.m_SaveData->AddToTotal(key, ::StringHash(key), 1, true, true);
    }
    // Defunct: m_State=10 transition omitted (UpsellScreen is Phantom).
    (void)idx;
}

// Defunct: upsell return path -- no-op stub; binary @ 0x0013e07c sets m_State=1 (transition-in resume)
void GameModeScreen::UpsellFinished() {
    // Defunct: upsell return path omitted (UpsellScreen is Phantom)
}

// Defunct: online-MP shrink hook -- no-op stub; binary @ 0x0013e02c snapshots fruit pose + zeroes vel/scale
void GameModeScreen::ShrinkedMultiplayerButton() {
    // Defunct: online-MP fruit snapshot omitted
}

// Defunct: online-MP button lifecycle -- no-op stub; binary @ 0x0013ecdc grows/shrinks the 4th MenuButton based on connectivity
void GameModeScreen::UpdateOnlineMultiplayerButton(float /*dt*/) {
    // Defunct: online-MP button grow/shrink based on connectivity omitted
}
