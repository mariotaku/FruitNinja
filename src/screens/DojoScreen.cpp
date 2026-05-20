//
// DojoScreen — secondary menu shown after tapping Dojo button.
// See DojoScreen.h for binary refs and docs/screens/dojo.md.
//
// Analysed: 2026-04-17T01:00
//
// Defunct: 4 binary symbols compiled into FruitNinja.exe but with ZERO
// callsite xrefs -- leftover .o code from other Halfbrick build variants
// (iPhone/iPad with Twitter/Facebook + More-Games buttons on the Dojo menu).
// Not ported.
//   SwitchCallback         @ 0x00137694
//   MoreGamesCallback      @ 0x0013769c
//   TwitterFacbookButtons  @ 0x00137738
//   SwitchNetworkButton    @ 0x001379b0

#include "DojoScreen.h"
#include "debug/Logger.h"
#include "AboutScreen.h"
#include "ShopScreen.h"
#include "MainScreen.h"
#include "game/FruitSaveData.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/TutorialControl.h"
#include "hud/MenuButton.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "audio/GameSound.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstdlib>
#include "game/GameWork.h"

// --- Constants (resolved from binary via read_memory) ---

// Transition alpha
static const float ALPHA_LERP_IN       = 0.25f;   // exponential approach step
static const float ALPHA_IN_DONE       = 0.999f;  // DAT_001389dc
static const float ALPHA_BUTTON_CREATE = 0.95f;   // DAT_00138684
static const float ALPHA_DECAY         = 0.75f;   // geometric decay per frame
static const float ALPHA_OUT_DONE      = 0.001f;  // DAT_001389e0

// Button positions (DAT_00138688/8c/90, vmov, DAT_001389d0/d4/d8)
static const Vec3 POS_BACK_BUTTON  ( 185.0f, -106.0f, 0.0f);
static const Vec3 POS_SHOP_BUTTON  ( -18.0f,  -15.0f, 0.0f);
static const Vec3 POS_ABOUT_BUTTON ( 145.0f,   42.0f, 0.0f);

// Button scale multipliers
static const float BACK_SCALE  = 0.825f;  // DAT_00138694
static const float SHOP_SCALE  = 0.575f;  // DAT_001386b4

// Background panel position — dojo_sensei at bottom-left area.
// DAT_001383c4/c8/c0 = (-180, -47, 0).
// Slides in from left: X -= texWidth * (1 - alpha).
static const Vec3 POS_DOJO_BG(-180.0f, -47.0f, 0.0f);

// Helpers
static GLuint TexIdOf(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}

// --- Static texture storage (binary: GOT-relative globals) ---
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexDojo;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexSensei;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexShop;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexAbout;
Mortar::SmartPtr<Mortar::Texture> DojoScreen::s_TexBackIcon;

// ===================================================================
// Matches DojoScreen::DojoScreen @ 0x00137b90
// ===================================================================
DojoScreen::DojoScreen(Game& g)
    : m_pPlayButton(nullptr)      // field_0x94
    , m_pShopButton(nullptr)      // field_0x98
    , m_pAboutButton(nullptr)     // field_0x9c
    , m_pAboutScreen(nullptr)     // field_0xa0
    , game(g)
{
    LOG_INFO("SCREEN/DojoScreen", "%s (%s)", "create", "DojoScreen::DojoScreen @ 0x00137b90");
    LoadContent();
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
    m_bNoDestructor = 0;
}

// ===================================================================
// Matches DojoScreen::~DojoScreen @ 0x00137cf4
// Binary: set vtable, call Release(), call ~BaseScreen()
// ===================================================================
DojoScreen::~DojoScreen() {
    LOG_INFO("SCREEN/DojoScreen", "%s (%s)", "destroy", "DojoScreen::~DojoScreen @ 0x00137cf4");
    Release();
    // ~BaseScreen() called implicitly by ~HUDControl3d chain
}

// ===================================================================
// Matches DojoScreen::LoadContent @ 0x00137a20
// ===================================================================
void DojoScreen::LoadContent() {
    // +0x08: loading.tex — skipped (not used in Draw)
    s_TexDojo   = Mortar::TextureManager::LoadLocalisedTexture("dojo.tex");         // +0x0c
    s_TexSensei = Mortar::TextureManager::LoadLocalisedTexture("dojo_sensei.tex");  // +0x10
    BaseScreen::LoadContent();  // loads sml_title.tex + blurry_backing.tex (slots +0x00, +0x04)
    s_TexShop   = Mortar::TextureManager::LoadLocalisedTexture("senseis_swag.tex"); // +0x14
    s_TexAbout  = Mortar::TextureManager::LoadLocalisedTexture("about.tex");        // +0x18
    s_TexBackIcon = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");
}

// ===================================================================
// Matches DojoScreen::UnLoadContent @ 0x00137c04
// ===================================================================
void DojoScreen::UnLoadContent() {
    BaseScreen::UnloadContent();  // releases sml_title + blurry_backing
    s_TexSensei.SetNull();
    s_TexDojo.SetNull();
    s_TexShop.SetNull();
    s_TexAbout.SetNull();
    s_TexBackIcon.SetNull();
}

// ===================================================================
// HUDControl::Init override
// ===================================================================
void DojoScreen::Init() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 0, "Init");
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

// Matches DojoScreen::Reset @ 0x0013767c (vtable slot +0x10).
// Binary writes only the BaseScreen::m_State (+0x90) field to 0 — used
// when AboutScreen completes its fade-out and wants DojoScreen to
// re-fade-in. Init() is more eager (also zeros alpha + sets active);
// at the AboutScreen-state-2 callsite the alpha is already <0.001 and
// m_bActive was never cleared, so Init's extras are no-ops there.
void DojoScreen::Reset() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 0, "Reset @ 0x0013767c");
    m_State = 0;
}

// ===================================================================
// HUDControl::Release override
// Matches the Release() called from ~DojoScreen @ 0x00137cf4
// ===================================================================
void DojoScreen::Release() {
    if (m_pPlayButton)  { m_pPlayButton->SetPendingRemoval();  m_pPlayButton  = nullptr; }
    if (m_pShopButton)  { m_pShopButton->SetPendingRemoval();  m_pShopButton  = nullptr; }
    if (m_pAboutButton) { m_pAboutButton->SetPendingRemoval(); m_pAboutButton = nullptr; }
}

// ===================================================================
// Matches DojoScreen::ButtonDeleted @ 0x00137684
// Remove callback for the shop button only (field_0x98 / this->button).
// ===================================================================
void DojoScreen::ButtonDeleted(HUDControl* ctrl) {
    if (ctrl == (HUDControl*)m_pShopButton) {
        m_pShopButton = nullptr;
    }
}

// ===================================================================
// Matches DojoScreen::Update @ 0x00138414 (247 lines)
// ===================================================================
void DojoScreen::Update(float dt) {
    (void)dt;

    // Binary: BaseScreen::UpdateButtons(&this->super, dt);
    BaseScreen::UpdateButtons(dt);

    switch (m_State) {

    // ---- STATE 0: Fade in, create buttons when alpha > 0.95 ----
    case 0: {
        // Exponential approach: alpha += (1 - alpha) * 0.25
        m_TransitionAlpha = m_TransitionAlpha +
                            (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;

        if (m_TransitionAlpha > ALPHA_BUTTON_CREATE) {

            // --- field_0x94: Back/Play button (back_icon.tex) ---
            if (m_pPlayButton == nullptr) {
                // Binary: fruit type from **(int**)(GOT + 0x7060) — this
                // is the bomb threshold global, equal to FruitInfo_GetCount().
                // MenuButton treats fruitType >= count as a BOMB spawn.
                const int bombFruitType = FruitInfo_GetCount();
                m_pPlayButton = new MenuButton();
                m_pPlayButton->m_Texture = (s_TexBackIcon);
                m_pPlayButton->Init(POS_BACK_BUTTON,
                                    Mortar::Delegate0<void>::Make(this, &DojoScreen::PlayCallback),
                                    bombFruitType, Vec3(0, 0, 0), nullptr);
                // Binary @ 0x0013856c: strb 1 at button+0x138 = m_bRespondsToBackKey.
                m_pPlayButton->m_bRespondsToBackKey = 1;
                m_pPlayButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
                game_work.mHud->AddControl(m_pPlayButton);
                if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos(m_pPlayButton);
                // Binary scales BOTH m_TargetSize AND fruit piece's scale by 0.825
                m_pPlayButton->m_TargetSize = m_pPlayButton->m_TargetSize * BACK_SCALE;
                if (m_pPlayButton->m_pFruitPiece) {
                    m_pPlayButton->m_pFruitPiece->scale =
                        m_pPlayButton->m_pFruitPiece->scale * BACK_SCALE;
                }
            }

            // --- field_0x98: Shop button (senseis_swag.tex) ---
            if (m_pShopButton == nullptr) {
                // Binary: Fruit::FruitType((char*)DAT_001386b0, false)
                const int shopFruitType = Fruit::FruitType("pineapple", false);
                m_pShopButton = new MenuButton();
                m_pShopButton->m_Texture = (s_TexShop);
                m_pShopButton->Init(POS_SHOP_BUTTON,
                                    Mortar::Delegate0<void>::Make(this, &DojoScreen::ShopCallback),
                                    shopFruitType, Vec3(0, 0, 0), nullptr);
                // Binary post-Init writes (order per 0x00138414, RE'd at
                // asm level not decompile to resolve operator*= target):
                //   [+0x124] m_TargetSize = (texW+1, texH+1, 1.0)   (absolute)
                //   [+0x13C] m_AnimScale  = 0.5
                //   [+0x140] m_BounceParams *= 0.575 (SHOP_SCALE)   (NOT m_TargetSize)
                //   [+0x150] m_HitInsetY  = -15.0  (was m_AnimSpeed)
                //   [+0x14C] m_HitInsetX  = -15.0  (was m_AnimSpeed2)
                // Earlier port had `m_TargetSize *= SHOP_SCALE` which
                // shrank the ring to ~57.5% of tex size -- wrong. 0.575
                // is the bounce multiplier, not a size multiplier.
                if (s_TexShop.IsValid()) {
                    m_pShopButton->m_TargetSize = Vec3(
                        (float)s_TexShop->m_Width + 1.0f,
                        (float)s_TexShop->m_Height + 1.0f,
                        1.0f);
                }
                m_pShopButton->m_AnimScale = 0.5f;
                // TODO: m_BounceParams *= SHOP_SCALE. The port doesn't
                // read m_BounceParams yet (MenuButton rework reverted),
                // so this write is currently a no-op. Restore once the
                // bounce/new-indicator draw path is ported.
                m_pShopButton->m_HitInsetY  = -15.0f;
                m_pShopButton->m_HitInsetX = -15.0f;
                m_pShopButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
                m_pShopButton->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &DojoScreen::ButtonDeleted);
                game_work.mHud->AddControl(m_pShopButton);
                if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos(m_pShopButton);
                // ItemManager not ported — always show no badge
                m_pShopButton->SetNewSymbol(false);
            }

            // --- field_0x9c: About button (about.tex) ---
            if (m_pAboutButton == nullptr) {
                // Binary: Fruit::FruitType((char*)DAT_001389f0, false)
                const int aboutFruitType = Fruit::FruitType("plum", false);
                m_pAboutButton = new MenuButton();
                m_pAboutButton->m_Texture = (s_TexAbout);
                m_pAboutButton->Init(POS_ABOUT_BUTTON,
                                     Mortar::Delegate0<void>::Make(this, &DojoScreen::AboutCallback),
                                     aboutFruitType, Vec3(0, 0, 0), nullptr);
                m_pAboutButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
                game_work.mHud->AddControl(m_pAboutButton);
            }
        }

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
        // Binary: poll ItemManager::AreNewItems() each frame for shop badge.
        // ItemManager not ported — badge always hidden.
        if (m_pShopButton) {
            m_pShopButton->SetNewSymbol(false);
        }
        break;

    // ---- STATES 2, 3, 4: Fade out → sub-screen ----
    case 2:
    case 3:
    case 4: {
        m_TransitionAlpha *= ALPHA_DECAY;

        if (m_TransitionAlpha <= 0.0f) return;
        if (m_TransitionAlpha > ALPHA_OUT_DONE) return;

        // Fade complete — null all button pointers and reset alpha
        int prevState = m_State;
        m_pPlayButton  = nullptr;  // field_0x94
        m_TransitionAlpha = 0.0f;
        m_pShopButton  = nullptr;  // button (field_0x98)
        m_pAboutButton = nullptr;  // field_0x9c (stored as int 0 in binary)

        if (prevState == 3) {
            // State 3: push AboutScreen.
            // Binary: operator_new(0xa0), AboutScreen ctor, call vtable[2]=Init,
            //         HUD::AddControl. No RemoveCallback installed in binary.
            m_pAboutScreen = new AboutScreen(game, this);
            m_pAboutScreen->Init();  // matches binary: (*(code*)about->vtable[2])(about)
            // Port-specific: install RemoveCallback so DojoScreen clears its ptr.
            // Binary relies on AboutScreen calling parent->Reset() which sets state=0.
            // Port keeps both: Init() via RemoveCallback (redundant but harmless).
            m_pAboutScreen->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &DojoScreen::AboutScreenRemoved);
            game_work.mHud->AddControl(m_pAboutScreen);
            return;
        }

        if (prevState == 2) {
            // State 2: push ShopScreen.
            // Binary: FruitSaveData::CheckDatesHaveChanged(game->save), then
            //   ShopScreen* shop = operator_new(0xbc); ShopScreen::ShopScreen(shop, this);
            //   HUD::AddControl(hud, shop, false); shop->Init();
            if (game_work.m_SaveData) game_work.m_SaveData->CheckDatesHaveChanged();
            ShopScreen* shop = new ShopScreen(game, this);
            game_work.mHud->AddControl(shop, false);
            shop->Init();
            return;
        }

        if (prevState == 4) {
            // Defunct: NetworkManager dashboard -- state 4 unreachable on Bada (no
            // button creates it). Binary state-4 body kept for vtable parity.
            LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(prevState), 0, "Update/state-4 defunct");
            m_State = 0;
            return;
        }

        break;
    }

    // ---- STATE 6: Back/quit → MainScreen ----
    case 6: {
        m_TransitionAlpha *= ALPHA_DECAY;

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

// ===================================================================
// Matches DojoScreen::Draw @ 0x0013822c
// ===================================================================
void DojoScreen::Draw(const Vec3& hudScale, int layerMask) {
    if ((layerMask & m_LayerFlags) == 0) return;
    if (m_TransitionAlpha <= 0.0f) return;

    // --- Block A: dojo_sensei.tex (slot +0x10) — main panel (128x256) ---
    // Slides in from left (horizontal slide): X -= texW * (1 - alpha).
    if (s_TexSensei.IsValid()) {
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexSensei->m_Width + 1.0f,
            (float)s_TexSensei->m_Height + 1.0f,
            1.0f);
        const float slideX = -(float)s_TexSensei->m_Width * (1.0f - m_TransitionAlpha);
        mat.GlobalTranslate44(Vec3(POS_DOJO_BG.x + slideX, POS_DOJO_BG.y, POS_DOJO_BG.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexSensei->Set();
        if (Renderer* r = Renderer::GetInstance()) {
            r->DrawQuad(Colour(255, 255, 255, 255));
        }
        s_TexSensei->UnSet();
    }

    // --- Block B: DrawBorders (shade triangles + sml_title + dojo.tex) ---
    // Binary: copies dojo.tex SmartPtr, builds translate (-184, -136, 0),
    //         calls BaseScreen::DrawBorders(this, &tex, alpha, &pos)
    static const Vec3 BORDER_POS(-184.0f, -136.0f, 0.0f);
    DrawBorders(s_TexDojo, m_TransitionAlpha, BORDER_POS);
}

// ===================================================================
// Matches DojoScreen::QuitCallback @ 0x001389f4
// Binary: SFXPlay("menu-bomb"), state=6, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::PlayCallback() {
    // 1. SFX
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // 2. State 6 (quit fade-out)
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 6, "PlayCallback @ 0x001389f4");
    m_State = 6;

    // 3. Fling the back-bomb with random rightward velocity.
    //    Binary @ 0x001389f4: indirects through m_pPlayButton->m_pFruitPiece
    //    (+0x134), writes *(byte*)(piece+0x80) = 1 unconditionally (aliases
    //    Fruit::m_ChuckDelay low byte / Bomb::m_bMovement), then writes
    //    Vec3(r1+5, -r2, 0) to piece->vel. Port omits the byte write since
    //    m_ChuckDelay is already 0 at this point and the write is write-only.
    if (m_pPlayButton && m_pPlayButton->m_pFruitPiece) {
        Fruit* piece = m_pPlayButton->m_pFruitPiece;
        const float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        piece->vel = Vec3(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);

    // Port specific: cascade-release Shop/About fruits so they fly off
    // and their buttons shrink alongside the back-bomb. Binary doesn't
    // do this on PlayCallback (ClearMenuItems only fires from fruit-
    // slice paths, not bomb-hit paths). Matches the same port-specific
    // deviation applied to GameModeScreen::QuitCallback.
    FN::ClearMenuItems();
}

// ===================================================================
// Matches DojoScreen::ShopCallback @ 0x00137864
// Binary: state=2, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::ShopCallback() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 2, "ShopCallback @ 0x00137864");
    m_State = 2;

    // Binary @ 0x00137864: m_pPlayButton->m_pFruitPiece (+0x134), set
    // *(byte*)(piece+0x80) = 1 (aliases m_ChuckDelay low byte), write fling vel.
    // Port omits the byte write; m_ChuckDelay stays 0.
    if (m_pPlayButton && m_pPlayButton->m_pFruitPiece) {
        Fruit* piece = m_pPlayButton->m_pFruitPiece;
        const float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        piece->vel = Vec3(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
}

// ===================================================================
// Matches DojoScreen::AboutCallback @ 0x001378e0
// Binary: state=3, fling fruit piece, ResetTutePos
// ===================================================================
void DojoScreen::AboutCallback() {
    LOG_INFO("SCREEN/DojoScreen", "%d -> %d (%s)", (int)(m_State), 3, "AboutCallback @ 0x001378e0");
    m_State = 3;

    // Binary @ 0x001378e0: m_pPlayButton->m_pFruitPiece (+0x134), set
    // *(byte*)(piece+0x80) = 1 (aliases m_ChuckDelay low byte), write fling vel.
    // Port omits the byte write; m_ChuckDelay stays 0.
    if (m_pPlayButton && m_pPlayButton->m_pFruitPiece) {
        Fruit* piece = m_pPlayButton->m_pFruitPiece;
        const float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        piece->vel = Vec3(r1 + 5.0f, -r2, 0.0f);
    }

    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
}

// ---- Defunct callbacks (zero callsite xrefs in Bada shipped binary) ----
// See file-header "Defunct: 4 binary symbols compiled into FruitNinja.exe but
// with ZERO callsite xrefs" comment. iPhone/iPad-variant leftover .o code.
// Defunct: iOS "More Games" button -- no-op stub; binary @ 0x0013769c
void DojoScreen::MoreGamesCallback() {}
// Defunct: iOS Quit-from-Dojo callback -- no-op stub; binary @ 0x????
void DojoScreen::QuitCallback() {}
// Defunct: network-switch button -- no-op stub; binary @ 0x00137694
void DojoScreen::SwitchCallback() {}
// Defunct: network-switch ScreenButton frame helper -- no-op stub; binary @ 0x001379b0
void DojoScreen::SwitchNetworkButton(MenuButton*, float, ScreenButton&) {}
// Defunct: Twitter/Facebook social buttons (iOS variant) -- no-op stub; binary @ 0x00137738
void DojoScreen::TwitterFacbookButtons(MenuButton*, float, ScreenButton&) {}
