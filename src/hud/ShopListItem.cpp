// ASM-spec v1.6.1 ShopListItem::Draw @0x001b5da4 (thin dispatcher -> NewDraw)
// ASM-spec v1.6.1 ShopListItem::NewDraw @0x001b58e8
// ASM-spec v1.6.1 ShopListItem::DrawDescription @0x001b1f20
// ASM-spec v1.6.1 ShopListItem::DrawDividers @0x001b1a98
// ASM-spec v1.6.1 ShopListItem::DrawIcon @0x001b578c
// ASM-spec v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8
// ASM-spec v1.6.1 ShopListItem::DrawInAppPurchaseTags @0x001b1798
// ASM-spec v1.6.1 ShopListItem::Create @0x001b27f0
// ASM-spec v1.6.1 ShopListItem::Move @0x001b54b0

#include "ShopListItem.h"
#include "ScrollingMenu.h"
#include "game/ItemInfo.h"
#include "game/ItemManager.h"
#include "screens/ShopScreen.h"
#include "Game.h"
#include "engine/asset/Mesh.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Font.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/math/Matrix44.h"
#include "engine/math/Vec2.h"
#include "engine/math/Colour.h"
#include "engine/math/Vec3.h"
#include "engine/math/MathUtil.h"
#include "asset/TextureManager.h"
#include "engine/util/StringTable.h"
#include "engine/util/Localisation.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "game/GameMode.h"
#include "debug/Logger.h"
#include "hud/IngamePopup.h"

// Binary: RandFloat5_GameTask @ 0x0015c658. Returns [0, 5).
static float RandFloat5() {
    return ((float)rand() / (float)RAND_MAX) * 5.0f;
}

// File-static colour cache. Mirrors static_block+0x8C in the binary.
// Stores the last drawn m_Type for the divider colour cache;
// reset to 0xFFFFFFFF when m_bSelected is set (Draw resets, DrawDividers reads).
static int32_t s_lastDrawnType = (int32_t)0xFFFFFFFF;

// ---------------------------------------------------------------------------
// Process-wide statics (binary static_block +0x68 / +0x6c).
// ---------------------------------------------------------------------------
uint16_t ShopListItem::s_ShimmerPhase = 0;
float    ShopListItem::s_ShimmerY    = 0.0f;

// ---------------------------------------------------------------------------
// ShopListItem::ShopListItem() @ v1.6.1 0x001b41f0
// Binary: ScrollingMenuItem base ctor, all 5 box ptrs = 0, TintA=0xFF,
//         TrailFlag=0, SmartPtr::SetNull(m_pIconTex), m_pItemInfo=0,
//         m_bOnscreenItem=1, m_bSelected=0, m_bIsNew=0, m_CostAlpha=0.
// ---------------------------------------------------------------------------
ShopListItem::ShopListItem()
    : ScrollingMenuItem()
    , m_pShopScreen(nullptr)
    , m_NewItemAlpha(0.0f)
    , m_SelectedAlpha(0.0f)
    , m_LockFlashAlpha(0.0f)
    , m_IconPos(0.0f, 0.0f, 0.0f)
    , m_pItemInfo(nullptr)
    , m_bOnscreenItem(1)
    , m_bSelected(0)
    , m_bIsNew(0)
    , _pad3(0)
    , m_CostAlpha(0.0f)
    , m_pBox0(nullptr)
    , m_pBox1(nullptr)
    , m_TintA(0xFF)
    , m_pBox2(nullptr)
    , m_pBox3(nullptr)
    , m_pBox4(nullptr)
    , m_TrailFlag(0)
{
    memset(m_DescText, 0, sizeof(m_DescText));
    memset(_pad, 0, sizeof(_pad));
    memset(_pad4, 0, sizeof(_pad4));
    memset(_pad5, 0, sizeof(_pad5));
    memset(_pad6, 0, sizeof(_pad6));
    m_pIconTex.SetNull();
}

// ---------------------------------------------------------------------------
// ~ShopListItem() @ v1.6.1 0x001b4270
// Binary: delete all 5 BakedStringBox pointers, then base dtor.
// ---------------------------------------------------------------------------
ShopListItem::~ShopListItem() {
    delete m_pBox0; m_pBox0 = nullptr;
    delete m_pBox1; m_pBox1 = nullptr;
    delete m_pBox2; m_pBox2 = nullptr;
    delete m_pBox3; m_pBox3 = nullptr;
    delete m_pBox4; m_pBox4 = nullptr;
}

// ---------------------------------------------------------------------------
// ShopListItem::Move @ v1.6.1 0x001b54b0 (vtable slot 6, +0x18)
// ---------------------------------------------------------------------------
void ShopListItem::Move(float x, float y, float z) {
    Game* g = Game::GetInstance();
    const float dt = g ? game_work.dt : 0.0f;

    // (1) Sin-jitter when selected.
    if (m_bSelected) {
        const float step = dt * 65520.0f;
        float advanced = (float)s_ShimmerPhase + step;
        if (advanced < 0.0f) advanced = 0.0f;
        s_ShimmerPhase = (uint16_t)advanced;
        const float sinVal = SinIdx(s_ShimmerPhase);
        s_ShimmerY = (sinVal < 0.0f ? -sinVal : sinVal) * 6.0f;
    }

    // (2) Copy pos into base.
    pos.x = x;
    pos.y = y;
    pos.z = z;

    // (3) Icon position copy + offset.
    // Binary @0x001b54b0: *(Vec3*)(this+0x268) = pos, then offset x.
    if (m_pIconTex.IsValid()) {
        m_IconPos.x = pos.x;
        m_IconPos.y = pos.y;
        m_IconPos.z = pos.z;
        m_IconPos.x += 35.2f + m_Size.x;  // DAT_0015d474 + m_Size.x = 95.2f

        if (m_LockFlashAlpha > 0.0f) {
            m_LockFlashAlpha -= dt;
            m_IconPos.x += RandFloat5() - 2.5f;
            m_IconPos.y += RandFloat5() - 2.5f;
        }
    }

    // (4) Per-frame alpha ramps.
    const float kRate = 5.0f;

    if (m_pItemInfo) {
        bool isNew = (m_pItemInfo->m_bSeen == 0);
        float c = m_NewItemAlpha + dt * (isNew ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_NewItemAlpha = c;
    }

    {
        ItemManager* im = ItemManager::GetInstance();
        bool equipped = (im && m_pItemInfo && im->IsEquipped(m_pItemInfo) != 0);
        float c = m_SelectedAlpha + dt * (equipped ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_SelectedAlpha = c;
    }

    {
        bool isCentered = m_pShopScreen
            && (m_pShopScreen->GetSelectedItem() == this);
        float c = m_CostAlpha + dt * (isCentered ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_CostAlpha = c;
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::Create @ v1.6.1 0x001b27f0
// ---------------------------------------------------------------------------
void ShopListItem::Create(ItemInfo* pItemInfo, ShopScreen* pShopScreen) {
    m_Height = 80.0f;
    m_Width  = 290.0f;
    m_Size.x = 60.0f;
    m_Size.y = 13.0f;
    m_Size.z = 0.0f;

    m_pShopScreen = pShopScreen;
    m_pItemInfo   = pItemInfo;
    m_CostAlpha   = 0.0f;

    if (!pItemInfo) return;

    // Icon texture load.
    if (pItemInfo->m_pTextureName && pItemInfo->m_pTextureName[0] != '\0') {
        char buf[64];
        const char* fmt = (pItemInfo->m_Type == ITEM_TYPE_BACKGROUND)
                              ? "item_%s.tex"
                              : "%s.tex";
        snprintf(buf, sizeof(buf), fmt, pItemInfo->m_pTextureName);
        m_pIconTex = Mortar::TextureManager::LoadLocalisedTexture(buf);
    }

    // Description text.
    {
        const char* src = nullptr;
        char remainingBuf[256] = {0};

        if (!pItemInfo->IsLocked()) {
            src = pItemInfo->m_pDescText;
        } else if (pItemInfo->m_pTotalStatKey == nullptr) {
            src = pItemInfo->m_pLockedText;
        } else {
            int remaining = pItemInfo->m_CountDownFrom;
            if (remaining < 0) remaining = 0;
            if (pItemInfo->m_pProgressFmt && remaining == 1) {
                src = pItemInfo->m_pProgressFmt;
            } else if (pItemInfo->m_pLockedText) {
                snprintf(remainingBuf, sizeof(remainingBuf),
                         pItemInfo->m_pLockedText, remaining);
                src = remainingBuf;
            }
        }

        if (src && src[0] != '\0') {
            strncpy(m_DescText, src, sizeof(m_DescText) - 1);
            m_DescText[sizeof(m_DescText) - 1] = '\0';
        } else if (pItemInfo->m_pTitle && pItemInfo->m_pTitle[0] != '\0') {
            strncpy(m_DescText, pItemInfo->m_pTitle, sizeof(m_DescText) - 1);
            m_DescText[sizeof(m_DescText) - 1] = '\0';
        }
    }

    ItemManager* im = ItemManager::GetInstance();
    if (im && im->IsEquipped(pItemInfo)) {
        m_SelectedAlpha = 1.0f;
    }

    if (pItemInfo->m_bSeen == 0) {
        m_NewItemAlpha = 1.0f;
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::Draw @ v1.6.1 0x001b5da4 -- thin dispatcher
// ---------------------------------------------------------------------------
void ShopListItem::Draw() {
    // Legacy bitmap font ref (binary: pM_Fonts[1] = pFontMain); used only in
    // the offscreen branch which draws nothing visible when m_CostAlpha==0.
    // Kept as a faithful variable reference matching the binary's register load.
    Mortar::Font* f = game_work.pFontMain.IsValid()
                      ? game_work.pFontMain.Get() : nullptr;
    (void)f;

    // Reset divider colour cache when this row is selected.
    // Binary @0x001b5da4: *(static_block+0x8C) = 0xFFFFFFFF when *(this+0x27D) != 0.
    if (m_bSelected) {
        s_lastDrawnType = (int32_t)0xFFFFFFFF;
    }

    // Dispatch: onscreen -> full TTF draw; offscreen -> loading-stripe only.
    if (!m_bOnscreen) {
        DrawDarkness();
    } else {
        NewDraw();
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::NewDraw @ v1.6.1 0x001b58e8 -- all visible rendering (TTF)
// ---------------------------------------------------------------------------
void ShopListItem::NewDraw() {
    if (!m_pItemInfo) { DrawDarkness(); return; }
    if (!game_work.m_pTTFFontMain) { DrawDarkness(); return; }

    Mortar::FontCacheObjectTTF* ttfFont = game_work.m_pTTFFontMain;

    bool isLocked = (m_pItemInfo->IsLocked() != 0);
    Colour itemColour = isLocked ? Colour(200, 200, 200, 255) : Colour(255, 255, 255, 255);

    // --- Box0: title (lazy-build once) ---
    // v1.6.1 NewDraw @0x001b58e8: operator_new(0xc8), ctor(font,16,195,30,align,1,0),
    // SetText(m_pItemInfo->m_pTitle), SetShadow(0,black,Vec3(4,-4,0),1), Update.
    if (!m_pBox0) {
        // ASM-verified v1.6.1 ShopListItem::NewDraw @0x001b58e8: title/category align = 0x0E
        // (RIGHT-horizontal | center-vertical), NOT 0x0d. RebuildAlignments @0x00245c78
        // low-2-bits: 2=right. Right edge -> local x=195; translate x=pos.x-195 puts the
        // shared right edge at pos.x (rest pos.x=-95). 0x0d (left) overflowed long titles left.
        m_pBox0 = new Mortar::BakedStringBox(ttfFont, 16.0f, 195, 30, 0x0e, 1, 0);
        const char* title = m_pItemInfo->m_pTitle ? m_pItemInfo->m_pTitle : "";
        m_pBox0->SetText(title);
        m_pBox0->SetShadow(0.0f, Colour(0, 0, 0, 255), Vec3(4.0f, -4.0f, 0.0f), true);
        m_pBox0->Update();
    }

    // --- Box1: category (rebuild when m_TintA != m_Type) ---
    // v1.6.1 NewDraw @0x001b58e8: same ctor pattern with fontSize=14, 175x30.
    // typeNames (filled by Create @0x001b27f0): BLADE=0xCA, BG=0xC9, FULL=0xCB, SPECIAL=0x12F.
    if (!m_pBox1 || m_TintA != (uint8_t)m_pItemInfo->m_Type) {
        delete m_pBox1;
        m_pBox1 = new Mortar::BakedStringBox(ttfFont, 14.0f, 175, 30, 0x0e, 1, 0);
        const char* catStr = nullptr;
        switch ((int)m_pItemInfo->m_Type) {
            case 0: catStr = GETSTRING_CAST_0(LSTR_SHOP_BLADE);        break; // 0xCA
            case 1: catStr = GETSTRING_CAST_0(LSTR_SHOP_BACKGROUND);   break; // 0xC9
            case 2: catStr = GETSTRING_CAST_0(LSTR_SHOP_FULL_VERSION); break; // 0xCB
            case 3: catStr = GETSTRING_CAST_0(LSTR_SHOP_SPECIAL);      break; // 0x12F
            default: catStr = nullptr; break;
        }
        if (catStr) m_pBox1->SetText(catStr);
        m_pBox1->SetShadow(0.0f, Colour(0, 0, 0, 255), Vec3(4.0f, -4.0f, 0.0f), true);
        m_pBox1->Update();
        m_TintA = (uint8_t)m_pItemInfo->m_Type;
    }

    // --- Set colour and draw both boxes ---
    // box0 translate = pos + (-175,16,0) + (-20,0,0) = pos + (-195,16,0)
    // box1 translate = pos + (-175,-10,0)
    m_pBox0->SetColour(itemColour, 0);
    m_pBox0->SetTranslation(Vec3(pos.x - 195.0f, pos.y + 16.0f, 0.0f), 0);
    m_pBox0->Draw(Vec2(1.0f, 1.0f), 0.0f, 0);

    m_pBox1->SetColour(itemColour, 0);
    m_pBox1->SetTranslation(Vec3(pos.x - 175.0f, pos.y - 10.0f, 0.0f), 0);
    m_pBox1->Draw(Vec2(1.0f, 1.0f), 0.0f, 0);

    // --- Helper chain ---
    DrawDividers();
    DrawFloatingText();
    DrawIcon();
    DrawInAppPurchaseTags();
    DrawDescription();
    DrawDarkness();
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawDividers @ v1.6.1 0x001b1a98
// ---------------------------------------------------------------------------
void ShopListItem::DrawDividers() {
    if (!m_pItemInfo) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    const Colour colGrey(128, 128, 128, 255);
    const Colour colWhite(255, 255, 255, 200);

    // Colour cache: white if same type as last draw, else grey (and update cache).
    int32_t costType = (int32_t)(int8_t)m_pItemInfo->m_Type;
    Colour dividerColour;
    if (s_lastDrawnType == costType) {
        dividerColour = colWhite;
    } else {
        s_lastDrawnType = costType;
        dividerColour = colGrey;
    }

    // Divider 1: pos + UnitY * GetHeight()/2 (row top edge).
    float halfRowH = m_Height * 0.5f;
    {
        Matrix44 mat = Matrix44::Scale44(257.0f, 17.0f, 0.0f);
        mat.GlobalTranslate44(pos.x, pos.y + halfRowH, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        if (ShopScreen::s_TexScratch.IsValid()) {
            ShopScreen::s_TexScratch->Set();
            Mortar::Mesh::DrawQuadUnCached(dividerColour, NULL);
            ShopScreen::s_TexScratch->UnSet();
        }
    }

    // Divider 2: only when m_bIsNew; pos - UnitY * GetHeight()/2 (row bottom), grey.
    if (m_bIsNew) {
        Matrix44 mat2 = Matrix44::Scale44(257.0f, 17.0f, 0.0f);
        mat2.GlobalTranslate44(pos.x, pos.y - halfRowH, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(mat2);
        mm.UploadModelViewOnly();
        if (ShopScreen::s_TexScratch.IsValid()) {
            ShopScreen::s_TexScratch->Set();
            Mortar::Mesh::DrawQuadUnCached(colGrey, NULL);
            ShopScreen::s_TexScratch->UnSet();
        }
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawIcon @ v1.6.1 0x001b578c
// ---------------------------------------------------------------------------
void ShopListItem::DrawIcon() {
    if (!m_pIconTex.IsValid()) return;
    if (!m_pItemInfo) return;

    bool isLocked = (m_pItemInfo->IsLocked() != 0);
    MatrixManager& mm = MatrixManager::GetInstance();
    const Colour colWhite(255, 255, 255, 255);

    // Binary: scale 64x64, translate = Vec3(0,0,0) + m_IconPos.
    Matrix44 mat = Matrix44::Scale44(64.0f, 64.0f, 0.0f);
    mat.GlobalTranslate44(m_IconPos.x, m_IconPos.y, m_IconPos.z);
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    if (!isLocked) {
        m_pIconTex->Set();
        Mortar::Mesh::DrawQuadUnCached(colWhite, NULL);
        m_pIconTex->UnSet();
    } else {
        if (ShopScreen::s_TexLockedStroke.IsValid()) {
            ShopScreen::s_TexLockedStroke->Set();
            Mortar::Mesh::DrawQuadUnCached(colWhite, NULL);
            ShopScreen::s_TexLockedStroke->UnSet();
        }
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawFloatingText @ v1.6.1 0x001b4bc8
// ASM-spec v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8
// ---------------------------------------------------------------------------
void ShopListItem::DrawFloatingText() {
    // ASM-spec v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8:
    // world base = pos + m_Size (m_Size = (60,13,0) from Create()).
    const float baseX = pos.x + m_Size.x;
    const float baseY = pos.y + m_Size.y;

    // NEW badge (popup 0x10, scale 0.8): guard also requires m_pBox0 != null.
    if (m_NewItemAlpha > 0.0f && m_pBox0 != 0) {
        float boxW = m_pBox0->GetTextWidth();
        Vec3 anchor(baseX - boxW - 4.0f, baseY + 8.0f + s_ShimmerY, 0.0f);
        // ShopScreen state-1 clamp: pushes the badge off-screen during a shop transition.
        if (m_pShopScreen != 0 && m_pShopScreen->m_State == 1) {
            float clampX = 240.0f - 0.25f * (65.0f * m_NewItemAlpha * m_NewItemAlpha);
            if (clampX > anchor.x) anchor.x = clampX;
        }
        IngamePopup* popup = GetIngamePopup(0x10);
        // TODO: v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8 -- binary modulates
        //   Draw scale by alpha^2 (65x33 matrix pre-built with alpha^2 factor). IngamePopup::Draw
        //   has no internal alpha pulse; the modulation must be applied to the scale arg here.
        if (popup) popup->Draw(anchor, 0.8f);
    }

    // SELECTED badge (popup 0x11, scale 0.5).
    if (m_SelectedAlpha > 0.0f) {
        float boxW = (m_pBox1 != 0) ? m_pBox1->GetTextWidth() : 0.0f;
        // binary truncates to int before the float convert (explicit vcvt.s32/f32).
        Vec3 anchor((float)(int)(baseX - boxW - 32.0f), baseY - 26.0f, 0.0f);
        IngamePopup* popup = GetIngamePopup(0x11);
        // TODO: v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8 -- same alpha^2 scale
        //   modulation applies to SELECTED badge (scale 0.5 * alpha^2 factor).
        if (popup) popup->Draw(anchor, 0.5f);
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawInAppPurchaseTags @ v1.6.1 0x001b1798
// Defunct: in-app purchase tags -- no-op stub;
// v1.6.1 ShopListItem::DrawInAppPurchaseTags @0x001b1798
// ---------------------------------------------------------------------------
void ShopListItem::DrawInAppPurchaseTags() {
    // Defunct: in-app purchase tags -- no-op stub; v1.6.1 ShopListItem::DrawInAppPurchaseTags @0x001b1798
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawDescription @ v1.6.1 0x001b1f20
// ---------------------------------------------------------------------------
void ShopListItem::DrawDescription() {
    if (!m_pShopScreen) return;
    if (!m_pItemInfo) return;
    if (!game_work.m_pTTFFontMain) return;

    uint32_t alphaU = (uint32_t)(m_CostAlpha * 255.0f);
    if (alphaU > 0xFF) alphaU = 0xFF;
    if ((int32_t)alphaU < 0) alphaU = 0;
    uint8_t alpha = (uint8_t)alphaU;
    if (alpha == 0) return;

    Mortar::FontCacheObjectTTF* ttfFont = game_work.m_pTTFFontMain;
    bool isLocked = (m_pItemInfo->IsLocked() != 0);
    int8_t purchaseState = m_pItemInfo->m_RequirementType;

    // bVar6: locked AND requirement type 1 or 2.
    bool bVar6 = isLocked && (purchaseState == 1 || purchaseState == 2);

    // Determine base height and fontSize for box3.
    float descH = bVar6 ? 62.0f : 82.0f;
    float fontSize = 14.0f;
    // v1.6.1 DrawDescription @0x001b1f20: 0x14=Arabic, 0x0c=Japanese (NOT Korean/Chinese)
    bool isArabic = (game_work.languageFlag == 0x14);
    bool isJapanese = (game_work.languageFlag == 0x0C);

    if (isArabic) {
        descH -= 20.0f;
        fontSize = 14.0f;
    } else if (isJapanese) {
        fontSize = 12.0f;
        // Side-effect-only GETSTRING call for Japanese locale.
        // Binary @0x001b1f20: GETSTRING_CAST_0(0x111) result discarded.
        (void)GETSTRING_CAST_0((LocalizedString)0x111);
    }

    // Rebuild box3 when nullptr or bVar6 cache changed.
    if (!m_pBox3 || m_TrailFlag != (uint8_t)(bVar6 ? 1 : 0)) {
        delete m_pBox3;
        // ASM-verified v1.6.1 DrawDescription @0x001b1f20: desc align=0x0F (center), maxLines=7, lineSpacing=4.
        m_pBox3 = new Mortar::BakedStringBox(ttfFont, fontSize, 160, (int)descH, 0x0f, 7, 4);
        m_pBox3->SetText(m_DescText);
        m_pBox3->Update();
        m_pBox3->FitIntoVerticalBounds();
    }

    // Rebuild box4 (prompt) only when bVar6.
    if (bVar6) {
        if (!m_pBox4) {
            delete m_pBox4;
            // ASM-verified v1.6.1 DrawDescription @0x001b1f20: prompt align=0x0F (center), maxLines=2, lineSpacing=4.
            m_pBox4 = new Mortar::BakedStringBox(ttfFont, 12.0f, 160, 21, 0x0f, 2, 4);

            // Determine prompt string id and colour.
            LocalizedString promptId;
            bool conditionMet = false;
            if (purchaseState == 1) {
                bool upsideDown = Mortar::IsDeviceUpsideDown();
                conditionMet = upsideDown;
                promptId = upsideDown
                    ? LSTR_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN   // 0xD8 (met)
                    : LSTR_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP;  // 0xD7 (not met)
            } else {
                bool playedToday = (game_work.m_SaveData != nullptr)
                    && game_work.m_SaveData->PlayedModeToday(Mortar::GAME_MODE_ZEN);
                conditionMet = playedToday;
                promptId = playedToday
                    ? LSTR_DJ_BAMBOO_BLADE_PLAYED_TODAY       // 0xCF (met)
                    : LSTR_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY;  // 0xCE (not met)
            }
            const char* promptStr = GETSTRING_CAST_0(promptId);
            if (promptStr) m_pBox4->SetText(promptStr);
            m_pBox4->Update();

            // Colour: red if not met, green if met.
            Colour promptColour = conditionMet
                ? Colour(0xA0, 0xDC, 0, alpha)
                : Colour(0xBD, 0, 0, alpha);
            m_pBox4->SetColour(promptColour, 0);
        }
    }

    // Update bVar6 cache for next frame.
    m_TrailFlag = (uint8_t)(bVar6 ? 1 : 0);

    // Draw box4 (prompt) first when bVar6.
    float xPos = m_pShopScreen->GetDescriptionTextXPos();
    if (bVar6 && m_pBox4) {
        // Position: (xPos, Arabic(0x14) ? -5 : -20, 0).
        float promptY = isArabic ? -5.0f : -20.0f;
        m_pBox4->SetTranslation(Vec3(xPos, promptY, 0.0f), 0);
        m_pBox4->Draw(Vec2(1.0f, 1.0f), 0.0f, 0);
    }

    // Draw box3 (description body).
    if (m_pBox3) {
        // Colour: white (locked) or (0x74,0x5D,0x3B) (unlocked), with alpha.
        Colour descColour;
        if (isLocked) {
            descColour = Colour(255, 255, 255, alpha);
        } else {
            descColour = Colour(0x74, 0x5D, 0x3B, alpha);
        }
        m_pBox3->SetColour(descColour, 0);
        m_pBox3->SetTranslation(Vec3(xPos, 42.0f, 0.0f), 0);
        m_pBox3->Draw(Vec2(1.0f, 1.0f), 0.0f, 0);
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::DrawDarkness -- loading.tex stripe overlay
// Extracted from v1.6.1 ShopListItem::Draw @0x001b5da4:
//   Two 290x120 black(0,0,0,128) quads at parent->pos.x-2, ±105.
//   Gated on m_bIsNew.
// ---------------------------------------------------------------------------
void ShopListItem::DrawDarkness() {
    if (!m_bIsNew) return;

    float parentX = m_pParent ? m_pParent->pos.x : pos.x;

    if (!ShopScreen::s_TexLoading.IsValid()) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    ShopScreen::s_TexLoading->Set();

    {
        Matrix44 matTop = Matrix44::Scale44(290.0f, 120.0f, 0.0f);
        matTop.GlobalTranslate44(parentX - 2.0f, 105.0f, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matTop);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour(0, 0, 0, 128), NULL);
    }
    {
        Matrix44 matBot = Matrix44::Scale44(290.0f, 120.0f, 0.0f);
        matBot.GlobalTranslate44(parentX - 2.0f, -105.0f, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matBot);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour(0, 0, 0, 128), NULL);
    }

    ShopScreen::s_TexLoading->UnSet();
}

// ---------------------------------------------------------------------------
// ShopListItem::ButtonClicked @ v1.6.1 0x001b3e10
// ---------------------------------------------------------------------------
void ShopListItem::ButtonClicked() {
    if (m_pShopScreen != nullptr) {
        m_pShopScreen->SetSelected(this);
    }
}
