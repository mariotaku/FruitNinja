// AboutScreen -- credits/about page launched from DojoScreen.
// v1.6.1 binary refs:
//   ctor                  0x0015b764  (AboutScreen::AboutScreen(DojoScreen*))
//   LoadContent           0x0015b6d4  (static)
//   Draw                  0x0015a654  (board panel + credits quad + sensei quads)
//   NewDraw               0x0015a264  (BakedStringBox credit text pass + DrawMarquee gate)
//   CreateCreditsMarquee  0x0015ac0c  (builds m_Marquees scrolling credits list)
//   AddLine               0x0015aaf0  (allocates one MarqueeText entry)
//   DrawMarquee           0x0015a138  (draws m_Marquees + rotated heading)
//   Update                0x0015c350  (state machine + marquee scroll)

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
// AboutScreen::LoadContent  @ 0x0015b6d4
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
    , m_EntryDelay(3.0f)
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

    // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764: calls CreateCreditsMarquee after text boxes.
    CreateCreditsMarquee();
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

    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        if (*it) {
            delete (*it)->m_pBox;
            delete *it;
        }
    }
    m_Marquees.clear();
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
    if (m_pBackButton->m_pTrackedFruit) {
        m_pBackButton->m_pTrackedFruit->scale =
            m_pBackButton->m_pTrackedFruit->scale * BACK_SCALE;
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
// AboutScreen::Update  @ 0x0015c350
// State machine + marquee scroll.
// ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: after state machine,
// m_EntryDelay countdown gate then scroll loop per m_Marquees item.
// -----------------------------------------------------------------------
void AboutScreen::Update(float dt)
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

    // ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: marquee scroll.
    // Wait m_EntryDelay seconds before scrolling starts.
    if (m_EntryDelay > 0.0f) {
        m_EntryDelay -= dt;
        return;
    }

    const float count = (float)m_Marquees.size();
    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        MarqueeText* mt = *it;
        if (!mt) continue;
        mt->pos.y += dt * 25.0f;
        if (mt->pos.y >= count * 12.0f) {
            mt->pos.y = -50.0f;
        }
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
//   HeadingBox:   positioned by DrawMarquee (binary uses m_HeadingBox in DrawMarquee only)
//   VersionBox:   SetTranslation(x0+5,     y0+0x15,    0), flag=0  (x0+5, y0+21)
//   Each box is drawn with Draw(0, Vec2(1,1), 1).
//   DrawMarquee gate: if (m_TransitionAlpha > 0.6f) DrawMarquee();
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

    // Version box
    if (m_VersionBox) {
        m_VersionBox->SetTranslation(Vec3((float)(x0 + 5), (float)(y0 + 0x15), 0.0f), 0);
        m_VersionBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // ASM-verified: 2026-06-21T00:00:00Z v1.6.1 AboutScreen::NewDraw @0x0015a264 (re-analyst):
    //   vcmpe alpha,0.6f ; ble skip => DrawMarquee runs when alpha > 0.6 (idle/settled state).
    if (m_TransitionAlpha > 0.6f) {
        DrawMarquee();
    }
}

// -----------------------------------------------------------------------
// AboutScreen::Draw  @ 0x0015a654
// Render passes (v1.6.1):
//   A) haiku background panel (m_Texture = s_TexHaiku) -- slides down from top
//      Note: the port calls this texture s_TexHaiku; the binary calls it
//            s_boardTexture. Same texture, different port-side name.
//   B) OFN/GameCenter overlay (null in port, defunct)
//   C) credits.tex lower plate -- slides up from below via m_TransitionAlpha
//   D) sensei.tex -- slides in from right
//   E) NewDraw() -- BakedStringBox credit text (+ DrawMarquee when alpha < 0.6)
//
// ASM-spec v1.6.1 AboutScreen::Draw @0x0015a654: blocks A, B (null skip), C,
// D unchanged; NewDraw call added; block C pos = Vec3(-50, -96 + (1-alpha)*(-320), 0).
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

    // ================================================================
    // Block C: credits.tex lower plate -- slides up from below.
    // ASM-spec v1.6.1 AboutScreen::Draw @0x0015a654:
    //   pos = Vec3(-50, -416 + 320 * alpha, 0)
    //   (at alpha=1: y=-96; at alpha=0: y=-416 / off-bottom)
    // ================================================================
    if (s_TexCredits.IsValid()) {
#if !defined(__bada__)
        const float cW = (float)s_TexCredits->m_Width;
        const float cH = (float)s_TexCredits->m_Height;
#else
        const float cW = 0.0f;
        const float cH = 0.0f;
#endif
        mm.GetWorldStack().Reset();
        Matrix44 matC = Matrix44::MakeScale(cW + 1.0f, cH + 1.0f, 1.0f);
        matC.GlobalTranslate44(Vec3(-50.0f, -416.0f + 320.0f * alpha, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(matC);
        mm.UploadModelViewOnly();

        s_TexCredits->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexCredits->UnSet();
    }

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
// AboutScreen::AddLine  @ 0x0015aaf0
// Allocates one BakedStringBox (fontSize, 350x20) and wraps it in a
// MarqueeText pushed onto m_Marquees.
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0:
//   new BakedStringBox(font, fontSize, 350, 20, ...)
//   SetText(text), SetColour(colour, 1), SetWorldspaceClipping(-240,-46,400,108), Update()
// -----------------------------------------------------------------------
void AboutScreen::AddLine(const char* text, const Colour& colour, float fontSize)
{
    Mortar::FontCacheObjectTTF* font = GetAboutTTFFont();
    if (!font) return;

    Mortar::BakedStringBox* box = new Mortar::BakedStringBox(font, fontSize, 350.0f, 20.0f, 0xf, 1, 0.0f);
    box->SetText(text);
    box->SetColour(colour, 1);
    box->SetWorldspaceClipping(-240.0f, -46.0f, 400.0f, 108.0f);
    box->Update();

    MarqueeText* mt = new MarqueeText();
    mt->m_pBox = box;
    m_Marquees.push_back(mt);
}

// -----------------------------------------------------------------------
// AboutScreen::CreateCreditsMarquee  @ 0x0015ac0c
// Builds the m_Marquees scrolling credits list.
// ASM-spec v1.6.1 AboutScreen::CreateCreditsMarquee @0x0015ac0c:
//   Adds lines via AddLine; lays out positions: Vec3(-220, 47 - 12*i, 0) per item.
//   Lang gate: if langFlag in {0x0D, 0x0E, 0x14}, insert blank padding after LSTR 0x349.
// -----------------------------------------------------------------------
void AboutScreen::CreateCreditsMarquee()
{
    const Colour& titleColour = game_work.m_TitleColour;

    // LSTR 0x349 -- heading line (Colour(0xB9,0x4F,0x37), fontSize 12)
    AddLine(Mortar::GETSTRING(LSTR_ABOUT_HEADING, 0), Colour(0xB9, 0x4F, 0x37, 255), 12.0f);

    // Lang gate: if languageFlag in {0x0D=13, 0x0E=14, 0x14=20}, add blank padding.
    // ASM-spec v1.6.1 AboutScreen::CreateCreditsMarquee @0x0015ac0c: langId gate.
    {
        const int langId = (int)game_work.languageFlag;
        if (langId == 0x0D || langId == 0x0E || langId == 0x14) {
            AddLine("", Colour(0, 0, 0, 0), 8.0f);
        }
    }

    // LSTR 0x347 -- colour-leader line 0 (Colour(0x68,0x9A,0x27), fontSize 10)
    AddLine(Mortar::GETSTRING(LSTR_ABOUT_MARQUEE_LEAD0, 0), Colour(0x68, 0x9A, 0x27, 255), 10.0f);

    // 6 dev-name lines -- Colour = m_TitleColour, fontSize 8
    AddLine("Luke Muscat, Shath, Steven Last,",                           titleColour, 8.0f);
    AddLine("Jason Harwood, Adam Wood, Jesse Higginson,",                 titleColour, 8.0f);
    AddLine("Brent Hobson, Matt Ross, Jason Maundrell,",                  titleColour, 8.0f);
    AddLine("Richard McKinney, Will Goddard, Hugh Walters,",              titleColour, 8.0f);
    AddLine("Grant Peters, Joe Gatling,",                                  titleColour, 8.0f);
    AddLine("Peter McNeill, Michael Szewczyk, Paul McNab",                 titleColour, 8.0f);

    // LSTR 0x348 -- colour-leader line 1 (Colour(0x8D,0x4A,0xB9), fontSize 10)
    AddLine(Mortar::GETSTRING(LSTR_ABOUT_MARQUEE_LEAD1, 0), Colour(0x8D, 0x4A, 0xB9, 255), 10.0f);
    AddLine("Shainiel Deo, Phil Larsen, Tony Takoushi,",                  Colour(0x8D, 0x4A, 0xB9, 255), 8.0f);

    // LSTR 0x34A -- colour-leader line 2 (Colour(0x8D,0x4A,0xB9), fontSize 10)
    AddLine(Mortar::GETSTRING(LSTR_ABOUT_MARQUEE_LEAD2, 0), Colour(0x8D, 0x4A, 0xB9, 255), 10.0f);
    AddLine("Natalie Clarke, Chloe Pearson,",                             Colour(0x8D, 0x4A, 0xB9, 255), 8.0f);
    AddLine("Char + Emma Wood, Nell + Calyb Rehua",                       Colour(0x8D, 0x4A, 0xB9, 255), 8.0f);

    // Lay out positions: Vec3(-220, 47 - 12*i, 0) per item (i = 0..n-1).
    for (int i = 0; i < (int)m_Marquees.size(); ++i) {
        if (m_Marquees[i]) {
            m_Marquees[i]->pos = Vec3(-220.0f, 47.0f - 12.0f * (float)i, 0.0f);
        }
    }
}

// -----------------------------------------------------------------------
// AboutScreen::DrawMarquee  @ 0x0015a138
// Draws each m_Marquees item translated by the transition offset,
// plus the heading box rotated 90 degrees.
// ASM-spec v1.6.1 AboutScreen::DrawMarquee @0x0015a138:
//   transOffset = Vec3(0, -416 + 320*alpha, 0)
//   per item: m_pBox->SetTranslation(pos + transOffset, 0); m_pBox->Draw(0, Vec2(1,1), 1)
//   heading: m_HeadingBox->SetTranslation(Vec3(-191, transOffset.y - 67, 0), 1)
//             m_HeadingBox->SetRotation(90.0f); m_HeadingBox->Draw(0, Vec2(1,1), 1)
// -----------------------------------------------------------------------
void AboutScreen::DrawMarquee()
{
    const float transY = -416.0f + 320.0f * m_TransitionAlpha;

    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        MarqueeText* mt = *it;
        if (!mt || !mt->m_pBox) continue;
        Vec3 drawPos = mt->pos + Vec3(0.0f, transY, 0.0f);
        mt->m_pBox->SetTranslation(drawPos, 0);
        mt->m_pBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    if (m_HeadingBox) {
        m_HeadingBox->SetTranslation(Vec3(-191.0f, transY - 67.0f, 0.0f), 1);
        m_HeadingBox->SetRotation(90.0f);
        m_HeadingBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }
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
