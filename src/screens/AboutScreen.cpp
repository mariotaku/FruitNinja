// AboutScreen -- credits/about page launched from DojoScreen.
// v1.6.1 binary refs:
//   ctor         0x0015b764  (AboutScreen::AboutScreen(DojoScreen*))
//   LoadContent  0x0015b6d4  (static)
//   Draw         0x0015a654  (board panel + sensei quads)
//   NewDraw      0x0015a264  (BakedStringBox credit text pass)

#include "AboutScreen.h"
#include "DojoScreen.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "math/Colour.h"
#include "math/Vec2.h"
#include "audio/GameSound.h"
#include "util/StringTable.h"
#include <cstdio>
#include "game/GameWork.h"

// -----------------------------------------------------------------------
// Constants  (resolved from binary via read_memory)
// -----------------------------------------------------------------------

// Transition alpha thresholds / rates
// DAT_0012f2fc = 0.9990  (alpha-in done)
// 0.125 = step (from decompile: 1/8 = 0.125)
// DAT_0012f328 = 0x3A83126F = 0.001 (decay done threshold)
static const float ALPHA_LERP_IN  = 0.125f;
static const float ALPHA_IN_DONE  = 0.9990f;
static const float ALPHA_DECAY    = 0.75f;
static const float ALPHA_OUT_DONE = 0.001f;

// Back button position  (DAT_0012f300 = 185.0, DAT_0012f304 = -106.0)
static const Vec3 POS_BACK_BUTTON(185.0f, -106.0f, 0.0f);
static const float BACK_SCALE = 0.825f;

// OFN button position -- off-screen right, defunct
static const Vec3 POS_OFN_BUTTON(480.0f, 0.0f, 0.0f);

// ---- Draw constants ----

// Background panel (haiku tex):
//   Y_start = BG_Y_CACHE(160) + texH * 0.5
//   Y_drawn = Y_start - (Y_start - BG_Y_REST(63)) * alpha
//   X = BG_X = -50
// ASM-spec v1.6.1 AboutScreen::Draw @0x0015a654: panel slide formula unchanged.
static const float BG_X         = -50.0f;
static const float BG_Y_CACHE   = 160.0f;
static const float BG_Y_REST    =  63.0f;

// OFN button follows the panel (defunct)
static const float OFN_OFFSET_X = 132.0f;
static const float OFN_OFFSET_Y =  70.0f;

// Sensei tex (block D -- slides in from right):
//   X_start = SENSEI2_X_CACHE(240) + texW * 0.5
//   X_drawn = X_start - (X_start - SENSEI2_X_REST(155)) * alpha
//   Y = SENSEI2_Y = 56
static const float SENSEI_FRAC   = 0.3f;
static const float SENSEI_X_OFS  = 50.0f;
static const float SENSEI2_X_CACHE = 240.0f;
static const float SENSEI2_X_REST  = 155.0f;
static const float SENSEI2_Y       =  56.0f;

// -----------------------------------------------------------------------
// Static storage
// -----------------------------------------------------------------------
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexHaiku;    // binary: s_boardTexture
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexCredits;  // binary: m_creditsTexture
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexSensei;   // binary: m_senseiTexture
static Mortar::SmartPtr<Mortar::Texture> s_TexBackIcon;
// s_bContentLoaded: not ported (redundant -- never read)

// -----------------------------------------------------------------------
// GetVersionString
// ASM-spec v1.6.1 Game::SelfVersion @0x0011fbd8: returns literal "1.6.1".
// DIFFERS: v1.5.1 port had "1.5.1"; updated to match v1.6.1 binary.
// -----------------------------------------------------------------------
static const char* GetVersionString()
{
    return "1.6.1";
}

// -----------------------------------------------------------------------
// GetAboutTTFFont
// The binary reads the shared gangofchinese.ttf face from a global slot.
// Port mirrors the pattern used by FruitFactPage/BSButton: file-static
// SmartPtr<Font> + FontTTFRegistry lookup.
// DIFFERS: original = *(g_GameData+0x614) shared face; port uses a
//   file-local SmartPtr<Font> + FontTTFRegistry because game_work has not
//   been extended past 0x610 to carry the +0x614 slot.
// -----------------------------------------------------------------------
static Mortar::FontCacheObjectTTF* GetAboutTTFFont()
{
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// -----------------------------------------------------------------------
// AboutScreen::LoadContent  @ 0x0012ec14
// Binary loads haikus.tex, credits.tex, sensei.tex unconditionally
// (no early-return guard).
// -----------------------------------------------------------------------
// static
void AboutScreen::LoadContent()
{
    s_TexHaiku   = Mortar::TextureManager::LoadLocalisedTexture("haikus.tex");
    s_TexCredits = Mortar::TextureManager::LoadLocalisedTexture("credits.tex");
    s_TexSensei  = Mortar::TextureManager::LoadLocalisedTexture("sensei.tex");
}

// -----------------------------------------------------------------------
// AboutScreen::UnLoadContent  @ 0x0012efd8
// Binary nulls all three static texture SmartPtrs.
// -----------------------------------------------------------------------
// static
void AboutScreen::UnLoadContent()
{
    s_TexHaiku.SetNull();
    s_TexCredits.SetNull();
    s_TexSensei.SetNull();
    s_TexBackIcon.SetNull();
}

// -----------------------------------------------------------------------
// AboutScreen::AboutScreen  @ 0x0015b764
// v1.6.1 ctor: creates 9 BakedStringBox objects for title/heading/version
// and 6 credit lines.
// -----------------------------------------------------------------------
AboutScreen::AboutScreen(DojoScreen* parent)
    : m_TransitionAlpha(0.0f)
    , m_pBackButton(nullptr)
    , m_pParent(parent)
    , m_pOFNButton(nullptr)
    , m_State(0)
    , m_TitleBox(0)
    , m_HeadingBox(0)
    , m_VersionBox(0)
    , m_CreditLine0(0)
    , m_CreditLine1(0)
    , m_CreditLine2(0)
    , m_CreditLine3(0)
    , m_CreditLine4(0)
    , m_CreditLine5(0)
{
    LoadContent();

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
    m_bNoDestructor = 0;
    m_Texture = s_TexHaiku;

    // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764:
    // Creates BakedStringBox objects for all credit text. The binary uses
    // OS_SPrintf(buf, 0x200, "%s %s", "V", ver) for the version string.

    // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764: per-box BakedStringBox(fontSize,width,align)
    Mortar::FontCacheObjectTTF* font = GetAboutTTFFont();
    if (font) {
        // Title box -- LSTR 0x3c3
        // fontSize 20, width 0xa0(160), align 0xf, height 30, maxLines 1
        // Colour RGB(0xB9, 0x4F, 0x37), setBase=1
        m_TitleBox = new Mortar::BakedStringBox(font, 20.0f, 160.0f, 30.0f, 0xf, 1, 0.0f);
        m_TitleBox->SetText(Mortar::GETSTRING(LSTR_ABOUT_TITLE, 0));
        m_TitleBox->SetColour(Colour(0xB9, 0x4F, 0x37, 255), 1);

        // Heading box -- LSTR 0x349
        // fontSize 20, width 0x64(100), align 0xf, height 30, maxLines 1
        // Colour RGB(0xB9, 0x4F, 0x37), setBase=1
        // Constructed for layout parity; NOT drawn in NewDraw (binary never positions/draws it).
        m_HeadingBox = new Mortar::BakedStringBox(font, 20.0f, 100.0f, 30.0f, 0xf, 1, 0.0f);
        m_HeadingBox->SetText(Mortar::GETSTRING(LSTR_ABOUT_HEADING, 0));
        m_HeadingBox->SetColour(Colour(0xB9, 0x4F, 0x37, 255), 1);

        // Version box -- "V <ver>"
        // fontSize 10, width 0x50(80), align 1, height 30, maxLines 1
        // Colour RGB(0x74, 0x5D, 0x3C), setBase=1
        // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764:
        //   OS_SPrintf(buf, 0x200, "%s %s", "V", GetVersionString())
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "%s %s", "V", GetVersionString());
        m_VersionBox = new Mortar::BakedStringBox(font, 10.0f, 80.0f, 30.0f, 1, 1, 0.0f);
        m_VersionBox->SetText(vbuf);
        m_VersionBox->SetColour(Colour(0x74, 0x5D, 0x3C, 255), 1);
        m_VersionBox->SetHorizontalLineSpacing(-1.0f);

        // Credit lines 0..5 -- LSTR 0x34b..0x350
        // fontSize 12, width 0x140(320), align 0xf, height 30, maxLines 1
        // Colour: game_work.m_TitleColour (binary game_work+0x6a0), setBase=0
        // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764: credit line colour + per-box SetHorizontalLineSpacing(-1)
        const Colour& creditColour = game_work.m_TitleColour;

        m_CreditLine0 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine0->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT0, 0));
        m_CreditLine0->SetColour(creditColour, 0);
        m_CreditLine0->SetHorizontalLineSpacing(-1.0f);

        m_CreditLine1 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine1->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT1, 0));
        m_CreditLine1->SetColour(creditColour, 0);
        m_CreditLine1->SetHorizontalLineSpacing(-1.0f);

        m_CreditLine2 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine2->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT2, 0));
        m_CreditLine2->SetColour(creditColour, 0);
        m_CreditLine2->SetHorizontalLineSpacing(-1.0f);

        m_CreditLine3 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine3->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT3, 0));
        m_CreditLine3->SetColour(creditColour, 0);
        m_CreditLine3->SetHorizontalLineSpacing(-1.0f);

        m_CreditLine4 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine4->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT4, 0));
        m_CreditLine4->SetColour(creditColour, 0);
        m_CreditLine4->SetHorizontalLineSpacing(-1.0f);

        m_CreditLine5 = new Mortar::BakedStringBox(font, 12.0f, 320.0f, 30.0f, 0xf, 1, 0.0f);
        m_CreditLine5->SetText(Mortar::GETSTRING(LSTR_ABOUT_CREDIT5, 0));
        m_CreditLine5->SetColour(creditColour, 0);
        m_CreditLine5->SetHorizontalLineSpacing(-1.0f);

        // TODO: v1.6.1 AboutScreen ctor @0x0015b764 -- min-fontSize equalization pass across credit boxes not ported (no BakedStringBox font-size getter)
    }
}

// -----------------------------------------------------------------------
// AboutScreen::~AboutScreen
// -----------------------------------------------------------------------
AboutScreen::~AboutScreen()
{
    Release();

    delete m_TitleBox;    m_TitleBox    = 0;
    delete m_HeadingBox;  m_HeadingBox  = 0;
    delete m_VersionBox;  m_VersionBox  = 0;
    delete m_CreditLine0; m_CreditLine0 = 0;
    delete m_CreditLine1; m_CreditLine1 = 0;
    delete m_CreditLine2; m_CreditLine2 = 0;
    delete m_CreditLine3; m_CreditLine3 = 0;
    delete m_CreditLine4; m_CreditLine4 = 0;
    delete m_CreditLine5; m_CreditLine5 = 0;
}

// -----------------------------------------------------------------------
// HUDControl::Init  override
// -----------------------------------------------------------------------
void AboutScreen::Init()
{
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_Active = 1;
}

// -----------------------------------------------------------------------
// HUDControl::Release  override
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
// CreateBackButton
// -----------------------------------------------------------------------
void AboutScreen::CreateBackButton()
{
    if (m_pBackButton) return;

    const int bombFruitType = FruitInfo_GetCount();

    m_pBackButton = new MenuButton();

    // ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: button m_Texture = back_icon
    //   (game_work.pM_Textures[0x10]) set BEFORE Init; HUDControl3d::Draw early-returns on null m_Texture.
    if (!s_TexBackIcon.IsValid())
        s_TexBackIcon = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");
    m_pBackButton->m_Texture = s_TexBackIcon;

    m_pBackButton->Init(POS_BACK_BUTTON,
                        Mortar::Delegate0<void>::Make(this, &AboutScreen::BackCallback),
                        bombFruitType,
                        Vec3(0.0f, 0.0f, 0.0f),
                        nullptr);

    m_pBackButton->m_bRespondsToBackKey = 1;
    game_work.mHud->AddControl(m_pBackButton);

    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos(m_pBackButton);
    }

    m_pBackButton->m_RestScale = m_pBackButton->m_RestScale * BACK_SCALE;
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
// AboutScreen::Update
// -----------------------------------------------------------------------
void AboutScreen::Update(float /*dt*/)
{
    // OFN button creation stub (defunct -- OpenFeint/GameCenter)
    if (s_TexSensei.IsValid() && m_pOFNButton == nullptr) {
        (void)POS_OFN_BUTTON;
    }

    switch (m_State) {

    case 0: {
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;

        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            CreateBackButton();
            m_State = 1;
        }
        break;
    }

    case 1:
        break;

    case 2: {
        m_TransitionAlpha *= ALPHA_DECAY;

        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
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
// AboutScreen::NewDraw  @ 0x0015a264
// Draws BakedStringBox credit text over the haiku board panel.
// Called from Draw() after the textured quads, passing yDrawn (the
// panel's animated Y position).
//
// ASM-spec v1.6.1 AboutScreen::NewDraw @0x0015a264: SetTranslation flag 0, credit y-deltas.
//   base coords: x0 = (int)(BG_X - 160) = -210, y0 = (int)(yDrawn + 64)
//   CreditLine0:  SetTranslation(x0,       y0,         0), flag=0
//   CreditLine1:  SetTranslation(x0,       y0-0x14,    0), flag=0  (y0-20)
//   CreditLine2:  SetTranslation(x0,       y0-0x28,    0), flag=0  (y0-40)
//   CreditLine3:  SetTranslation(x0,       y0-0x4b,    0), flag=0  (y0-75)
//   CreditLine4:  SetTranslation(x0,       y0-0x5f,    0), flag=0  (y0-95)
//   CreditLine5:  SetTranslation(x0,       y0-0x73,    0), flag=0  (y0-115)
//   TitleBox:     SetTranslation(x0+0x50,  y0+0x1a,    0), flag=0  (x0+80, y0+26)
//   HeadingBox:   NOT positioned or drawn (binary never calls SetTranslation/Draw for it)
//   VersionBox:   SetTranslation(x0+5,     y0+0x15,    0), flag=0  (x0+5, y0+21)
//   Each box is drawn with Draw(0, Vec2(1,1), 1).
//   Marquee (alpha < 0.6): TODO -- not yet RE'd.
// -----------------------------------------------------------------------
void AboutScreen::NewDraw(float yDrawn)
{
    const int x0 = (int)(BG_X - 160.0f);    // -210
    const int y0 = (int)(yDrawn + 64.0f);

    // Credit lines
    if (m_CreditLine0) {
        m_CreditLine0->SetTranslation(Vec3((float)x0, (float)y0,            0.0f), 0);
        m_CreditLine0->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
    if (m_CreditLine1) {
        m_CreditLine1->SetTranslation(Vec3((float)x0, (float)(y0 - 0x14),   0.0f), 0);
        m_CreditLine1->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
    if (m_CreditLine2) {
        m_CreditLine2->SetTranslation(Vec3((float)x0, (float)(y0 - 0x28),   0.0f), 0);
        m_CreditLine2->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
    if (m_CreditLine3) {
        m_CreditLine3->SetTranslation(Vec3((float)x0, (float)(y0 - 0x4b),   0.0f), 0);
        m_CreditLine3->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
    if (m_CreditLine4) {
        m_CreditLine4->SetTranslation(Vec3((float)x0, (float)(y0 - 0x5f),   0.0f), 0);
        m_CreditLine4->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
    if (m_CreditLine5) {
        m_CreditLine5->SetTranslation(Vec3((float)x0, (float)(y0 - 0x73),   0.0f), 0);
        m_CreditLine5->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // Title box
    if (m_TitleBox) {
        m_TitleBox->SetTranslation(Vec3((float)(x0 + 0x50), (float)(y0 + 0x1a), 0.0f), 0);
        m_TitleBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // HeadingBox: constructed for layout parity; binary never positions or draws it.

    // Version box
    if (m_VersionBox) {
        m_VersionBox->SetTranslation(Vec3((float)(x0 + 5), (float)(y0 + 0x15), 0.0f), 0);
        m_VersionBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // TODO: v1.6.1 0x0015a264 (AboutScreen::NewDraw) -- DrawMarquee/m_Marquees
    //       scrolling credits (alpha < 0.6) not yet RE'd.
}

// -----------------------------------------------------------------------
// AboutScreen::Draw  @ 0x0015a654
// Two render passes (v1.6.1):
//   A) haiku background panel (m_Texture = s_TexHaiku) -- slides down from top
//      Note: the port calls this texture s_TexHaiku; the binary calls it
//            s_boardTexture. Same texture, different port-side name.
//   B) OFN/GameCenter overlay (null in port, defunct)
//   C) credits.tex REMOVED in v1.6.1
//   D) sensei.tex -- slides in from right
//   E) NewDraw() -- BakedStringBox credit text
//
// ASM-spec v1.6.1 AboutScreen::Draw @0x0015a654: blocks A, B (null skip),
// D unchanged from v1.5.1; block C (credits.tex) is absent; NewDraw call added.
// -----------------------------------------------------------------------
void AboutScreen::Draw(const Vec3& /*hudScale*/, int /*layerMask*/)
{
    if (m_TransitionAlpha <= 0.0f) return;

    MatrixManager& mm  = MatrixManager::GetInstance();
    const float alpha = m_TransitionAlpha;

    // ================================================================
    // Block A: haiku background panel (s_TexHaiku / binary: s_boardTexture)
    // ================================================================
    float yDrawn = 0.0f;
    if (m_Texture.IsValid()) {
#if !defined(__bada__)
        const float texW = (float)m_Texture->m_Width;
        const float texH = (float)m_Texture->m_Height;
#else
        const float texW = 0.0f;
        const float texH = 0.0f;
#endif

        // Y_start = BG_Y_CACHE(160) + texH * 0.5
        // Y_drawn = yStart - (yStart - BG_Y_REST(63)) * alpha
        const float yStart = BG_Y_CACHE + texH * 0.5f;
        yDrawn = yStart - (yStart - BG_Y_REST) * alpha;

        if (m_pOFNButton) {
            m_pOFNButton->pos = Vec3(BG_X + OFN_OFFSET_X, yDrawn + OFN_OFFSET_Y, 0.0f);
        }

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(BG_X, yDrawn, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_Texture->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        m_Texture->UnSet();

        // ================================================================
        // Block B: OFN overlay texture (null in port -- OFN defunct)
        // ================================================================
        if (m_TexOFNOverlay.IsValid()) {
#if !defined(__bada__)
            const float ovW = (float)m_TexOFNOverlay->m_Width;
            const float ovH = (float)m_TexOFNOverlay->m_Height;
#else
            const float ovW = 0.0f;
            const float ovH = 0.0f;
#endif
            mm.GetWorldStack().Reset();
            Matrix44 mOv = Matrix44::MakeScale(ovW + 1.0f, ovH + 1.0f, 1.0f);
            mOv.GlobalTranslate44(Vec3(
                SENSEI_FRAC * texW - SENSEI_X_OFS,
                yDrawn + SENSEI_FRAC * texH,
                0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mOv);
            mm.UploadModelViewOnly();
            m_TexOFNOverlay->Set();
            Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
            m_TexOFNOverlay->UnSet();
        }
    }

    // Block C: credits.tex sliding quad (v1.6.1 still present; loaded into
    // s_TexCredits in LoadContent but not yet drawn in this port.)
    // TODO: v1.6.1 0x0012f394 (AboutScreen::Draw) -- credits.tex quad block
    //       using creditStart static (texH*-0.5 - 160) animated by m_TransitionAlpha.

    // ================================================================
    // Block D: sensei.tex -- slides in from right
    // ================================================================
    if (s_TexSensei.IsValid()) {
#if !defined(__bada__)
        const float texW = (float)s_TexSensei->m_Width;
        const float texH = (float)s_TexSensei->m_Height;
#else
        const float texW = 0.0f;
        const float texH = 0.0f;
#endif

        const float xStart = SENSEI2_X_CACHE + texW * 0.5f;
        const float xDrawn = xStart - (xStart - SENSEI2_X_REST) * alpha;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(xDrawn, SENSEI2_Y, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        s_TexSensei->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexSensei->UnSet();
    }

    // ================================================================
    // Block E: NewDraw -- BakedStringBox credit text
    // ASM-spec v1.6.1 AboutScreen::Draw @0x0015a654: calls NewDraw after quads.
    // ================================================================
    NewDraw(yDrawn);
}

// -----------------------------------------------------------------------
// BackCallback
// -----------------------------------------------------------------------
void AboutScreen::BackCallback()
{
    m_State = 2;
}

// -----------------------------------------------------------------------
// QuitGameCallback
// Binary @ 0x0012eb30 (re-analyst 2026-06-07).
// -----------------------------------------------------------------------
void AboutScreen::QuitGameCallback() {
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }
    m_State = 2;
    if (game_work.m_TutorialControl) {
        float rx = ((float)(rand() % 500) / 100.0f) + 5.0f;
        float ry = -((float)(rand() % 500) / 100.0f);
        game_work.m_TutorialControl->ResetTutePos(Vec3(rx, ry, 0.0f));
    }
}
