// AboutScreen — credits/about page launched from DojoScreen.
// Binary refs: ctor 0x0012ecb8, LoadContent 0x0012ec14,
//              Update 0x0012f020, Draw 0x0012f394
//
// Analysed: 2026-04-25T10:00

#include "AboutScreen.h"
#include "DojoScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/Font.h"
#include "math/Colour.h"
#include <cstdio>

// -----------------------------------------------------------------------
// Constants  (resolved from binary via read_memory)
// -----------------------------------------------------------------------

// Transition alpha thresholds / rates
// DAT_0012f2fc = 0.9990  (alpha-in done)
// DAT_0012f2f8 = 480.0   (button pos)
// 0.125 = step (from decompile: 1/8 = 0.125)
// DAT_0012f328 = 0x3A83126F = 0.001 (decay done threshold)
static const float ALPHA_LERP_IN  = 0.125f;     // DAT derived
static const float ALPHA_IN_DONE  = 0.9990f;    // DAT_0012f2fc
static const float ALPHA_DECAY    = 0.75f;      // from decompile
static const float ALPHA_OUT_DONE = 0.001f;     // DAT_0012f328

// Back button position  (DAT_0012f300 = 185.0, DAT_0012f304 = -106.0)
// Post-Init, the binary scales m_TargetSize and fruit piece by 0.825
// via Vec3_ScaleConst @ 0x0012e6bc (DAT_0012e6e8 = 0.825).
static const Vec3 POS_BACK_BUTTON(185.0f, -106.0f, 0.0f);
static const float BACK_SCALE = 0.825f;   // DAT_0012e6e8

// OFN button position  (DAT_0012f2f4 = 0.0, DAT_0012f2f8 = 480.0)
// X = 0, Y = 480  (off screen right, OpenFeint/GameCenter stub)
static const Vec3 POS_OFN_BUTTON(0.0f, 480.0f, 0.0f);

// ---- Draw constants ----

// Background panel (field_0x74 = haiku tex):
//   Y_start    = DAT_0012f690(160) + tex_h * 0.5   (one-time cached)
//   Y_drawn    = Y_start - (Y_start - DAT_0012f694(63)) * alpha
//   X = DAT_0012f698 = -50
//   Z = DAT_0012f69c = 0
static const float BG_X         = -50.0f;   // DAT_0012f698
static const float BG_Y_CACHE   = 160.0f;   // DAT_0012f690  (added to tex_h*0.5)
static const float BG_Y_REST    =  63.0f;   // DAT_0012f694

// OFN button follows the panel, offset by:
//   DAT_0012f6a0 = 132.0, DAT_0012f6a4 = 70.0 (added to BG pos)
static const float OFN_OFFSET_X = 132.0f;   // DAT_0012f6a0
static const float OFN_OFFSET_Y =  70.0f;   // DAT_0012f6a4

// Haiku text draw (font, version string):
//   Y = BG_Y_drawn + DAT_0012f6a8(97) - 10
//   (two font draws: haiku string [RTTI garbage in binary] and GetVersionString())
//   font scale = DAT_0012f6ac = -200.0 (used as left x position for DrawString)
//   max_width  = DAT_0012f6b0 = 200.0
//   colour     = RGB(0x74, 0x5D, 0x3C)  (from binary MakeColour_RGB calls)
static const float FONT_TEXT_Y_OFFSET = 97.0f;   // DAT_0012f6a8
static const float FONT_X            = -200.0f;  // DAT_0012f6ac (left edge)
static const float FONT_MAX_W        =  200.0f;  // DAT_0012f6b0
// Version string X offset: -(str_width * 14.0) where str_width = MeasureString
// DAT_0012f6b4 = 0.3   (used as: 0.3*bgW - 50 for sensei overlay X)
static const float SENSEI_FRAC  = 0.3f;     // DAT_0012f6b4
static const float SENSEI_X_OFS = 50.0f;    // DAT_0012f6b8

// Credits tex (block 3 — slides up from below):
//   Y_start = tex_h * -0.5 - DAT_0012f8d8(160)   (one-time cached)
//   Y_drawn = Y_start - (Y_start + DAT_0012f8dc(96)) * alpha
//   X = DAT_0012f8e0 = -50
//   Z = DAT_0012f8e4 = 0
static const float CREDITS_X        = -50.0f;   // DAT_0012f8e0
static const float CREDITS_Y_CACHE  = 160.0f;   // DAT_0012f8d8
static const float CREDITS_Y_TARGET = -96.0f;   // at alpha=1: Y_start - (Y_start+96) = -96
static const float CREDITS_Y_OFS    =  96.0f;   // DAT_0012f8dc (added to cached Y)

// Sensei / haiku tex (block 4 — slides in from right):
//   X_start = DAT_0012f8e8(240) + tex_w * 0.5   (one-time cached)
//   X_drawn = X_start - (X_start - DAT_0012f8ec(155)) * alpha
//   Y = DAT_0012f8f0 = 56
//   Z = DAT_0012f8e4 = 0
static const float SENSEI2_X_CACHE  = 240.0f;   // DAT_0012f8e8
static const float SENSEI2_X_REST   = 155.0f;   // DAT_0012f8ec
static const float SENSEI2_Y        =  56.0f;   // DAT_0012f8f0

// -----------------------------------------------------------------------
// Static storage
// -----------------------------------------------------------------------
SmartPtr<Mortar::Texture> AboutScreen::s_TexHaiku;
SmartPtr<Mortar::Texture> AboutScreen::s_TexCredits;
SmartPtr<Mortar::Texture> AboutScreen::s_TexSensei;
SmartPtr<Mortar::Texture> AboutScreen::s_TexBackIcon;
bool AboutScreen::s_bContentLoaded = false;

// -----------------------------------------------------------------------
// GetVersionString — returns a C string for the version number.
// Binary: GetVersionString @ 0x0010d594 reads a GOT-relative string object.
// Port: return a hardcoded placeholder; replace when version tracking is wired.
// DIFFERS: binary reads a runtime-constructed version string from GOT/BSS;
//          port uses a literal until version infrastructure is implemented.
// -----------------------------------------------------------------------
static const char* GetVersionString()
{
    return "1.0.0";
}

// -----------------------------------------------------------------------
// AboutScreen::LoadContent  @ 0x0012ec14
// Loads 3 textures into static storage (once per process).
// -----------------------------------------------------------------------
// static
void AboutScreen::LoadContent()
{
    if (s_bContentLoaded) return;

    // Binary order: haikus.tex, credits.tex, sensei.tex
    // (string addrs 0x001BAE10, 0x001BAE1B, 0x001BB4D7)
    s_TexHaiku   = Mortar::TextureManager::LoadLocalisedTexture("haikus.tex");
    s_TexCredits = Mortar::TextureManager::LoadLocalisedTexture("credits.tex");
    s_TexSensei  = Mortar::TextureManager::LoadLocalisedTexture("sensei.tex");
    // Port specific: binary reads back_icon from game->field_0x17c (a global
    // slot loaded once at game init). Port loads it locally so the back-button
    // ring renders on the AboutScreen back-bomb. Same path as DojoScreen.
    if (!s_TexBackIcon.IsValid())
        s_TexBackIcon = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");

    // Binary also loads openfeint_gamecenter.tex into its own SmartPtr slot
    // (offset DAT_0012eca0 area). Port skips — OFN is defunct.
    // TODO: if openfeint_gamecenter.tex is re-enabled, load it here.

    s_bContentLoaded = true;
}

// -----------------------------------------------------------------------
// AboutScreen::UnLoadContent
// -----------------------------------------------------------------------
// static
void AboutScreen::UnLoadContent()
{
    s_TexHaiku.SetNull();
    s_TexCredits.SetNull();
    s_TexSensei.SetNull();
    s_TexBackIcon.SetNull();
    s_bContentLoaded = false;
}

// -----------------------------------------------------------------------
// AboutScreen::AboutScreen  @ 0x0012ecb8
// -----------------------------------------------------------------------
AboutScreen::AboutScreen(Game& g, DojoScreen* parent)
    : game(g)
    , m_pParent(parent)
    , m_pOFNButton(nullptr)
    , m_pBackButton(nullptr)
    , m_TransitionAlpha(0.0f)   // DAT_0012ed88 = 0.0
    , m_State(0)                // field121_0x94 initial value = 0 (wait, m_State is field126_0x9c)
{
    // Binary: lazy-load content if not yet loaded (checks static flag)
    LoadContent();

    // field40_0x34 = 0x80 (layer flags, matches all other screens)
    m_LayerFlags = 0x80;

    // field_0x32 = 0 (m_bNoDestructor)
    m_bNoDestructor = 0;

    // field119_0x8c = 0 (back button ptr)
    // field126_0x9c = 0 (state = 0)
    // Already set by member init above.

    // Copy s_TexHaiku SmartPtr into field101_0x74 (per-instance copy for
    // dimension queries). Binary: SmartPtr::operator=(field_0x74, *(static_ptr))
    m_TexHaiku = s_TexHaiku;

    // field_0x98 SmartPtr initialized to null (SmartPtr::SmartPtr default)
    // m_TexOFNOverlay stays null — OFN defunct.

    printf("[AboutScreen] ctor: haiku=%d credits=%d sensei=%d parent=%p\n",
           s_TexHaiku.IsValid(), s_TexCredits.IsValid(), s_TexSensei.IsValid(),
           (void*)parent);
}

// -----------------------------------------------------------------------
// AboutScreen::~AboutScreen  @ 0x0012eee0
// -----------------------------------------------------------------------
AboutScreen::~AboutScreen()
{
    Release();
    // SmartPtr dtors for m_TexHaiku and m_TexOFNOverlay called implicitly.
}

// -----------------------------------------------------------------------
// HUDControl::Init  override
// -----------------------------------------------------------------------
void AboutScreen::Init()
{
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

// -----------------------------------------------------------------------
// HUDControl::Release  override
// Called from dtor and on demand.
// -----------------------------------------------------------------------
void AboutScreen::Release()
{
    RemoveBackButton();

    if (m_pOFNButton) {
        m_pOFNButton->SetPendingRemoval();
        m_pOFNButton = nullptr;
    }
}

// -----------------------------------------------------------------------
// CreateBackButton — lazily creates back button at state-0 completion.
// Binary: Update @ 0x0012f020, second block when alpha > DAT_0012f2fc.
// -----------------------------------------------------------------------
void AboutScreen::CreateBackButton()
{
    if (m_pBackButton) return;

    // Texture: binary reads game->field_0x17c (a Texture* to back_icon.tex).
    // Port: use DojoScreen's static s_TexBackIcon if accessible, else skip.
    // DIFFERS: binary gets texture from game->field_0x17c; port accesses
    //          DojoScreen::s_TexBackIcon (same texture, different path).
    // TODO: expose DojoScreen::s_TexBackIcon or use a shared back-icon slot.

    // Fruit type: binary reads *(int**)(update_base + DAT_0012f324) which is
    // the bomb-threshold (FruitInfo_GetCount()). Matches DojoScreen pattern.
    const int bombFruitType = FruitInfo_GetCount();

    m_pBackButton = new MenuButton();
    // Wire the back-icon ring texture (binary reads game->field_0x17c).
    // Without this the menu-button quad renders as an untextured square.
    if (s_TexBackIcon.IsValid()) {
        m_pBackButton->m_Texture = s_TexBackIcon->m_TexId;
        m_pBackButton->size = Vec3(
            (float)(s_TexBackIcon->m_Width  + 1),
            (float)(s_TexBackIcon->m_Height + 1),
            1.0f);
    } else {
        m_pBackButton->size = Vec3(65.0f, 65.0f, 1.0f);
    }

    m_pBackButton->Init(POS_BACK_BUTTON,
                        [this]() { BackCallback(); },
                        bombFruitType,
                        Vec3(0.0f, 0.0f, 0.0f),
                        nullptr);

    // Binary: strb 1 at button+0x138 (m_bRemovalPending? no — m_bVisible).
    // From MenuButton.h: +0x121 = m_bVisible, +0x138 = m_bRemovalPending.
    // Disasm at 0x0012f2bc: strb.w r6,[r3,#0x138] where r6=1.
    // +0x138 = m_bRemovalPending — but that would immediately remove the button.
    // Actually: the strb sets m_bRemovalPending to 0 first? No, r6=1 at 0x0012f29c.
    // Looking more carefully at context: this is after AddControl, sets a "seen" flag.
    // DIFFERS: exact semantic of +0x138 write unclear; port skips for now.

    // Binary post-Init scales: Vec3_ScaleConst(field_0x8c + 0x124) and fruit piece scale
    m_pBackButton->m_TargetSize = m_pBackButton->m_TargetSize * BACK_SCALE;
    if (m_pBackButton->m_pFruitPiece) {
        m_pBackButton->m_pFruitPiece->scale =
            m_pBackButton->m_pFruitPiece->scale * BACK_SCALE;
    }

    m_pBackButton->m_LayerFlags = 0x40;
    game.hud->AddControl(m_pBackButton);

    // Binary: TutorialControl::ResetTutePos(game->field_0x168, back_button)
    if (game.pTutorialCtrl) {
        game.pTutorialCtrl->ResetTutePos(m_pBackButton);
    }
}

// -----------------------------------------------------------------------
// RemoveBackButton
// -----------------------------------------------------------------------
void AboutScreen::RemoveBackButton()
{
    if (m_pBackButton) {
        m_pBackButton->SetPendingRemoval();
        m_pBackButton = nullptr;
    }
}

// -----------------------------------------------------------------------
// AboutScreen::Update  @ 0x0012f020
// -----------------------------------------------------------------------
void AboutScreen::Update(float /*dt*/)
{
    // ---- OFN button creation (defunct — OpenFeint/GameCenter) ----
    // Binary: if (s_TexSensei valid AND m_pOFNButton == nullptr):
    //   create OFN button at (0, 480, 0) with AskUserToChoosePreferredNetwork
    //   callback and openfeint_gamecenter.tex texture.
    // Port: s_TexSensei is the sensei animation tex, not the OFN tex.
    //       OFN is defunct so we never create this button.
    //       The check still gates on s_TexSensei validity in binary.
    // DIFFERS: port stubs this block. No OFN button is created.
    if (s_TexSensei.IsValid() && m_pOFNButton == nullptr) {
        // TODO: if OpenFeint/GameCenter is re-enabled, create button here
        // at POS_OFN_BUTTON = (0, 480, 0) with AskUserToChoosePreferredNetwork callback.
        // Binary also copies sound callback (MenuCallbackClicked) as delete callback.
        // For now: mark the slot with a sentinel so we don't re-enter.
        // Binary sets field121_0x94 = new MenuButton (we store in m_pOFNButton).
        // The port intentionally omits the actual button allocation here.
        // We still guard against re-entry by noting we've seen the sensei texture.
        // Real implementation note: m_pOFNButton would be created at POS_OFN_BUTTON
        // and follow the background panel in Draw using OFN_OFFSET_X/Y.
        (void)POS_OFN_BUTTON;
    }

    // ---- State machine ----
    switch (m_State) {

    case 0: {
        // Lerp alpha toward 1.0 exponentially
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;

        // When alpha crosses 0.9990 (DAT_0012f2fc): create back button,
        // clamp alpha to 1.0, advance to state 1.
        // ARM idiom: vcmpe / vmrs / ble — fires when alpha > ALPHA_IN_DONE
        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            CreateBackButton();
            m_State = 1;
        }
        break;
    }

    case 1:
        // Idle — buttons are interactive, nothing to do per-frame.
        break;

    case 2: {
        // Fade-out: alpha *= 0.75 each frame
        m_TransitionAlpha *= ALPHA_DECAY;

        // When alpha < 0.001 (DAT_0012f328):
        // Binary ARM idiom: bpl 0x0012f35a fires when NOT (alpha < 0.001),
        // so the block runs when alpha IS < 0.001 i.e. fade complete.
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            // Binary: call parent->vtable[Reset]() (vtable offset 0x10).
            // DojoScreen inherits HUDControl::Reset which is a no-op by
            // default. But the INTENT is to reset DojoScreen to state 0
            // (re-start its fade-in). Port calls Init() directly.
            if (m_pParent) {
                m_pParent->Init();
            }

            // Binary: this->field_0x33 = 1  (m_bPendingRemoval)
            // HUD::Update will delete us next frame, which fires m_RemoveCallback.
            m_bPendingRemoval = 1;
        }
        break;
    }

    default:
        break;
    }
}

// -----------------------------------------------------------------------
// AboutScreen::Draw  @ 0x0012f394
// Four render passes:
//   A) haiku background panel   (field_0x74 = s_TexHaiku) — slides down from top
//   B) OFN/GameCenter overlay   (field_0x98 = null in port) — on top of panel
//   C) credits tex              (s_TexCredits) — slides up from bottom
//   D) sensei tex               (s_TexSensei)  — slides in from right
// -----------------------------------------------------------------------
void AboutScreen::Draw(const Vec3& /*hudScale*/, int /*layerMask*/)
{
    // Layer check and alpha guard
    if (m_TransitionAlpha <= 0.0f) return;

    Mortar::MatrixManager& mm  = Mortar::MatrixManager::GetInstance();
    Renderer*              r   = Renderer::GetInstance();
    if (!r) return;

    const float alpha = m_TransitionAlpha;

    // ================================================================
    // Block A: haiku background panel (field101_0x74 = s_TexHaiku)
    // ================================================================
    if (m_TexHaiku.IsValid()) {
        const float texW = (float)m_TexHaiku->m_Width;
        const float texH = (float)m_TexHaiku->m_Height;

        // Binary computes Y_start once (one-time cached in BSS guard).
        // Port recomputes each frame — same result, avoids BSS guard pattern.
        // Y_start = BG_Y_CACHE(160) + texH * 0.5
        const float yStart = BG_Y_CACHE + texH * 0.5f;

        // Y_drawn = yStart - (yStart - BG_Y_REST(63)) * alpha
        const float yDrawn = yStart - (yStart - BG_Y_REST) * alpha;

        // Position the OFN button to follow the panel (if created)
        if (m_pOFNButton) {
            m_pOFNButton->pos = Vec3(
                BG_X + OFN_OFFSET_X,
                yDrawn + OFN_OFFSET_Y,
                0.0f);
        }

        // Draw background quad
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(BG_X, yDrawn, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_TexHaiku->m_TexId);
        r->DrawQuad(Colour(255, 255, 255, 255));
        glBindTexture(GL_TEXTURE_2D, 0);

        // ---- Font draws (version text) ----
        // Binary draws two strings via Font::DrawString:
        //   1. A Utf8String at draw_base + DAT_0012f6cc (= RTTI garbage in this build)
        //   2. GetVersionString() — the game version number
        // Both at pos = Vec3(FONT_X(-200), yDrawn + FONT_TEXT_Y_OFFSET(97) - 10, 0).
        // Font comes from game.pFontMain (+0x54). Scale = 1.0f, maxWidth = FONT_MAX_W(200).
        // Colour = RGB(0x74, 0x5D, 0x3C) = warm brown.
        //
        // Port: skip draw #1 (the Utf8String is RTTI garbage in the Bada binary;
        //   the actual haiku text lives in haikus.tex as a pre-rendered image).
        //   Draw #2 (version string) uses Font::DrawString when pFontMain is available.
        if (game.pFontMain.IsValid()) {
            const Vec3 fontPos(FONT_X, yDrawn + FONT_TEXT_Y_OFFSET - 10.0f, 0.0f);
            const Colour fontColour(0x74, 0x5D, 0x3C, 255);
            game.pFontMain->DrawString(1.0f, FONT_MAX_W, 0.0f,
                                       GetVersionString(), fontPos,
                                       fontColour, Mortar::FONT_ALIGN_LEFT);
        }

        // ================================================================
        // Block B: OFN overlay texture (field_0x98) — null in port
        // ================================================================
        // Binary: if (field_0x98.IsValid()):
        //   draw at (SENSEI_FRAC(0.3)*texW - SENSEI_X_OFS(50), yDrawn + SENSEI_FRAC*texH, 0)
        // Port: m_TexOFNOverlay is always null (OFN defunct), block is a no-op.
        if (m_TexOFNOverlay.IsValid()) {
            const float ovW = (float)m_TexOFNOverlay->m_Width;
            const float ovH = (float)m_TexOFNOverlay->m_Height;
            mm.GetWorldStack().Reset();
            Matrix44 mOv = Matrix44::MakeScale(ovW + 1.0f, ovH + 1.0f, 1.0f);
            mOv.GlobalTranslate44(Vec3(
                SENSEI_FRAC * texW - SENSEI_X_OFS,
                yDrawn + SENSEI_FRAC * texH,
                0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mOv);
            mm.UploadModelViewOnly();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_TexOFNOverlay->m_TexId);
            r->DrawQuad(Colour(255, 255, 255, 255));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // ================================================================
    // Block C: credits.tex — slides up from below screen
    // ================================================================
    if (s_TexCredits.IsValid()) {
        const float texH = (float)s_TexCredits->m_Height;

        // Y_start = texH * -0.5 - CREDITS_Y_CACHE(160)  (off bottom)
        // Y_drawn = Y_start - (Y_start + CREDITS_Y_OFS(96)) * alpha
        const float yStart = texH * -0.5f - CREDITS_Y_CACHE;
        const float yDrawn = yStart - (yStart + CREDITS_Y_OFS) * alpha;

        mm.GetWorldStack().Reset();
        const float texW = (float)s_TexCredits->m_Width;
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(CREDITS_X, yDrawn, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_TexCredits->m_TexId);
        r->DrawQuad(Colour(255, 255, 255, 255));
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ================================================================
    // Block D: sensei.tex — slides in from the right
    // ================================================================
    // Binary uses Mesh::DrawQuadUnCached for this block (not DrawQuad_Colour_Draw).
    // Port uses r->DrawQuad — same net effect.
    if (s_TexSensei.IsValid()) {
        const float texW = (float)s_TexSensei->m_Width;
        const float texH = (float)s_TexSensei->m_Height;

        // X_start = SENSEI2_X_CACHE(240) + texW * 0.5  (off right edge)
        // X_drawn = X_start - (X_start - SENSEI2_X_REST(155)) * alpha
        const float xStart = SENSEI2_X_CACHE + texW * 0.5f;
        const float xDrawn = xStart - (xStart - SENSEI2_X_REST) * alpha;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(xDrawn, SENSEI2_Y, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_TexSensei->m_TexId);
        r->DrawQuad(Colour(255, 255, 255, 255));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// -----------------------------------------------------------------------
// BackCallback — pressed back button starts fade-out
// Binary: Delegate0<void>::QCallee<AboutScreen> wrapping this method
// -----------------------------------------------------------------------
void AboutScreen::BackCallback()
{
    printf("[AboutScreen] BackCallback -> state 2 (fade out)\n");
    m_State = 2;
}
