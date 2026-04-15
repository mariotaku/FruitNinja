//
// DojoScreen — secondary menu shown after tapping Dojo button.
// See DojoScreen.h for binary refs and docs/screens/dojo.md.
//
// Analysed: 2026-04-14T02:00
//

#include "DojoScreen.h"
#include "AboutScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "entities/FruitInfo.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include <cstdio>

// Transition constants — resolved from docs/screens/dojo-re-notes.md.
// State 0: alpha += (1-alpha)*0.25, done at alpha > 0.9991 (DAT_001389dc)
// State 0 skip-button-create: alpha <= 0.95 (DAT_00138684)
// State 2/3/6: alpha *= 0.75, done at alpha < 0.001 (DAT_001389e0)
static const float ALPHA_LERP_IN       = 0.25f;
static const float ALPHA_IN_DONE       = 0.9991f;
static const float ALPHA_BUTTON_CREATE = 0.95f;
static const float ALPHA_DECAY         = 0.75f;
static const float ALPHA_OUT_DONE      = 0.001f;

// Button positions — verified from DojoScreen::Update @ 0x00138414:
//   Back   button @ DAT_00138688/8c/90 = (185, -106, 0)
//   Shop   button @ vmov immediates    = (-18,  -15, 0)
//   About  button @ DAT_001389d0/d4/d8 = (145,   42, 0)
// Positions are RAW — the 0.825 multiplier seen elsewhere is applied
// to m_TargetSize (hit bounds), NOT pos.
static const Vec3 POS_BACK_BUTTON  ( 185.0f, -106.0f, 0.0f);
static const Vec3 POS_SHOP_BUTTON  ( -18.0f,  -15.0f, 0.0f);
static const Vec3 POS_ABOUT_BUTTON ( 145.0f,   42.0f, 0.0f);

// Helpers (mirror MainScreen.cpp).
static GLuint TexIdOf(const SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}
static Vec3 TexSizeOf(const SmartPtr<Mortar::Texture>& tex,
                      float defW, float defH) {
    if (tex.IsValid()) {
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    }
    return Vec3(defW, defH, 1.0f);
}

// Background board position — from Draw function:
//   Translate to (-180.0, -47.0, 0.0) (DAT_001383c4, DAT_001383c8, DAT_001383c0)
// Z is bumped to +10 in the port so the dojo background draws in front
// of the MainScreen's lingering button entities (which sit at z=-50).
// Binary uses z=0 with the rotation matrix giving an inherent depth
// offset; port renders without rotation so we lift z manually.
static const Vec3 POS_DOJO_BG     (-180.0f, -47.0f, 10.0f);

DojoScreen::DojoScreen(Game& g)
    : game(g)
    , m_TransitionAlpha(0.0f)
    , m_State(0)
    , m_pPlayButton(NULL)
    , m_pShopButton(NULL)
    , m_pAboutButton(NULL)
    , m_pAboutScreen(NULL)
    , m_bButtonsCreated(false)
{
    // Binary writes 0x80 to HUDControl::m_LayerFlags in the ctor.
    m_LayerFlags = 0x80;

    // Load screen textures. Binary loads in LoadContent (0x137a20):
    //   dojo.tex (background), dojo_sensei.tex (decoration — skipped),
    //   senseis_swag.tex (skipped), newgame.tex (Play button icon — the
    //   MenuButton already handles button icons separately), dojo_icon.tex.
    m_TexDojo     = Mortar::TextureManager::LoadLocalisedTexture("dojo.tex");
    m_TexSensei   = Mortar::TextureManager::LoadLocalisedTexture("dojo_sensei.tex");
    m_TexBackIcon = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");
    m_TexShop     = Mortar::TextureManager::LoadLocalisedTexture("senseis_swag.tex");
    m_TexAbout    = Mortar::TextureManager::LoadLocalisedTexture("about.tex");

    printf("[DojoScreen] ctor: dojo=%d sensei=%d back=%d shop=%d about=%d\n",
           m_TexDojo.IsValid(), m_TexSensei.IsValid(),
           m_TexBackIcon.IsValid(), m_TexShop.IsValid(), m_TexAbout.IsValid());

    if (m_TexDojo.IsValid()) {
        m_Texture = m_TexDojo->m_TexId;
        // Background panel size = texture width+1, height+1 per
        // Draw function (DAT_001383c0 + 1 pattern).
        size = Vec3((float)m_TexDojo->m_Width + 1.0f,
                    (float)m_TexDojo->m_Height + 1.0f,
                    1.0f);
    } else {
        size = Vec3(480.0f, 320.0f, 1.0f);
    }
    pos = POS_DOJO_BG;
}

DojoScreen::~DojoScreen() {
    RemoveButtons();
}

void DojoScreen::Init() {
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

void DojoScreen::Release() {
    RemoveButtons();
}

// Matches DojoScreen::Update state-0 sub-button creation block at
// 0x00138470..0x00138878. Each button has a different texture, fruit
// type, and post-Init field tweak.
//
// Fruit types: btn1 (back) reads from a runtime global int via
//   `**(int **)(GOT + 0x7060)` — port uses watermelon (3) as a reasonable
//   placeholder until that global is resolved.
//   btn2 (shop) calls Fruit::FruitType("pineapple", false) → index 6.
//   btn3 (about) calls Fruit::FruitType("plum", false) → index 7.
void DojoScreen::CreateButtons() {
    if (m_bButtonsCreated) return;

    // Indices into fruitlist.xml ordering (apple=0, banana=1, orange=2,
    // watermelon=3, strawberry=4, kiwifruit=5, pineapple=6, plum=7,
    // pear=8, mango=9, ...).
    //
    // Back button uses the bomb threshold (= FruitInfo_GetCount()).
    // Verified 2026-04-15: the binary reads `**(int**)(GOT+0x7060)` →
    // resolved BSS at 0x0024D754, set in Fruit::LoadInfo @ 0x00179964
    // to the count of <fruit> elements in fruitlist.xml — exactly what
    // FruitInfo_GetCount() returns. Passing this as fruitType triggers
    // MenuButton::Init's bomb branch (`bombThreshold <= fruitType`).
    const int FT_BOMB      = FruitInfo_GetCount();
    const int FT_PINEAPPLE = 6;
    const int FT_PLUM      = 7;

    // ---- Button 1: Back (back_icon.tex) ----
    // Binary: HUD::AddControl + TutorialControl::ResetTutePos.
    // m_TargetSize gets *= 0.825 after Init.
    m_pPlayButton = new MenuButton();
    m_pPlayButton->m_Texture = TexIdOf(m_TexBackIcon);
    m_pPlayButton->size      = TexSizeOf(m_TexBackIcon, 64.0f, 64.0f);
    m_pPlayButton->Init(POS_BACK_BUTTON,
                        [this]() { PlayCallback(); },
                        FT_BOMB, Vec3(0, 0, 0), nullptr);
    m_pPlayButton->m_TargetSize = m_pPlayButton->m_TargetSize * 0.825f;
    m_pPlayButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pPlayButton);

    // ---- Button 2: Sensei's Swag / Shop (senseis_swag.tex) ----
    // Binary post-Init tweaks at 0x001386c8..0x00138750:
    //   size       = (texW+1, texH+1, 1)
    //   field+0x13c = 0.5
    //   m_TargetSize *= 0.575
    //   m_AnimSpeed = m_AnimSpeed2 = -15
    // Then HUD::AddControl + TutorialControl::ResetTutePos +
    //   MenuButton::SetNewSymbol(ItemManager::AreNewItems()).
    m_pShopButton = new MenuButton();
    m_pShopButton->m_Texture = TexIdOf(m_TexShop);
    m_pShopButton->size      = TexSizeOf(m_TexShop, 64.0f, 64.0f);
    m_pShopButton->Init(POS_SHOP_BUTTON,
                        [this]() { ShopCallback(); },
                        FT_PINEAPPLE, Vec3(0, 0, 0), nullptr);
    m_pShopButton->m_TargetSize = m_pShopButton->m_TargetSize * 0.575f;
    m_pShopButton->m_AnimSpeed  = -15.0f;
    m_pShopButton->m_AnimSpeed2 = -15.0f;
    m_pShopButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pShopButton);

    // ---- Button 3: About (about.tex) ----
    // Binary just Init + AddControl. No post-Init scaling, no
    // TutorialControl, no SetNewSymbol.
    m_pAboutButton = new MenuButton();
    m_pAboutButton->m_Texture = TexIdOf(m_TexAbout);
    m_pAboutButton->size      = TexSizeOf(m_TexAbout, 64.0f, 64.0f);
    m_pAboutButton->Init(POS_ABOUT_BUTTON,
                         [this]() { AboutCallback(); },
                         FT_PLUM, Vec3(0, 0, 0), nullptr);
    m_pAboutButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pAboutButton);

    m_bButtonsCreated = true;
    printf("[DojoScreen] Created 3 sub-buttons (back/shop/about)\n");
}

void DojoScreen::RemoveButtons() {
    if (m_pPlayButton)  { m_pPlayButton->SetPendingRemoval();  m_pPlayButton  = NULL; }
    if (m_pShopButton)  { m_pShopButton->SetPendingRemoval();  m_pShopButton  = NULL; }
    if (m_pAboutButton) { m_pAboutButton->SetPendingRemoval(); m_pAboutButton = NULL; }
    m_bButtonsCreated = false;
}

// Matches DojoScreen::Update (0x00138414) state machine.
void DojoScreen::Update(float dt) {
    (void)dt;

    switch (m_State) {
    case 0: {
        // Transition in: lerp alpha → 1.0 at step 0.25
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;

        // Binary gates button creation on alpha > 0.95 (DAT_00138684).
        // Before that, the panel is still sliding in and we don't
        // want the buttons touchable.
        if (m_TransitionAlpha > ALPHA_BUTTON_CREATE) {
            CreateButtons();
        }

        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            m_State = 1;
        }
        break;
    }
    case 1:
        // Idle — buttons handle themselves. The child AboutScreen
        // notifies us via its m_RemoveCallback (installed when we
        // create it) so we don't have to poll a possibly-dangling
        // pointer here. Binary uses a reverse vtable dispatch on
        // a parent pointer for the same purpose.
        break;
    case 2:
    case 3:
    case 6: {
        // Fade out at decay 0.75, done at alpha < 0.001.
        m_TransitionAlpha *= ALPHA_DECAY;
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            m_TransitionAlpha = 0.0f;
            RemoveButtons();

            if (m_State == 3) {
                // Create AboutScreen as child. Install a remove
                // callback so HUD::Update nulls our weak ref BEFORE
                // freeing the child — same dangling-pointer trap as
                // the MainScreen → DojoScreen relationship.
                m_pAboutScreen = new AboutScreen(game, this);
                m_pAboutScreen->m_RemoveCallback = [this](HUDControl*) {
                    m_pAboutScreen = NULL;
                    // Re-enter state 0 so the Dojo buttons get
                    // re-created and the panel fades back in.
                    m_State = 0;
                };
                game.hud->AddControl(m_pAboutScreen);
                m_State = 1;
            } else if (m_State == 2) {
                // Shop — port stub returns to MainScreen via the
                // same path as state 6 until ShopScreen is ported.
                m_bPendingRemoval = 1;
                if (game.mainScreen) {
                    game.mainScreen->SetState(STATE_SLIDE_IN);
                }
            } else {
                // State 6: back to MainScreen. Binary @ 0x001389B0:
                //   field_0x33 = 1                        (m_bPendingRemoval)
                //   game->mainScreen->m_State = 8         (STATE_SLIDE_IN)
                // The state injection happens HERE inside DojoScreen,
                // not via a remove callback on the parent side. This
                // ensures the parent transitions in the same frame
                // the child marks itself for removal.
                m_bPendingRemoval = 1;
                if (game.mainScreen) {
                    game.mainScreen->SetState(STATE_SLIDE_IN);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

void DojoScreen::Draw(const Vec3& hudScale, int layerMask) {
    if ((layerMask & m_LayerFlags) == 0) return;
    if (m_TransitionAlpha <= 0.0f) return;

    // Binary Draw applies a horizontal slide offset based on alpha:
    //   drawX = base_x + (1 - alpha) * DAT_001383e0  (some large offset)
    // The exact offset DAT wasn't resolved; approximate with -240 so
    // the panel slides in from off-screen left to POS_DOJO_BG.
    const float slideOffset = (1.0f - m_TransitionAlpha) * -240.0f;
    pos = Vec3(POS_DOJO_BG.x + slideOffset, POS_DOJO_BG.y, POS_DOJO_BG.z);

    m_DrawColour.a = (uint8_t)(m_TransitionAlpha * 255.0f);
    HUDControl3d::Draw(hudScale, layerMask);

    // Sensei texture — drawn inside BaseScreen::DrawBorders @ 0x00130230.
    // Binary base position at alpha=1.0:
    //   X = 182.0  (DAT_0013059c)
    //   Y = 137.0  (DAT_001305a0)
    //   Z =   0.0  (DAT_0013056c)
    // Slide-in: (1 - alpha) * 48.0 on X (DAT_001305a4).
    //
    // Coord-system: the binary draws into the Bada portrait framebuffer
    // (480×800) which is rotated 90° for landscape display. The net
    // visual effect is that the binary's positive X/Y values land in
    // the OPPOSITE corner from the port's centred ortho (which is
    // already in landscape space). We negate both axes (180° rotation)
    // to put sensei in the bottom-left where the binary intends.
    if (m_TexSensei.IsValid()) {
        // Y nudged up from raw -137 because the binary's
        // TranslateMatrix_DrawUtil likely anchors the quad at a corner
        // while the port's DrawQuad is centre-anchored — adding a
        // half-texture-height offset puts the centre where the binary
        // wants the corner.
        const float SENSEI_BASE_X  = -182.0f;
        const float SENSEI_BASE_Y  =  -55.0f;
        const float SENSEI_SLIDE_X =  -48.0f;  // negated
        const float senseiX = SENSEI_BASE_X +
                              (1.0f - m_TransitionAlpha) * SENSEI_SLIDE_X;

        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        // Quad scale = (texW + 1, texH + 1, 1) — matches binary's
        // `vtable[+0x14] + 1` width/height query in DrawBorders.
        Matrix44 mat = Matrix44::MakeScale(
            (float)m_TexSensei->m_Width  + 1.0f,
            (float)m_TexSensei->m_Height + 1.0f,
            1.0f);
        // z=20 puts sensei in front of the dojo background (z=10).
        mat.GlobalTranslate44(Vec3(senseiX, SENSEI_BASE_Y, 20.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_TexSensei->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (Renderer* r = Renderer::GetInstance()) {
            // Binary uses a fixed colour from GOT (no alpha modulation —
            // fade is purely the slide-in offset). Port mirrors that.
            r->DrawQuad(Colour(255, 255, 255, 255));
        }
        m_TexSensei->UnSet();
    }

    // TODO: BaseScreen::DrawBorders corner decorations at (-184, -136, 0).
}

// --- Sub-button callbacks ---

void DojoScreen::PlayCallback() {
    // Play button on DojoScreen = return to MainScreen (state 6).
    // Binary flow: fades out, sets `GameState = 8` which the outer
    // menu loop interprets as "close DojoScreen, reopen MainScreen".
    printf("[DojoScreen] Play -> return to MainScreen\n");
    m_State = 6;
}

void DojoScreen::ShopCallback() {
    // Stub — port doesn't have ShopScreen yet. Return to MainScreen
    // like Play does so the user isn't stuck.
    printf("[DojoScreen] Shop (stub) -> return to MainScreen\n");
    m_State = 2;
}

void DojoScreen::AboutCallback() {
    // Transition to AboutScreen — state 3 fades out the Dojo buttons
    // then creates AboutScreen inside the transition.
    printf("[DojoScreen] About -> AboutScreen\n");
    m_State = 3;
}
