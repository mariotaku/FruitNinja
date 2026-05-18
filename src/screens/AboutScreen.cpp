// AboutScreen — credits/about page launched from DojoScreen.
// Binary refs: ctor 0x0012ecb8, LoadContent 0x0012ec14,
//              Update 0x0012f020, Draw 0x0012f394
//
// Analysed: 2026-04-25T10:00

#include "AboutScreen.h"
#include "DojoScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/Font.h"
#include "math/Colour.h"
#include "audio/GameSound.h"
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

// OFN button position. Binary @ 0x0012f04e-0x0012f060 builds Vec3(480, 0, 0)
// (s0=480, s1=0, s2=0) — off-screen right. Stub in port (defunct OFN).
static const Vec3 POS_OFN_BUTTON(480.0f, 0.0f, 0.0f);

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
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexHaiku;
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexCredits;
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexSensei;
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexBackIcon;
bool AboutScreen::s_bContentLoaded = false;

// -----------------------------------------------------------------------
// GetVersionString — returns a C string for the version number.
// Binary: Game::SelfVersion @ 0x0010d9ec returns the literal at .rodata
// 0x001B9938 ("1.5.1"). GetVersionString @ 0x0010d594 reads the same
// constant via the MortarGame::m_VersionStr slot populated by SetVersion.
// -----------------------------------------------------------------------
static const char* GetVersionString()
{
    return "1.5.1";
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
    // (offset DAT_0012eca0 area). Port skips — OFN is defunct (online-services-audit).

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
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

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
    // Note: AboutScreen has its own s_TexBackIcon copy loaded in LoadContent;
    //       field_0x17c is a shared global slot not yet in Game struct.

    // Fruit type: binary reads *(int**)(update_base + DAT_0012f324) which is
    // the bomb-threshold (FruitInfo_GetCount()). Matches DojoScreen pattern.
    const int bombFruitType = FruitInfo_GetCount();

    // Binary @ 0x0012f1a0..0x0012f242: caller does NOT pre-set size from
    // texture dims. MenuButton::Init's bomb-branch writes:
    //   m_TargetSize = (1,1,1) * 2 * FruitInfo_GetBombSize() = (110,110,110)
    // which the post-Init *= 0.825 then scales to (90.75, 90.75, 90.75).
    m_pBackButton = new MenuButton();
    if (s_TexBackIcon.IsValid()) {
        m_pBackButton->m_Texture = s_TexBackIcon;
    }

    m_pBackButton->Init(POS_BACK_BUTTON,
                        Mortar::Delegate0<void>::Make(this, &AboutScreen::BackCallback),
                        bombFruitType,
                        Vec3(0.0f, 0.0f, 0.0f),
                        nullptr);

    // Binary @ 0x0012f2a8-0x0012f2ea step order:
    //   1. virtual Init (already covered by Init() above — vtable[2] is empty)
    //   2. strb 1 at button+0x138 = m_bRespondsToBackKey
    //   3. game.hud->AddControl(button)
    //   4. TutorialControl::ResetTutePos(button)
    //   5. scale m_TargetSize *= 0.825
    //   6. scale m_pFruitPiece->scale *= 0.825
    m_pBackButton->m_bRespondsToBackKey = 1;
    game.hud->AddControl(m_pBackButton);

    if (game.pTutorialCtrl) {
        game.pTutorialCtrl->ResetTutePos(m_pBackButton);
    }

    m_pBackButton->m_TargetSize = m_pBackButton->m_TargetSize * BACK_SCALE;
    if (m_pBackButton->m_pFruitPiece) {
        m_pBackButton->m_pFruitPiece->scale =
            m_pBackButton->m_pFruitPiece->scale * BACK_SCALE;
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
// ASM-verified: 2026-04-29 binary @ 0x0012f020..0x0012f360 (asm-inspector)
// State machine + transitions + animation formulas all match.
// -----------------------------------------------------------------------
void AboutScreen::Update(float /*dt*/)
{
    // ---- OFN button creation (defunct — OpenFeint/GameCenter) ----
    // Binary: if (s_TexSensei valid AND m_pOFNButton == nullptr):
    //   create OFN button at (480, 0, 0) with AskUserToChoosePreferredNetwork
    //   callback and openfeint_gamecenter.tex texture.
    // Port: s_TexSensei is the sensei animation tex, not the OFN tex.
    //       OFN is defunct so we never create this button.
    //       The check still gates on s_TexSensei validity in binary.
    // DIFFERS: port stubs this block. No OFN button is created.
    if (s_TexSensei.IsValid() && m_pOFNButton == nullptr) {
        // Note: OpenFeint/GameCenter button is omitted (defunct per online-services-audit).
        // Binary creates MenuButton at POS_OFN_BUTTON with AskUserToChoosePreferredNetwork
        // callback; port intentionally skips. Guard re-entry via m_pOFNButton sentinel below.
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
            // Binary @ 0x0012f350: parent->vtable[+0x10] = Reset().
            // DojoScreen::Reset @ 0x0013767c writes only the BaseScreen
            // m_State field to 0, bringing DojoScreen back to its state 0
            // (re-fade-in). ASM-verified 2026-04-29.
            if (m_pParent) {
                m_pParent->Reset();
            }
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
// ASM-verified: 2026-04-29 binary @ 0x0012f394..0x0012f8d2 (asm-inspector)
// All 4 draw blocks (haiku panel / OFN overlay / credits slide / sensei
// slide) and their Y/X interpolation formulas match the binary.
void AboutScreen::Draw(const Vec3& /*hudScale*/, int /*layerMask*/)
{
    // Layer check and alpha guard
    if (m_TransitionAlpha <= 0.0f) return;

    MatrixManager& mm  = MatrixManager::GetInstance();
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

        m_TexHaiku->Set();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_TexHaiku->UnSet();

        // ---- Font draws (version text "V1.5.1") ----
        // Binary draws two Font::DrawString calls back-to-back:
        //   1. "V" — 1-char literal at .rodata 0x001BAE40 (drawn at FONT_X)
        //   2. GetVersionString() — "1.5.1" at .rodata 0x001B9938 (drawn
        //      immediately right of "V" via MeasureString("V") * scale)
        // Both share scale=14.0f, maxWidth=FONT_MAX_W(200), colour
        // RGB(0x74,0x5D,0x3C). pFontMain comes from game (+0x54).
        // Net displayed text: "V1.5.1".
        if (game.pFontMain.IsValid()) {
            // Reset the world matrix before Font::DrawString. The haiku
            // quad draw above left a Scale(texW+1, texH+1, 1) on the
            // stack; Font::DrawString does Push+Scale(scale,scale,1) and
            // would multiply into that existing scale, blowing up the
            // glyphs by ~256-512x. Binary: each Draw block resets the
            // matrix before its own draw.
            mm.GetWorldStack().Reset();
            mm.UploadModelViewOnly();

            // ASM-verified: 2026-05-17 binary @ 0x0012f49a..0x0012f4f0 (re-analyst).
            // Scale = 14.0f (vmov.f32 s3,#0x41600000 inline immediate).
            const float versionScale = 14.0f;
            const float fontY = yDrawn + FONT_TEXT_Y_OFFSET - 10.0f;
            const Colour fontColour(0x74, 0x5D, 0x3C, 255);

            const char* kVerPrefix = "V";
            game.pFontMain->DrawString(versionScale, FONT_MAX_W, 0.0f,
                                       kVerPrefix,
                                       Vec3(FONT_X, fontY, 0.0f),
                                       fontColour, Mortar::FONT_ALIGN_LEFT);

            // Binary caches MeasureString("V") * 14 in BSS via __cxa_guard
            // one-time init; port recomputes each frame (same numeric result).
            // DIFFERS: port skips the one-time-init guard.
            const float prefixW = game.pFontMain->MeasureString(kVerPrefix) * versionScale;
            game.pFontMain->DrawString(versionScale, FONT_MAX_W, 0.0f,
                                       GetVersionString(),
                                       Vec3(prefixW - FONT_MAX_W, fontY, 0.0f),
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
            m_TexOFNOverlay->Set();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_TexOFNOverlay->UnSet();
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

        s_TexCredits->Set();
        r->DrawQuad(Colour(255, 255, 255, 255));
        s_TexCredits->UnSet();
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

        s_TexSensei->Set();
        r->DrawQuad(Colour(255, 255, 255, 255));
        s_TexSensei->UnSet();
    }
}

// -----------------------------------------------------------------------
// BackCallback — pressed back button starts fade-out
// Binary: Mortar::Delegate0<void>::QCallee<AboutScreen> wrapping this method
// -----------------------------------------------------------------------
void AboutScreen::BackCallback()
{
    m_State = 2;
}

// Binary @ 0x0012eb30 (re-analyst 2026-05-18). Plays menu-bomb SFX,
// transitions to fade-out state 2, repositions tutorial ninja to a
// random off-screen point. Mirrors the binary's exit handler when the
// player taps the AboutScreen's quit/back-out button.
void AboutScreen::QuitGameCallback() {
    if (game.pGameSound) {
        game.pGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }
    m_State = 2;
    if (game.pTutorialCtrl) {
        // Binary randomises off-screen target via RandFloat5() (≈ [0,5)).
        // Simple rand() fallback -- the exact distribution is cosmetic.
        float rx = ((float)(rand() % 500) / 100.0f) + 5.0f;   // [5, 10)
        float ry = -((float)(rand() % 500) / 100.0f);          // (-5, 0]
        game.pTutorialCtrl->ResetTutePos(Vec3(rx, ry, 0.0f));
    }
}
