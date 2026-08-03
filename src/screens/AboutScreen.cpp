// AboutScreen -- credits/about page launched from DojoScreen.
// v1.6.1 binary refs:
//   ctor                  0x0015b764  (AboutScreen::AboutScreen(DojoScreen*))
//   LoadContent           0x0015a548  (static)
//   UnLoadContent         0x0015c2c0  (static)
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
#include "entities/Bomb.h"
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
#include "math/_Vector2.h"
#include "math/Random.h"
#include "audio/GameSound.h"
#include "util/StringTable.h"
#include "render/Layout.h"
#include <cstdio>
#include <cmath>
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

// ---------------------------------------------------------------------------
// Rate-independence macros for m_TransitionAlpha easing (states 0/2).
// Mirrors ShopScreen's SS_APPROACH_F/SS_DECAY_F pattern (see ShopScreen.cpp):
// under __bada__ these expand to the ORIGINAL per-60Hz-tick scalar forms
// (byte-identical to the binary, no powf); under the port the same call sites
// expand to dt-scaled forms using a local `float f` in scope at each use site
// (f = clamp(dtSeconds,0,0.1)*60 in UpdateRealtime()) so f==1 exactly
// reproduces one 60Hz tick's worth of easing.
// ---------------------------------------------------------------------------
#ifdef __bada__
    // v += (to - v) * k  (exponential approach towards `to` by factor k each call)
    #define AS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (k))
    // v *= k  (geometric decay towards zero by factor k each call)
    #define AS_DECAY_F(v, k)         ((v) *= (k))
#else
    #define AS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (1.0f - powf(1.0f - (k), f)))
    #define AS_DECAY_F(v, k)         ((v) *= powf((k), f))
#endif

// Back button position  (DAT_0012f300 = 185.0, DAT_0012f304 = -106.0)
// ASM-verified pos=Vec3::Zero (matches DojoScreen/MainScreen/ShopScreen back-button
// convention); final screen anchor comes from m_HudScale below, not this Vec3.
static const _Vector3<float> POS_BACK_BUTTON(0.0f, 0.0f, 0.0f);
static const float BACK_SCALE = 0.825f;

// OFN button position -- off-screen right, defunct
static const _Vector3<float> POS_OFN_BUTTON(480.0f, 0.0f, 0.0f);

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
Mortar::SmartPtr<Mortar::Texture> AboutScreen::s_TexNetworkSwitch; // binary: s_switchNetworkTexture (defunct)
static Mortar::SmartPtr<Mortar::Texture> s_TexBackIcon;
// s_bContentLoaded: not ported (redundant -- never read)

// -----------------------------------------------------------------------
// GetVersionString
// ASM-spec v1.6.1 Game::SelfVersion @0x0011fbd8: returns literal "1.6.1".
// DIFFERS: v1.5.1 port had "1.5.1"; updated to match v1.6.1 binary.
// -----------------------------------------------------------------------
const char* GetVersionString()
{
    return "1.6.1";
}

// -----------------------------------------------------------------------
// GetAboutTTFFont
// v1.6.1: reads game_work.m_pTTFFontMain (GameWork+0x614, the locale face
//   PreloadFontsTTF @0x0011c1fc sets to arabic.ttf when languageFlag==0x14,
//   else gangofchinese.ttf). Falls back to a lazily-created gangofchinese.ttf
//   only if PreloadFontsTTF hasn't run yet.
// -----------------------------------------------------------------------
static Mortar::FontCacheObjectTTF* GetAboutTTFFont()
{
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// -----------------------------------------------------------------------
// AboutScreen::LoadContent  @ 0x0015a548
// Binary loads haikus.tex, credits.tex, sensei.tex unconditionally
// (no early-return guard). s_switchNetworkTexture is NOT set here --
// it stays null until AskUserToChoosePreferredNetwork (defunct) fires.
// -----------------------------------------------------------------------
// static
void AboutScreen::LoadContent()
{
    s_TexHaiku   = Mortar::TextureManager::LoadLocalisedTexture("haikus.tex");
    s_TexCredits = Mortar::TextureManager::LoadLocalisedTexture("credits.tex");
    s_TexSensei  = Mortar::TextureManager::LoadLocalisedTexture("sensei.tex");
}

// -----------------------------------------------------------------------
// AboutScreen::UnLoadContent  @ 0x0015c2c0
// Binary nulls all four static texture SmartPtrs (s_boardTexture,
// m_creditsTexture, m_senseiTexture, s_switchNetworkTexture) and clears
// s_isContentLoaded.
// -----------------------------------------------------------------------
// static
void AboutScreen::UnLoadContent()
{
    s_TexHaiku.SetNull();
    s_TexCredits.SetNull();
    s_TexSensei.SetNull();
    s_TexNetworkSwitch.SetNull();
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
#ifndef __bada__
    , m_bMarqueeActive(false)
#endif
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
        m_TitleBox = new Mortar::BakedStringBox(font, 20.0f, 160, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_TitleBox->SetText(GETSTRING(LSTR_ABOUT_TITLE, 0));
        m_TitleBox->SetColour(Colour(0xB9, 0x4F, 0x37, 255), 1);

        // Heading box -- LSTR 0x349
        // fontSize 20, width 0x64(100), align 0xf, height 30, maxLines 1
        // Colour RGB(0xB9, 0x4F, 0x37), setBase=1
        // Constructed for layout parity; NOT drawn in NewDraw (binary never positions/draws it).
        m_HeadingBox = new Mortar::BakedStringBox(font, 20.0f, 100, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_HeadingBox->SetText(GETSTRING(LSTR_ABOUT_HEADING, 0));
        m_HeadingBox->SetColour(Colour(0xB9, 0x4F, 0x37, 255), 1);

        // Version box -- "V <ver>"
        // fontSize 10, width 0x50(80), align 1, height 30, maxLines 1
        // Colour RGB(0x74, 0x5D, 0x3C), setBase=1
        // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764:
        //   OS_SPrintf(buf, 0x200, "%s %s", "V", GetVersionString())
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "%s %s", "V", GetVersionString());
        m_VersionBox = new Mortar::BakedStringBox(font, 10.0f, 80, 30, (Mortar::ALIGNMENT_TYPE)1, 1, 0);
        m_VersionBox->SetText(vbuf);
        m_VersionBox->SetColour(Colour(0x74, 0x5D, 0x3C, 255), 1);
        m_VersionBox->SetHorizontalLineSpacing(-1);

        // Credit lines 0..5 -- LSTR 0x34b..0x350
        // fontSize 12, width 0x140(320), align 0xf, height 30, maxLines 1
        // Colour: game_work.m_TitleColour (binary game_work+0x6a0), setBase=0
        // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764: credit line colour + per-box SetHorizontalLineSpacing(-1)
        const Colour& creditColour = game_work.m_TitleColour;

        // FAITHFUL: in some locales the About intro renders a mix of localized +
        // English lines (e.g. Korean langId 0x0b: CREDIT0/2/4 return English, 1/3/5
        // Korean). This is the original SKU's PARTIAL translation, not a bug --
        // verified live: v1.6.1 GETSTRING(843)="ALL NINJAS HATE FRUIT!" under langId
        // 0x0b. All 6 boxes use the one gangofchinese.ttf face (m_pTTFFontMain); the
        // chunky English look is that CJK face's own Latin glyphs, NOT a bitmap font.
        // Chinese (0x0d) has all 6 translated -> no mix.
        m_CreditLine0 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine0->SetText(GETSTRING(LSTR_ABOUT_CREDIT0, 0));
        m_CreditLine0->SetColour(creditColour, 0);
        m_CreditLine0->SetHorizontalLineSpacing(-1);

        m_CreditLine1 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine1->SetText(GETSTRING(LSTR_ABOUT_CREDIT1, 0));
        m_CreditLine1->SetColour(creditColour, 0);
        m_CreditLine1->SetHorizontalLineSpacing(-1);

        m_CreditLine2 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine2->SetText(GETSTRING(LSTR_ABOUT_CREDIT2, 0));
        m_CreditLine2->SetColour(creditColour, 0);
        m_CreditLine2->SetHorizontalLineSpacing(-1);

        m_CreditLine3 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine3->SetText(GETSTRING(LSTR_ABOUT_CREDIT3, 0));
        m_CreditLine3->SetColour(creditColour, 0);
        m_CreditLine3->SetHorizontalLineSpacing(-1);

        m_CreditLine4 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine4->SetText(GETSTRING(LSTR_ABOUT_CREDIT4, 0));
        m_CreditLine4->SetColour(creditColour, 0);
        m_CreditLine4->SetHorizontalLineSpacing(-1);

        m_CreditLine5 = new Mortar::BakedStringBox(font, 12.0f, 320, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        m_CreditLine5->SetText(GETSTRING(LSTR_ABOUT_CREDIT5, 0));
        m_CreditLine5->SetColour(creditColour, 0);
        m_CreditLine5->SetHorizontalLineSpacing(-1);

        // ASM-spec v1.6.1 AboutScreen ctor @0x0015b764: min-fontSize equalization across the 6 credit lines.
        // Binary calls FitIntoVerticalBounds on each credit box (via its Update), scans GetFontSize()
        // for the minimum, then SetFontSize(min) on all 6 so every line renders at one uniform size.
        // Port calls FitIntoVerticalBounds explicitly here because the port's BakedStringBox::Update()
        // only calls Layout() and does not call FitIntoVerticalBounds (the binary's Update does both).
        m_CreditLine0->FitIntoVerticalBounds();
        m_CreditLine1->FitIntoVerticalBounds();
        m_CreditLine2->FitIntoVerticalBounds();
        m_CreditLine3->FitIntoVerticalBounds();
        m_CreditLine4->FitIntoVerticalBounds();
        m_CreditLine5->FitIntoVerticalBounds();

        {
            float minSize = m_CreditLine0->GetFontSize();
            if (m_CreditLine1->GetFontSize() < minSize) minSize = m_CreditLine1->GetFontSize();
            if (m_CreditLine2->GetFontSize() < minSize) minSize = m_CreditLine2->GetFontSize();
            if (m_CreditLine3->GetFontSize() < minSize) minSize = m_CreditLine3->GetFontSize();
            if (m_CreditLine4->GetFontSize() < minSize) minSize = m_CreditLine4->GetFontSize();
            if (m_CreditLine5->GetFontSize() < minSize) minSize = m_CreditLine5->GetFontSize();

            m_CreditLine0->SetFontSize(minSize);
            m_CreditLine1->SetFontSize(minSize);
            m_CreditLine2->SetFontSize(minSize);
            m_CreditLine3->SetFontSize(minSize);
            m_CreditLine4->SetFontSize(minSize);
            m_CreditLine5->SetFontSize(minSize);
        }

        // ASM-spec v1.6.1 AboutScreen ctor @0x0015b764: SetShadow(1.0f, white, Vec3(0,0,0), 0) on
        // title/heading/credit boxes. offset (0,0,0) draws the shadow directly under the foreground
        // (near-invisible at runtime) but faithfully matches the binary call.
        m_TitleBox->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_HeadingBox->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine0->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine1->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine2->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine3->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine4->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        m_CreditLine5->SetShadow(1.0f, Colour(255, 255, 255, 255), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
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
    // ASM-spec v1.6.1 AboutScreen::Release @0x0015bd70: nulls texture SmartPtrs, zeroes m_TransitionScale,
    // removes ONLY the OFN button (binary m_pCreditsButton +0x94). The back button is NOT torn down here --
    // it self-reaps via MenuButton::Update @0x0019acbc shrink-out after its tap flings the bomb.
    // (Removing the port-invented RemoveBackButton() fixes #317: it was pending-removing Dojo's
    // heap-reused back button mid-grow-in.)
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

    const int bombFruitType = g_FruitInfoCount;

    m_pBackButton = new MenuButton();

    // ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: m_Texture = pM_Textures[0x10]
    //   = game_work.m_RingTex[16] -- the shared PLAIN menu-ring texture (same slot DojoScreen
    //   uses for its back button), set BEFORE Init. This is NOT a file "back_icon.tex": the
    //   prior port loaded an English-"BACK"-baked ring via LoadLocalisedTexture, which then
    //   double-printed with the SetText 返回 overlay below. The binary's ring is plain; the
    //   localized "back" label comes solely from SetText (verified vs HLE: clean 返回).
    m_pBackButton->m_Texture = game_work.m_RingTex[16];

    m_pBackButton->Init(POS_BACK_BUTTON,
                        Mortar::Delegate0<void>::Make(this, &AboutScreen::QuitGameCallback),
                        bombFruitType,
                        _Vector3<float>(0.0f, 0.0f, 0.0f),
                        nullptr);

    // ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: m_pOkButton->m_HudScale.x=0.375f
    // (vmul 0.5*0.75, str [+0x14] @0x0015c848-0x15c85c), .y=-0.3f (vmul -0.5*0.6,
    // str [+0x18] @0x0015c860-0x15c86c). Same idiom as DojoScreen/MainScreen/ShopScreen
    // back buttons -- GetAdjustedPos() moves from pos=Zero to (480*0.375, 320*-0.3, 0)
    // = (180, -96, 0).
    // DIFFERS: opt-in widescreen -- same red bomb back/quit button pattern as
    // MainScreen::CreateQuitButton / DojoScreen::CreateButtons (m_RingTex[16],
    // back-key responder, QuitGameCallback). Back/quit buttons edge-anchor
    // universally -- MapX the pre-scale x and re-derive the HudScale fraction.
    m_pBackButton->m_HudScale.x = MapX(180.0f, "about.btn.back") / 480.0f;
    m_pBackButton->m_HudScale.y = -0.3f;

    // ASM-spec v1.6.1 AboutScreen::Update @0x0015c350 (state==0 branch, @0x0015c894):
    // SetText ring label = GETSTRING(LSTR_DJ_BACK_BUTTON 0x352), m_RingColours[0]/[1],
    // radius 31, fontScale 10, glow+innerGlow. Identical to DojoScreen::CreateButtons.
    // (Was omitted -> ring showed the English "BACK" baked into back_icon.tex with no
    // localized overlay; add it so About's back ring localizes like Dojo's, e.g. 返回.)
    m_pBackButton->SetText(
        GETSTRING_CAST_0(LSTR_DJ_BACK_BUTTON),
        game_work.m_RingColours[0],
        game_work.m_RingColours[1],
        31.0f, 10.0f, true, true);

    m_pBackButton->m_bBackdropActive = 1; // v1.6.1 AboutScreen::Update @0x0015c894 (strb #1, [btn,#0x150])
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
// AboutScreen::Update  @ 0x0015c350
// State machine + marquee scroll.
// ASM-spec v1.6.1 AboutScreen::Update @0x0015c350: after state machine,
// m_EntryDelay countdown gate then scroll loop per m_Marquees item.
// UNVERIFIED GAP: the __bada__ path of this body compiles to 103 instructions
// against the binary's 369 -- two thirds of the binary's Update is unaccounted
// for. The function-boundary split into UpdateRealtime() does NOT explain it
// (the cross-build defines __bada__, so UpdateRealtime is not compiled and the
// easing/scroll live inline here, as in the binary). The defunct OFN-button
// creation stubbed at the top of the body is a partial candidate only. This
// carried an ASM-verified stamp that the instruction counts do not support;
// needs a real body-level read before it can be re-stamped.
// -----------------------------------------------------------------------
void AboutScreen::Update(float dt)
{
    // Defunct: OpenFeint/GameCenter — no-op stub; v1.6.1 AboutScreen::Update @ 0x0015c350
    if (s_TexSensei.IsValid() && m_pOFNButton == nullptr) {
        (void)POS_OFN_BUTTON;
    }

    switch (m_State) {

    case 0: {
#ifdef __bada__
        AS_APPROACH_F(m_TransitionAlpha, 1.0f, ALPHA_LERP_IN);
#endif
        // Port: easing already advanced by UpdateRealtime() (per-present,
        // dt-scaled); this 60Hz tick only reads the current value to fire
        // the (rate-independent, threshold-based) state transition below.
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
#ifdef __bada__
        AS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.
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

#ifndef __bada__
    // Port specific: bridge flag -- the gate above (m_EntryDelay countdown +
    // early-return) stays a 60Hz Update decision exactly like the binary;
    // once passed, latch m_bMarqueeActive so UpdateRealtime() can scroll the
    // (pure-visual, no side effect) marquee every presented frame instead of
    // only at 60Hz.
    m_bMarqueeActive = true;
#else
    const float count = (float)m_Marquees.size();
    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        MarqueeText* mt = *it;
        if (!mt) continue;
        mt->pos.y += dt * 25.0f;
        if (mt->pos.y >= count * 12.0f) {
            mt->pos.y = -50.0f;
        }
    }
#endif
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// the state-machine split comment above Update(). Advances m_TransitionAlpha
// per PRESENTED frame (dt-scaled via AS_APPROACH_F/AS_DECAY_F, defined near
// the top of this file) for whichever of states 0/2 is currently active, and
// scrolls the marquee (pure-visual, no side effect) once m_bMarqueeActive has
// been latched by Update()'s m_EntryDelay gate. Update() (60Hz) reads the
// resulting alpha to fire the (already rate-independent, threshold-based)
// state transitions and one-shot side effects (CreateBackButton,
// m_pParent->Reset()) -- those stay in Update() exactly like ShopScreen/
// DojoScreen keep their state-transition side effects in Update() rather
// than UpdateRealtime().
//
// Under __bada__ this function does not exist (see AboutScreen.h); Update()
// eases m_TransitionAlpha and scrolls the marquee inline, byte-identical to
// the binary.
//
// DIFFERS: v1.6.1 AboutScreen::Update @0x0015c350 eases m_TransitionAlpha and
// scrolls the marquee per 60Hz sim tick; port eases/scrolls them per rendered
// frame (dt-scaled) so both track display refresh. __bada__ keeps the
// faithful 60Hz path.
// ---------------------------------------------------------------------------
void AboutScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    const float f = dtSeconds * 60.0f;

    switch (m_State) {
    case 0:
        AS_APPROACH_F(m_TransitionAlpha, 1.0f, ALPHA_LERP_IN);
        break;
    case 2:
        AS_DECAY_F(m_TransitionAlpha, ALPHA_DECAY);
        break;
    default:
        // State 1: no alpha easing in the binary's Update either.
        break;
    }

    if (!m_bMarqueeActive) return;

    const float count = (float)m_Marquees.size();
    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        MarqueeText* mt = *it;
        if (!mt) continue;
        mt->pos.y += dtSeconds * 25.0f;
        if (mt->pos.y >= count * 12.0f) {
            mt->pos.y = -50.0f;
        }
    }
}
#endif

// -----------------------------------------------------------------------
// AboutScreen::NewDraw  @ 0x0015a264
// Draws BakedStringBox credit text over the haiku board panel.
// Called from Draw() after the textured quads. Computes panel Y
// position internally from m_Texture->GetWidth() and m_TransitionAlpha.
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
void AboutScreen::NewDraw()
{
    // Binary reads m_Texture->GetWidth() (+0x28 = texture width) to compute panelBaseY.
    // panelBaseY = texWidth * 0.5 + BG_Y_CACHE(160); panelY lerps toward BG_Y_REST(63).
    const int x0 = (int)(BG_X - 160.0f);    // -210
    float panelY = 0.0f;
    if (m_Texture.IsValid()) {
        const float panelBaseY = (float)m_Texture->GetWidth() * 0.5f + BG_Y_CACHE;
        panelY = panelBaseY - (panelBaseY - BG_Y_REST) * m_TransitionAlpha;
    }
    const int y0 = (int)(panelY + 64.0f);

    // Credit lines
    if (m_CreditLine0) {
        m_CreditLine0->SetTranslation(_Vector3<float>((float)x0, (float)y0,            0.0f), 0);
        m_CreditLine0->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
    if (m_CreditLine1) {
        m_CreditLine1->SetTranslation(_Vector3<float>((float)x0, (float)(y0 - 0x14),   0.0f), 0);
        m_CreditLine1->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
    if (m_CreditLine2) {
        m_CreditLine2->SetTranslation(_Vector3<float>((float)x0, (float)(y0 - 0x28),   0.0f), 0);
        m_CreditLine2->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
    if (m_CreditLine3) {
        m_CreditLine3->SetTranslation(_Vector3<float>((float)x0, (float)(y0 - 0x4b),   0.0f), 0);
        m_CreditLine3->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
    if (m_CreditLine4) {
        m_CreditLine4->SetTranslation(_Vector3<float>((float)x0, (float)(y0 - 0x5f),   0.0f), 0);
        m_CreditLine4->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
    if (m_CreditLine5) {
        m_CreditLine5->SetTranslation(_Vector3<float>((float)x0, (float)(y0 - 0x73),   0.0f), 0);
        m_CreditLine5->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    // Title box
    if (m_TitleBox) {
        m_TitleBox->SetTranslation(_Vector3<float>((float)(x0 + 0x50), (float)(y0 + 0x1a), 0.0f), 0);
        m_TitleBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    // Version box
    if (m_VersionBox) {
        m_VersionBox->SetTranslation(_Vector3<float>((float)(x0 + 5), (float)(y0 + 0x15), 0.0f), 0);
        m_VersionBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
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
void AboutScreen::Draw(float* /*hudScaleRaw*/)
{
    if (m_TransitionAlpha <= 0.0f) return;

    MatrixManager& mm  = MatrixManager::GetInstance();
    const float alpha = m_TransitionAlpha;

    // ================================================================
    // Block A: haiku background panel (s_TexHaiku / binary: s_boardTexture)
    // ================================================================
    float yDrawn = 0.0f;
    if (m_Texture.IsValid()) {
        const float texW = (float)m_Texture->GetWidth();
        const float texH = (float)m_Texture->GetHeight();

        // Y_start = BG_Y_CACHE(160) + texH * 0.5
        // Y_drawn = yStart - (yStart - BG_Y_REST(63)) * alpha
        const float yStart = BG_Y_CACHE + texH * 0.5f;
        yDrawn = yStart - (yStart - BG_Y_REST) * alpha;

        if (m_pOFNButton) {
            m_pOFNButton->pos = _Vector3<float>(BG_X + OFN_OFFSET_X, yDrawn + OFN_OFFSET_Y, 0.0f);
        }

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(_Vector3<float>(BG_X, yDrawn, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_Texture->Set();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        m_Texture->UnSet();

        // ================================================================
        // Block B: OFN overlay texture (null in port -- OFN defunct)
        // ================================================================
        if (m_TexOFNOverlay.IsValid()) {
            const float ovW = (float)m_TexOFNOverlay->GetWidth();
            const float ovH = (float)m_TexOFNOverlay->GetHeight();
            mm.GetWorldStack().Reset();
            Matrix44 mOv = Matrix44::MakeScale(ovW + 1.0f, ovH + 1.0f, 1.0f);
            mOv.GlobalTranslate44(_Vector3<float>(
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
        const float cW = (float)s_TexCredits->GetWidth();
        const float cH = (float)s_TexCredits->GetHeight();
        mm.GetWorldStack().Reset();
        Matrix44 matC = Matrix44::MakeScale(cW + 1.0f, cH + 1.0f, 1.0f);
        matC.GlobalTranslate44(_Vector3<float>(-50.0f, -416.0f + 320.0f * alpha, 0.0f));
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
        const float texW = (float)s_TexSensei->GetWidth();
        const float texH = (float)s_TexSensei->GetHeight();

        const float xStart = SENSEI2_X_CACHE + texW * 0.5f;
        const float xDrawn = xStart - (xStart - SENSEI2_X_REST) * alpha;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW + 1.0f, texH + 1.0f, 1.0f);
        mat.GlobalTranslate44(_Vector3<float>(MapX(xDrawn, "about.sensei"), SENSEI2_Y, 0.0f));
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
    NewDraw();
}

// -----------------------------------------------------------------------
// AboutScreen::AddLine  @ 0x0015aaf0
// Allocates one BakedStringBox (fontSize, 350x20) and wraps it in a
// MarqueeText pushed onto m_Marquees.
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0:
//   new BakedStringBox(font, fontSize, 350, 20, ...)
//   SetText(text), SetColour(colour, 1), SetWorldspaceClipping(-240,-46,400,108), Update()
// -----------------------------------------------------------------------
void AboutScreen::AddLine(const char* text, Colour colour, int fontSize)
{
    Mortar::FontCacheObjectTTF* font = GetAboutTTFFont();
    if (!font) return;

    Mortar::BakedStringBox* box = new Mortar::BakedStringBox(font, (float)fontSize, 350, 20, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
    box->SetText(text);
    box->SetColour(colour, 1);
    // DIFFERS: opt-in widescreen -- the -240 X0 is the left-field clip edge;
    // use the real (possibly widened) edge so the credits marquee isn't
    // clipped early against the stale original boundary. Y/width/height are
    // not field-edge references and stay fixed. Identity under disabled/__bada__.
#ifdef __bada__
    const int clipX0 = -240;
#else
    const int clipX0 = -(int)Layout::HalfWidth();
#endif
    box->SetWorldspaceClipping(clipX0, -46, 400, 108);
    box->Update();

    MarqueeText* mt = new MarqueeText();
    mt->m_pBox = box;
    m_Marquees.push_back(mt);
}

// -----------------------------------------------------------------------
// AboutScreen::CreateCreditsMarquee  @ 0x0015ac0c
// Builds the m_Marquees scrolling credits list.
// ASM-spec v1.6.1 AboutScreen::CreateCreditsMarquee @0x0015ac0c:
//   20 AddLine calls total, in order: heading; gated blank; unconditional
//   blank (titleColour); LEAD0; gated blank; 6 dev-name lines; unconditional
//   blank (titleColour); LEAD1; gated blank; "Shainiel Deo..." line;
//   unconditional blank (titleColour); LEAD2; gated blank; "Natalie
//   Clarke..." line; "Char + Emma Wood..." line. The 6 blank lines are
//   section spacers (no text content) but still occupy marquee slots and
//   shift every later line's y in the layout loop below.
//   langGate is computed once and reused at all four gated-blank sites:
//   langId in {0x0D, 0x0E, 0x14}. Colour helper @0x0015a064 always writes
//   alpha=0xff, so the gated blanks use Colour(0,0,0,255), not alpha 0.
//   Lays out positions: Vec3(-220, 47 - 12*i, 0) per item.
// -----------------------------------------------------------------------
void AboutScreen::CreateCreditsMarquee()
{
    const Colour& titleColour = game_work.m_TitleColour;
    const int langId = (int)game_work.languageFlag;
    const bool langGate = (langId == 0x0D || langId == 0x0E || langId == 0x14);

    // LSTR 0x349 -- heading line (Colour(0xB9,0x4F,0x37), fontSize 12)                    [1]
    AddLine(GETSTRING(LSTR_ABOUT_HEADING, 0), Colour(0xB9, 0x4F, 0x37, 255), 12);

    if (langGate) AddLine("", Colour(0, 0, 0, 255), 8);                                    // [2]
    AddLine("", titleColour, 8);                                                            // [3]

    // LSTR 0x347 -- colour-leader line 0 (Colour(0x68,0x9A,0x27), fontSize 10)             [4]
    AddLine(GETSTRING(LSTR_ABOUT_MARQUEE_LEAD0, 0), Colour(0x68, 0x9A, 0x27, 255), 10);

    if (langGate) AddLine("", Colour(0, 0, 0, 255), 8);                                    // [5]

    // 6 dev-name lines -- Colour = m_TitleColour, fontSize 8                              [6..11]
    AddLine("Luke Muscat, Shath, Steven Last,",                           titleColour, 8);
    AddLine("Jason Harwood, Adam Wood, Jesse Higginson,",                 titleColour, 8);
    AddLine("Brent Hobson, Matt Ross, Jason Maundrell,",                  titleColour, 8);
    AddLine("Richard McKinney, Will Goddard, Hugh Walters,",              titleColour, 8);
    AddLine("Grant Peters, Joe Gatling,",                                  titleColour, 8);
    AddLine("Peter McNeill, Michael Szewczyk, Paul McNab",                 titleColour, 8);

    AddLine("", titleColour, 8);                                                            // [12]

    // LSTR 0x348 -- colour-leader line 1 (Colour(0x8D,0x4A,0xB9), fontSize 10)             [13]
    AddLine(GETSTRING(LSTR_ABOUT_MARQUEE_LEAD1, 0), Colour(0x8D, 0x4A, 0xB9, 255), 10);

    if (langGate) AddLine("", Colour(0, 0, 0, 255), 8);                                    // [14]
    AddLine("Shainiel Deo, Phil Larsen, Tony Takoushi,",                  Colour(0x8D, 0x4A, 0xB9, 255), 8); // [15]

    AddLine("", titleColour, 8);                                                            // [16]

    // LSTR 0x34A -- colour-leader line 2 (Colour(0x8D,0x4A,0xB9), fontSize 10)             [17]
    AddLine(GETSTRING(LSTR_ABOUT_MARQUEE_LEAD2, 0), Colour(0x8D, 0x4A, 0xB9, 255), 10);

    if (langGate) AddLine("", Colour(0, 0, 0, 255), 8);                                    // [18]
    AddLine("Natalie Clarke, Chloe Pearson,",                             Colour(0x8D, 0x4A, 0xB9, 255), 8); // [19]
    AddLine("Char + Emma Wood, Nell + Calyb Rehua",                       Colour(0x8D, 0x4A, 0xB9, 255), 8); // [20]

    // Lay out positions: Vec3(-220, 47 - 12*i, 0) per item (i = 0..n-1).
    for (int i = 0; i < (int)m_Marquees.size(); ++i) {
        if (m_Marquees[i]) {
            m_Marquees[i]->pos = _Vector3<float>(-220.0f, 47.0f - 12.0f * (float)i, 0.0f);
        }
    }
}

// -----------------------------------------------------------------------
// AboutScreen::DrawMarquee  @ 0x0015a138
// Draws each m_Marquees item translated by the transition offset,
// plus the heading box rotated 90 degrees.
// ASM-verified: 2026-06-21T00:00Z v1.6.1 AboutScreen::DrawMarquee @0x0015a138 (re-analyst):
//   transOffset = Vec3(0, -416 + 320*alpha, 0)
//   per item: m_pBox->SetTranslation(pos + transOffset, 0); m_pBox->Draw(Vec2(1,1), 0, 1)
//   heading: m_HeadingBox->SetTranslation(Vec3(-191, transOffset.y - 67, 0), 1)
//            T_1164(90.0, m_HeadingBox) @0x0015a0e8 = Draw(box, Vec2(1,1), 90.0f, center=1).
//            Binary has NO SetRotation; rotation is Draw's first arg (Draw @0x00246e20 applies theta=arg*pi/180).
// -----------------------------------------------------------------------
void AboutScreen::DrawMarquee()
{
    const float transY = -416.0f + 320.0f * m_TransitionAlpha;

    for (std::vector<MarqueeText*>::iterator it = m_Marquees.begin(); it != m_Marquees.end(); ++it) {
        MarqueeText* mt = *it;
        if (!mt || !mt->m_pBox) continue;
        _Vector3<float> drawPos = mt->pos + _Vector3<float>(0.0f, transY, 0.0f);
        mt->m_pBox->SetTranslation(drawPos, 0);
        mt->m_pBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    if (m_HeadingBox) {
        m_HeadingBox->SetTranslation(_Vector3<float>(-191.0f, transY - 67.0f, 0.0f), 1);
        m_HeadingBox->Draw(_Vector2<float>(1.0f, 1.0f), 90.0f, 1);
    }
}

// -----------------------------------------------------------------------
// QuitGameCallback  @ 0x0015c914
// Bound to m_pBackButton (binary: m_pOkButton). Flits the back-bomb so the
// ActorManager reaps it during the state-2 fade, freeing the single-slot
// menu-bomb pool for DojoScreen::Reset->CreateButtons on re-entry.
// ASM-spec v1.6.1 AboutScreen::QuitGameCallback @0x0015c914:
//   SFXPlay("menu-bomb"); m_State=2; fling m_pBackButton->m_pTrackedFruit;
//   ResetTutePos((MenuButton*)0).
// -----------------------------------------------------------------------
void AboutScreen::QuitGameCallback() {
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }
    m_State = 2;
    // ASM-spec v1.6.1 AboutScreen::QuitGameCallback @0x0015c914 (outlined helper
    // T.1155 @0x00159fc4): Math::g_random.RandF(5.0) x2, UNGATED -- the binary
    // draws before it ever touches m_pBackButton, so the two draws must stay
    // outside the port's null guard or the shared stream desyncs.
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
