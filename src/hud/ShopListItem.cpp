// ASM-spec v1.6.1 ShopListItem::Draw @0x001b5da4 (dispatcher + offscreen desc fade)
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
#include "engine/math/_Vector2.h"
#include "engine/math/Colour.h"
#include "engine/math/_Vector3.h"
#include "engine/math/MathUtil.h"
#include "engine/math/Random.h"
#include "asset/TextureManager.h"
#include "engine/util/StringTable.h"
#include "engine/util/Localisation.h"
#include "engine/render/Layout.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "game/GameMode.h"
#include "debug/Logger.h"
#include "hud/IngamePopup.h"

// Port-side stand-in for the compiler-OUTLINED per-TU helper the binary emits
// around Math::g_random (T.1421 @0x001b19cc; also RandFloat5_GameTask
// @0x0015c658 in the GameTask TU). Returns [0, 5).
// ASM-spec v1.6.1 ShopListItem::Move @0x001b54b0 (T.1421 @0x001b19cc):
//   Math::g_random.RandF(5.0) x2 -> icon offset -2.5f, only when m_LockFlashAlpha > 0.
static float RandFloat5() {
    return Math::g_Random.RandF(5.0f);
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
void ShopListItem::Move(_Vector3<float> v) {
    const float x = v.x, y = v.y, z = v.z;
    Game* g = Game::GetInstance();
    const float dt = g ? game_work.dt : 0.0f;

#ifdef __bada__
    // (1) Sin-jitter when selected -- binary advances this inline in Move at
    // its single fixed 60Hz call rate. Byte-identical to the original; do NOT
    // extract to AdvanceAnim here. Port equivalent: AdvanceAnim() below.
    if (m_bSelected) {
        const float step = dt * 65520.0f;
        float advanced = (float)s_ShimmerPhase + step;
        if (advanced < 0.0f) advanced = 0.0f;
        s_ShimmerPhase = (uint16_t)advanced;
        const float sinVal = SinIdx(s_ShimmerPhase);
        s_ShimmerY = (sinVal < 0.0f ? -sinVal : sinVal) * 6.0f;
    }
#endif

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
#ifdef __bada__
            // Binary advances this inline in Move at its single fixed 60Hz
            // call rate. Port equivalent: AdvanceAnim() below.
            m_LockFlashAlpha -= dt;
#endif
            m_IconPos.x += RandFloat5() - 2.5f;
            m_IconPos.y += RandFloat5() - 2.5f;
        }
    }

#ifdef __bada__
    // (4) Per-frame alpha ramps -- binary advances these inline in Move at
    // its single fixed 60Hz call rate. Byte-identical to the original;
    // do NOT extract to AdvanceAnim here (that would add a `bl` the binary
    // does not have). Port equivalent: AdvanceAnim() below, called once per
    // present from ScrollingMenu::UpdateRealtime instead of from here.
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
#endif
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart. Binary's ShopListItem::Move @
// 0x001b54b0 advances m_NewItemAlpha/m_SelectedAlpha/m_CostAlpha inline at
// its single fixed 60Hz call rate. The port calls Move() from BOTH
// ScrollingMenu::Update() (60Hz) and ScrollingMenu::UpdateRealtime()
// (per-present) for positioning; advancing the timers inside Move would
// double-advance them (and the per-present call is refresh-rate dependent,
// so 120Hz doubles the doubling). AdvanceAnim carries the timer step out of
// Move and is called exactly once per present, from UpdateRealtime's Phase 5
// loop, with the real per-present dtSeconds -- these are linear rate*dt
// ramps (not spring/decay), so real dtSeconds (not dtSeconds*60) reproduces
// the binary's kRate/sec rate exactly regardless of display refresh rate.
// ---------------------------------------------------------------------------
void ShopListItem::AdvanceAnim(float dtSeconds) {
    const float kRate = 5.0f;

    // Sin-jitter bounce (mirrors Move's __bada__ block above, real dtSeconds).
    // s_ShimmerPhase/s_ShimmerY are file-static (shared across items) but
    // gated by m_bSelected, so only the one selected item advances them --
    // still exactly once per present.
    if (m_bSelected) {
        const float step = dtSeconds * 65520.0f;
        float advanced = (float)s_ShimmerPhase + step;
        if (advanced < 0.0f) advanced = 0.0f;
        s_ShimmerPhase = (uint16_t)advanced;
        const float sinVal = SinIdx(s_ShimmerPhase);
        s_ShimmerY = (sinVal < 0.0f ? -sinVal : sinVal) * 6.0f;
    }

    // Lock-flash countdown (mirrors Move's __bada__ block above, real dtSeconds).
    if (m_LockFlashAlpha > 0.0f) {
        m_LockFlashAlpha -= dtSeconds;
    }

    if (m_pItemInfo) {
        bool isNew = (m_pItemInfo->m_bSeen == 0);
        float c = m_NewItemAlpha + dtSeconds * (isNew ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_NewItemAlpha = c;
    }

    {
        ItemManager* im = ItemManager::GetInstance();
        bool equipped = (im && m_pItemInfo && im->IsEquipped(m_pItemInfo) != 0);
        float c = m_SelectedAlpha + dtSeconds * (equipped ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_SelectedAlpha = c;
    }

    {
        bool isCentered = m_pShopScreen
            && (m_pShopScreen->GetSelectedItem() == this);
        float c = m_CostAlpha + dtSeconds * (isCentered ? +kRate : -kRate);
        if (c < 0.0f) c = 0.0f; else if (c > 1.0f) c = 1.0f;
        m_CostAlpha = c;
    }
}
#endif

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
            // ASM-spec v1.6.1 ShopListItem::Create @0x1b2864-0x1b28b0: remaining count is
            // the target minus the player's actual save-data progress, not the raw target.
            int total = game_work.m_SaveData ? game_work.m_SaveData->GetTotal(pItemInfo->m_pTotalStatKey) : 0;
            int remaining = total;
            if (pItemInfo->m_CountDownFrom > 0) {
                remaining = pItemInfo->m_CountDownFrom - total;
                if (remaining < 0) remaining = 0;
            }
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
// ShopListItem::Draw @ v1.6.1 0x001b5da4
// ASM-spec v1.6.1 ShopListItem::Draw @0x001b5da4: offscreen path draws the
// fading description in the side panel with the LEGACY BITMAP font
// (pM_Fonts[1]) before DrawDarkness, gated m_pShopScreen && m_pItemInfo &&
// (int)(m_CostAlpha*255)>0; auto-shrink from 18.0 by 0.25 while
// GetStringHeight(desc,size,160)>82.5; locked+reqType1/2: requirement string
// (0xD7/0xD8 upside-down, 0xCE/0xCF zen-played; red 0xBD0000 unmet / green
// 0xA0DC00 met, alpha=a) at (GetDescriptionTextXPos(),-20) scale size*0.8
// align 3, then white desc at (x,+10) scale size*0.9 (type2 size*0.81)
// align 0xF; else desc at (x,0) scale size, white if locked else 0x745D3B,
// align 0xF; wrap 160, rotZ 0, no clip.
// ---------------------------------------------------------------------------
void ShopListItem::Draw() {
    // Reset divider colour cache when this row is selected.
    // Binary @0x001b5da4: *(static_block+0x8C) = 0xFFFFFFFF when *(this+0x27D) != 0.
    if (m_bSelected) {
        s_lastDrawnType = (int32_t)0xFFFFFFFF;
    }

    if (m_bOnscreen) {
        NewDraw();
        return;
    }

    // OFFSCREEN: keep fading the description while m_CostAlpha ramps out
    // (a row deselected then flung out of the viewport stays offscreen with
    // alpha > 0 for up to ~0.2s at the +/-5.0/s ramp).
    if (m_pShopScreen) {
        int a = (int)(m_CostAlpha * 255.0f);
        if (a > 255) a = 255;
        if (a < 0) a = 0;
        if (m_pItemInfo && a > 0) {
            // Legacy bitmap font (binary GOT+0x58: pM_Fonts[1] = pFontMain) --
            // NOT the TTF path the onscreen branch uses; deliberate v1.6.1
            // asymmetry.
            Mortar::Font* font = game_work.pFontMain.Get();
            if (font) {
                // Auto-shrink FONT SIZE only: from 18.0 by 0.25 while the
                // wrapped height at width 160 exceeds 82.5.
                float size = 18.0f;
                float h = font->GetStringHeight(
                    Mortar::Utf8StringIterator(m_DescText), size, 160.0f);
                while (h > 82.5f) {
                    size -= 0.25f;
                    h = font->GetStringHeight(
                        Mortar::Utf8StringIterator(m_DescText), size, 160.0f);
                }

                bool isLocked = (m_pItemInfo->IsLocked() != 0);
                int8_t reqType = m_pItemInfo->m_RequirementType;
                float xPos = m_pShopScreen->GetDescriptionTextXPos();

                if (isLocked && (reqType == 1 || reqType == 2)) {
                    // Requirement path: red unless the requirement is met.
                    Colour reqColour(0xBD, 0, 0, 0xFF);
                    LocalizedString promptId;
                    float s17 = size;
                    if (reqType == 1) {
                        bool met = Mortar::IsDeviceUpsideDown();
                        if (met) reqColour = Colour(0xA0, 0xDC, 0, 0xFF);
                        promptId = met
                            ? LSTR_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN   // 0xD8 (met)
                            : LSTR_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP;  // 0xD7 (not met)
                    } else {
                        s17 = size * 0.9f;
                        bool met = (game_work.m_SaveData != nullptr)
                            && game_work.m_SaveData->PlayedModeToday(GAME_MODE_ZEN);
                        if (met) reqColour = Colour(0xA0, 0xDC, 0, 0xFF);
                        promptId = met
                            ? LSTR_DJ_BAMBOO_BLADE_PLAYED_TODAY       // 0xCF (met)
                            : LSTR_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY;  // 0xCE (not met)
                    }
                    reqColour.a = (uint8_t)a;
                    const char* reqStr = GETSTRING_CAST_0(promptId);
                    if (reqStr) {
                        font->DrawString(Mortar::Utf8StringIterator(reqStr),
                                         xPos, -20.0f, 0.0f, reqColour,
                                         size * 0.8f, 160.0f, 0.0f,
                                         /*alignment*/3, NULL, 0.0f);
                    }
                    font->DrawString(Mortar::Utf8StringIterator(m_DescText),
                                     xPos, 10.0f, 0.0f,
                                     Colour(255, 255, 255, (uint8_t)a),
                                     s17 * 0.9f, 160.0f, 0.0f,
                                     /*alignment*/0x0F, NULL, 0.0f);
                } else {
                    Colour descColour = isLocked
                        ? Colour(255, 255, 255, (uint8_t)a)
                        : Colour(0x74, 0x5D, 0x3B, (uint8_t)a);
                    font->DrawString(Mortar::Utf8StringIterator(m_DescText),
                                     xPos, 0.0f, 0.0f, descColour, size,
                                     160.0f, 0.0f,
                                     /*alignment*/0x0F, NULL, 0.0f);
                }
            }
        }
    }

    DrawDarkness();
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
        // (RIGHT-horizontal | center-vertical). low-2-bits: 2=right, width=195 px.
        // Binary base = pos + m_Size(60,13,0), then box0 offset (-195,+16,0):
        //   title right edge = (pos.x+60)-195+195 = pos.x+60; icon left ~ pos.x+63 -> ~3px gap.
        m_pBox0 = new Mortar::BakedStringBox(ttfFont, 16.0f, 195, 30, (Mortar::ALIGNMENT_TYPE)0x0e, 1, 0);
        const char* title = m_pItemInfo->m_pTitle ? m_pItemInfo->m_pTitle : "";
        m_pBox0->SetText(title);
        // v1.6.1 ShopListItem::NewDraw @0x001b58e8: shadow Colour(0,0,0,0x40).
        m_pBox0->SetShadow(0.0f, Colour(0, 0, 0, 64), _Vector3<float>(4.0f, -4.0f, 0.0f), true);
        m_pBox0->Update();
    }

    // --- Box1: category (rebuild when m_TintA != m_Type) ---
    // v1.6.1 NewDraw @0x001b58e8: same ctor pattern with fontSize=14, 175x30.
    // typeNames (filled by Create @0x001b27f0): BLADE=0xCA, BG=0xC9, FULL=0xCB, SPECIAL=0x12F.
    if (!m_pBox1 || m_TintA != (uint8_t)m_pItemInfo->m_Type) {
        delete m_pBox1;
        m_pBox1 = new Mortar::BakedStringBox(ttfFont, 14.0f, 175, 30, (Mortar::ALIGNMENT_TYPE)0x0e, 1, 0);
        const char* catStr = nullptr;
        switch ((int)m_pItemInfo->m_Type) {
            case 0: catStr = GETSTRING_CAST_0(LSTR_SHOP_BLADE);        break; // 0xCA
            case 1: catStr = GETSTRING_CAST_0(LSTR_SHOP_BACKGROUND);   break; // 0xC9
            case 2: catStr = GETSTRING_CAST_0(LSTR_SHOP_FULL_VERSION); break; // 0xCB
            case 3: catStr = GETSTRING_CAST_0(LSTR_SHOP_SPECIAL);      break; // 0x12F
            default: catStr = nullptr; break;
        }
        if (catStr) m_pBox1->SetText(catStr);
        // v1.6.1 ShopListItem::NewDraw @0x001b58e8: shadow Colour(0,0,0,0x40).
        m_pBox1->SetShadow(0.0f, Colour(0, 0, 0, 64), _Vector3<float>(4.0f, -4.0f, 0.0f), true);
        m_pBox1->Update();
        m_TintA = (uint8_t)m_pItemInfo->m_Type;
    }

    // --- Set colour and draw both boxes ---
    // v1.6.1 NewDraw @0x001b58e8: base = pos + m_Size(60,13,0), then + (-195,16,0)/(-20,0,0) for box0 ; + (-175,-10,0) for box1.
    m_pBox0->SetColour(itemColour, 0);
    m_pBox0->SetTranslation(_Vector3<float>(pos.x + m_Size.x - 195.0f, pos.y + m_Size.y + 16.0f, 0.0f), 0);
    m_pBox0->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 0);

    m_pBox1->SetColour(itemColour, 0);
    m_pBox1->SetTranslation(_Vector3<float>(pos.x + m_Size.x - 175.0f, pos.y + m_Size.y - 10.0f, 0.0f), 0);
    m_pBox1->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 0);

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
        Matrix44 mat = Matrix44::MakeScale(257.0f, 17.0f, 0.0f);
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
        Matrix44 mat2 = Matrix44::MakeScale(257.0f, 17.0f, 0.0f);
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
    Matrix44 mat = Matrix44::MakeScale(64.0f, 64.0f, 0.0f);
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
        _Vector3<float> anchor(baseX - boxW - 4.0f, baseY + 8.0f + s_ShimmerY, 0.0f);
        // v1.6.1 DrawFloatingText @0x001b4cd4 (VNMLS): clampX = 0.25*65*alpha^2 - 240.0
        // (negative floor, -240..-223.75). max(anchor.x, clampX) leaves on-screen badges at
        // their computed anchor; only floors X during off-screen fade. (Prior port formula
        // was sign-inverted -> +224, pinning every NEW badge off-screen since m_State==1 is
        // the shop's normal Active state.)
        if (m_pShopScreen != 0 && m_pShopScreen->m_State == 1) {
            // DIFFERS: opt-in widescreen -- the -240.0f floor is the left-field
            // edge reference; use the real (possibly widened) edge so the NEW
            // badge clamps to the actual visible boundary. Identity under
            // disabled/__bada__.
#ifdef __bada__
            const float clampEdge = 240.0f;
#else
            const float clampEdge = Layout::HalfWidth();
#endif
            float clampX = 0.25f * (65.0f * m_NewItemAlpha * m_NewItemAlpha) - clampEdge;
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
        _Vector3<float> anchor((float)(int)(baseX - boxW - 32.0f), baseY - 26.0f, 0.0f);
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

    // ASM-verified: 2026-07-27T00:00Z v1.6.1 ShopListItem::DrawDescription @ 0x001b1f20 (asm-inspector)
    // Binary 0x001b1f74-0x001b2084: the prompt colour AND the prompt string id are
    // recomputed EVERY frame, before any box bookkeeping:
    //   Colour colour(0xBD,0,0,0xFF);                       // 0x001b1f7c mov r1,#0xbd
    //   if (IsLocked() && reqType != 0 && reqType != 3) {   // 0x001b1fa8/0x001b1fb0
    //       ... colour = Colour(0xA0,0xDC,0,0xFF) when met  // 0x001b1fd8 / 0x001b203c
    //       promptStr = GETSTRING_CAST_0(id);               // 0x001b2068
    //       colour.a = alpha;                               // 0x001b2070 strb r10,[sp,#0x47]
    //       reqFlag = 1;                                    // 0x001b2074
    //   }
    // reqType is 0..3 only (ItemInfo.h +0x24), so "!=0 && !=3" == "==1 || ==2".
    Colour promptColour(0xBD, 0, 0, 0xFF);
    const char* promptStr = NULL;
    uint8_t reqFlag = 0;

    bool bVar6 = isLocked && (purchaseState == 1 || purchaseState == 2);
    if (bVar6) {
        LocalizedString promptId;
        if (purchaseState == 1) {
            bool upsideDown = Mortar::IsDeviceUpsideDown();
            if (upsideDown) promptColour = Colour(0xA0, 0xDC, 0, 0xFF);
            promptId = upsideDown
                ? LSTR_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN   // 0xD8 (met)
                : LSTR_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP;  // 0xD7 (not met)
        } else {
            bool playedToday = (game_work.m_SaveData != nullptr)
                && game_work.m_SaveData->PlayedModeToday(GAME_MODE_ZEN);
            if (playedToday) promptColour = Colour(0xA0, 0xDC, 0, 0xFF);
            promptId = playedToday
                ? LSTR_DJ_BAMBOO_BLADE_PLAYED_TODAY       // 0xCF (met)
                : LSTR_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY;  // 0xCE (not met)
        }
        promptStr = GETSTRING_CAST_0(promptId);
        promptColour.a = alpha;
        reqFlag = 1;
    }

    // v1.6.1 DrawDescription @0x001b1f20: 0x14=Arabic, 0x0c=Japanese (NOT Korean/Chinese)
    bool isArabic = (game_work.languageFlag == 0x14);
    bool isJapanese = (game_work.languageFlag == 0x0C);

    // Binary 0x001b2084-0x001b20d8: on a reqFlag change BOTH boxes are destroyed
    // (m_pBox3 @+0x298 at 0x001b2090, m_pBox4 @+0x29C at 0x001b20b4), then
    // m_TrailFlag (+0x2A0) is updated -- not just box3.
    if (m_TrailFlag != reqFlag) {
        delete m_pBox3;
        m_pBox3 = NULL;
        delete m_pBox4;
        m_pBox4 = NULL;
        m_TrailFlag = reqFlag;
    }

    // Binary 0x001b20dc: box3 is (re)built only when the pointer is null.
    if (!m_pBox3) {
        // Base height and fontSize (binary 0x001b20e8-0x001b2124).
        float descH = reqFlag ? 62.0f : 82.0f;   // 0x3e : 0x52
        float fontSize = 14.0f;                  // 0xe
        if (isArabic) {
            descH -= 20.0f;
        } else if (isJapanese) {
            fontSize = 12.0f;                    // 0xc
            // Side-effect-only GETSTRING call for Japanese locale.
            // Binary @0x001b211c: GETSTRING_CAST_0(0x111) result discarded.
            (void)GETSTRING_CAST_0((LocalizedString)0x111);
        }
        // ASM-verified v1.6.1 DrawDescription @0x001b1f20: desc align=0x0F (center), maxLines=7, lineSpacing=4.
        m_pBox3 = new Mortar::BakedStringBox(ttfFont, fontSize, 160, (int)descH, (Mortar::ALIGNMENT_TYPE)0x0f, 7, 4);
        m_pBox3->SetText(m_DescText);
        m_pBox3->Update();
        m_pBox3->FitIntoVerticalBounds();
    }

    // Binary 0x001b2194-0x001b2220: build box4 when a prompt string exists and
    // the box is null. No SetColour here -- the colour is applied at draw time.
    if (promptStr && !m_pBox4) {
        // ASM-verified v1.6.1 DrawDescription @0x001b1f20: prompt align=0x0F (center), maxLines=2, lineSpacing=4.
        m_pBox4 = new Mortar::BakedStringBox(ttfFont, 12.0f, 160, 21, (Mortar::ALIGNMENT_TYPE)0x0f, 2, 4);
        m_pBox4->SetText(promptStr);
        m_pBox4->Update();
        m_pBox4->FitIntoVerticalBounds();
    }

    // Binary 0x001b2224-0x001b22b0: drawn whenever m_pBox4 != NULL (NOT gated on
    // reqFlag), and SetColour(colour, 1) runs EVERY frame at 0x001b229c so the
    // prompt tracks both the live m_CostAlpha fade and the live met/unmet colour.
    float xPos = m_pShopScreen->GetDescriptionTextXPos();
    if (m_pBox4) {
        // Position: (xPos, Arabic(0x14) ? -5 : -20, 0).
        float promptY = isArabic ? -5.0f : -20.0f;
        m_pBox4->SetTranslation(_Vector3<float>(xPos, promptY, 0.0f), 0);
        m_pBox4->SetColour(promptColour, 1);
        m_pBox4->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 0);
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
        m_pBox3->SetColour(descColour, 1);
        m_pBox3->SetTranslation(_Vector3<float>(xPos, 42.0f, 0.0f), 0);
        m_pBox3->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 0);
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

    // DIFFERS: opt-in widescreen (Layout::HalfWidth) -- these are the only dark
    // (0,0,0,128) shade quads in the shop list. At rest (m_TransitionAlpha==1)
    // parentX == LIST_SLIDE_OFF's resting value (-95.0f; see ShopScreen::Update),
    // so the 3:2 quad spans center=-97 +-145 = [-242, +48] -- already reaching
    // 2px past the 3:2 field edge (-240) by original binary design (the -2.0f
    // anchor fudge). At 16:9 the field's left edge moves out to -HalfWidth()
    // (e.g. -256 at 16:9), but a symmetric width scale only grows each edge by
    // the same amount, under-reaching the true left edge while overshooting
    // unnecessarily on the right (which must stay clear of the description
    // plate/equip button resting at X=145, POS_EQUIP_BUTTON_X). So the two
    // edges are computed independently instead of a single width*k scale:
    //   rightEdge: unchanged from 3:2 (parentX-2+145) -- same right boundary
    //              as the original binary, well clear of the plate at X=145.
    //   leftEdge:  extended by the exact field-edge shift so it reaches
    //              -HalfWidth() the same way A2's field-centred BG panel does,
    //              instead of scaling proportionally to the quad's own (off-
    //              center) width.
    // Identity when not wide / under __bada__ (HalfWidth()==240 -> leftEdge
    // reduces to the original parentX-2-145, byte-identical to pre-fix).
    // Vertical size (120) and the +105/-105 y offsets are unchanged.
#ifdef __bada__
    const float rightEdge = (parentX - 2.0f) + 145.0f;
    const float leftEdge  = (parentX - 2.0f) - 145.0f;
#else
    const float fieldLeftShift = Layout::HalfWidth() - 240.0f;  // 0 at 3:2
    const float rightEdge = (parentX - 2.0f) + 145.0f;
    const float leftEdge  = (parentX - 2.0f) - 145.0f - fieldLeftShift;
#endif
    // Mesh::DrawQuadUnCached draws a unit quad in [-0.5,0.5] local space, so
    // MakeScale+GlobalTranslate44(center, ...) centers the quad AT `center` --
    // recover width/center from the two independently-computed edges.
    const float shadeW  = rightEdge - leftEdge;
    const float anchorX = (leftEdge + rightEdge) * 0.5f;

    MatrixManager& mm = MatrixManager::GetInstance();
    ShopScreen::s_TexLoading->Set();

    {
        Matrix44 matTop = Matrix44::MakeScale(shadeW, 120.0f, 0.0f);
        matTop.GlobalTranslate44(anchorX, 105.0f, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matTop);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour(0, 0, 0, 128), NULL);
    }
    {
        Matrix44 matBot = Matrix44::MakeScale(shadeW, 120.0f, 0.0f);
        matBot.GlobalTranslate44(anchorX, -105.0f, 0.0f);
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
