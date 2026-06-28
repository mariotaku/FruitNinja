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
#include "engine/math/Vec2.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

// The FruitFact page-book uses the same shared TTF face as MainScreen, BSButton, etc.
// Binary: *(g_GameData + 0x614) -- the global FontCacheObjectTTF* loaded over
// "fontstruetype/gangofchinese.ttf". Port resolves it via a file-static SmartPtr<Font>
// + FontTTFRegistry, exactly as BSButton.cpp does.
// DIFFERS: original = *(g_GameData+0x614) shared face owned by GameContext,
//   using a file-local shared SmartPtr<Font> + FontTTFRegistry::Lookup because
//   the port has not extended game_work past 0x608 to carry the +0x614 slot.
static Mortar::FontCacheObjectTTF* GetPageTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Cached sensei head texture (GOT-relative global in binary; one per process).
// Binary @ 0x0017c3b4 reads from GOT+DAT_0017c4b0 -> the global SmartPtr<Texture>.
// LoadContent for FruitFactControl populates it; here we load on demand.
static Mortar::SmartPtr<Mortar::Texture> g_SenseisHeadTex;

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

// Binary @ 0x0017c3b4
// Creates a GenericHUDControl displaying the cached Sensei head texture.
// Consts: X=140.0 (DAT_0017c4a4), fadeIn=fadeOut=0.0 (DAT_0017c4a8), flags=0x400.
// NOTE: scale param feeds pos.Y (binary-faithful: Vec3(140, scale, 0)).
// ASM-verified: 2026-06-13T03:40Z v1.6.1 binary @ 0x0017c3b4 (asm-inspector)
GenericHUDControl* FruitFactPage::CreateSenseisHead(float scale) {
    if (!g_SenseisHeadTex.IsValid()) {
        // TODO: v1.6.1 FruitFactPage::CreateSenseisHead @0x0017c3b4 -- resolve sensei head tex name from DAT_0017c4b0 string pool
        g_SenseisHeadTex = Mortar::TextureManager::LoadLocalisedTexture("sensei_head.tex");
    }
    Mortar::SmartPtr<Mortar::Texture> tex(g_SenseisHeadTex);
    Vec3 pos(140.0f, scale, 0.0f);
    Vec3 sc(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
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
    Vec3 pos(-7.5f, 0.0f, 0.0f);
    Vec3 sc(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
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
// ASM-verified: 2026-06-13T03:40Z v1.6.1 binary @ 0x0017c4cc (asm-inspector)
GenericHUDControl* FruitFactPage::CreateTitleTextControl(const char* str) {
    Vec3 size(270.0f, 14.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex;
    Vec3 anchor(-7.5f, 92.0f, 0.0f);
    Vec3 ppos = anchor - size * 0.5f;
    Vec3 sc(1.0f, 1.0f, 1.0f);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 1);
    c->m_LayerFlags = 0x400;

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 12.0f, 270.0f, 14.0f, 0xf, 1, 0.0f);
        box->SetGradient(
            Colour(255, 255, 255, 255),
            Colour(0xeb, 0xd7, 0x1e, 255),
            false);
        box->SetShadow(
            0.0f,
            Colour(0x4b, 0x32, 0x28, 0xc8),
            Vec3(1.0f, -1.0f, 0.0f),
            true);
        box->SetText(str);
        box->SetHorizontalLineSpacing(-1.0f);
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
    Vec3 size(270.0f, -14.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex;
    Vec3 anchor(-8.0f, -8.0f, 0.0f);
    Vec3 ppos = anchor - size * 0.5f;
    float s = 0.85f;
    Vec3 sc(s, s, s);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 0x400);

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 12.0f, 270.0f, 14.0f, 0xf, 1, 0.0f);
        box->SetColour(m_pController->m_FactColour, 0);
        box->SetShadow(
            5.0f,
            Colour(0x5d, 0x46, 0x20, 255),
            Vec3(2.0f, -2.0f, 0.0f),
            false);
        box->SetText(GETSTRING(LSTR_FRUIT_FACT_TITLE, 0));
        box->SetHorizontalLineSpacing(-1.0f);
        c->SetText(box);
    }

    AddGenericControl(c);
    return c;
}

// Binary @ 0x0017c99c
// Creates a GenericHUDControl with a BakedStringBox displaying the fact body text.
// pos=Vec3(-141,-24,0), scale=Vec3(1,1,1)*0.85.
// Body text from controller+0x7c (m_FactText). Colour (116,93,59,255).
// ASM-verified: 2026-06-13T03:40Z v1.6.1 binary @ 0x0017c99c (asm-inspector)
GenericHUDControl* FruitFactPage::CreateSenseisFruitFactText() {
    Mortar::SmartPtr<Mortar::Texture> tex;
    Vec3 ppos(-141.0f, -24.0f, 0.0f);
    float s = 0.85f;
    Vec3 sc(s, s, s);
    Colour col(255, 255, 255, 255);
    GenericHUDControl* c = new GenericHUDControl(0.0f, 0.0f, tex, NULL, ppos, sc, col, 0x400);

    Mortar::FontCacheObjectTTF* font = GetPageTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 10.0f, 270.0f, 36.0f, 0xf, 3, 3.0f);
        box->SetText(m_pController->m_FactText);
        box->SetColour(Colour(0x74, 0x5d, 0x3b, 255), 0);
        box->SetHorizontalLineSpacing(-1.0f);
        c->SetText(box);
    }

    AddGenericControl(c);
    return c;
}
