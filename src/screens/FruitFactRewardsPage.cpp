// FruitFactRewardsPage -- v1.6.1 rewards fact page.
// Binary refs: ctor 0x0017e4d8.

#include "FruitFactRewardsPage.h"
#include "hud/GenericHUDControl.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/util/StringTable.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"

// Shared TTF face (same as FruitFactPage.cpp helper; each TU keeps its own
// file-static SmartPtr so the face stays alive across TU lifetimes).
// DIFFERS: original = *(g_GameData+0x614); using FontTTFRegistry lookup.
static Mortar::FontCacheObjectTTF* GetRewardsTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Binary @ 0x0017e4d8
FruitFactRewardsPage::FruitFactRewardsPage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
    , m_field98(0)
    , m_pTitleBox(0)
    , m_animA(0.0f)
    , m_animB(0.0f)
    , m_animC(0.0f)
    , m_timerB0(0.0f)
    , m_timerB4(0.0f)
    , m_intB8(-1)
    , m_byteC0(0)
    , m_floatC8(1.0f)
    , m_floatCC(1.0f)
    , m_shortD0((short)32000)
    , m_floatD4(1.0f)
    , m_floatDC(0.0f)
    , m_byteE4(0)
    , m_floatE8(1.0f)
{
    _pad9C[0] = 0; _pad9C[1] = 0; _pad9C[2] = 0; _pad9C[3] = 0;
    _padBC[0] = 0; _padBC[1] = 0; _padBC[2] = 0; _padBC[3] = 0;
    _padC1[0] = 0; _padC1[1] = 0; _padC1[2] = 0;
    _padC4[0] = 0; _padC4[1] = 0; _padC4[2] = 0; _padC4[3] = 0;
    _padD2[0] = 0; _padD2[1] = 0;
    _padD8[0] = 0; _padD8[1] = 0; _padD8[2] = 0; _padD8[3] = 0;
    _padE0[0] = 0; _padE0[1] = 0; _padE0[2] = 0; _padE0[3] = 0;
    _padE5[0] = 0; _padE5[1] = 0; _padE5[2] = 0;
}

FruitFactRewardsPage::~FruitFactRewardsPage() {
    delete m_pTitleBox;
    m_pTitleBox = 0;
}

// Binary @ 0x0017e4d8 (ctor body -- state init + head + title box)
// DAT_0017e6c0 = 0.0f (zero constant for most field inits).
// DAT_0017e6c4 = 68.0f (head scale).
// LSTR 0x15D (349) = "Rewards" title string.
// ASM-verified: 0x0017e4d8 ready for re-verify (field layout corrected per disasm).
void FruitFactRewardsPage::Init() {
    // State field inits (offsets confirmed from disasm, [r4,#off]):
    m_timerB0  = 0.0f;    // [r4,#0xb0]
    m_floatDC  = 0.0f;    // [r4,#0xdc]
    m_timerB4  = 0.0f;    // [r4,#0xb4]
    m_timerB0  = 0.0f;    // [r4,#0xb0] (binary writes this twice in sequence)
    m_byteE4   = 0;       // [r4,#0xe4]
    m_intB8    = -1;      // [r4,#0xb8]
    m_shortD0  = (short)32000;  // [r4,#0xd0] = 0x7d00
    m_floatC8  = 1.0f;    // [r4,#0xc8]
    m_floatCC  = 1.0f;    // [r4,#0xcc]

    CreateSenseisHead(68.0f);               // DAT_0017e6c4 = 68

    m_floatE8  = 1.0f;    // [r4,#0xe8]
    m_floatD4  = 1.0f;    // [r4,#0xd4]
    m_byteC0   = 0;       // [r4,#0xc0]

    // Binary @ 0x0017e4d8: this->HUDControl.field_0x30 = 1 -> m_Active = 1
    m_Active = 1;

    m_animA    = 0.0f;    // [r4,#0xa4]
    m_animB    = 0.0f;    // [r4,#0xa8]
    m_animC    = 0.0f;    // [r4,#0xac]

    // Build the owned title BakedStringBox at field_0xa0.
    // Binary: new(200) BakedStringBox(font, 12.0, width=250, height=14, align=0xf, wrap=1, ls=0)
    // Then SetText(GETSTRING(0x15D,0)), SetHorizontalLineSpacing(-1),
    //   SetGradient(255,255,255,255 -> 0xeb,0xd7,0x1e,255, perGlyph=0),
    //   SetShadow(0.0, Colour(0x4b,0x32,0x28,0xc8), Vec3(1,-1,0), flag=1).
    // LSTR 0x15D = LocalizedString with integer ID 349 ("Rewards" title).
    // The box is owned directly at m_pTitleBox (+0xa0).
    Mortar::FontCacheObjectTTF* font = GetRewardsTTFFont();
    if (font) {
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font, 12.0f, 250.0f, 14.0f, 0xf, 1, 0.0f);
        m_pTitleBox = box;
        box->SetText(Mortar::GETSTRING(LSTR_REWARDS_TITLE, 0));
        box->SetHorizontalLineSpacing(-1.0f);
        box->SetGradient(
            Colour(255, 255, 255, 255),
            Colour(0xeb, 0xd7, 0x1e, 255),
            false);
        box->SetShadow(
            0.0f,
            Colour(0x4b, 0x32, 0x28, 0xc8),
            Vec3(1.0f, -1.0f, 0.0f),
            true);
    }
}
