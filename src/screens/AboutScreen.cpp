//
// AboutScreen — credits page inside DojoScreen.
// See AboutScreen.h for binary refs and docs/screens/about.md.
//
// Analysed: 2026-04-14T02:00
//

#include "AboutScreen.h"
#include "DojoScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "asset/TextureManager.h"
#include <cstdio>

// Transition constants — resolved from docs/screens/about-re-notes.md.
// State 0: alpha += (1-alpha) * 0.125, done at > 0.9991 (DAT_0012f2fc)
// State 2: alpha *= 0.75, done at < 0.001 (DAT_0012f328)
static const float ALPHA_LERP_IN  = 0.125f;
static const float ALPHA_IN_DONE  = 0.9991f;
static const float ALPHA_DECAY    = 0.75f;
static const float ALPHA_OUT_DONE = 0.001f;

// Back button position (DATs 0x0012f300, 0x0012f304, 0x0012f2f4):
//   (185, -106, 0) — same position as DojoScreen's Play/Quit button.
// Binary also post-scales via Vec3_ScaleConst (same 0.825 pattern).
static const Vec3 POS_BACK_BUTTON(185.0f * 0.825f, -106.0f * 0.825f, 0.0f);

// Background panel slides down from `160 + h*0.5` to resting Y = 63.
// DATs: 0x0012f690=160.0, 0x0012f694=63.0, 0x0012f698=-50.0 (X).
static const float ABOUT_BG_X     = -50.0f;
static const float ABOUT_BG_Y_END =  63.0f;
static const float ABOUT_BG_Y_OFS = 160.0f;

// Credits panel slides up from below. Y_start = -h*0.5 - 160, Y drawn
// interpolates by (Y_start + 96.0) * alpha. DAT 0x0012f8dc = 96.0.
static const float CREDITS_X      = -50.0f;
static const float CREDITS_Y_OFS  = 160.0f;
static const float CREDITS_Y_TGT  =  96.0f;

AboutScreen::AboutScreen(Game& g, DojoScreen* parent)
    : game(g)
    , m_pParent(parent)
    , m_TransitionAlpha(0.0f)
    , m_State(0)
    , m_pBackButton(NULL)
    , m_pCreditsButton(NULL)
    , m_bButtonsCreated(false)
{
    // Binary writes 0x80 to m_LayerFlags in ctor.
    m_LayerFlags = 0x80;

    // LoadContent @ 0x0012ec14 loads 3 textures:
    //   s_boardTexture  = about.tex
    //   m_creditsTexture = credits.tex
    //   m_senseiTexture  = dojo_sensei.tex (skipped in port)
    m_TexAbout   = Mortar::TextureManager::LoadLocalisedTexture("about.tex");
    m_TexCredits = Mortar::TextureManager::LoadLocalisedTexture("credits.tex");

    printf("[AboutScreen] ctor: about=%d credits=%d parent=%p\n",
           m_TexAbout.IsValid(), m_TexCredits.IsValid(), (void*)parent);

    if (m_TexAbout.IsValid()) {
        m_Texture = m_TexAbout->m_TexId;
        // Binary stores texture dims at +0x20..+0x28 (field29_0x20,
        // field30_0x24, field31_0x28) and scales Draw by them.
        size = Vec3((float)m_TexAbout->m_Width  + 1.0f,
                    (float)m_TexAbout->m_Height + 1.0f,
                    1.0f);
    } else {
        size = Vec3(480.0f, 320.0f, 1.0f);
    }

    // Initial position — actual Y computed each frame in Draw with
    // the slide-in curve. Start off-screen above.
    pos = Vec3(ABOUT_BG_X, ABOUT_BG_Y_OFS + size.y * 0.5f, 0.0f);
}

AboutScreen::~AboutScreen() {
    RemoveButtons();
}

void AboutScreen::Init() {
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

void AboutScreen::Release() {
    RemoveButtons();
}

void AboutScreen::CreateButtons() {
    if (m_bButtonsCreated) return;

    // Back button — plain button with back_icon.tex. Port reuses
    // MenuButton with no fruit entity (fruitType = -1 means "toggle
    // style" — no spinning fruit decoration).
    m_pBackButton = new MenuButton();
    m_pBackButton->size = Vec3(48.0f, 48.0f, 1.0f);
    m_pBackButton->Init(POS_BACK_BUTTON,
                        [this]() { BackCallback(); },
                        -1, Vec3(0, 0, 0), nullptr);
    m_pBackButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pBackButton);

    m_bButtonsCreated = true;
    printf("[AboutScreen] Created back button\n");
}

void AboutScreen::RemoveButtons() {
    if (m_pBackButton)    { m_pBackButton->SetPendingRemoval();    m_pBackButton    = NULL; }
    if (m_pCreditsButton) { m_pCreditsButton->SetPendingRemoval(); m_pCreditsButton = NULL; }
    m_bButtonsCreated = false;
}

void AboutScreen::Update(float dt) {
    (void)dt;

    switch (m_State) {
    case 0: {
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;
        if (m_TransitionAlpha > 0.4f) CreateButtons();
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            m_State = 1;
        }
        break;
    }
    case 1:
        // Idle.
        break;
    case 2:
        m_TransitionAlpha *= ALPHA_DECAY;
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            m_TransitionAlpha = 0.0f;
            RemoveButtons();
            m_bPendingRemoval = 1;  // HUD::Update will delete us next frame
            // DojoScreen polls IsPendingRemoval and resumes state 1
        }
        break;
    default:
        break;
    }
}

void AboutScreen::Draw(const Vec3& hudScale, int layerMask) {
    if ((layerMask & m_LayerFlags) == 0) return;
    if (m_TransitionAlpha <= 0.0f) return;

    // Slide-in Y curve from the binary:
    //   start = 160 + height*0.5
    //   drawn = start - (start - 63.0) * m_TransitionAlpha
    // At alpha=0: drawn = start (off screen top). At alpha=1: drawn = 63.
    const float h     = size.y;
    const float start = ABOUT_BG_Y_OFS + h * 0.5f;
    const float drawn = start - (start - ABOUT_BG_Y_END) * m_TransitionAlpha;

    pos = Vec3(ABOUT_BG_X, drawn, 0.0f);
    m_DrawColour.a = (uint8_t)(m_TransitionAlpha * 255.0f);
    HUDControl3d::Draw(hudScale, layerMask);

    // TODO: second pass — credits.tex. Binary slides it up from
    // Y = -h*0.5 - 160 to 96 using the same alpha curve, with a
    // separate textured quad draw. Needs a helper that doesn't
    // overwrite `this->pos` since HUDControl3d::Draw uses it.
}

void AboutScreen::BackCallback() {
    printf("[AboutScreen] Back -> fade out\n");
    m_State = 2;
}
