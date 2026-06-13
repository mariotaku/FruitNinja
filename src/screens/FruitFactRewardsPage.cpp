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
    , m_pTitleBox(0)
    , m_animA(0.0f)
    , m_animB(0.0f)
    , m_animC(0.0f)
    , m_timerAc(0.0f)
    , m_timerB0(0.0f)
    , m_intB4(-1)
    , m_byteBC(0)
    , m_timerC4(0.0f)
    , m_floatC8(1.0f)
    , m_shortCC((short)32000)
    , m_timerD0(0.0f)
    , m_timerD8(0.0f)
    , m_byteE0(0)
    , m_timerE4(0.0f)
{
    _padB8[0] = 0; _padB8[1] = 0; _padB8[2] = 0; _padB8[3] = 0;
    _padBD[0] = 0; _padBD[1] = 0; _padBD[2] = 0;
    _padCE[0] = 0; _padCE[1] = 0;
    _padD4[0] = 0; _padD4[1] = 0; _padD4[2] = 0; _padD4[3] = 0;
    _padDC[0] = 0; _padDC[1] = 0; _padDC[2] = 0; _padDC[3] = 0;
    _padE1[0] = 0; _padE1[1] = 0; _padE1[2] = 0;
}

FruitFactRewardsPage::~FruitFactRewardsPage() {
    delete m_pTitleBox;
    m_pTitleBox = 0;
}

// Binary @ 0x0017e4d8 (ctor body -- state init + head + title box)
// DAT_0017e6c0 = 0.0f (zero constant for most field inits).
// DAT_0017e6c4 = 68.0f (head scale).
// LSTR 0x15D (349) = "Rewards" title string.
void FruitFactRewardsPage::Init() {
    // State field init (all from DAT_0017e6c0 = 0.0 / binary literal values)
    m_timerAc  = 0.0f;
    m_timerD8  = 0.0f;
    m_timerC4  = 0.0f;
    m_timerB0  = 0.0f;
    m_byteE0   = 0;
    // field_0x94 = 0: binary init of what may be an extra field at +0x94
    // (overlaps FruitFactPage::m_pController in port layout; port skips this store
    // to preserve the ctrl pointer set by the base ctor).
    m_intB4    = -1;
    m_shortCC  = (short)32000;
    m_floatC8  = 1.0f;

    CreateSenseisHead(68.0f);               // DAT_0017e6c4 = 68

    m_timerE4  = 0.0f;
    m_timerD0  = 0.0f;
    m_byteBC   = 0;

    // Binary @ 0x0017e4d8: this->HUDControl.field_0x30 = 1 -> m_Active = 1
    m_Active = 1;

    m_animA    = 0.0f;
    m_animB    = 0.0f;
    m_animC    = 0.0f;

    // Build the owned title BakedStringBox at field_0x9c.
    // Binary: new(200) BakedStringBox(font, 12.0, width=250, height=14, align=0xf, wrap=1, ls=0)
    // Then SetText(GETSTRING(0x15D,0)), SetHorizontalLineSpacing(-1),
    //   SetGradient(255,255,255,255 -> 0xeb,0xd7,0x1e,255, perGlyph=0),
    //   SetShadow(0.0, Colour(0x4b,0x32,0x28,0xc8), Vec3(1,-1,0), flag=1).
    // LSTR 0x15D = LocalizedString with integer ID 349 ("Rewards" title).
    // NOTE: this box is owned by the page directly at m_pTitleBox (field_0x9c),
    // not stored via GenericHUDControl::SetText. The page's own draw/update uses it.
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
