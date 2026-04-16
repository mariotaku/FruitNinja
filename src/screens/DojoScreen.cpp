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
#include "entities/Fruit.h"
#include "audio/GameSound.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "debug/DebugFlags.h"
#include <cstdio>
#include <cstdlib>

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

// Background panel position — dojo_sensei.tex (256x256).
// Binary DAT_001383c4/c8/c0 = (-180, -47, 0). DrawQuadUnCached uses
// the same center-anchored ±0.5 unit quad as the port's DrawQuad, so
// the binary values are usable directly.
// Z is 10 so the panel draws in front of MainScreen's lingering
// button entities (which sit at z=-50).
static const Vec3 POS_DOJO_BG(-180.0f, -47.0f, 10.0f);

// Static texture storage — binary stores these at GOT-relative globals.
SmartPtr<Mortar::Texture> DojoScreen::s_TexDojo;
SmartPtr<Mortar::Texture> DojoScreen::s_TexSensei;
SmartPtr<Mortar::Texture> DojoScreen::s_TexShop;
SmartPtr<Mortar::Texture> DojoScreen::s_TexAbout;
SmartPtr<Mortar::Texture> DojoScreen::s_TexBlurryBacking;
SmartPtr<Mortar::Texture> DojoScreen::s_TexBackIcon;

// Matches DojoScreen::DojoScreen @ 0x00137b90.
DojoScreen::DojoScreen(Game& g)
    : game(g)
    , m_TransitionAlpha(0.0f)  // DAT_00137bf4 = 0.0
    , m_State(0)               // field_0x90
    , m_pPlayButton(NULL)      // field_0x94
    , m_pShopButton(NULL)      // field_0x98
    , m_pAboutButton(NULL)     // field_0x9c
    , m_pAboutScreen(NULL)     // field_0xa0
{
    LoadContent();
    m_LayerFlags = 0x80;
    m_bNoDestructor = 0;
}

// Matches DojoScreen::LoadContent @ 0x00137a20.
// Binary loads 5 textures at GOT-relative static storage,
// calls BaseScreen::LoadContent() in the middle.
void DojoScreen::LoadContent() {
    // +0x08: loading.tex (not used in Draw — skipped)
    s_TexDojo      = Mortar::TextureManager::LoadLocalisedTexture("dojo.tex");         // +0x0c
    s_TexSensei    = Mortar::TextureManager::LoadLocalisedTexture("dojo_sensei.tex");  // +0x10
    // BaseScreen::LoadContent() called here in binary
    s_TexShop      = Mortar::TextureManager::LoadLocalisedTexture("senseis_swag.tex"); // +0x14
    s_TexAbout     = Mortar::TextureManager::LoadLocalisedTexture("about.tex");        // +0x18
    // Port specific: binary's DrawBorders loads blurry_backing from a
    // shared global; port loads it here since we lack that global.
    s_TexBlurryBacking = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_TexBackIcon      = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");
}

// Matches DojoScreen::UnLoadContent @ 0x00137c04.
// Binary clears the loaded flag, calls BaseScreen::UnloadContent(),
// then SetNull on all texture SmartPtrs.
void DojoScreen::UnLoadContent() {
    // BaseScreen::UnloadContent() — not ported yet
    s_TexSensei        = (Mortar::Texture*)NULL;
    s_TexDojo          = (Mortar::Texture*)NULL;
    s_TexShop          = (Mortar::Texture*)NULL;
    s_TexAbout         = (Mortar::Texture*)NULL;
    s_TexBlurryBacking = (Mortar::Texture*)NULL;
    s_TexBackIcon      = (Mortar::Texture*)NULL;
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
    // Binary checks field_0x94 == NULL (play button pointer).
    if (m_pPlayButton != NULL) return;

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
    m_pPlayButton->m_Texture = TexIdOf(s_TexBackIcon);
    m_pPlayButton->size      = TexSizeOf(s_TexBackIcon, 64.0f, 64.0f);
    m_pPlayButton->Init(POS_BACK_BUTTON,
                        [this]() { PlayCallback(); },
                        FT_BOMB, Vec3(0, 0, 0), nullptr);
    m_pPlayButton->m_TargetSize = m_pPlayButton->m_TargetSize * 0.825f;
    m_pPlayButton->m_LayerFlags = 0x40;
    // RemoveCallback nulls our weak ref BEFORE HUD frees the
    // button — prevents UAF if the button self-removes via its
    // FadeCounter shrink-disappear path (state 6 sub-button
    // animation finishes before DojoScreen itself fades out).
    m_pPlayButton->m_RemoveCallback = [this](HUDControl*) {
        m_pPlayButton = NULL;
    };
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
    m_pShopButton->m_Texture = TexIdOf(s_TexShop);
    m_pShopButton->size      = TexSizeOf(s_TexShop, 64.0f, 64.0f);
    m_pShopButton->Init(POS_SHOP_BUTTON,
                        [this]() { ShopCallback(); },
                        FT_PINEAPPLE, Vec3(0, 0, 0), nullptr);
    m_pShopButton->m_TargetSize = m_pShopButton->m_TargetSize * 0.575f;
    m_pShopButton->m_AnimSpeed  = -15.0f;
    m_pShopButton->m_AnimSpeed2 = -15.0f;
    m_pShopButton->m_LayerFlags = 0x40;
    m_pShopButton->m_RemoveCallback = [this](HUDControl*) {
        m_pShopButton = NULL;
    };
    game.hud->AddControl(m_pShopButton);

    // ---- Button 3: About (about.tex) ----
    // Binary just Init + AddControl. No post-Init scaling, no
    // TutorialControl, no SetNewSymbol.
    m_pAboutButton = new MenuButton();
    m_pAboutButton->m_Texture = TexIdOf(s_TexAbout);
    m_pAboutButton->size      = TexSizeOf(s_TexAbout, 64.0f, 64.0f);
    m_pAboutButton->Init(POS_ABOUT_BUTTON,
                         [this]() { AboutCallback(); },
                         FT_PLUM, Vec3(0, 0, 0), nullptr);
    m_pAboutButton->m_LayerFlags = 0x40;
    m_pAboutButton->m_RemoveCallback = [this](HUDControl*) {
        m_pAboutButton = NULL;
    };
    game.hud->AddControl(m_pAboutButton);

}

void DojoScreen::RemoveButtons() {
    if (m_pPlayButton)  { m_pPlayButton->SetPendingRemoval();  m_pPlayButton  = NULL; }
    if (m_pShopButton)  { m_pShopButton->SetPendingRemoval();  m_pShopButton  = NULL; }
    if (m_pAboutButton) { m_pAboutButton->SetPendingRemoval(); m_pAboutButton = NULL; }
}

// Matches DojoScreen::Update (0x00138414) state machine.
void DojoScreen::Update(float dt) {
    (void)dt;

    switch (m_State) {
    case 0: {
        // Transition in: lerp alpha → 1.0 at step 0.25. Binary-exact
        // at full speed. Port specific: the rate is additionally
        // multiplied by FN::g_DebugTimeScale so the F7 slowdown
        // also slows this per-tick lerp — without it the sensei
        // slide-in (driven by m_TransitionAlpha) runs at full speed
        // while fruit physics run 10x slower, which looks broken.
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) *
                             ALPHA_LERP_IN * FN::g_DebugTimeScale;

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

            if (m_State == 3) {
                // Create AboutScreen as child. Install a remove
                // callback so HUD::Update nulls our weak ref BEFORE
                // freeing the child — same dangling-pointer trap as
                // the MainScreen → DojoScreen relationship.
                //
                // AboutScreen draws over the DojoScreen panel, so
                // RemoveButtons() here is required: the idle sub
                // buttons (back/shop/about) need to actually go away
                // while About is open, and they need to be re-created
                // when we return to state 0 via the remove callback
                // below — RemoveButtons NULLs the pointers so
                // state 0 re-creates them.
                RemoveButtons();
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
                //
                // DO NOT call RemoveButtons() here. The sub-buttons
                // were already released by FN_ClearMenuItems the frame
                // the user sliced the back-bomb, so their attached
                // fruit entities are drifting with random fall vels
                // and the buttons are mid-way through their own
                // FadeCounter shrink-disappear animation. Calling
                // SetPendingRemoval on them here would short-circuit
                // HUD::Update into deleting them immediately, which
                // runs MenuButton::Release → m_pEntity->Deactivate()
                // and the fruits vanish instead of falling naturally.
                // The binary doesn't null the button pointers in
                // state 6 either — the ~DojoScreen destructor handles
                // any stragglers when HUD finally frees this control
                // after m_bPendingRemoval takes effect.
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

    // Two angled shade triangles. Matches BaseScreen::DrawBorders
    // @ 0x00130230. Literal pool at 0x13056c resolved via read_memory.
    // Wedges: 656 wide, ±82 tall, alpha 0x80 black.
    //
    //   tri1 (right wedge) @ (240, 160 - 48*alpha, 0)
    //   tri2 (left wedge)  @ (-240, 160 - 103*alpha, 0)
    //     (s17 accumulates: tri2_Y = (160-48a) - 55a = 160-103a)
    //
    // The wedges use blurry_backing as a tint texture.
    if (s_TexBlurryBacking.IsValid()) {
        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        const uint8_t a = (uint8_t)(128.0f * m_TransitionAlpha);
        const uint32_t kCol =
            Colour(0, 0, 0, a).PlatformColour();

        s_TexBlurryBacking->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Renderer* r = Renderer::GetInstance();

        // --- Right wedge (tri1) ---
        // Anchored at (240, 160 - 48*alpha).
        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            const float yOff = 160.0f - m_TransitionAlpha * 48.0f;
            mat.GlobalTranslate44(Vec3(240.0f, yOff, 4.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            QUADCUSTOMVERTEX tri1[3] = {
                {    0.0f,   82.0f, 0.0f,  0,0,1,  kCol,  0.0f, 0.0078125f },
                { -656.0f,   82.0f, 0.0f,  0,0,1,  kCol,  1.0f, 0.0078125f },
                { -656.0f,  -82.0f, 0.0f,  0,0,1,  kCol,  1.0f, 1.0f       },
            };
            if (r) r->DrawTriList(tri1, 3);
        }

        // --- Left wedge (tri2) ---
        // s17 accumulates from tri1: Y = (160-48a) - 55a = 160-103a
        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            const float yOff = 160.0f - m_TransitionAlpha * 103.0f;
            mat.GlobalTranslate44(Vec3(-240.0f, yOff, 4.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            QUADCUSTOMVERTEX tri2[3] = {
                {    0.0f,  -82.0f, 0.0f,  0,0,1,  kCol,  0.0f, 0.0078125f },
                {  656.0f,  -82.0f, 0.0f,  0,0,1,  kCol,  1.0f, 0.0078125f },
                {  656.0f,   82.0f, 0.0f,  0,0,1,  kCol,  1.0f, 1.0f       },
            };
            if (r) r->DrawTriList(tri2, 3);
        }

        s_TexBlurryBacking->UnSet();
    }

    // Block A — dojo_sensei.tex (field +0x10): main 256x256 panel.
    // Binary @ 0x0013822c: Scale(w+1, h+1, 0) → Translate(-180-slide, -47, 0)
    // → UploadMatrices → Texture::Set → DrawQuadUnCached × 2 → UnSet.
    // Slide: X -= texWidth * (1-alpha), slides in from the left.
    if (s_TexSensei.IsValid()) {
        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
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
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (Renderer* r = Renderer::GetInstance()) {
            const uint8_t a = (uint8_t)(m_TransitionAlpha * 255.0f);
            r->DrawQuad(Colour(255, 255, 255, a));
        }
        s_TexSensei->UnSet();
    }

    // Sensei decoration — drawn inside BaseScreen::DrawBorders @ 0x00130230.
    // Exact constants from literal pool at 0x13059c-0x1305a4:
    //   Base:  (182, 137, 0) — top-right area
    //   Slide: X += 48 * (1 - alpha) → slides in from the right
    if (s_TexDojo.IsValid()) {
        const float senseiX = 182.0f + (1.0f - m_TransitionAlpha) * 48.0f;
        const float senseiY = 137.0f;

        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexDojo->m_Width  + 1.0f,
            (float)s_TexDojo->m_Height + 1.0f,
            1.0f);
        // z=20 puts sensei in front of the dojo background (z=10).
        mat.GlobalTranslate44(Vec3(senseiX, senseiY, 20.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexDojo->Set();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (Renderer* r = Renderer::GetInstance()) {
            r->DrawQuad(Colour(255, 255, 255, 255));
        }
        s_TexDojo->UnSet();
    }

    // TODO: dojo.tex (field +0x0c) passed to DrawBorders at (-184, -136, 0).
}

// --- Sub-button callbacks ---

// Matches DojoScreen::QuitCallback @ 0x001389F4.
// Binary flow:
//   1. GameSound::SFXPlay(gameSound, "menu-bomb", 1.0, 1.0, cb)
//   2. m_State = 6
//   3. If m_pPlayButton && m_pPlayButton->m_pFruitPiece:
//        SetVisible_FruitFact(piece)  // detach from MenuButton tracking
//        piece->vel = (RandFloat5() + 5.0, -RandFloat5(), 0.0)
//   4. TutorialControl::ResetTutePos(..., NULL)
//
// RandFloat5 (@ 0x00147fb0) returns a float in [0, 5) — port uses
// the standard rand() path since no GameSound / RNG state divergence
// would be visible from this one call site.
void DojoScreen::PlayCallback() {
    printf("[DojoScreen] Play -> return to MainScreen\n");

    if (game.pGameSound) {
        game.pGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    m_State = 6;

    // Fling the back-bomb's fruit piece off-screen rightward. Binary
    // calls SetVisible_FruitFact which flips a "detached" bit on the
    // piece so MenuButton::Update stops pinning it to the button
    // centre — port achieves the same effect by writing vel directly,
    // because MenuButton's pin-to-centre branch only fires while
    // m_pEntity == m_pFruitPiece and the entity isn't sliced. Here
    // the piece will start drifting as soon as this write lands.
    if (m_pPlayButton && m_pPlayButton->m_pFruitPiece) {
        const float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        m_pPlayButton->m_pFruitPiece->vel = Vec3(r1 + 5.0f, -r2, 0.0f);
    }

    // TODO: TutorialControl::ResetTutePos when tutorial system is ported.
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
