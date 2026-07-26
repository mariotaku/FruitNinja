//
// GameModeScreen — mode select child screen (Classic / Zen / Arcade).
// See GameModeScreen.h for binary refs.
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
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "math/_Vector2.h"
#include "render/Layout.h"
#include "debug/DebugFlags.h"
#include "util/StringHash.h"
#include "util/StringTable.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#include "resource/BlockLoader.h"
#endif

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
static const _Vector3<float> POS_BACK(195.0f, -110.0f, 0.0f);  // DAT_0013ea04/08/0c — button 1
static const _Vector3<float> POS_CLASSIC(-70.0f, 71.0f, 0.0f);  // DAT_0013ea18/1c/0c — button 2
static const _Vector3<float> POS_ZEN(88.0f, 48.0f, 0.0f);  // DAT_0013ea58/5c/60 — button 3
static const _Vector3<float> POS_ARCADE(19.0f, -76.0f, 0.0f);  // DAT_0013ea__/__   — button 4

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
static const _Vector3<float> POS_BG(-188.0f, -32.0f, 0.0f);    // DAT_0013fb84/88
static const _Vector3<float> POS_BORDER(-115.0f, -130.0f, 0.0f);    // DAT_0013fb8c/90
static const _Vector3<float> POS_CONNECT(-40.0f, -53.0f, 0.0f);    // DAT_0013fb94/98
// Zen sign lerp endpoints (offline branch — network-gated online values
// at DAT_0013fba0/fba4 skipped).
//   SRC = resting pos at alpha=1 (DAT_0013fb9c + literals 14.0, 10.0)
//   DST = start pos at alpha=0  (DAT_0013fba8 + literals 29.0, 10.0)
// Binary lerp: pos = src + (src - dst) * alpha
static const _Vector3<float> POS_LOGO_SRC(314.0f, 14.0f, 10.0f);   // DAT_0013fb9c
static const _Vector3<float> POS_LOGO_DST(194.0f, 29.0f, 10.0f);   // DAT_0013fba8

// Fruit type name strings resolved at runtime via Fruit::FruitType() — matches binary call.
// Binary: DAT_0013ea50 = "watermelon" (button 2 classic),
//         DAT_0013ecc4 = "apple_red"  (button 3 zen),
//         DAT_0013ecd8 = "banana"     (button 4 arcade).
static const char* FRUIT_CLASSIC = "watermelon";
static const char* FRUIT_ZEN     = "apple_red";
static const char* FRUIT_ARCADE  = "banana";

// SinIdx scale for DrawConnectTexture pulsation
static const float SIN_SCALE   = 16380.0f;  // DAT_0013f8b4

// Port specific: widescreen-only extra spread for the 3 mode-select rings
// (Classic/Zen/Arcade). Their MapX keys are NOT in Layout.cpp's edge-anchor
// table, so they normally just scale proportionally with HalfWidth()/240 --
// correct but leaves Zen (rightmost, +x) with a gap to the right-side
// description plate (modeselect.plate, which IS edge-anchored and tracks the
// screen edge more aggressively). This nudges the rings' own proportional
// spread a bit wider in widescreen so Zen sits closer to the plate.
// 1.0 = pure proportional (no extra spread).
static const float MODESELECT_RING_SPREAD = 1.20f;

// Port specific: widescreen-only re-centring nudge for the ARCADE ring only.
// Even at 3:2 (POS_CLASSIC.x=-70, POS_ZEN.x=88, POS_ARCADE.x=19), Arcade sits
// 10 units right of the Classic/Zen midpoint (9.0) -- that imbalance is the
// binary's own DAT layout, left untouched. MODESELECT_RING_SPREAD scales all
// 3 rings' offsets from centre uniformly, so it amplifies that pre-existing
// 10-unit offset to ~14.2 units at 16:9, which reads as a visibly uneven
// classic<->arcade / arcade<->zen gap. This coefficient pulls Arcade's mapped
// X back toward the Classic/Zen midpoint by (HalfWidth-240)*ARCADE_RECENTER,
// which is exactly 0 at 3:2/__bada__ (HalfWidth==240) and grows only with the
// widescreen spread. Tuned so Arcade lands on the Classic/Zen midpoint at
// 16:9 (HalfWidth~284.44): needed shift 14.2222 / (284.44-240) = 0.32.
static const float ARCADE_RECENTER = 0.32f;

// ---------------------------------------------------------------------------
// Rate-independence macros for the per-present (UpdateRealtime) split of
// m_TransitionAlpha/m_SecondaryAlpha easing (states 2 and 0xf only -- states
// 0/1 stay in Update() at 60Hz, see the UpdateRealtime() doc comment for why).
// Mirrors ShopScreen's SS_APPROACH_F/SS_DECAY_F (see ShopScreen.cpp) and
// ScrollingMenu's SM_DECAY_F/SM_SPRING_F. Under __bada__ these are unused
// (Update() keeps the original scalar forms inline, folding g_DebugTimeScale
// into the rate as `1 - (1-k)*scale` per the existing MainScreen/GameModeScreen
// convention); under the port, UpdateRealtime() uses the powf dt-scaled forms
// with the same g_DebugTimeScale fold so f==1 (dtSeconds == 1/60) reproduces
// one 60Hz tick.
// ---------------------------------------------------------------------------
#ifndef __bada__
    // v += (to - v) * effective_k  (spring towards `to`, g_DebugTimeScale-scaled, dt-scaled)
    #define GMS_APPROACH_F(v, to, k) \
        ((v) += ((to) - (v)) * (1.0f - powf(1.0f - ((k) * FN::g_DebugTimeScale), f)))
    // v *= effective_k  (decay towards zero, g_DebugTimeScale-scaled, dt-scaled)
    #define GMS_DECAY_F(v, k) \
        ((v) *= powf(1.0f - (1.0f - (k)) * FN::g_DebugTimeScale, f))
#endif

// Shared TTF face for BakedStringBox text on the Zen sign plate.
// v1.6.1: reads game_work.m_pTTFFontMain (GameWork+0x614, the locale face
//   PreloadFontsTTF @0x0011c1fc sets to arabic.ttf when languageFlag==0x14,
//   else gangofchinese.ttf). Falls back to a lazily-created gangofchinese.ttf
//   only if PreloadFontsTTF hasn't run yet.
static Mortar::FontCacheObjectTTF* GetGameModeTTFFont() {
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// --- Static texture storage (file-scope statics matching binary's module-level globals) ---
namespace {
    Mortar::SmartPtr<Mortar::Texture> s_TexModeSensei;
    Mortar::SmartPtr<Mortar::Texture> s_TexModeSelect;
    Mortar::SmartPtr<Mortar::Texture> s_TexClassic;
    Mortar::SmartPtr<Mortar::Texture> s_TexMode2;
    Mortar::SmartPtr<Mortar::Texture> s_TexArcadeMode;
    Mortar::SmartPtr<Mortar::Texture> s_TexComingSoon;
    Mortar::SmartPtr<Mortar::Texture> s_TexZenSign;
    Mortar::SmartPtr<Mortar::Texture> s_TexBackIcon;
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
// Matches GameModeScreen::GameModeScreen(bool) @ 0x00182da0
// Binary @ 0x00182da0 — initialise BaseScreen, default m_State=0,
// m_SecondaryAlpha=-2.5, online-MP slot=null, m_FrameTimer=0.
// ===================================================================
GameModeScreen::GameModeScreen(Game& g, bool isFromPause)
    : m_pBackButton(nullptr)        // +0xa0
    , m_ButtonDelay(-1.0f)          // +0xa4 (binary init)
    , m_TransitionTimer(-1.0f)      // +0xa8 (binary: set to -1 in state-0 transition)
    , m_pClassicButton(nullptr)     // +0xac
    , m_pZenButton(nullptr)         // +0xb0
    , m_SecondaryAlpha(-2.5f)       // +0xb4 DAT_0013e5a0
    , m_bIsFromPause(isFromPause)   // +0xb8
    , m_bChallenge(0)               // +0xb9
    , m_ChallengeId(0)              // +0xbc
    , m_pChallengeData(0)           // +0xc0
    , m_LayerFlagsAlt(0x80)         // +0xc4 DAT matches ctor write movs r2,#1; adds r2,#0x7f
    , m_FrameTimer(0.0f)            // +0xc8 DAT_0013e59c
    , m_pOnlineMpButton(nullptr)    // +0xcc
    , m_pTitleBox(nullptr)          // +0xd0
    , m_pDescBox(nullptr)           // +0xd4
    , m_pInfoBox(nullptr)           // +0xd8
#if !defined(__bada__)
    , m_pGame(&g)
    , m_bSetupLevelFired(false)
#if defined(FN_BLOCK_PRELOAD)
    , m_bLoading(false)
#endif
    , m_pArcadeButton(nullptr)
#endif
{
#if defined(__bada__)
    (void)g;
#endif
    LoadContent();
    m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;  // binary sets to 1 in ctor; raised to HUD_LAYER_POST_ACTOR by subclass Draw
    m_State           = 0;
    m_TransitionAlpha = 0.0f;
    m_Active          = 1;

    // Binary ctor @0x00182da0 (v1.6.1 GameModeScreen): constructs three BakedStringBoxes
    // for the text drawn over the zen_sign board quad.
    // Metallic gradient for DescBox / InfoBox approximated as gold-bronze two-stop.
    Mortar::FontCacheObjectTTF* font = GetGameModeTTFFont();
    if (font) {
        // m_pTitleBox: 3-line feature list "NO BOMBS!\nNO LIVES!\n90 SECS!"
        // ASM-spec v1.6.1 GameModeScreen ctor @0x00182da0: (font, 12.0, w=73, h=53,
        // align=0xf, maxLines=4, lineSpacing=6). lineSpacing is the line-pitch addend ->
        // multi-line step = (int)(fontSize+lineSpacing) = (int)(12+6) = 18px. The port
        // previously passed maxLines=3/lineSpacing=0 -> step 12px (6px too tight = crammed).
        Mortar::BakedStringBox* tbox = new Mortar::BakedStringBox(
            font, 12.0f, 73, 53, (Mortar::ALIGNMENT_TYPE)0xf, 4, 6);
        {
            const char* s0 = GETSTRING_CAST_0((LocalizedString)0x3be); // "NO BOMBS!"
            const char* s1 = GETSTRING_CAST_0((LocalizedString)0x3bf); // "NO LIVES!"
            const char* s2 = GETSTRING_CAST_0((LocalizedString)0x3c0); // "90 SECS!"
            char buf[256];
            snprintf(buf, sizeof(buf), "%s\n%s\n%s",
                     s0 ? s0 : "", s1 ? s1 : "", s2 ? s2 : "");
            tbox->SetText(buf);
        }
        // ASM-spec v1.6.1: game_work+0x6a0 (PreloadRings @0x0011cd44) = #6F461E
        tbox->SetColour(Colour(111, 70, 30, 255), 0);
        tbox->SetHorizontalLineSpacing(-1);
        tbox->FitIntoVerticalBounds();
        m_pTitleBox = tbox;

        // m_pDescBox: single-line "MODE SELECT"
        // fontSize=22, w=200, h=22, align=0xf, maxLines=1, lineSpacing=0
        Mortar::BakedStringBox* dbox = new Mortar::BakedStringBox(
            font, 22.0f, 200.0f, 22.0f, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0.0f);
        dbox->SetText(GETSTRING_CAST_0((LocalizedString)0x3ba)); // "MODE SELECT"
        // Binary SetMetallicGradient @0x002458e0 (4-stop metallic, m_ColourMode=4).
        // Port: renders 2-stop top/bottom via SetMetallicGradient; full 4-stop pending.
        // TODO: 4-stop metallic render path (binary SetMetallicGradient @0x002458e0)
        dbox->SetMetallicGradient(
            Colour(120, 178, 42, 255),
            Colour(200, 223, 84, 255),
            Colour(121, 183, 20, 255),
            Colour(96,  116, 14, 255),
            false);
        dbox->SetHorizontalLineSpacing(-1);
        m_pDescBox = dbox;

        // m_pInfoBox: single-line "MULTIPLAYER"
        // fontSize=22, w=200, h=22, align=0xf, maxLines=1, lineSpacing=0
        Mortar::BakedStringBox* ibox = new Mortar::BakedStringBox(
            font, 22.0f, 200.0f, 22.0f, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0.0f);
        ibox->SetText(GETSTRING_CAST_0((LocalizedString)0x39f)); // "MULTIPLAYER"
        // Binary SetMetallicGradient @0x002458e0 (4-stop metallic, m_ColourMode=4).
        // Port: renders 2-stop top/bottom via SetMetallicGradient; full 4-stop pending.
        // TODO: 4-stop metallic render path (binary SetMetallicGradient @0x002458e0)
        ibox->SetMetallicGradient(
            Colour(175, 90,  48, 255),
            Colour(231, 50,  35, 255),
            Colour(170, 9,   19, 255),
            Colour(141, 1,   22, 255),
            false);
        ibox->SetHorizontalLineSpacing(-1);
        m_pInfoBox = ibox;
    }
}

GameModeScreen::~GameModeScreen() {
    // Binary ~GameModeScreen D1 @0x00183694: call Release(), then delete the 3 boxes.
    // Release() does NOT touch the 4 MenuButtons; they self-remove via their own
    // shrink-out lifecycle. Calling RemoveButtons() here was a port divergence
    // that caused a heap-use-after-free when a button had already been self-reaped
    // by HUD::Update() before GameModeScreen's own reap ran.
#if defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 1 -- teardown safety: if this screen is destroyed while
    // still draining the INGAME work-queue (e.g. torn down before its LOADING
    // sub-state finished), drop the queue rather than leaving it to silently
    // resume draining under whatever next screen calls PreloadBlockBegin.
    if (m_bLoading) fn::wii::BlockLoader::Reset();
#endif
    GameModeScreen::Release();
    delete m_pTitleBox; m_pTitleBox = nullptr;
    delete m_pDescBox;  m_pDescBox  = nullptr;
    delete m_pInfoBox;  m_pInfoBox  = nullptr;
}

// Binary Init @ 0x00181060 (v1.6.1) forwards to vtable+0x10 = Reset @ 0x00181074,
// which is bare BX LR. Both are no-ops; activation is done in the ctor.
// MainScreen does NOT call Init() on GameModeScreen (matching binary behaviour).
void GameModeScreen::Init() {}

// Binary @ 0x00181074 — vtable slot 2 Reset(): no-op override stub
void GameModeScreen::Reset() {}

// Binary @ 0x00140498 — vtable slot 11 UpdateSpecific(): no-op (Update does all work)
void GameModeScreen::UpdateSpecific(float /*dt*/) {}

// Binary @ 0x0013df94 — vtable slot 15 IsTransitionInFinished(): bare BX LR, returns false
bool GameModeScreen::IsTransitionInFinished() { return false; }

// Binary GameModeScreen::Release @0x001835a4:
//   1. Nulls SmartPtr<Texture> at +0x74 (HUDControl3d::m_Texture / back-icon slot).
//   2. Sets m_TransitionAlpha = 0.
//   3. Calls BaseScreen::Release().
// Does NOT call RemoveButtons() -- the 4 MenuButtons self-remove via their own
// shrink-out lifecycle (fruit sliced -> m_pEntity null -> phase drain -> pending).
void GameModeScreen::Release() {
    m_Texture.SetNull();       // +0x74: binary nulls this SmartPtr<Texture>
    m_TransitionAlpha = 0.0f;  // +0x8c: binary writes 0
    BaseScreen::Release();
}

// ===================================================================
// Matches GameModeScreen::CreateControls @ 0x001819bc
// Binary creates 4 buttons in this order:
//   Back(1)    : construct -> Init -> m_bRemovalPending=1 -> AddControl
//                -> m_TargetSize *= 0.75 -> fruitPiece->scale *= 0.75
//   Classic(2) : construct -> ResetTutePos -> Init -> AddControl
//                -> m_TargetSize *= 0.90 -> fruitPiece->scale *= 0.95
//                -> sharedVec = classicBtn->m_RestScale * 0.85
//   Zen(3)     : construct -> Init -> m_TargetSize = sharedVec
//                -> fruitPiece->scale *= 0.90 -> AddControl
//   Arcade(4)  : construct -> Init -> m_TargetSize = sharedVec
//                -> fruitPiece->scale *= 0.75 -> RotateFacingUp(false,(0,1,0))
//                -> AddControl
// ===================================================================
void GameModeScreen::CreateControls() {
    // Port specific: widescreen-only extra spread factor for the 3 mode rings
    // (see MODESELECT_RING_SPREAD comment above). Identity (1.0) at 3:2 and
    // under __bada__, so MapX(x*spread) == MapX(x) there -- byte-identical
    // to the pre-existing faithful path.
    float spread = 1.0f;
#ifndef __bada__
    if (Layout::IsWideLayout()) spread = MODESELECT_RING_SPREAD;
#endif

    // --- Button 1: BACK (plain ring m_RingTex[0x10], bomb fruit, QuitCallback) ---
    // ASM-spec v1.6.1 GameModeScreen::CreateControls @0x001819bc: ring label =
    //   MenuButton::SetText(GETSTRING(...)) over plain m_RingTex[...].
    // Binary fruit type: FruitInfo_GetCount() (bomb threshold index).
    m_pBackButton = new MenuButton();
    m_pBackButton->m_Texture = game_work.m_RingTex[0x10];
    {
        MenuButton* btn = m_pBackButton;
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo and PauseScreen's pause.retry
        // convention for edge-pinned ring/menu buttons).
        m_pBackButton->Init(_Vector3<float>(MapX(POS_BACK.x, "modeselect.btn.back"), POS_BACK.y, POS_BACK.z),
                            Mortar::Delegate0<void>::Make(this, &GameModeScreen::QuitCallback),
                            FruitInfo_GetCount(), _Vector3<float>(0, 0, 0),
                            Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    // Binary @ 0x0013e86a: writes 1 to MenuButton+0x138 = m_bRespondsToBackKey.
    // Marks this button as the screen's hardware Back-key handler.
    m_pBackButton->m_bRespondsToBackKey = 1;
    m_pBackButton->m_bBackdropActive = 1; // v1.6.1 GameModeScreen::CreateMenuItems @0x00181bac
    m_pBackButton->SetText(
        GETSTRING_CAST_0(LSTR_DJ_BACK_BUTTON),
        game_work.m_RingColours[0],
        game_work.m_RingColours[1],
        26.5f, 10.0f, true, true);
    game_work.mHud->AddControl(m_pBackButton);
    m_pBackButton->m_RestScale = m_pBackButton->m_RestScale * BACK_TARGET_SCALE;
    if (m_pBackButton->m_pTrackedFruit) {
        m_pBackButton->m_pTrackedFruit->scale =
            m_pBackButton->m_pTrackedFruit->scale * BACK_FRUIT_SCALE;
    }

    // --- Button 2: CLASSIC (plain ring m_RingTex[2], watermelon, ClassicModeCallback) ---
    // Binary: ResetTutePos is called on THIS button (not Zen).
    m_pClassicButton = new MenuButton();
    m_pClassicButton->m_Texture = game_work.m_RingTex[2];
    {
        MenuButton* btn = m_pClassicButton;
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo convention), extra-spread by
        // MODESELECT_RING_SPREAD in widescreen (see const comment above). Identity
        // (spread=1) at 3:2/__bada__.
        m_pClassicButton->Init(_Vector3<float>(MapX(POS_CLASSIC.x * spread, "modeselect.btn.classic"), POS_CLASSIC.y, POS_CLASSIC.z),
                               Mortar::Delegate0<void>::Make(this, &GameModeScreen::ClassicModeCallback),
                               Fruit::FruitType(FRUIT_CLASSIC, false), _Vector3<float>(0, 0, 0),
                               Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos(m_pClassicButton);
    }
    m_pClassicButton->SetText(
        GETSTRING_CAST_0(LSTR_GM_CLASSIC),
        game_work.m_RingColours[4],
        game_work.m_RingColours[5],
        48.5f, 14.0f, true, true);
    game_work.mHud->AddControl(m_pClassicButton);
    m_pClassicButton->m_RestScale = m_pClassicButton->m_RestScale * CLASSIC_TARGET_SCALE;
    if (m_pClassicButton->m_pTrackedFruit) {
        m_pClassicButton->m_pTrackedFruit->scale =
            m_pClassicButton->m_pTrackedFruit->scale * CLASSIC_FRUIT_SCALE;
    }
    // Binary computes classicBtn->m_RestScale * 0.85 and stores to a module-level
    // global. Zen and Arcade buttons receive this as an absolute assignment (NOT
    // a multiply of their own size).
    _Vector3<float> sharedTargetSize = m_pClassicButton->m_RestScale * SHARED_TARGET_SCALE;

    // --- Button 3: ZEN (plain ring m_RingTex[6], apple_red, ZenModeCallback) ---
    // m_TargetSize = sharedTargetSize (absolute, NOT *= own size).
    m_pZenButton = new MenuButton();
    m_pZenButton->m_Texture = game_work.m_RingTex[6];
    {
        MenuButton* btn = m_pZenButton;
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo convention), extra-spread by
        // MODESELECT_RING_SPREAD in widescreen so Zen sits closer to the right-side
        // description plate. Identity (spread=1) at 3:2/__bada__.
        m_pZenButton->Init(_Vector3<float>(MapX(POS_ZEN.x * spread, "modeselect.btn.zen"), POS_ZEN.y, POS_ZEN.z),
                           Mortar::Delegate0<void>::Make(this, &GameModeScreen::ZenModeCallback),
                           Fruit::FruitType(FRUIT_ZEN, false), _Vector3<float>(0, 0, 0),
                           Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    m_pZenButton->m_RestScale = sharedTargetSize;
    if (m_pZenButton->m_pTrackedFruit) {
        m_pZenButton->m_pTrackedFruit->scale =
            m_pZenButton->m_pTrackedFruit->scale * ZEN_FRUIT_SCALE;
    }
    m_pZenButton->SetText(
        GETSTRING_CAST_0(LSTR_GM_ZEN),
        game_work.m_RingColours[6],
        game_work.m_RingColours[7],
        40.0f, 12.0f, true, true);
    game_work.mHud->AddControl(m_pZenButton);

    // --- Button 4: ARCADE (plain ring m_RingTex[0xd], banana, ArcadeModeCallback) ---
    // Binary: scale -> RotateFacingUp(false, Vec3(0,1,0)) -> AddControl.
    // m_TargetSize = sharedTargetSize (absolute, NOT *= own size).
    // spinVelAxis confirmed from DAT_0013ecbc=0.0f, literal 1.0, 0.0f.
    // Arcade has no slot in the 220-byte binary struct (see class-layout comment
    // in the header) -- built via a local; the port-only m_pArcadeButton cache is
    // assigned at the end under !__bada__.
    MenuButton* arcadeBtn = new MenuButton();
    arcadeBtn->m_Texture = game_work.m_RingTex[0xd];
    {
        MenuButton* btn = arcadeBtn;
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo convention), extra-spread by
        // MODESELECT_RING_SPREAD in widescreen. Identity (spread=1) at 3:2/__bada__.
        // Port specific: ARCADE_RECENTER pulls the mapped X back toward the
        // Classic/Zen midpoint in widescreen only (0 at 3:2/__bada__, see
        // ARCADE_RECENTER comment above) -- Classic/Zen positions are untouched.
        float arcadeX = MapX(POS_ARCADE.x * spread, "modeselect.btn.arcade");
#ifndef __bada__
        arcadeX -= (Layout::HalfWidth() - 240.0f) * ARCADE_RECENTER;
#endif
        arcadeBtn->Init(_Vector3<float>(arcadeX, POS_ARCADE.y, POS_ARCADE.z),
                        Mortar::Delegate0<void>::Make(this, &GameModeScreen::ArcadeModeCallback),
                        Fruit::FruitType(FRUIT_ARCADE, false),
                        _Vector3<float>(0, 0, 0),
                        Mortar::Delegate0<void>(BtnDeletedFn{this, btn}));
    }
    arcadeBtn->m_RestScale = sharedTargetSize;
    if (arcadeBtn->m_pTrackedFruit) {
        arcadeBtn->m_pTrackedFruit->scale =
            arcadeBtn->m_pTrackedFruit->scale * ARCADE_FRUIT_SCALE;
        arcadeBtn->m_pTrackedFruit->RotateFacingUp(
            false,
            _Vector3<float>(0.0f, 1.0f, 0.0f));
    }
    arcadeBtn->SetText(
        GETSTRING_CAST_0(LSTR_GM_ARCADE),
        game_work.m_RingColours[10],
        game_work.m_RingColours[11],
        41.0f, 12.0f, true, true);
    game_work.mHud->AddControl(arcadeBtn);
#if !defined(__bada__)
    m_pArcadeButton = arcadeBtn;
#endif
}

void GameModeScreen::RemoveButtons() {
    if (m_pBackButton)    { m_pBackButton->SetPendingRemoval();    m_pBackButton    = nullptr; }
    if (m_pClassicButton) { m_pClassicButton->SetPendingRemoval(); m_pClassicButton = nullptr; }
    if (m_pZenButton)     { m_pZenButton->SetPendingRemoval();     m_pZenButton     = nullptr; }
#if !defined(__bada__)
    if (m_pArcadeButton)  { m_pArcadeButton->SetPendingRemoval();  m_pArcadeButton  = nullptr; }
#endif
}

// ===================================================================
// Matches GameModeScreen::Update @ 0x1827d0
// ===================================================================
void GameModeScreen::Update(float dt) {
    switch (m_State) {
    case 0: {
        // v1.6.1 GameModeScreen::Update @0x001827d0 state 0: alpha += (1-alpha)*0.15
        // VISUAL lerp; binary ADVANCE is gated on IsTransitionInFinished (vtable slot16),
        // NOT on alpha directly. IsTransitionInFinished is a bare-BX-LR stub returning
        // false in this build (see GameModeScreen::IsTransitionInFinished() in this
        // file) -- the alpha threshold is the only available advance proxy, so
        // unlike states 2/0xf this lerp is NOT split into
        // UpdateRealtime(): the gate that fires CreateControls (a HUD control-list
        // mutation, forbidden inside UpdateRealtime per rule A) must observe the lerp's
        // result at the SAME 60Hz cadence it tests it at, so it stays here for both
        // __bada__ and the port.
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP * FN::g_DebugTimeScale;

        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            m_State = 2;
            CreateControls();
            m_ButtonDelay = -1.0f;
            m_TransitionTimer = -1.0f;
        }
        break;
    }

    case 1: {
        // Alternate transition in (from state 9 network recovery).
        // Port: not reachable, but kept for faithful state machine. Same rationale
        // as state 0 above -- lerp stays here, not split into UpdateRealtime().
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP * FN::g_DebugTimeScale;
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_State = 2;
            CreateControls();
        }
        break;
    }

    case 2: {
        // v1.6.1 GameModeScreen::Update @0x001827d0 state 2 (idle): alpha settle-to-1
        // lerp + m_SecondaryAlpha lerp-to-1 (rate 0.25, clamp +/-0.1) are PURE VISUAL --
        // moved to UpdateRealtime(). m_ButtonDelay countdown is state-machine/input
        // logic and stays here at 60Hz.
#ifdef __bada__
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
#endif

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
#if defined(FN_BLOCK_PRELOAD)
        // Task #66 Phase 1 -- the INGAME preload was a synchronous ~1.4s stall
        // (GAME-START freeze). Now cooperative: begin the work-queue behind
        // the still-fully-opaque panel (same trigger point as the old
        // one-shot SetupLevel() call), then drain it a few items per frame
        // while holding the panel + gating input + spinning the picked
        // button's loading symbol.
        //
        // The ARM (PreloadBlockBegin + SetInputModal + SetLoadingSymbol(true)
        // + m_bLoading latch) happens in the picked mode's *ModeCallback
        // (below), NOT here -- the callback runs inside the picked button's
        // own Update, on the exact frame the slice fires, with a guaranteed-
        // valid button pointer. Arming one frame later here raced the
        // button's ungated post-pick shrink (MenuButton.cpp) reaping the
        // button (DeletedMenuButton nulls the ptr) before PickedModeButton()
        // could resolve it at large/spiky dt -- the spinner silently never
        // armed. See m_bLoading's declaration comment.
        if (m_bLoading) {
            if (!fn::wii::BlockLoader::PreloadBlockStep(1)) break;  // still loading -- hold + keep spinner

            if (MenuButton* btn = PickedModeButton()) btn->SetLoadingSymbol(false);
            if (game_work.mHud) game_work.mHud->SetInputModal(nullptr);
            PrepareForLevelStart();  // SetupLevel's tail (non-preload part)
            m_bLoading = false;
            // fall through to the existing decay below -- this frame resumes normally.
        }
#endif

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
#if !defined(__bada__) && !defined(FN_BLOCK_PRELOAD)
            if (!m_bSetupLevelFired && camT > -0.9f) {
                SetupLevel();
                m_bSetupLevelFired = true;
            }
#endif

            if (fabsf(camT) < ALPHA_OUT_DONE) {
                if (game_work.mGameSound) {
                    game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f);
                }
                // ASM-spec v1.6.1 GameModeScreen::Update @0x1829e4 cases 3-6: GameModeScreen is the sole NewGame owner on the mode-select start path.
                game_work.mMainScreen->SetCameraTransition(0.0f);
                game_work.bM_bPaused = 0;
                m_bPendingRemoval = 1;
                game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
                WaveManager::GetInstance()->NewGame();
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
        // Defunct: Challenges (SocialLib ChallengeMenuScreen) -- no-op stub; v1.6.1 binary @ 0x1827d0 case 0xe.
        // ChallengesCallback (@0x181154) sets m_State=0xE but the binary's case 0xE is empty -- no
        // ChallengeMenuScreen ctor/push is reachable (it exists only as a SocialLib static-init symbol,
        // _GLOBAL__I_ChallengeMenuScreen.cpp @0x166010, with no instantiation xref). The earlier
        // "push via PLT 0x108354" note was wrong: 0x108354 -> GOT 0x2D2F20 -> UpdateOnlineMultiplayerButton.
        break;
    }

#ifdef __bada__
    case 0xf: {
        // Back-out: quicker fade, cross 0.25 -> MainScreen SLIDE_IN.
        // __bada__ only: port moves this whole body (decay + threshold
        // crossings) atomically into UpdateRealtime() -- see the case-0xf
        // comment there for why this must NOT be split between Update/UpdateRealtime.
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
#else
    case 0xf:
        // DIFFERS: v1.6.1 GameModeScreen::Update @0x001827d0 case 0xf (back-out) body
        // moved WHOLE to UpdateRealtime() -- decay, m_SecondaryAlpha mirror, and BOTH
        // threshold crossings (old>0.25&&new<=0.25 -> MainScreen SLIDE_IN; new<0.001 ->
        // m_bPendingRemoval) must stay atomic in one place (see UpdateRealtime() below).
        // Splitting the decay from its own threshold checks was the prior bug: the
        // decay ran dt-scaled per-present while the checks ran at 60Hz against a
        // value that had already crossed/re-crossed between ticks, so MainScreen's
        // SLIDE_IN transition was missed and the menu never reappeared.
        break;
#endif

    default:
        break;
    }

    // FrameTimer accumulator (drives DrawConnectTexture animation).
    // Only ticks when state > 2.
    if ((int)m_State > 2) {
        m_FrameTimer += dt / FRAMETIMER_RATE;
        if (m_FrameTimer >= 0.0f) m_FrameTimer = 0.0f;   // binary @0x00182d70 vmovpl: clamp to <= 0
    }
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// ShopScreen::UpdateRealtime for the sibling pattern. Advances the per-present
// (dt-scaled) VISUAL-ONLY parts of m_TransitionAlpha/m_SecondaryAlpha for
// state 2, and moves the WHOLE state-0xf back-out body here atomically.
//
// States 0/1 are deliberately NOT split here: their alpha lerp is the only
// available proxy for the binary's IsTransitionInFinished gate (a bare-BX-LR
// stub returning false -- see GameModeScreen::IsTransitionInFinished() in this
// file), and that gate fires CreateControls(), a HUD control-list mutation forbidden inside
// UpdateRealtime (rule A -- HUD::UpdateRealtime is iterating the list when it
// calls this). The lerp and its threshold check must run at the SAME cadence,
// so both stay in Update() at 60Hz for states 0/1.
//
// SAFETY (rule A): this function must ONLY do field writes (alpha,
// m_SecondaryAlpha, MainScreen.m_State, m_bPendingRemoval). It must NEVER call
// AddControl/RemoveControl/CreateControls. m_ButtonDelay/input handling
// (state 2) stays in Update() at 60Hz.
//
// State 0xf atomicity (rule B/C -- the actual bug fix): a prior attempt split
// the *= 0.75 decay into this function while leaving the old>0.25&&new<=0.25
// and new<0.001 threshold checks in Update() at 60Hz. Because the decay here
// runs once per PRESENTED frame (could be many ticks between one Update()
// call, e.g. 90/120Hz displays) while the checks in Update() only ran once
// per SIM tick, the transient old/new pair the checks needed had already been
// overwritten by additional decay steps by the time Update() next ran -- the
// crossing was stepped over and never observed, so MainScreen::m_State was
// never set to STATE_SLIDE_IN and mode-select's fade-out was never followed
// by MainScreen reappearing. Fix: compute oldAlpha, decay, and test BOTH
// crossings against that same before/after pair in ONE place (here).
// ---------------------------------------------------------------------------
void GameModeScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    const float f = dtSeconds * 60.0f;

    switch (m_State) {
    case 2:
        // v1.6.1 GameModeScreen::Update @0x001827d0 state 2 (idle): alpha settle-to-1
        // lerp + m_SecondaryAlpha lerp-to-1 (rate 0.25, clamp +/-0.1). m_ButtonDelay
        // countdown stays in Update() (input/state-machine logic, not visual).
        if (m_TransitionAlpha < ALPHA_IN_DONE) {
            GMS_APPROACH_F(m_TransitionAlpha, 1.0f, ALPHA_LERP_STEP);
        } else {
            m_TransitionAlpha = 1.0f;
        }
        {
            float step = (1.0f - m_SecondaryAlpha) * (1.0f - powf(1.0f - SECONDARY_RATE, f));
            if (step >  SECONDARY_CLAMP) step =  SECONDARY_CLAMP;
            if (step < -SECONDARY_CLAMP) step = -SECONDARY_CLAMP;
            m_SecondaryAlpha += step;
        }
        break;

    case 0xf: {
        // v1.6.1 GameModeScreen::Update @0x001827d0 case 0xf (back-out), moved WHOLE
        // (see the safety-rule comment above the function and rule B/C at the top
        // of this file's task spec) -- decay, m_SecondaryAlpha mirror, and BOTH
        // threshold crossings computed from the SAME before/after pair, atomically.
        float oldAlpha = m_TransitionAlpha;
        GMS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY_BACK);
        m_SecondaryAlpha = m_TransitionAlpha;

        if (oldAlpha > 0.25f && m_TransitionAlpha <= 0.25f) {
            if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_SLIDE_IN);
        }
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            m_bPendingRemoval = 1;
        }
        break;
    }

    default:
        // States 3-6 (camera-gated NewGame determinism) and others: no alpha
        // easing moved here -- they stay entirely in Update() at 60Hz.
        break;
    }
}
#endif

// ===================================================================
// Matches GameModeScreen::DrawConnectTexture @ 0x0013f754
// Binary: P2P-only animation (matchmaker connection indicator).
// Guards on isP2PSupported flag — port has no P2P, always no-op.
// Texture at g_instance+0x34 (primary) / +0x38 (alt) — NOT zen_sign.
// ===================================================================
void GameModeScreen::DrawConnectTexture(_Vector3<float> pos) {
    (void)pos;
    // Port: no P2P network support — skip entirely.
    // Binary: if (m_FrameTimer <= 0 || !isP2PSupported) return;
}

// ===================================================================
// Matches GameModeScreen::Draw @ 0x00183ac8
// 1. Background panel (mode_sensei.tex) with slide-in from m_TransitionAlpha
// 2. Borders via BaseScreen::DrawBorders (mode_select.tex)
// 3. Connect animation (zen_sign.tex pulsating)
// 4. Logo panel (zen_sign.tex) lerped by m_SecondaryAlpha
// ASM-verified: 2026-07-26T05:43Z v1.6.1 GameModeScreen::Draw @ 0x00183ac8 (general-purpose)
//   0x00183b8c vldr s15,[r5,#0x8c] -> sensei-panel slide reads +0x8c = m_TransitionAlpha
//   0x00183df8 add r2,r5,#0xb4     -> zen-plate lerp scalar reads +0xb4 = m_SecondaryAlpha
//   (m_SecondaryAlpha starts -2.5 and lags: the wooden plate flies in late/springy
//   from far right while the sensei backdrop tracks the fast transition.)
//   No early-return on m_TransitionAlpha in the binary.
// ===================================================================
void GameModeScreen::Draw(float* /*hudScaleRaw*/) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // --- 1. Background panel (mode_sensei.tex) ---
    // Slide factor = m_TransitionAlpha (+0x8c, vldr @0x00183b8c) — the fast
    // screen transition, NOT the lagging m_SecondaryAlpha.
    // Mode_sensei panel — same pattern as DojoScreen's dojo_sensei:
    // bottom-left position (-188, -32) with horizontal slide from left.
    if (s_TexModeSensei.IsValid()) {
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexModeSensei->GetWidth() + 1.0f,
            (float)s_TexModeSensei->GetHeight() + 1.0f,
            1.0f);
        const float slideX = -(float)s_TexModeSensei->GetWidth() * (1.0f - m_TransitionAlpha);
        // DIFFERS: opt-in widescreen -- MapX the bottom-left corner anchor (edge-anchored,
        // same pattern as AboutScreen's about.sensei / DojoScreen's dojo.sensei).
        // Identity when disabled/__bada__.
        mat.GlobalTranslate44(_Vector3<float>(MapX(POS_BG.x, "modeselect.sensei") + slideX, POS_BG.y, POS_BG.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexModeSensei->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexModeSensei->UnSet();
    }

    // --- 2. Borders (BaseScreen::DrawBorders BakedStringBox* overload, NULL box) ---
    // Binary GameModeScreen::Draw @0x00183c34 calls DrawBorders(nullptr, alpha, (-115,-130,0)).
    // This overload draws shade triangles + sml_title deco only (no secondary texture).
    // DIFFERS: port previously called DrawBorders(s_TexModeSelect,...) which drew mode_select.tex
    // as secondary texture. The binary does NOT draw mode_select.tex in this call path; removing
    // it is binary-faithful. The DrawBorders anchor is used to position m_pDescBox below.
    // DIFFERS: opt-in widescreen -- MapX the left-side border/title anchor (edge-anchored,
    // same convention as DojoScreen's dojo.border). This anchor feeds both the border deco
    // AND m_pDescBox ("MODE SELECT" title) below, so wrapping it here keeps both aligned.
    // Identity when disabled/__bada__.
    _Vector3<float> anchor = DrawBorders(
        nullptr, m_TransitionAlpha,
        _Vector3<float>(MapX(POS_BORDER.x, "modeselect.title"), POS_BORDER.y, POS_BORDER.z));

    // --- 3. DescBox positioned from DrawBorders anchor ---
    // Binary @0x00183c34: anchor += (-24, 11, 0); m_pDescBox->SetTranslation(anchor, 1); rot=-7
    if (m_pDescBox) {
        anchor += _Vector3<float>(-24.0f, 11.0f, 0.0f);
        m_pDescBox->SetTranslation(anchor, 1);
        m_pDescBox->Draw(_Vector2<float>(1.0f, 1.0f), -7.0f, 1);
    }

    // --- 4. Connect texture animation ---
    DrawConnectTexture(POS_CONNECT);

    // --- 5. Logo panel (zen_sign.tex — slot 8, NOT mode_sensei) ---
    // Binary DAT_0013fbc0 = 0x76f8 -> BSS slot for zen_sign.tex.
    // Slide-in lerp src + (dst - src) * t with t = m_SecondaryAlpha
    // (+0xb4, add r2,r5,#0xb4 @0x00183df8): starts at -2.5 so the plate
    // begins FAR off-right (past SRC=(314,14,10)) and flies in late and
    // springy, resting at DST=(194,29,10) as t -> 1.
    // DIFFERS: opt-in widescreen -- MapX both lerp endpoints (edge-anchored,
    // right side). This is the wooden mode-description plate; it's a FIXED
    // right-side position independent of the mode-select rings, so it needs
    // its own edge anchor (unlike the ring buttons, which spread
    // proportionally). m_pTitleBox (the rules text) draws relative to
    // logoPos below, so it tracks the plate automatically. Identity when
    // disabled/__bada__.
    if (s_TexZenSign.IsValid()) {
        _Vector3<float> src(MapX(POS_LOGO_SRC.x, "modeselect.plate"), POS_LOGO_SRC.y, POS_LOGO_SRC.z);
        _Vector3<float> dst(MapX(POS_LOGO_DST.x, "modeselect.plate"), POS_LOGO_DST.y, POS_LOGO_DST.z);
        _Vector3<float> logoPos = src + (dst - src) * m_SecondaryAlpha;
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexZenSign->GetWidth() + 1.0f,
            (float)s_TexZenSign->GetHeight() + 1.0f,
            1.0f);
        mat.GlobalTranslate44(logoPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexZenSign->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexZenSign->UnSet();

        // Draw the feature-bullet text over the zen board quad.
        // Binary @0x00183c34 (GameModeScreen::Draw): m_pTitleBox drawn at
        // logoPos + (8, 3, 0), rotation -9 degrees, scale (1,1), centered.
        if (m_pTitleBox) {
            m_pTitleBox->SetTranslation(logoPos + _Vector3<float>(8.0f, 3.0f, 0.0f), 1);
            m_pTitleBox->Draw(_Vector2<float>(1.0f, 1.0f), -9.0f, 1);
        }
    }

    // TODO: confirm m_pInfoBox draw site (MP/challenge path?)
    //   m_pInfoBox is constructed but its Draw site is unconfirmed -- left undrawn.
}

// --- Button callbacks ---

// Matches GameModeScreen::QuitCallback @ 0x0013F5E0.
// Plays "menu-bomb" SFX, sets m_State = 0xF (back-out), detaches back
// button's fruit piece and flings it up-right, then resets tutorial arrow.
void GameModeScreen::QuitCallback() {
#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- block-enter hook (log-only labelling, see
    // tmp/wii/loader-blueprint.md section 2/7). Mode-select "back to menu".
    fn::wii::SetCurrentBlock(fn::wii::RES_BLOCK_MENU);
#endif
    // 1. SFX
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // 2. Enter back-out state
    m_State = 0xf;

    // 3. Fling back button (binary: *(byte*)(piece+0x80) = 1, then random vel).
    // +0x80 aliases Bomb::m_bMovement / Fruit+0x80 (unconfirmed field, no reader).
    // Port omits the Fruit byte write since Fruit+0x80 has no reader; Bomb write kept.
    if (m_pBackButton && m_pBackButton->m_pEntity) {
        Mortar::Entity* e = m_pBackButton->m_pEntity;
        float rx = (float)rand() / (float)RAND_MAX;
        float ry = (float)rand() / (float)RAND_MAX;
        if (e->entityType == 1) {
            static_cast<Bomb*>(e)->m_bMovement = 1;
        }
        e->vel = _Vector3<float>(rx + 5.0f, -ry, 0.0f);
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

#if defined(FN_BLOCK_PRELOAD)
// Task #66 Phase 1 -- see header. game_work.gameMode is set by the picked
// mode's *ModeCallback (ClassicModeCallback=0, ZenModeCallback=3,
// ArcadeModeCallback=2) immediately before m_State enters 3-6, so it's
// already valid by the time Update's case 3-6 body runs this same frame.
// Only used for the disarm side (SetLoadingSymbol(false) in the drain) --
// the arm side now uses the callback's own button field directly (see
// ClassicModeCallback/ZenModeCallback/ArcadeModeCallback) to avoid the
// cross-frame reap race this used to hit when called from Update state 3-6.
MenuButton* GameModeScreen::PickedModeButton() {
    switch (game_work.gameMode) {
    case 0: return m_pClassicButton;
    case 3: return m_pZenButton;
    case 2: return m_pArcadeButton;
    default: return nullptr;
    }
}

// Task #66 -- shared arm helper: begin the INGAME preload work-queue and
// gate input/spinner atomically on the trigger frame, using a button
// pointer guaranteed valid because the caller is that button's own
// *ModeCallback (running inside the button's own Update this frame).
static void ArmModeLoading(GameModeScreen* self, MenuButton* pickedBtn) {
    fn::wii::SetCurrentBlock(fn::wii::RES_BLOCK_INGAME);
    fn::wii::BlockLoader::PreloadBlockBegin(fn::wii::RES_BLOCK_INGAME);
    if (game_work.mHud) game_work.mHud->SetInputModal(self);
    if (pickedBtn) pickedBtn->SetLoadingSymbol(true);
    self->m_bLoading = true;
}
#endif

// vtable[18] @ 0x00181428
void GameModeScreen::SetupLevel() {
#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- block-enter hook (log-only labelling, see
    // tmp/wii/loader-blueprint.md section 2/7). Camera-fade-triggered latch
    // (see m_bSetupLevelFired above) -- the predictive IN-GAME preload point.
    fn::wii::SetCurrentBlock(fn::wii::RES_BLOCK_INGAME);
    // Task #36 Stage 2 -- force-load the INGAME+GAMEOVER mid-block deltas
    // synchronously HERE, behind the camera fade that already covers this
    // moment, instead of letting them lazy-load mid-gameplay / at the
    // gameover pop. See BlockLoader.h; idempotent across repeated level
    // starts.
    fn::wii::BlockLoader::PreloadBlock(fn::wii::RES_BLOCK_INGAME);
#endif
    PrepareForLevelStart();
}

// Matches ClassicModeCallback @ 0x0013dfb4
void GameModeScreen::ClassicModeCallback() {
#if !defined(__bada__)
    m_bSetupLevelFired = false;
#endif
    m_State = 3;
    game_work.gameMode = 0;
#if defined(FN_BLOCK_PRELOAD)
    // Task #66 -- arm here (not Update state 3), while m_pClassicButton is
    // guaranteed valid: this callback runs inside that button's own Update.
    ArmModeLoading(this, m_pClassicButton);
#endif
}

// Matches ZenModeCallback @ 0x0013dffc
void GameModeScreen::ZenModeCallback() {
#if !defined(__bada__)
    m_bSetupLevelFired = false;
#endif
    m_State = 6;
    game_work.gameMode = 3;
#if defined(FN_BLOCK_PRELOAD)
    ArmModeLoading(this, m_pZenButton);
#endif
}

// Matches ArcadeModeCallback @ 0x0013e19c
// Binary: FruitSaveData::AddToTotal("coming_soon", ..., 10) — skipped
void GameModeScreen::ArcadeModeCallback() {
#if !defined(__bada__)
    m_bSetupLevelFired = false;
#endif
    m_State = 5;
    game_work.gameMode = 2;
#if defined(FN_BLOCK_PRELOAD)
    ArmModeLoading(this, m_pArcadeButton);
#endif
}

// Binary @ 0x0013df84 — sets m_bChallenge=true + stores id and data ptr
//                       (entry from challenge-invite flow)
void GameModeScreen::SetIsChallenge(int challengeId, int data) {
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

// Binary @ 0x00183814 (v1.6.1) — clears m_p*Button cache on HUDControl destroy;
//                               online-MP slot (+0xcc) also flings the orphan fruit off-screen.
void GameModeScreen::DeletedMenuButton(HUDControl* ctrl) {
    MenuButton* btn = static_cast<MenuButton*>(ctrl);
    // DIFFERS: port-specific back-button reap; v1.6.1 DeletedMenuButton @0x00183814
    // does not null field_0xa0 (m_pBackButton) -- binary handles only the classic/zen
    // slots and the online-MP slot (+0xcc). Kept: guards against UAF if m_pBackButton
    // is accessed after HUD reaps the button. Binary relies on HUD lifetime ordering
    // the port may not fully replicate.
    if (btn == m_pBackButton)    { m_pBackButton    = nullptr; return; }
    if (btn == m_pClassicButton) { m_pClassicButton = nullptr; return; }
    if (btn == m_pZenButton)     { m_pZenButton     = nullptr; return; }
#if !defined(__bada__)
    // Port-only convenience cache; the binary has no Arcade slot (see the
    // class-layout comment in the header), so there is no binary branch to match.
    if (btn == m_pArcadeButton)  { m_pArcadeButton = nullptr; return; }
#endif
    if (btn == m_pOnlineMpButton) {
        m_pOnlineMpButton = nullptr;
        // TODO: v1.6.1 0x00183814 (GameModeScreen::DeletedMenuButton) -- the +0xcc
        // branch also flings the tracked fruit off-screen (reads +0x18 fruit ptr,
        // writes offscreen pos); port only nulls the pointer. Inert on main: the
        // slot is never populated (Defunct online MP).
    }
}

// Defunct: online MP (Casino) -- no-op stub; v1.6.1 binary @ 0x001810e8 sets gameMode=1, m_State=4 + NetworkManager flag
void GameModeScreen::CasinoModeCallback() {
    game_work.gameMode = 1;
    m_State = 4;
    // Defunct: NetworkManager online-MP flag omitted
}

// Defunct: online MP (Versus) -- no-op stub; v1.6.1 binary @ 0x00181140 sets m_State=7 + alpha=1.0 to enter matchmaker
void GameModeScreen::VersusModeCallback() {
    m_State = 7;
    m_TransitionAlpha = 1.0f;
    // Defunct: matchmaker entry omitted
}

// Defunct: P2P connect -- no-op stub; v1.6.1 binary @ 0x001810dc sets m_State=8 (GameCenter connect)
void GameModeScreen::P2PConnectCallback() {
    m_State = 8;
    // Defunct: GameCenter/P2P connect omitted
}

// Defunct: upsell store handoff -- no-op stub; v1.6.1 binary @ 0x00181290 calls GotoFruitNinjaPage(1,-1) then m_State=0xd
void GameModeScreen::BuyNow() {
    // Defunct: GotoFruitNinjaPage(1,-1) omitted (online upsell)
    // Defunct: m_State=0xd transition omitted (UpsellScreen is Phantom)
}

// Defunct: upsell glue -- UpsellScreen never instantiated; v1.6.1 binary @ 0x001811c8 sets m_State=10 + bumps modeS_p* counters
void GameModeScreen::SwitchToUpsell(int idx) {
    // Binary @ 0x001811c8: FruitSaveData::AddToTotal for the matching
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

// Defunct: upsell return path -- no-op stub; v1.6.1 binary @ 0x001811bc sets m_State=1 (transition-in resume)
void GameModeScreen::UpsellFinished() {
    // Defunct: upsell return path omitted (UpsellScreen is Phantom)
}

// Defunct: online-MP shrink hook -- no-op stub; v1.6.1 binary @ 0x00181160 snapshots fruit pose + zeroes vel/scale
void GameModeScreen::ShrinkedMultiplayerButton() {
    // Defunct: online-MP fruit snapshot omitted
}

// Defunct: online-MP button lifecycle -- no-op stub; v1.6.1 binary @ 0x0018234c grows/shrinks the 4th MenuButton based on connectivity
void GameModeScreen::UpdateOnlineMultiplayerButton(float /*dt*/) {
    // Defunct: online-MP button grow/shrink based on connectivity omitted
}
