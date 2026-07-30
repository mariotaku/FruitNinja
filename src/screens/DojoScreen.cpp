//
// DojoScreen -- secondary menu shown after tapping Dojo button.
// See DojoScreen.h for binary refs.
//
// Defunct: 4 binary symbols with ZERO callsite xrefs -- leftover .o code from
// other Halfbrick build variants (iPhone/iPad Twitter/Facebook + More-Games).
// Not ported.
//   SwitchCallback         @ v1.6.1 DojoScreen::SwitchCallback @0x00169ea8
//   MoreGamesCallback      @ v1.6.1 DojoScreen::MoreGamesCallback @0x00169eec
//   TwitterFacbookButtons  -- TODO: re-verify v1.6.1 address (not present in Bada v1.6.1 binary)
//   SwitchNetworkButton    -- TODO: re-verify v1.6.1 address (not present in Bada v1.6.1 binary)

#include "DojoScreen.h"
#include "debug/Logger.h"
#include "AboutScreen.h"
#include "ShopScreen.h"
#include "MainScreen.h"
#include "game/FruitSaveData.h"
#include "game/ItemManager.h"
#include "game/Social.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/TutorialControl.h"
#include "hud/MenuButton.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/ActorManager.h"
#include "audio/GameSound.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/BakedStringBox.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/_Vector2.h"
#include "math/Colour.h"
#include "math/Random.h"
#include "engine/util/StringTable.h"
#include "render/Layout.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "game/GameWork.h"

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#include "resource/BlockLoader.h"
#endif

// --- Constants (resolved from binary via read_memory) ---

// Transition alpha
static const float ALPHA_LERP_IN       = 0.25f;   // exponential approach step
static const float ALPHA_IN_DONE       = 0.999f;  // DAT_001389dc
static const float ALPHA_DECAY         = 0.75f;   // geometric decay per frame
static const float ALPHA_OUT_DONE      = 0.001f;  // DAT_001389e0

// ---------------------------------------------------------------------------
// Rate-independence macros for m_TransitionAlpha easing (states 0/2/3/4/6).
// Mirrors ShopScreen's SS_APPROACH_F/SS_DECAY_F pattern (see ShopScreen.cpp):
// under __bada__ these expand to the ORIGINAL per-60Hz-tick scalar forms
// (byte-identical to the binary, no powf); under the port the same call sites
// expand to dt-scaled forms using a local `float f` in scope at each use site
// (f = clamp(dtSeconds,0,0.1)*60 in UpdateRealtime()) so f==1 exactly
// reproduces one 60Hz tick's worth of easing.
// ---------------------------------------------------------------------------
#ifdef __bada__
    // v += (to - v) * k  (exponential approach towards `to` by factor k each call)
    #define DS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (k))
    // v *= k  (geometric decay towards zero by factor k each call)
    #define DS_DECAY_F(v, k)         ((v) *= (k))
#else
    #define DS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (1.0f - powf(1.0f - (k), f)))
    #define DS_DECAY_F(v, k)         ((v) *= powf((k), f))
#endif

// ASM-spec v1.6.1 CreateButtons @0x0016ad9c: back=(0,0,0) shop=(-55,65,0) about=(35,-74,0)
// [prior values were stale v1.5.x: back=(185,-106,0) shop=(-18,-15,0) about=(145,42,0)]
static const _Vector3<float> POS_BACK_BUTTON(0.0f, 0.0f, 0.0f);
static const _Vector3<float> POS_SHOP_BUTTON(-55.0f, 65.0f, 0.0f);
static const _Vector3<float> POS_ABOUT_BUTTON(35.0f, -74.0f, 0.0f);

// Button scale multipliers
static const float BACK_SCALE  = 0.825f;  // DAT_00138694
static const float SHOP_SCALE  = 0.575f;  // DAT_001386b4

// Background panel position — dojo_sensei at bottom-left area.
// DAT_001383c4/c8/c0 = (-180, -47, 0).
// Slides in from left: X -= texWidth * (1 - alpha).
static const _Vector3<float> POS_DOJO_BG(-180.0f, -47.0f, 0.0f);

// Helpers
static GLuint TexIdOf(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->GetTexId() : 0;
}

// --- Static texture storage (binary: GOT-relative globals) ---
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexDojo;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexSensei;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexShop;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexAbout;

// ===================================================================
// Matches DojoScreen::DojoScreen @ 0x0016bad8
// ===================================================================
DojoScreen::DojoScreen()
    : m_pBackButton(nullptr)    // +0x94
    , m_pShopButton(nullptr)    // +0x98
    , m_pAboutButton(nullptr)   // +0x9c
    , m_pBSButton0(nullptr)     // +0xa0
    , m_pBSButton1(nullptr)     // +0xa4
    , m_pButton4(nullptr)       // +0xa8
    , m_ResetValue(0)           // +0xac
    , m_TransitionDelay(0.0f)   // +0xb0
    , m_pVersionText(nullptr)   // +0xb4
    , m_pAboutScreen(nullptr)   // port-only tail
#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    , m_bShopLoading(false)
    , m_pPendingShop(nullptr)
#endif
{
    LOG_INFO("SCREEN/DojoScreen", "%s (%s)", "create", "DojoScreen::DojoScreen @ 0x0016bad8");
    LoadContent();
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
    m_bNoDestructor = 0;

    // --- m_pVersionText: BakedStringBox "DOJO" title ---
    // ASM-spec v1.6.1 DojoScreen::DojoScreen @0x0016bad8: operator_new(0xc8), ctor
    //   (font=*(g_GameData+0x614), fontSize=30, w=110, h=30, align=0xf, maxLines=1, lineSpacing=0),
    //   SetGradient(Colour(1,146,208), Colour(0,26,69), 0), SetText(GETSTRING(0x397,0)),
    //   SetHorizontalLineSpacing(-1). Drawn in Draw() at DrawBorders anchor + Vec3(0,5,0).
    m_pVersionText = new Mortar::BakedStringBox(
        game_work.m_pTTFFontMain,
        30.0f, 110.0f, 30.0f,
        (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
    m_pVersionText->SetGradient(Colour(1, 146, 208, 255), Colour(0, 26, 69, 255), false);
    m_pVersionText->SetText(GETSTRING_CAST_0(LSTR_DOJO_TITLE));
    m_pVersionText->SetHorizontalLineSpacing(-1);

    // --- m_pBSButton0: Facebook defunct visible stub ---
    // ASM-spec v1.6.1 DojoScreen::DojoScreen @0x0016bad8: operator_new(0xe8),
    //   BSButton(Vec3(152,100,0), GETSTRING(0x11e,0), Vec3(1,1,1)), Init(),
    //   SetCallback(FacebookPressed) -- binary binds the FREE function via a
    //   free-function Delegate0<void> (MakeFree), not a bound member delegate.
    //   SetTexture("join_fb.tex", true),
    //   SetTextOffset(Vec3(-70,20,0)), m_pLabelBox->SetColour(0xef,0xf7,0xff),
    //   SetStroke, SetFontSize(14), ReshapeBounds(0x78,0x16,1,0),
    //   m_DrawRotation.x=-7.17, HUD::AddControl(hud, btn, false).
    // HLE screenshot: FB ("become a fan") is the UPPER button; TW is below.
    {
        BSButton* btn = new BSButton(
            _Vector3<float>(152.0f, 100.0f, 0.0f),
            GETSTRING_CAST_0(LSTR_SOCIAL_FACEBOOK),
            _Vector3<float>(1.0f, 1.0f, 1.0f));
        btn->Init();
        btn->SetCallback(Mortar::Delegate0<void>::MakeFree(&FacebookPressed));
        btn->SetTexture(Mortar::TextureManager::LoadLocalisedTexture("join_fb.tex"), true);
        btn->SetTextOffset(_Vector3<float>(-70.0f, 20.0f, 0.0f));
        if (btn->m_pLabelBox) {
            btn->m_pLabelBox->SetColour(Colour(0xef, 0xf7, 0xff, 0xff), 0);
            // TODO: v1.6.1 DojoScreen::DojoScreen @0x0016bad8 -- verify stroke width (1.0f assumed)
            btn->m_pLabelBox->SetStroke(1.0f,
                Colour(0x21, 0x3c, 0x84, 0xff),
                Colour(0x4a, 0x6d, 0xb5, 0xff));
            btn->m_pLabelBox->SetFontSize(14.0f);
            btn->m_pLabelBox->ReshapeBounds(0x78, 0x16, 1, 0);
        }
        btn->m_DrawRotation.x = -7.17f;
        m_pBSButton0 = btn;
        if (game_work.mHud) game_work.mHud->AddControl(btn, false);
    }

    // --- m_pBSButton1: Twitter defunct visible stub ---
    // ASM-spec v1.6.1 DojoScreen::DojoScreen @0x0016bad8: same pattern as FB,
    //   GETSTRING(0x11f,0), "join_tw.tex", stroke (0x31,0xae,0xd6)/(0x52,0xba,0xde).
    // Ctor builds both at (152,100,0); UpdateBSButton slides each to its final
    //   per-frame position (FB idx=0 -> y=100, TW idx=1 -> y=54) on every Update frame.
    {
        BSButton* btn = new BSButton(
            _Vector3<float>(152.0f, 100.0f, 0.0f),
            GETSTRING_CAST_0(LSTR_SOCIAL_TWITTER),
            _Vector3<float>(1.0f, 1.0f, 1.0f));
        btn->Init();
        btn->SetCallback(Mortar::Delegate0<void>::MakeFree(&TwitterPressed));
        btn->SetTexture(Mortar::TextureManager::LoadLocalisedTexture("join_tw.tex"), true);
        btn->SetTextOffset(_Vector3<float>(-70.0f, 20.0f, 0.0f));
        if (btn->m_pLabelBox) {
            btn->m_pLabelBox->SetColour(Colour(0xef, 0xf7, 0xff, 0xff), 0);
            // TODO: v1.6.1 DojoScreen::DojoScreen @0x0016bad8 -- verify stroke width (1.0f assumed)
            btn->m_pLabelBox->SetStroke(1.0f,
                Colour(0x31, 0xae, 0xd6, 0xff),
                Colour(0x52, 0xba, 0xde, 0xff));
            btn->m_pLabelBox->SetFontSize(14.0f);
            btn->m_pLabelBox->ReshapeBounds(0x78, 0x16, 1, 0);
        }
        btn->m_DrawRotation.x = -7.17f;
        m_pBSButton1 = btn;
        if (game_work.mHud) game_work.mHud->AddControl(btn, false);
    }
}

// ===================================================================
// Matches DojoScreen::~DojoScreen @ 0x0016c904 (v1.6.1)
// Binary: set vtable, call Release(), call ~BaseScreen()
// ===================================================================
DojoScreen::~DojoScreen() {
    LOG_INFO("SCREEN/DojoScreen", "%s (%s)", "destroy", "DojoScreen::~DojoScreen");
#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 2 refinement -- teardown safety if the dojo is destroyed
    // mid-hold (queue not drained yet). Mirrors GameModeScreen::~GameModeScreen
    // (Phase 1) / ShopScreen::~ShopScreen (Phase 2).
    if (m_bShopLoading) fn::wii::BlockLoader::Reset();
    delete m_pPendingShop;
#endif
    Release();
    // ~BaseScreen() called implicitly by ~HUDControl3d chain
}

// ===================================================================
// Matches DojoScreen::LoadContent @ 0x0016a554 (v1.6.1)
// ===================================================================
void DojoScreen::LoadContent() {
    // +0x08: loading.tex — skipped (not used in Draw)
    s_TexDojo   = Mortar::TextureManager::LoadLocalisedTexture("dojo.tex");         // +0x0c
    s_TexSensei = Mortar::TextureManager::LoadLocalisedTexture("dojo_sensei.tex");  // +0x10
    BaseScreen::LoadContent();  // loads sml_title.tex + blurry_backing.tex (slots +0x00, +0x04)
    s_TexShop   = Mortar::TextureManager::LoadLocalisedTexture("senseis_swag.tex"); // +0x14
    s_TexAbout  = Mortar::TextureManager::LoadLocalisedTexture("about.tex");        // +0x18
}

// ===================================================================
// Matches DojoScreen::UnLoadContent @ 0x0016c788 (v1.6.1)
// ===================================================================
void DojoScreen::UnLoadContent() {
    BaseScreen::UnloadContent();  // releases sml_title + blurry_backing
    s_TexSensei.SetNull();
    s_TexDojo.SetNull();
    s_TexShop.SetNull();
    s_TexAbout.SetNull();
}

// ===================================================================
// Matches DojoScreen::Init @ 0x00169e80 (vtable slot +0x08)
// ASM-spec v1.6.1 DojoScreen::Init @0x00169e80: sets state=0, alpha=0, active=1,
// then calls Reset() which calls CreateButtons(). This is the initial activation
// path (called by MainScreen before AddControl). Reset() handles re-activation
// (called by AboutScreen when returning). Both paths converge on CreateButtons().
// ===================================================================
void DojoScreen::Init() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 0, "Init @ 0x00169e80");
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_Active = 1;
    // ASM-spec v1.6.1 DojoScreen::Init @0x00169e80: calls Reset() -> CreateButtons()
    Reset();
}

// Matches DojoScreen::Reset @ 0x0016b568 (vtable slot +0x10).
// Binary: writes m_State=0, then calls CreateButtons() (v1.6.1 confirmed;
// CreateButtons has per-pointer null guards so re-entry is safe).
void DojoScreen::Reset() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 0, "Reset @ 0x0016b568");
    m_State = 0;
    // ASM-spec v1.6.1 DojoScreen::Reset @0x0016b568: sets m_TransitionDelay=0.2f before CreateButtons.
    m_TransitionDelay = 0.2f;
    CreateButtons();
}

// ===================================================================
// HUDControl::Release override
// Matches DojoScreen::Release @ 0x0016c7f8
// Binary: calls BaseScreen::RemoveButtons() then cleans up fields at +0xa0..+0xb4
// (BSButton0/1, Button4, ResetValue, TransitionDelay, VersionText).
// The +0x94..+0x9c button fields are NOT touched here -- handled by HUD/base dtor.
// ===================================================================
void DojoScreen::Release() {
    BaseScreen::RemoveButtons();

    // Binary cleans up BSButton0/1 (+0xa0, +0xa4): mark pending removal from HUD.
    if (m_pBSButton0) {
        m_pBSButton0->m_bPendingRemoval = 1;
        m_pBSButton0 = nullptr;
    }
    if (m_pBSButton1) {
        m_pBSButton1->m_bPendingRemoval = 1;
        m_pBSButton1 = nullptr;
    }

    // Binary cleans up VersionText (+0xb4): delete the BakedStringBox.
    if (m_pVersionText) {
        delete m_pVersionText;
        m_pVersionText = nullptr;
    }

    // Port-only: clean up AboutScreen child pointer.
    if (m_pAboutScreen) {
        m_pAboutScreen->SetPendingRemoval();
        m_pAboutScreen = nullptr;
    }
}

// ===================================================================
// Matches DojoScreen::ButtonDeleted @ 0x00169e94 (v1.6.1)
// Binary nulls only m_pShopButton; no RemoveCallback installed for back/about.
// ===================================================================
void DojoScreen::ButtonDeleted(HUDControl* ctrl) {
    // v1.6.1 DojoScreen::ButtonDeleted @0x00169e94: nulls only m_pShopButton.
    if (ctrl == (HUDControl*)m_pShopButton)  m_pShopButton  = nullptr;
}

// ===================================================================
// Matches DojoScreen::CreateButtons @ 0x0016ad9c
// Creates m_pBackButton, m_pShopButton, m_pAboutButton with per-pointer
// null guards. Called from Reset() (v1.6.1 binary pattern).
// ===================================================================
void DojoScreen::CreateButtons() {
    if (!game_work.mHud) return;

    // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: button m_Texture = generic ring
    //   game_work.m_RingTex[16/7/12]; baked swag/about tex loaded-but-not-drawn;
    //   label via SetText; gradient m_RingColours back[0,1]/shop[6,7]/about[10,11].

    // --- field_0x94: Back/Play button ---
    // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: bomb type, pos=(0,0,0),
    //   m_Texture=m_RingTex[16] (red_ring), m_HudScale.x=0.375, m_HudScale.y=-0.3,
    //   m_RestScale*=0.825, m_bBackdropActive=1. (Corrected #84: this block's only
    //   byte store is m_bBackdropActive, not m_bRespondsToBackKey -- see the
    //   Port specific marker on the m_bRespondsToBackKey write below.)
    if (m_pBackButton == nullptr) {
        const int bombFruitType = g_FruitInfoCount;
        m_pBackButton = new MenuButton();
        m_pBackButton->m_Texture = game_work.m_RingTex[16];
        // DIFFERS: opt-in widescreen -- this is the SAME red bomb back/quit
        // button pattern as MainScreen::CreateQuitButton (m_RingTex[16],
        // back-key responder, LSTR_DJ_BACK_BUTTON label). pos=(0,0,0) here is
        // a red herring: GetAdjustedPos() = pos + 480*m_HudScale.x, and
        // m_HudScale.x=0.375 below puts the true on-screen x at 180 (right
        // edge), NOT screen-centered. Back/quit buttons edge-anchor
        // universally -- MapX the pre-scale x and re-derive the HudScale
        // fraction (see MainScreen::CreateQuitButton for the same recipe).
        m_pBackButton->Init(POS_BACK_BUTTON,
                            Mortar::Delegate0<void>::Make(this, &DojoScreen::PlayCallback),
                            bombFruitType, _Vector3<float>(0, 0, 0), nullptr);
        // Port specific: no binary counterpart at this call site (v1.6.1
        // CreateButtons @0x0016ad9c -- disassembly's only byte store here is
        // m_bBackdropActive, #84). Sets MenuButton's back-key-responder flag,
        // which also gates MenuButton::Update's touch-slot-retention-on-drag-off
        // behaviour (MenuButton.cpp ~858-863). Why the port adds it here is not
        // established -- desktop ESC-as-back (GameSDL.cpp) keys off
        // m_bBackdropActive only, not this field, so that isn't the reason.
        m_pBackButton->m_bRespondsToBackKey = 1;
        m_pBackButton->m_bBackdropActive = 1; // v1.6.1 DojoScreen::CreateButtons @0x0016afe8
        m_pBackButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_HudScale.x=0.375, m_HudScale.y=-0.3
        m_pBackButton->m_HudScale.x = MapX(180.0f, "dojo.btn.back") / 480.0f;
        m_pBackButton->m_HudScale.y = -0.3f;
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: field_0x13c(m_RestScale)*=0.825
        m_pBackButton->m_RestScale = m_pBackButton->m_RestScale * BACK_SCALE;
        // v1.6.1 CreateButtons @0x0016ad9c: m_pTrackedFruit->m_LaunchVelocity *= 0.825.
        //   m_LaunchVelocity IS Entity::scale (+0x28), so this is the tracked-fruit scale *= BACK_SCALE.
        if (m_pBackButton->m_pTrackedFruit) {
            m_pBackButton->m_pTrackedFruit->scale =
                m_pBackButton->m_pTrackedFruit->scale * BACK_SCALE;
        }
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: SetText ring label, gradient [0],[1]
        m_pBackButton->SetText(
            GETSTRING_CAST_0(LSTR_DJ_BACK_BUTTON),
            game_work.m_RingColours[0],
            game_work.m_RingColours[1],
            31.0f, 10.0f, true, true);
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_GrowInTimer=0.25 before AddControl
        m_pBackButton->m_GrowInTimer = 0.25f;
        game_work.mHud->AddControl(m_pBackButton);
        if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos(m_pBackButton);
    }

    // --- field_0x98: Shop button (pineapple ring, m_RingTex[7]) ---
    if (m_pShopButton == nullptr) {
        const int shopFruitType = Fruit::FruitType("pineapple", false);
        m_pShopButton = new MenuButton();
        m_pShopButton->m_Texture = game_work.m_RingTex[7];
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo convention).
        m_pShopButton->Init(_Vector3<float>(MapX(POS_SHOP_BUTTON.x, "dojo.btn.shop"), POS_SHOP_BUTTON.y, POS_SHOP_BUTTON.z),
                            Mortar::Delegate0<void>::Make(this, &DojoScreen::ShopCallback),
                            shopFruitType, _Vector3<float>(0, 0, 0), nullptr);
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_RestScale=(texW+1,texH+1,1)
        if (m_pShopButton->m_Texture.IsValid()) {
            m_pShopButton->m_RestScale = _Vector3<float>(
                (float)m_pShopButton->m_Texture->GetWidth() + 1.0f,
                (float)m_pShopButton->m_Texture->GetHeight() + 1.0f,
                1.0f);
        }
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_ShakeScale.x=0.5, y*=0.575, z*=0.575, y=-y
        m_pShopButton->m_ShakeScale.x = 0.5f;
        m_pShopButton->m_ShakeScale.y *= 0.575f;
        m_pShopButton->m_ShakeScale.z *= 0.575f;
        m_pShopButton->m_ShakeScale.y = -m_pShopButton->m_ShakeScale.y;
        // TODO: v1.6.1 DojoScreen::CreateButtons @0x0016ad9c -- confirm X+Y inset exact value
        m_pShopButton->m_HitInsetX   = -15.0f;
        m_pShopButton->m_HitInsetY   = -15.0f;
        m_pShopButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        m_pShopButton->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &DojoScreen::ButtonDeleted);
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: SetText ring label, gradient [6],[7]
        m_pShopButton->SetText(
            GETSTRING_CAST_0(LSTR_DJ_SHOP_BUTTON),
            game_work.m_RingColours[6],
            game_work.m_RingColours[7],
            54.5f, 14.0f, true, true);
        // ASM-spec v1.6.1 DojoScreen::CreateButtons @0x0016ad9c (call @0x0016b334):
        // badge set from AreNewItems() AT CREATION so it grows in with the button's
        // m_AnimPhase sine ramp (badge scale = size.x/m_RestScale.x in MenuButton::Draw).
        m_pShopButton->SetNewSymbol(ItemManager::GetInstance()->AreNewItems());
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_GrowInTimer=0.25 before AddControl
        m_pShopButton->m_GrowInTimer = 0.25f;
        game_work.mHud->AddControl(m_pShopButton);
        if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos(m_pShopButton);
    }

    // --- field_0x9c: About button (orange ring, m_RingTex[12]) ---
    if (m_pAboutButton == nullptr) {
        const int aboutFruitType = Fruit::FruitType("plum", false);
        m_pAboutButton = new MenuButton();
        m_pAboutButton->m_Texture = game_work.m_RingTex[12];
        // DIFFERS: opt-in widescreen -- MapX the ring-button anchor (proportional,
        // matching MainScreen's menu.play/menu.dojo convention).
        m_pAboutButton->Init(_Vector3<float>(MapX(POS_ABOUT_BUTTON.x, "dojo.btn.about"), POS_ABOUT_BUTTON.y, POS_ABOUT_BUTTON.z),
                             Mortar::Delegate0<void>::Make(this, &DojoScreen::AboutCallback),
                             aboutFruitType, _Vector3<float>(0, 0, 0), nullptr);
        m_pAboutButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: SetText ring label, gradient [10],[11]
        m_pAboutButton->SetText(
            GETSTRING_CAST_0(LSTR_ABOUT_TITLE),
            game_work.m_RingColours[10],
            game_work.m_RingColours[11],
            39.5f, 10.0f, true, true);
        // ASM-spec v1.6.1 CreateButtons @0x0016ad9c: m_GrowInTimer=0.25 before AddControl
        m_pAboutButton->m_GrowInTimer = 0.25f;
        game_work.mHud->AddControl(m_pAboutButton);
    }
}

// ===================================================================
// Matches DojoScreen::Update @ 0x0016b6a4
// ASM-verified: 2026-07-15T00:00Z v1.6.1 DojoScreen::Update @ 0x0016b6a4..0x0016b968 (asm-inspector)
// ===================================================================
void DojoScreen::Update(float dt) {
#ifndef __bada__
    // DIFFERS: v1.6.1 DojoScreen::Update @0x0016b6a4 eases m_TransitionAlpha per
    // 60Hz sim tick; port eases it per rendered frame (dt-scaled) so the
    // transition tracks display refresh. __bada__ keeps the faithful 60Hz path.
    // The easing itself has already been advanced by UpdateRealtime() (called
    // once per presented frame via HUD::UpdateRealtime); this 60Hz Update only
    // reads the current m_TransitionAlpha value to fire the (rate-independent,
    // threshold-based) state transitions and one-shot side effects below.
#endif

    // ASM-spec v1.6.1 DojoScreen::Update @0x0016b6a4
    BaseScreen::UpdateButtons(dt);
#ifdef __bada__
    // ASM-spec v1.6.1 DojoScreen::UpdateBSButtons @0x0016b580: called every frame.
    UpdateBSButtons(dt);
#endif

    switch (m_State) {

    // ---- STATE 0: Fade in ----
    // Binary: alpha lerp only. CreateButtons() is called from Reset(), not here.
    case 0: {
        // Exponential approach: alpha += (1 - alpha) * 0.25
#ifdef __bada__
        DS_APPROACH_F(m_TransitionAlpha, 1.0f, ALPHA_LERP_IN);
#endif
        // Port: easing already advanced by UpdateRealtime() (per-present, dt-scaled);
        // this 60Hz tick only reads the current value to fire the state transition.

        // Transition to state 1 when fully faded in
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 1, "Update/state-0 alpha settled");
            m_State = 1;
        }
        break;
    }

    // ---- STATE 1: Idle ----
    case 1:
        // v1.6.1 DojoScreen::Update @0x0016b6a4 case 1: poll ItemManager::AreNewItems()
        // each frame for the shop badge, then return immediately (skips shared tail).
        if (m_pShopButton) {
            m_pShopButton->SetNewSymbol(ItemManager::GetInstance()->AreNewItems());
            return;
        }
        break;

    // ---- STATES 2, 3, 4: Fade out → sub-screen ----
    // ASM-spec v1.6.1 DojoScreen::Update states 2/3/4 @0x0016b778: alpha decays each frame;
    //   entity-count gate (bombs AND fruit gone) + m_TransitionDelay countdown before pushing
    //   child screen. Prevents spawning the new back ring while the outgoing menu bomb
    //   still occupies the pool (new ring Add(1)=null -> no entity -> shrinks out).
    case 2:
    case 3:
    case 4: {
#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
        // Task #66 Phase 2 refinement -- while the SHOP work-queue drains,
        // hold the dojo panel at its covering alpha (skip DS_DECAY_F below)
        // and keep the spinner on the sliced shop-ring button armed. Mirrors
        // GameModeScreen's m_bLoading hold (Phase 1): `break` here skips this
        // frame's decay entirely, same as GameModeScreen's `break` before its
        // decay line.
        if (m_bShopLoading) {
            if (!fn::wii::BlockLoader::PreloadBlockStep(1)) break;  // still loading -- hold + keep spinner

            if (m_pShopButton) m_pShopButton->SetLoadingSymbol(false);
            if (game_work.mHud) game_work.mHud->SetInputModal(nullptr);

            // Reveal: original state-2 completion, deferred until the queue
            // drained. Binary: HUD::AddControl(hud, shop, false); shop->Init();
            m_pBackButton  = nullptr;  // field_0x94
            m_pShopButton  = nullptr;  // field_0x98
            m_pAboutButton = nullptr;  // field_0x9c
            m_TransitionAlpha = 0.0f;
            m_bShopLoading = false;

            ShopScreen* shop = m_pPendingShop;
            m_pPendingShop = nullptr;
            if (shop) {
                game_work.mHud->AddControl(shop, false);
                shop->Init();
            }
            return;
        }
#endif
#ifdef __bada__
        DS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.

        {
            Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
            bool cleared = (am && am->GetNumEntities(1) == 0 && am->GetNumEntities(0) == 0);
            if (cleared) m_TransitionDelay -= dt;
            // v1.6.1 DojoScreen::Update @0x0016b6a4: one-shot latch = (m_pBackButton != NULL), compare-only
            // (pointer dangles after the back button self-removes ~0.15s into the fade; never dereferenced here).
            if (cleared && m_pBackButton != nullptr && m_TransitionDelay <= 0.0f) {
                int prevState = m_State;

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
                if (prevState == 2) {
                    // State 2 (SHOP), preload build: create the ShopScreen now
                    // (cheap ctor -- chrome LoadContent moved to the async
                    // queue, see ShopScreen.cpp) but do NOT AddControl/Init or
                    // reveal yet -- keep the dojo covering the screen (alpha
                    // NOT zeroed, buttons NOT nulled) while the SHOP queue
                    // drains above, same as game-mode holds its panel.
                    if (game_work.m_SaveData) game_work.m_SaveData->CheckDatesHaveChanged();
                    m_pPendingShop = new ShopScreen(this);
                    fn::wii::BlockLoader::PreloadBlockBegin(fn::wii::RES_BLOCK_SHOP);
                    if (game_work.mHud) game_work.mHud->SetInputModal(this);
                    if (m_pShopButton) m_pShopButton->SetLoadingSymbol(true);
                    m_bShopLoading = true;
                    break;  // hold this frame -- skip decay, wait for drain above
                }
#endif

                m_pBackButton  = nullptr;  // field_0x94
                m_TransitionAlpha = 0.0f;
                m_pShopButton  = nullptr;  // field_0x98
                m_pAboutButton = nullptr;  // field_0x9c

                if (prevState == 3) {
                    // State 3: push AboutScreen.
                    // Binary: operator_new(0xa0), AboutScreen ctor, call vtable[2]=Init,
                    //         HUD::AddControl. No RemoveCallback installed in binary.
                    m_pAboutScreen = new AboutScreen(this);
                    m_pAboutScreen->Init();
                    // Port-specific: install RemoveCallback so DojoScreen clears its ptr.
                    m_pAboutScreen->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &DojoScreen::AboutScreenRemoved);
                    game_work.mHud->AddControl(m_pAboutScreen);
                    return;
                }

#if !defined(FN_BLOCK_PRELOAD)
                if (prevState == 2) {
                    // State 2: push ShopScreen.
                    // Binary: FruitSaveData::CheckDatesHaveChanged(game->save), then
                    //   ShopScreen* shop = operator_new(0xbc); ShopScreen::ShopScreen(shop, this);
                    //   HUD::AddControl(hud, shop, false); shop->Init();
                    if (game_work.m_SaveData) game_work.m_SaveData->CheckDatesHaveChanged();
                    ShopScreen* shop = new ShopScreen(this);
                    game_work.mHud->AddControl(shop, false);
                    shop->Init();
                    return;
                }
#endif

                if (prevState == 4) {
                    // Defunct: NetworkManager dashboard -- state 4 unreachable on Bada.
                    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(prevState), 0, "Update/state-4 defunct");
                    m_State = 0;
                    return;
                }
            }
        }
        break;
    }

    // ---- STATE 6: Back/quit → MainScreen ----
    case 6: {
#ifdef __bada__
        DS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.

        // Binary: if (alpha < 0.001) → mark for removal
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            LOG_INFO("SCREEN/DojoScreen", "%s (%s)", "pending-removal", "state-6 alpha faded");
            m_bPendingRemoval = 1;
            // Binary: *(game_work.mMainScreen + 0x10c) = 8
            // 0x10c into MainScreen is m_State = STATE_SLIDE_IN (8)
            if (game_work.mMainScreen) {
                game_work.mMainScreen->SetState(STATE_SLIDE_IN);
            }
        }
        break;
    }

    default:
        break;
    }
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// the state-machine split comment above Update(). Advances m_TransitionAlpha
// per PRESENTED frame (dt-scaled via DS_APPROACH_F/DS_DECAY_F, defined near
// the top of this file), for whichever of states 0/2/3/4/6 is currently
// active, and repositions the (defunct, visible-stub) BSButtons via
// UpdateBSButtons -- a pure-visual slide that reads m_TransitionAlpha only
// and never changes state. Update() (60Hz) reads the resulting alpha value
// to fire the (already rate-independent, threshold-based) state transitions
// and one-shot side effects (entity-clear gate, m_TransitionDelay countdown,
// AddControl of the child screen, state-6 pending-removal +
// MainScreen::SetState) -- those stay in Update() exactly like ShopScreen
// keeps its equivalent one-shot side effects in Update() rather than here.
//
// Under __bada__ this function does not exist (see DojoScreen.h); Update()
// eases m_TransitionAlpha inline per-state and calls UpdateBSButtons
// directly, byte-identical to the binary.
//
// DIFFERS: v1.6.1 DojoScreen::Update @0x0016b6a4 eases m_TransitionAlpha per
// 60Hz sim tick; port eases it per rendered frame (dt-scaled) so the
// transition tracks display refresh. __bada__ keeps the faithful 60Hz path.
// ---------------------------------------------------------------------------
void DojoScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    const float f = dtSeconds * 60.0f;

    // ASM-spec v1.6.1 DojoScreen::UpdateBSButtons @0x0016b580: called every frame.
    UpdateBSButtons(dtSeconds);

    switch (m_State) {
    case 0:
        // Binary: alpha += (1 - alpha) * 0.25
        DS_APPROACH_F(m_TransitionAlpha, 1.0f, ALPHA_LERP_IN);
        break;
    case 2:
    case 3:
    case 4:
        // Binary: alpha *= 0.75
        DS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
        break;
    case 6:
        // Binary: alpha *= 0.75
        DS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
        break;
    default:
        // States 1 (idle) and others: no alpha easing in the binary's Update either.
        break;
    }
}
#endif

// ===================================================================
// Matches DojoScreen::Draw @ 0x0016a004
// ===================================================================
void DojoScreen::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
    (void)hudScale;
    if (m_TransitionAlpha <= 0.0f) return;

    // --- Block A: dojo_sensei.tex (slot +0x10) — main panel (128x256) ---
    // Slides in from left (horizontal slide): X -= texW * (1 - alpha).
    if (s_TexSensei.IsValid()) {
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexSensei->GetWidth() + 1.0f,
            (float)s_TexSensei->GetHeight() + 1.0f,
            1.0f);
        const float slideX = -(float)s_TexSensei->GetWidth() * (1.0f - m_TransitionAlpha);
        // DIFFERS: opt-in widescreen -- MapX the bottom-left corner anchor (edge-anchored,
        // same pattern as AboutScreen's about.sensei). Identity when disabled/__bada__.
        mat.GlobalTranslate44(_Vector3<float>(MapX(POS_DOJO_BG.x, "dojo.sensei") + slideX, POS_DOJO_BG.y, POS_DOJO_BG.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexSensei->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexSensei->UnSet();
    }

    // --- Block B: DrawBorders (shade triangles + sml_title) + version text ---
    // Binary DojoScreen::Draw @0x0016a004: calls DrawBorders(BakedStringBox* overload)
    //   with pos=(-184,-141,0) and uses the RETURNED Vec3 as title anchor.
    //   dojo.tex is vestigial in v1.6.1 (size-read only, not drawn here).
    // Port: use BakedStringBox* overload (no secondary texture). Pass nullptr
    //   to get the anchor back; draw m_pVersionText separately at anchor+(0,5,0).
    {
        // DIFFERS: opt-in widescreen -- MapX the bottom-left corner anchor (edge-anchored,
        // same corner as dojo.sensei above). Identity when disabled/__bada__.
        const _Vector3<float> BORDER_POS(MapX(-184.0f, "dojo.border"), -141.0f, 0.0f);
        _Vector3<float> titlePos = DrawBorders(
            static_cast<Mortar::BakedStringBox*>(nullptr),
            m_TransitionAlpha,
            BORDER_POS);

        // Draw "DOJO" version text at title anchor + Vec3(0,5,0).
        if (m_pVersionText) {
            titlePos += _Vector3<float>(0.0f, 5.0f, 0.0f);
            m_pVersionText->SetTranslation(titlePos, 1);
            m_pVersionText->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
        }
    }
}

// ===================================================================
// Matches DojoScreen::QuitCallback @ 0x0016b980 (v1.6.1; bound to the back/play button)
// Binary: SFXPlay("menu-bomb"), state=6, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::PlayCallback() {
    // DIFFERS: MSVC memory-safety guard -- binary v1.6.1 DojoScreen::QuitCallback @0x0016b980 has no guard;
    // MSVC can reuse the dangling m_pBackButton and deref its m_pTrackedFruit (UAF).
    // Guard prevents the crash the binary's allocator layout avoids.
    if (m_State != 0 && m_State != 1) return;
    // 1. SFX
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // 2. State 6 (quit fade-out)
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 6, "PlayCallback @ 0x0016b980");
    m_State = 6;

    // 3. Fling the back-bomb with random rightward velocity.
    // ASM-spec v1.6.1 DojoScreen::QuitCallback @0x0016b980 (outlined helper
    // T.1153 @0x00169fb4): Math::g_random.RandF(5.0) x2, GATED -- unlike its
    // Shop/About siblings this one keeps the binary's own null checks.
    if (m_pBackButton && m_pBackButton->m_pTrackedFruit) {
        Fruit* piece = m_pBackButton->m_pTrackedFruit;
        const float r1 = Math::g_Random.RandF(5.0f);
        const float r2 = Math::g_Random.RandF(5.0f);
        // ASM-spec v1.6.1 *Callback (AboutScreen::QuitGameCallback @0x0015c914 strb [+0x80];
        //   Dojo Shop/About/Play via T.1166 @0x0016a3ec): enable bomb physics so gravity +
        //   AccelGrowth fling the back-bomb off-screen deterministically -> KillBomb ->
        //   ActorManager reap before the next screen re-creates its single-slot menu bomb.
        //   Omitting it => constant-velocity drift => race => pool stays full => soft-lock.
        reinterpret_cast<Bomb*>(piece)->m_bMovement = 1;   // Bomb+0x80
        piece->vel = _Vector3<float>(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
}

// ===================================================================
// Matches DojoScreen::ShopCallback @ 0x0016a3f8 (v1.6.1)
// Binary: state=2, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::ShopCallback() {
    // DIFFERS: MSVC memory-safety guard -- binary v1.6.1 DojoScreen::ShopCallback @0x0016a3f8 has no guard;
    // MSVC can reuse the dangling m_pBackButton and deref its m_pTrackedFruit (UAF).
    // Guard prevents the crash the binary's allocator layout avoids.
    if (m_State != 0 && m_State != 1) return;
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 2, "ShopCallback @ 0x00137864");
    m_State = 2;

    // ASM-spec v1.6.1 DojoScreen::ShopCallback @0x0016a3f8 (outlined helper
    // T.1153 @0x00169fb4): Math::g_random.RandF(5.0) x2, UNGATED -- the binary
    // has no null checks here, so the draws stay outside the port's guard.
    const float r1 = Math::g_Random.RandF(5.0f);
    const float r2 = Math::g_Random.RandF(5.0f);
    if (m_pBackButton && m_pBackButton->m_pTrackedFruit) {
        Fruit* piece = m_pBackButton->m_pTrackedFruit;
        // ASM-spec v1.6.1 *Callback (AboutScreen::QuitGameCallback @0x0015c914 strb [+0x80];
        //   Dojo Shop/About/Play via T.1166 @0x0016a3ec): enable bomb physics so gravity +
        //   AccelGrowth fling the back-bomb off-screen deterministically -> KillBomb ->
        //   ActorManager reap before the next screen re-creates its single-slot menu bomb.
        //   Omitting it => constant-velocity drift => race => pool stays full => soft-lock.
        reinterpret_cast<Bomb*>(piece)->m_bMovement = 1;   // Bomb+0x80
        piece->vel = _Vector3<float>(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
}

// ===================================================================
// Matches DojoScreen::AboutCallback @ 0x0016a48c (v1.6.1)
// Binary: state=3, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::AboutCallback() {
    // DIFFERS: MSVC memory-safety guard -- binary v1.6.1 DojoScreen::AboutCallback @0x0016a48c has no guard;
    // MSVC can reuse the dangling m_pBackButton and deref its m_pTrackedFruit (UAF).
    // Guard prevents the crash the binary's allocator layout avoids.
    if (m_State != 0 && m_State != 1) return;
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 3, "AboutCallback @ 0x001378e0");
    m_State = 3;

    // ASM-spec v1.6.1 DojoScreen::AboutCallback @0x0016a48c (outlined helper
    // T.1153 @0x00169fb4): Math::g_random.RandF(5.0) x2, UNGATED -- the binary
    // has no null checks here, so the draws stay outside the port's guard.
    const float r1 = Math::g_Random.RandF(5.0f);
    const float r2 = Math::g_Random.RandF(5.0f);
    if (m_pBackButton && m_pBackButton->m_pTrackedFruit) {
        Fruit* piece = m_pBackButton->m_pTrackedFruit;
        // ASM-spec v1.6.1 *Callback (AboutScreen::QuitGameCallback @0x0015c914 strb [+0x80];
        //   Dojo Shop/About/Play via T.1166 @0x0016a3ec): enable bomb physics so gravity +
        //   AccelGrowth fling the back-bomb off-screen deterministically -> KillBomb ->
        //   ActorManager reap before the next screen re-creates its single-slot menu bomb.
        //   Omitting it => constant-velocity drift => race => pool stays full => soft-lock.
        reinterpret_cast<Bomb*>(piece)->m_bMovement = 1;   // Bomb+0x80
        piece->vel = _Vector3<float>(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
}

// ===================================================================
// Matches DojoScreen::UpdateBSButton @ 0x0016a2f4 + T_1162 @ 0x0016a274
// Repositions a BSButton each frame along a slide-in direction proportional
// to the screen's transition alpha.
// ===================================================================
void DojoScreen::UpdateBSButton(BSButton* btn, float /*dt*/, unsigned long idx) {
    // ASM-spec v1.6.1 T_1162 @0x0016a274 + UpdateBSButton @0x0016a2f4:
    //   anchor = (152, 100 - 46*idx, 0); slide = normalize(8,-1,0);
    //   pos = anchor + slide * (1 - m_TransitionAlpha) * 248.
    // DIFFERS: opt-in widescreen -- MapX the right-edge anchor so FB/TW hug the
    // widened right edge (same pattern as dojo.btn.shop/about). idx 0=FB, 1=TW
    // (2/3 are defunct/always-null slots and never reach here); identity when
    // disabled/__bada__.
    const char* key = (idx == 0) ? "social.facebook" : "social.twitter";
    _Vector3<float> anchor(MapX(152.0f, key), 100.0f - 46.0f * (float)idx, 0.0f);
    _Vector3<float> slide(8.0f, -1.0f, 0.0f);
    slide.Normalise();
    float offset = (1.0f - m_TransitionAlpha) * 248.0f;
    _Vector3<float> pos = anchor + slide * offset;
    btn->SetPosition(pos);
}

// ===================================================================
// Matches DojoScreen::UpdateBSButtons @ 0x0016b580
// Loops over all 4 social/network button slots and repositions non-null ones.
// ===================================================================
void DojoScreen::UpdateBSButtons(float dt) {
    // ASM-spec v1.6.1 DojoScreen::UpdateBSButtons @0x0016b580:
    //   idx 0=m_pBSButton0, 1=m_pBSButton1, 2=m_pButton4 (defunct), 3=m_ResetValue (defunct).
    if (m_pBSButton0) UpdateBSButton(m_pBSButton0, dt, 0);
    if (m_pBSButton1) UpdateBSButton(m_pBSButton1, dt, 1);
    // Defunct network-button slots -- always null; loop shape preserved.
    BSButton* btn4 = static_cast<BSButton*>(m_pButton4);
    if (btn4) UpdateBSButton(btn4, dt, 2);
    // m_ResetValue (+0xac) doubles as defunct BSButton* slot in the binary loop; always 0.
    if (m_ResetValue != 0) {
        BSButton* btn5 = reinterpret_cast<BSButton*>((size_t)(unsigned int)m_ResetValue);
        UpdateBSButton(btn5, dt, 3);
    }
}

// ---- Defunct callbacks ----
// FacebookPressed/TwitterPressed are FREE functions (see game/Social.h) bound
// directly via Delegate0<void>::MakeFree in CreateButtons() above -- the binary
// uses a free-function delegate here, not a DojoScreen member callback.

// ---- Defunct callbacks (zero callsite xrefs in Bada shipped binary) ----
// See file-header "Defunct: 4 binary symbols compiled into FruitNinja.exe but
// with ZERO callsite xrefs" comment. iPhone/iPad-variant leftover .o code.
// Defunct: iOS "More Games" button -- no-op stub; v1.6.1 DojoScreen::MoreGamesCallback @0x00169eec
void DojoScreen::MoreGamesCallback() {}
// Defunct: iOS Quit-from-Dojo callback -- no-op stub; v1.6.1 DojoScreen::QuitCallback @0x0016b980
void DojoScreen::QuitCallback() {}
// Defunct: network-switch button -- no-op stub; v1.6.1 DojoScreen::SwitchCallback @0x00169ea8
void DojoScreen::SwitchCallback() {}
// Defunct: network-switch ScreenButton frame helper -- no-op stub; TODO: re-verify v1.6.1 address (not present in Bada v1.6.1 binary)
void DojoScreen::SwitchNetworkButton(MenuButton*, float, ScreenButton&) {}
// Defunct: Twitter/Facebook social buttons (iOS variant) -- no-op stub; TODO: re-verify v1.6.1 address (not present in Bada v1.6.1 binary)
void DojoScreen::TwitterFacbookButtons(MenuButton*, float, ScreenButton&) {}

// FN_TEST helpers TestFireAboutSlice/TestFireShopSlice are inline in DojoScreen.h
// (this TU compiles into fruit-ninja-game.lib without FN_TEST, so out-of-line
// bodies here would never be emitted -> unresolved externals in the test exe).
