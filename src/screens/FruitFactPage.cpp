// FruitFactPage -- base class for v1.6.1 fruit-fact "page book" pages.
// Binary refs (v1.6.1): ctor 0x0017c214, dtor 0x0017d030, etc.
// NOTE: v1.6.1 addresses; port targets v1.6.1 binary.

#include "FruitFactPage.h"
#include "hud/FruitFactControl.h"
#include "hud/GenericHUDControl.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/asset/TextureManager.h"
#include "engine/util/StringTable.h"
#include "engine/math/_Vector2.h"
#include "engine/math/_Vector3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "game/GameWork.h"

// The FruitFact page-book uses the same shared TTF face as MainScreen, BSButton, etc.
// v1.6.1: reads game_work.m_pTTFFontMain (GameWork+0x614, the locale face
//   PreloadFontsTTF @0x0011c1fc sets to arabic.ttf when languageFlag==0x14,
//   else gangofchinese.ttf). Falls back to a lazily-created gangofchinese.ttf
//   only if PreloadFontsTTF hasn't run yet.
static Mortar::FontCacheObjectTTF* GetPageTTFFont() {
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

// Binary @ 0x0017c214 / 0x0017c250
FruitFactPage::FruitFactPage(FruitFactControl* pCtrl)
    : BaseScreen()
    , m_pController(pCtrl)
{
}

FruitFactPage::~FruitFactPage() {
}

// Binary @ 0x0017c19c
void FruitFactPage::Update(float dt) {
    UpdateButtons(dt);              // BaseScreen::UpdateButtons @0x001602cc
    SetExtraControlsDefaultPos();   // @0x0015f618 -- writes child m_BasePos2 = pos
}

// Binary @ 0x0017c1b4 -- vtable +0x44: show this page
void FruitFactPage::ShowPage() {
    m_Active = 1;
}

// Binary @ 0x0017c1e8 -- vtable +0x40: hide this page
void FruitFactPage::HidePage() {
    m_Active = 0;
}

// Binary @ 0x00173760 -- empty base-class virtual (bare `bx lr`).
// Concrete FruitFactPage subclasses override to show their child controls;
// the base default is a no-op (verified against v1.6.1 binary disassembly).
void FruitFactPage::ShowSubObjects() {
}

// Binary @ 0x0017375c -- empty base hook (single `bx lr`).
// Concrete page subclasses override this to hide their child HUD controls;
// the FruitFactPage base body is a genuine no-op in the binary.
void FruitFactPage::HideSubObjects() {
}

// ASM-spec v1.6.1 FruitFactPage::CreateSenseisHead @0x0017c3b4
// Creates a GenericHUDControl displaying the cached Sensei head texture.
// Consts: X=140.0 (DAT_0017c4a4), fadeIn=fadeOut=0.0 (DAT_0017c4a8), flags=0x400.
// NOTE: scale param feeds pos.Y (binary-faithful: Vec3(140, scale, 0)).
// The texture comes straight from the cached FruitFactControl::s_senseiHead slot
// (binary global @ base+0x724C, read via GOT+DAT_0017c4b0). The binary does NOT
// load anything here -- an empty slot simply yields a control with no texture.
// s_senseiHead is filled by FruitFactControl::LoadContent, called from the
// FruitFactControl ctor (binary @ 0x00170c78).
GenericHUDControl* FruitFactPage::CreateSenseisHead(float scale) {
    Mortar::SmartPtr<Mortar::Texture> tex(FruitFactControl::s_senseiHead);
    _Vector3<float> pos(140.0f, scale, 0.0f);
    _Vector3<float> sc(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, pos, sc, col, 0x400);
    AddGenericControl(c);
    return c;
}

// Binary @ 0x0017c2d0
// Creates a GenericHUDControl displaying the horizontal divider texture.
// Texture confirmed: "result_board_divider.tex". flags=8.
void FruitFactPage::CreateHorizontalDivider() {
    Mortar::SmartPtr<Mortar::Texture> tex =
        Mortar::TextureManager::LoadLocalisedTexture("result_board_divider.tex");
    _Vector3<float> pos(-7.5f, 0.0f, 0.0f);
    _Vector3<float> sc(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, pos, sc, col, 8);
    AddGenericControl(c);
}

// Binary @ 0x0017c4cc
// Creates a GenericHUDControl with a title BakedStringBox.
// Consts: size=Vec3(270,14,0), anchor=Vec3(-7.5,92,0), pos=anchor-size*0.5f.
// ctor flag=1 then field_0x34 (m_LayerFlags) overwritten to 0x400 (str r3,[r5,#0x34] @0x17c5d0).
// BakedStringBox: fontSize=12, 270x14, align=0xf, wrap=1, ls=0.
// SetShadow: scale=0.0 (DAT_0017c720).
// ASM-spec v1.6.1 FruitFactPage::CreateTitleTextControl @0x0017c4cc
//   The binary loads game_work.m_pTTFFontMain DIRECTLY and builds the
//   BakedStringBox with no null test. The port routes through GetPageTTFFont()
//   and wraps the box in `if (font)` so a missing TTF face degrades instead of
//   faulting -- a port-side guard, not a binary branch.
GenericHUDControl* FruitFactPage::CreateTitleTextControl(const char* str) {
    _Vector3<float> size(270.0f, 14.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex;
    _Vector3<float> anchor(-7.5f, 92.0f, 0.0f);
    _Vector3<float> ppos = anchor - size * 0.5f;
    _Vector3<float> sc(1.0f, 1.0f, 1.0f);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 1);
    c->m_LayerFlags = 0x400;

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 12.0f, 270.0f, 14.0f, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        box->SetGradient(
            Colour(255, 255, 255, 255),
            Colour(0xeb, 0xd7, 0x1e, 255),
            false);
        box->SetShadow(
            0.0f,
            Colour(0x4b, 0x32, 0x28, 0xc8),
            _Vector3<float>(1.0f, -1.0f, 0.0f),
            true);
        box->SetText(str);
        box->SetHorizontalLineSpacing(-1);
        c->SetText(box);
    }

    AddGenericControl(c);
    return c;
}

// Binary @ 0x0017c734
// Creates a GenericHUDControl with a "SENSEI'S FRUIT FACT" title BakedStringBox.
// size=Vec3(270,-14,0), anchor=Vec3(-8,-8,0), pos=anchor-size*0.5f.
// scale=Vec3(1,1,1)*0.85 (DAT_0017c988=0.85).
// Title colour from controller+0x98 (m_FactColour). LSTR 0xAE.
GenericHUDControl* FruitFactPage::CreateSenseisFruitFactTitle() {
    _Vector3<float> size(270.0f, -14.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex;
    _Vector3<float> anchor(-8.0f, -8.0f, 0.0f);
    _Vector3<float> ppos = anchor - size * 0.5f;
    float s = 0.85f;
    _Vector3<float> sc(s, s, s);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 0x400);

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 12.0f, 270.0f, 14.0f, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
        box->SetColour(m_pController->m_FactColour, 0);
        box->SetShadow(
            5.0f,
            Colour(0x5d, 0x46, 0x20, 255),
            _Vector3<float>(2.0f, -2.0f, 0.0f),
            false);
        box->SetText(GETSTRING(LSTR_FRUIT_FACT_TITLE, 0));
        box->SetHorizontalLineSpacing(-1);
        c->SetText(box);
    }

    AddGenericControl(c);
    return c;
}

// Binary @ 0x0017c99c
// Creates a GenericHUDControl with a BakedStringBox displaying the fact body text.
// pos=Vec3(-141,-24,0), scale=Vec3(1,1,1)*0.85.
// Body text from controller+0x7c (m_FactText). Colour (116,93,59,255).
// ASM-spec v1.6.1 FruitFactPage::CreateSenseisFruitFactText @0x0017c99c
//   Same font path as CreateTitleTextControl: the binary reads
//   game_work.m_pTTFFontMain directly with no null test; the port's
//   GetPageTTFFont() + `if (font)` pair is a port-side guard.
GenericHUDControl* FruitFactPage::CreateSenseisFruitFactText() {
    Mortar::SmartPtr<Mortar::Texture> tex;
    _Vector3<float> ppos(-141.0f, -24.0f, 0.0f);
    float s = 0.85f;
    _Vector3<float> sc(s, s, s);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 0x400);

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 10.0f, 270.0f, 36.0f, (Mortar::ALIGNMENT_TYPE)0xf, 3, 3);
        box->SetText(m_pController->m_FactText);
        box->SetColour(Colour(0x74, 0x5d, 0x3b, 255), 0);
        box->SetHorizontalLineSpacing(-1);
        c->SetText(box);
    }

    AddGenericControl(c);
    return c;
}
