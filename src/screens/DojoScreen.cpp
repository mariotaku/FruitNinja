//
// DojoScreen — secondary menu shown after tapping Dojo button.
// See DojoScreen.h for binary refs and docs/screens/dojo.md.
//
// Analysed: 2026-04-14T02:00
//

#include "DojoScreen.h"
#include "AboutScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "asset/TextureManager.h"
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

// Button positions — DATs resolved from Ghidra.
// Play/About positions post-multiplied by 0.825 (DAT_00138694).
static const float BUTTON_SCALE = 0.825f;
// Play/Quit button: (185, -106, 0) * 0.825 → (152.625, -87.45, 0)
static const Vec3 POS_PLAY_BUTTON ( 185.0f * 0.825f, -106.0f * 0.825f, 0.0f);
// Shop button: (-18, -15, 0) — NOT scaled
static const Vec3 POS_SHOP_BUTTON ( -18.0f,  -15.0f, 0.0f);
// About button: (145, 42, 0) * 0.825 → (119.625, 34.65, 0)
static const Vec3 POS_ABOUT_BUTTON( 145.0f * 0.825f,  42.0f * 0.825f, 0.0f);

// Background board position — from Draw function:
//   Translate to (-180.0, -47.0, 0.0) (DAT_001383c4, DAT_001383c8, DAT_001383c0)
static const Vec3 POS_DOJO_BG     (-180.0f, -47.0f, 0.0f);

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

    printf("[DojoScreen] ctor: dojo=%d sensei=%d\n",
           m_TexDojo.IsValid(), m_TexSensei.IsValid());

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

// Lazy-create the 3 sub-buttons. Matches binary pattern in
// DojoScreen::Update state 0 — creates Play/Shop/About on first frame.
void DojoScreen::CreateButtons() {
    if (m_bButtonsCreated) return;

    // Play button — fruitType = 3 (watermelon)
    m_pPlayButton = new MenuButton();
    m_pPlayButton->size = Vec3(64.0f, 64.0f, 1.0f);
    m_pPlayButton->Init(POS_PLAY_BUTTON,
                        [this]() { PlayCallback(); },
                        3, Vec3(0, 0, 0), nullptr);
    m_pPlayButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pPlayButton);

    // Shop button — fruitType = 9 (mango)
    m_pShopButton = new MenuButton();
    m_pShopButton->size = Vec3(64.0f, 64.0f, 1.0f);
    m_pShopButton->Init(POS_SHOP_BUTTON,
                        [this]() { ShopCallback(); },
                        9, Vec3(0, 0, 0), nullptr);
    m_pShopButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pShopButton);

    // About button — fruitType = 4 (pineapple or similar)
    m_pAboutButton = new MenuButton();
    m_pAboutButton->size = Vec3(64.0f, 64.0f, 1.0f);
    m_pAboutButton->Init(POS_ABOUT_BUTTON,
                         [this]() { AboutCallback(); },
                         4, Vec3(0, 0, 0), nullptr);
    m_pAboutButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pAboutButton);

    m_bButtonsCreated = true;
    printf("[DojoScreen] Created 3 sub-buttons\n");
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
                // Shop — port stub returns to MainScreen instead.
                m_bPendingRemoval = 1;
            } else {
                // State 6: return to MainScreen.
                m_bPendingRemoval = 1;
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

    // TODO: BaseScreen::DrawBorders at (-184, -136, 0) — draws corner
    // decorations around the panel. Skipped for v1.
    // TODO: sensei 3D mesh at (0, 20, -300). Skipped for v1.
}

// --- Sub-button callbacks ---

void DojoScreen::PlayCallback() {
    // Play button on DojoScreen = return to MainScreen (state 6).
    // Binary flow: fades out, sets `GameState = 8` which the outer
    // menu loop interprets as "close DojoScreen, reopen MainScreen".
    printf("[DojoScreen] Play → return to MainScreen\n");
    m_State = 6;
}

void DojoScreen::ShopCallback() {
    // Stub — port doesn't have ShopScreen yet. Return to MainScreen
    // like Play does so the user isn't stuck.
    printf("[DojoScreen] Shop (stub) → return to MainScreen\n");
    m_State = 2;
}

void DojoScreen::AboutCallback() {
    // Transition to AboutScreen — state 3 fades out the Dojo buttons
    // then creates AboutScreen inside the transition.
    printf("[DojoScreen] About → AboutScreen\n");
    m_State = 3;
}
