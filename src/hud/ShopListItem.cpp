// Analysed: 2026-04-26T00:00
//
// ShopListItem implementation.
// Binary: ctor (0-param) 0x0015f9e8, ctor (5-param) 0x0015f734.
// Draw @ 0x0015eb00 -- ~450 instructions, 5 Font::DrawString calls.
// Move @ 0x0015d1fc -- sets pos + _pad2 (iconPos cache).
//
// See docs/screens/shop-list-item-draw.md for full spec.

#include "ShopListItem.h"
#include "ScrollingMenu.h"
#include "game/ItemInfo.h"
#include "game/ItemManager.h"
#include "screens/ShopScreen.h"
#include "Game.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Renderer.h"
#include "engine/render/Font.h"
#include "engine/math/Matrix44.h"
#include "engine/math/Colour.h"
#include "engine/math/Vec3.h"
#include "asset/TextureManager.h"
#include "engine/util/Localisation.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
// GL symbols come via Renderer.h -> gl_funcs.h

// ---------------------------------------------------------------------------
// ShopListItem::ShopListItem() @ 0x0015f9e8
// Binary: ScrollingMenuItem base ctor, SmartPtr::SetNull(+0x274),
//         *(+0x264) = DAT_0015fa54 (0.0f), *(+0x278) = 0,
//         *(+0x260) = *(+0x25c) = *(+0x280) = DAT_0015fa54 (0.0f),
//         *(+0x27e) = 0, *(+0x27c) = 1, *(+0x27d) = 0.
// ---------------------------------------------------------------------------
ShopListItem::ShopListItem()
    : ScrollingMenuItem()
    , m_NewItemAlpha(0.0f)
    , m_SelectedAlpha(0.0f)
    , m_LockFlashAlpha(0.0f)
    , m_pItemInfo(nullptr)
    , m_bOnscreenItem(1)
    , m_bSelected(0)
    , m_bIsNew(0)
    , _pad3(0)
    , m_CostAlpha(0.0f)
    , m_pShopScreen(nullptr)
{
    memset(_pad, 0, sizeof(_pad));
    memset(_pad2, 0, sizeof(_pad2));
    m_pIconTex.SetNull();
}

ShopListItem::~ShopListItem() {}

// ---------------------------------------------------------------------------
// ShopListItem::Move @ 0x0015d1fc (vtable slot 6, +0x18)
//
// Binary sequence:
//   1. pos.x = x; pos.y = y; pos.z = z
//   2. *(Vec3*)(this+0x268) = pos     // copy pos into _pad2 (iconPos)
//   3. if (SmartPtr<Texture>::operator bool(this+0x274)):
//        *(float*)(this+0x268) += DAT_0015d474(35.2f) + *(this+0x18)(m_Size.x=60.0f)
//        => _pad2.x = pos.x + 95.2f
//   4. Animate two alpha fields each frame using game.dt:
//        - one ramps toward 1 when ScrollingMenu->field_0x3c == 0
//          (port maps to m_LockFlashAlpha @ +0x264 -- best fit for the
//           "ramp up while menu is active" semantic).
//        - one ramps toward 1 when ItemManager::IsEquipped(m_pItemInfo)
//          (port maps to m_CostAlpha @ +0x280 -- gates description text
//           draw in Part 7 of Draw).
//      Both ramp at +/-5.0 per dt and clamp to [0, 1].
// ---------------------------------------------------------------------------
void ShopListItem::Move(float x, float y, float z) {
    pos.x = x;
    pos.y = y;
    pos.z = z;
    // Step 2: copy pos into _pad2 (this+0x268 = Vec3 iconPos)
    float* iconPos = reinterpret_cast<float*>(_pad2);
    iconPos[0] = pos.x;
    iconPos[1] = pos.y;
    iconPos[2] = pos.z;
    // Step 3: if icon texture valid, add 35.2f + m_Size.x (=60.0f) to iconPos.x
    // Binary: DAT_0015d474 = 35.2f; m_Size.x at +0x18 = 60.0f -> sum = 95.2f
    if (m_pIconTex.IsValid()) {
        iconPos[0] += 35.2f + m_Size.x;  // DAT_0015d474(35.2f) + m_Size.x(60.0f) = 95.2f
    }

    // Step 4: per-frame alpha ramps. Binary's two alphas are at offsets
    // +0x25C..+0x264 (4 bytes apart in the decomp's `this[N].base.field_0xXX`
    // syntax). Mapping verified by elimination + visible behaviour:
    //
    //   - m_NewItemAlpha  (+0x25C) ramps based on parent ScrollingMenu's
    //     `field_0x3c` byte (some "list active" gate). Port has no clean
    //     accessor for that flag; left out of Move so the new-item badge
    //     keeps its Create-time fade.
    //   - m_SelectedAlpha (+0x260) ramps based on ItemManager::IsEquipped:
    //     the equipped row's "selected_sml" ring fades to 1; non-equipped
    //     rows fade to 0. This is the badge the user sees -- without the
    //     animation it sticks at the value Create assigned, so changing
    //     equipment leaves the old ring visible forever.
    //   - m_CostAlpha (+0x280) is NOT touched by binary Move. Port animates
    //     it locally based on whether this row is the centered selection
    //     so the description-text Part 7 fade tracks the user's scroll.
    Game* g = Game::GetInstance();
    if (!g) return;
    const float dt   = g->dt;
    const float kRate = 5.0f;

    auto rampAlpha = [&](float cur, bool up) -> float {
        float delta = up ? +kRate : -kRate;
        float candidate = cur + dt * delta;
        if (candidate <= 0.0f) return 0.0f;
        if (candidate >= 1.0f) return 1.0f;
        return candidate;
    };

    // 4a: m_SelectedAlpha -- equipped indicator (binary +0x260, IsEquipped-driven).
    {
        ItemManager* im = ItemManager::GetInstance();
        bool equipped = (im && m_pItemInfo && im->IsEquipped(m_pItemInfo) != 0);
        m_SelectedAlpha = rampAlpha(m_SelectedAlpha, equipped);
    }

    // 4b: m_CostAlpha -- description-text gate (port-only animation).
    {
        bool isSelected = true;
        if (m_pShopScreen) {
            isSelected = (m_pShopScreen->GetSelectedItem() == this);
        }
        m_CostAlpha = rampAlpha(m_CostAlpha, isSelected);
    }
}

// ---------------------------------------------------------------------------
// ShopListItem::Create @ 0x0015c988
// Binary signature: void Create(ShopListItem* this, ItemInfo* param_1, ShopScreen* param_2)
//
// Binary writes (resolved constants from DAT addresses):
//   *(this + 0x24) = DAT_0015cae8 = 0x42a00000 = 80.0f  --> m_RowHeight = GetHeight()
//   *(this + 0x28) = DAT_0015caec = 0x43910000 = 290.0f --> m_RowWidth
//   *(this + 0x18) = DAT_0015caf0 = 0x42700000 = 60.0f  --> m_BBoxWidth (Vec3.x)
//   *(this + 0x1c) =               0x41500000 = 13.0f   --> m_BBoxHeight (Vec3.y, literal in decompile)
//   *(this + 0x20) = DAT_0015cae4 = 0x00000000 = 0.0f   --> m_BBoxDepth (Vec3.z)
//   *(this + 0x280) = DAT_0015cae4 = 0.0f               --> m_CostAlpha init
//   *(this + 0x58)  = param_2                            --> ShopScreen* back-ptr at +0x58
//   *(this + 0x278) = param_1                            --> m_pItemInfo
//
// Also: loads item icon texture into m_pIconTex from ItemInfo::m_pType string,
//       builds cost/description text into m_DescText (+0x5c),
//       checks ItemManager::IsEquipped -> sets m_SelectedAlpha(+0x260) to 1.0f,
//       checks ItemInfo::IsNew (field +0x3c) -> sets m_NewItemAlpha(+0x25c) to 1.0f.
// ---------------------------------------------------------------------------
void ShopListItem::Create(ItemInfo* pItemInfo, ShopScreen* pShopScreen) {
    // --- Row height (critical): 80.0f = DAT_0015cae8 = 0x42a00000 ---
    // GetHeight() reads m_Height (+0x24); this is the only place it gets set for shop rows.
    m_Height = 80.0f;   // DAT_0015cae8
    m_Width  = 290.0f;  // DAT_0015caec

    // --- Display size Vec3 (m_Size at +0x18/+0x1C/+0x20) ---
    // Binary: Vec3(60.0f, 13.0f, 0.0f) written to +0x18/+0x1C/+0x20
    m_Size.x = 60.0f;   // DAT_0015caf0
    m_Size.y = 13.0f;   // literal in decompile
    m_Size.z = 0.0f;    // DAT_0015cae4

    // --- Back-pointers ---
    m_pShopScreen = pShopScreen;   // +0x58 in binary (port: m_pShopScreen)
    m_pItemInfo   = pItemInfo;     // +0x278

    // --- Cost alpha init ---
    m_CostAlpha = 0.0f;  // DAT_0015cae4

    if (!pItemInfo) return;

    // --- Icon texture ---
    // Binary: loads item thumbnail by name from ItemInfo::m_pTextureName
    // (e.g. "item_discoblade") into TextureManager's textures/ subdir.
    if (pItemInfo->m_pTextureName && pItemInfo->m_pTextureName[0] != '\0') {
        std::string texFile = std::string(pItemInfo->m_pTextureName) + ".tex";
        m_pIconTex = Mortar::TextureManager::LoadLocalisedTexture(texFile.c_str());
    }

    // --- Description text ---
    // Binary: copies either locked text, cost-text, or description string into +0x5c.
    // Port: copy m_pDescText if available, else m_pTitle.
    if (pItemInfo->m_pDescText && pItemInfo->m_pDescText[0] != '\0') {
        strncpy(m_DescText, pItemInfo->m_pDescText, sizeof(m_DescText) - 1);
        m_DescText[sizeof(m_DescText) - 1] = '\0';
    } else if (pItemInfo->m_pTitle && pItemInfo->m_pTitle[0] != '\0') {
        strncpy(m_DescText, pItemInfo->m_pTitle, sizeof(m_DescText) - 1);
        m_DescText[sizeof(m_DescText) - 1] = '\0';
    }

    // --- Selected alpha: 1.0f if item is currently equipped ---
    // Binary: ItemManager::IsEquipped(pItemInfo) != 0 -> *(+0x260) = 0x3f800000 = 1.0f
    ItemManager* im = ItemManager::GetInstance();
    if (im && im->IsEquipped(pItemInfo)) {
        m_SelectedAlpha = 1.0f;   // DAT = 0x3f800000
    }

    // --- New-item alpha: 1.0f if item has the "new" flag ---
    // Binary: if (*(char*)(pItemInfo + 0x3c) == '\0') -> *(+0x25c) = 0x3f800000 = 1.0f
    // Note: +0x3c of ItemInfo = IsNew flag; binary test is "== '\\0'" meaning NOT new sets it.
    // Corrected read: if (!pItemInfo->IsNew()) m_NewItemAlpha = 1.0f
    // TODO: confirm ItemInfo::IsNew field offset when fully RE'd.
    // DIFFERS: stubbed; not setting m_NewItemAlpha here until ItemInfo::IsNew is confirmed.
}

// ---------------------------------------------------------------------------
// ShopListItem::Draw @ 0x0015eb00
//
// Render order (binary-faithful):
//   Guard 1: m_bSelected -> reset colour cache (static_block+0x8C)
//   Guard 2: m_bVisible (+0x2D ScrollingMenuItem field) == 0 -> return
//   Part 1: Title text (2 draws: shadow + fill); local_d0.y -= 26.0f after
//   Part 2: Cost hint text (2 draws: shadow + fill); uses local_d0 (y decremented)
//   Part 3: new_item_sml badge (when m_NewItemAlpha > 0)
//   Part 4: selected_sml highlight ring (when m_SelectedAlpha > 0)
//   Part 5: Item icon texture (when m_pIconTex valid); translate from _pad2 (+0x268)
//   Part 6: scratch_deviders divider cell (always); width=257, translate = pos+half
//   Part 7: Description text (when m_CostAlpha > 0); font shrink loop
//   Part 8: loading.tex new-badge stripes (OUTSIDE the visibility guard; when m_bIsNew)
// ---------------------------------------------------------------------------
void ShopListItem::Draw() {
    // --- Static colour cache (static_block+0x8C in binary) ---
    // Stores the last seen costType (ItemInfo->m_Type). Reset to 0xFFFFFFFF when
    // m_bSelected is set so the cache is re-evaluated (binary: write 0xFFFFFFFF).
    static int32_t s_costTypeCache = (int32_t)0xFFFFFFFF;

    // Guard 1: m_bSelected resets the colour cache
    // Binary: if (*(this+0x27D) != 0) static_block[+0x8C] = 0xFFFFFFFF
    if (m_bSelected) {
        s_costTypeCache = (int32_t)0xFFFFFFFF;
    }

    // --- Static cost-width cache (static_block+0x90..+0x9C in binary) ---
    // Filled lazily: if [+0x90] == 0.0f, all 4 widths are measured once.
    // Port: mirrors the same lazy fill semantics using a static float[4].
    static float s_costWidths[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Guard 2: skip if not onscreen.
    // Binary: if (*(this+0x2D) == 0) return;
    // +0x2D is m_bOnscreen inside the Delegate1 header (ScrollingMenuItem::m_Delegate.m_bOnscreen).
    // This is NOT the same as m_bOnscreenItem (+0x27C).
    if (!m_Delegate.m_bOnscreen) {
        // Part 8 is OUTSIDE this guard (see binary spec).
        // Fall through to Part 8 after the guard block.
        goto draw_part8;
    }

    {
        // --- Guard: no item info, nothing to render ---
        if (!m_pItemInfo) goto draw_part8;

        Game* g = Game::GetInstance();
        if (!g) goto draw_part8;

        Mortar::Font* font = g->pFontMain.IsValid() ? g->pFontMain.Get() : nullptr;
        if (!font) goto draw_part8;

        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        Renderer* r = Renderer::GetInstance();
        if (!r) goto draw_part8;

        // White colour singleton (*(Colour**)(GOT+0x73a4) in binary = {255,255,255,255})
        const Colour colourWhite(255, 255, 255, 255);

        // Item colour: white if unlocked, grey(200,200,200,255) if locked.
        // Binary: if (ItemInfo::IsLocked) CStack_40 = Colour(200,200,200,255)
        Colour itemColour = colourWhite;
        bool isLocked = (m_pItemInfo->IsLocked() != 0);
        if (isLocked) {
            itemColour = Colour(200, 200, 200, 255);
        }

        // local_d0: binary actually computes pos + m_Size. The Ghidra
        // decompile of `_Vector3::operator+(&local_d0, p_Var9)` drops the
        // hidden r2 register arg = &m_Size (offset +0x18). See spec at
        // docs/screens/shop-list-item-draw.md "Position re-verification".
        // m_Size for ShopListItem = (60, 13, 0) per Create.
        Vec3 local_d0(pos.x + m_Size.x, pos.y + m_Size.y, pos.z);

        // HD mode: Game.field_0x03 == '\f' (0x0C).
        // Binary: if HD -> scale=20, else scale=25
        bool isHD = (g->languageFlag == 0x0C);
        float titleScale = isHD ? 20.0f : 25.0f;

        // fVar26: title fit ratio (1.0 if no shrink), used to derive costScale.
        float fVar26 = 1.0f;

        // -----------------------------------------------------------------------
        // Part 1: Title text (shadow + fill)
        // Binary: Font::DrawString(scale,1.0,0.0, font, titleStr, pos, colour, vec2, 0xE, 0)
        // -----------------------------------------------------------------------
        const char* titleStr = m_pItemInfo->m_pTitle ? m_pItemInfo->m_pTitle : "";

        // fVar27: measured title width (pixels), used in Part 3 badge X.
        // Binary: MeasureString called once before the title draw loop, stored in fVar27.
        float fVar27 = font->MeasureWidth(1.0f, titleStr);

        // Scale-to-fit check (HD mode only)
        // Binary: if HD { if (fVar27 * scale > 175.0f) shrink; else scale=20, fVar26=1.0 }
        if (isHD) {
            if (fVar27 * titleScale > 175.0f) {     // DAT_0015eea8 = 175.0f
                float textScale = 175.0f / (fVar27 * titleScale);
                float scaled = textScale * 20.0f;
                titleScale = (scaled > 0.0f) ? scaled : 0.0f;
                fVar26 = textScale;  // ratio < 1.0 when shrinking
            } else {
                titleScale = 20.0f;
                fVar26 = 1.0f;  // sentinel: no shrink needed
            }
        }

        {
            // Shadow: +4, -4, 0 offset
            Vec3 shadowPos(local_d0.x + 4.0f, local_d0.y - 4.0f, local_d0.z);
            font->DrawStringSized(titleScale, 0.0f, 0.0f,
                             titleStr, shadowPos,
                             Colour(0, 0, 0, 64),
                             0xE);
            // Fill at local_d0
            font->DrawStringSized(titleScale, 0.0f, 0.0f,
                             titleStr, local_d0,
                             itemColour,
                             0xE);
        }

        // Decrement local_d0.y by 26.0f (hardcoded literal in binary).
        // All subsequent parts use the decremented Y.
        local_d0.y -= 26.0f;

        // -----------------------------------------------------------------------
        // Part 2: Cost hint text (shadow + fill)
        // Binary: picks costStr from static_block[+0x1C + m_Type*4].
        //   index 0 (m_Type==0): GETSTRING(0xB7)
        //   index 1 (m_Type==1): GETSTRING(0xB6)
        //   index 2 (m_Type==2): GETSTRING(0xB8)
        //   index 3 (m_Type==3): GETSTRING(0x113)
        // Width cache: if static_block[+0x90] == 0.0f, measure all 4 and cache.
        // costScale: HD -> fVar26*16.0f; non-HD -> 20.0f (0x41A00000)
        //
        // DIFFERS: binary uses integer-keyed GETSTRING_CAST_0_STR (keys 0xB7, 0xB6,
        //   0xB8, 0x113 cast from int). Port's Localisation::Get() takes string keys
        //   only. No integer-key lookup API exists. The cost strings are looked up
        //   from m_pItemInfo fields as a stub until integer-key localisation is wired.
        //   See docs/screens/shop-list-item-draw.md Part 2 for the binary spec.
        // -----------------------------------------------------------------------
        // costScale: HD -> fVar26 * 16.0f; non-HD -> 20.0f literal (0x41A00000)
        float costScale = isHD ? (fVar26 * 16.0f) : 20.0f;

        // Cost string: binary indexes static_block[+0x1C + m_Type*4] which is
        // populated lazily from GETSTRING_CAST_0_STR with the per-type
        // CATEGORY label (e.g. "blade", "background"). Per
        // translations_header.str, the keys are:
        //   m_Type == 0 (BLADE)      -> CODE_SHOP_BLADE
        //   m_Type == 1 (BACKGROUND) -> CODE_SHOP_BACKGROUND
        //   m_Type == 2 (UPSELL)     -> CODE_SHOP_FULL_VERSION
        //   m_Type == 3 (REMOVEADS)  -> no shop list entry in shipped data
        // Localisation::Get returns the key itself on miss, which is the
        // correct fallback semantic.
        const char* costStr = nullptr;
        switch ((int)m_pItemInfo->m_Type) {
            case 0: costStr = Localisation::Get("CODE_SHOP_BLADE");        break;
            case 1: costStr = Localisation::Get("CODE_SHOP_BACKGROUND");   break;
            case 2: costStr = Localisation::Get("CODE_SHOP_FULL_VERSION"); break;
            default: costStr = nullptr; break;
        }

        // Width cache (lazy): binary loop @ 0x0015ed3a measures EACH of the
        // 4 per-type category labels into static_block[+0x90 + i*4]. Port
        // mirrors that so the selected_sml badge in Part 4 is positioned
        // relative to THIS row's category label width, not whichever
        // label happened to be rendered first.
        if (s_costWidths[0] == 0.0f && font) {
            const char* k[4] = {
                Localisation::Get("CODE_SHOP_BLADE"),
                Localisation::Get("CODE_SHOP_BACKGROUND"),
                Localisation::Get("CODE_SHOP_FULL_VERSION"),
                "",
            };
            for (int i = 0; i < 4; i++) {
                if (k[i] && k[i][0] != '\0') {
                    float w = font->MeasureWidth(1.0f, k[i]);
                    s_costWidths[i] = w * costScale;
                }
            }
        }

        if (costStr) {
            Vec3 cShadowPos(local_d0.x + 4.0f, local_d0.y - 4.0f, local_d0.z);
            font->DrawStringSized(costScale, 0.0f, 0.0f,
                             costStr, cShadowPos,
                             Colour(0, 0, 0, 64),
                             0xE);
            font->DrawStringSized(costScale, 0.0f, 0.0f,
                             costStr, local_d0,
                             itemColour,
                             0xE);
        }

        // -----------------------------------------------------------------------
        // Part 3: new_item_sml badge -- when m_NewItemAlpha > 0
        // Binary: Scale = Vec3(65.0f, 33.0f, 0.0f) * alpha^2   (DAT_0015eebc=65, DAT_0015eec0=33)
        //         X = (local_d0.x - fVar27 * costScale) - 4.0f
        //             (fVar27 = measured title width; pFVar30 reused as costScale float)
        //         Y = DAT_0015eec8(34.0f) + local_d0.y + static_block[+0x6C]
        //           = 34.0f + (pos.y - 26.0f) + 0.0f = pos.y + 8.0f  (cache is 0)
        //         Z = 0.0f
        // Colour: white singleton (255,255,255,255) -- NOT itemColour.
        // -----------------------------------------------------------------------
        if (m_NewItemAlpha > 0.0f) {
            float alphaS = m_NewItemAlpha * m_NewItemAlpha;
            Matrix44 matBadge = Matrix44::Scale44(65.0f * alphaS, 33.0f * alphaS, 0.0f);
            // Badge X: local_d0.x - fVar27*titleScale - 4.0f
            // fVar27 = MeasureString(titleStr), measured before title draw.
            // Binary uses titleScale (s18) here, not costScale -- traced
            // via vmul.f32 s17,s0,s18 at 0x0015ec04 + use at 0x0015ef26.
            float badgeX = (local_d0.x - fVar27 * titleScale) - 4.0f;
            // Badge Y: 34.0f + local_d0.y + static_block[+0x6C](=0.0f)
            // local_d0.y is already pos.y - 26.0f, so 34.0 + (pos.y-26.0) = pos.y + 8.0
            float badgeY = 34.0f + local_d0.y;   // DAT_0015eec8 = 34.0f; cache = 0.0f
            // Binary integer-snaps the translate (vcvt.s32.f32) at 0x0015ef90 area.
            matBadge.GlobalTranslate44((float)(int)badgeX, (float)(int)badgeY, 0.0f);
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matBadge);
            mm.UploadModelViewOnly();

            if (ShopScreen::s_TexNewItemSmlBadge.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexNewItemSmlBadge->m_TexId);
                r->DrawQuad(colourWhite);  // always white (binary uses white singleton)
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // -----------------------------------------------------------------------
        // Part 4: selected_sml highlight ring -- when m_SelectedAlpha > 0
        // Binary: Scale = Vec3(65.0f, 33.0f, 0.0f) * alpha^2   (DAT_0015f17c=65, DAT_0015f180=33)
        //         X = (local_d0.x - static_block[+0x90 + m_Type*4]) - DAT_0015f184(32.0f)
        //             (static_block[+0x90+i*4] = cached cost text pixel width for type i)
        //         Y = local_d0.y   (= pos.y - 26.0f)
        //         Z = 0.0f   (DAT_0015f19c)
        // Colour: white singleton (255,255,255,255) -- NOT itemColour.
        // -----------------------------------------------------------------------
        if (m_SelectedAlpha > 0.0f) {
            float alphaS = m_SelectedAlpha * m_SelectedAlpha;
            Matrix44 matSel = Matrix44::Scale44(65.0f * alphaS, 33.0f * alphaS, 0.0f);
            // X: pos.x - cached cost width for this item's m_Type - 32.0f
            int typeIdx = (int)(uint8_t)m_pItemInfo->m_Type;
            if (typeIdx < 0 || typeIdx > 3) typeIdx = 0;
            float cachedCostW = s_costWidths[typeIdx];
            float selX = (local_d0.x - cachedCostW) - 32.0f;  // DAT_0015f184 = 32.0f
            float selY = local_d0.y;    // = pos.y - 26.0f (already decremented)
            // Binary integer-snaps only X (vcvt at 0x0015f064); Y stays float.
            matSel.GlobalTranslate44((float)(int)selX, selY, 0.0f);
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matSel);
            mm.UploadModelViewOnly();

            if (ShopScreen::s_TexSelectedSml.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexSelectedSml->m_TexId);
                r->DrawQuad(colourWhite);  // always white (binary uses white singleton)
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // -----------------------------------------------------------------------
        // Part 5: Item icon texture -- when m_pIconTex valid
        // Binary: Scale = Vec3(64.0f, 64.0f, 0.0f)   DAT_0015f188 = 64.0f
        //         Translate: global_icon_vec3(BSS,0,0,0) + _pad2(this+0x268)
        //           = (0 + _pad2.x, 0 + _pad2.y, 0 + _pad2.z)
        //           = (_pad2.x, _pad2.y, _pad2.z)
        //         _pad2.x set by Move = pos.x + 95.2f (when icon valid)
        //         If not locked: draw m_pIconTex.
        //         If locked: draw static_block[+0x40] = locked_stroke.tex.
        // Colour: white singleton (255,255,255,255) -- NOT itemColour.
        // -----------------------------------------------------------------------
        if (m_pIconTex.IsValid()) {
            Matrix44 matIcon = Matrix44::Scale44(64.0f, 64.0f, 0.0f);  // DAT_0015f188 = 64.0f
            // Translate from _pad2 (iconPos cache, set by Move each frame).
            // global_icon_vec3 (BSS) is zeroed, so translate = _pad2 directly.
            const float* iconPos = reinterpret_cast<const float*>(_pad2);
            matIcon.GlobalTranslate44(iconPos[0], iconPos[1], iconPos[2]);
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matIcon);
            mm.UploadModelViewOnly();

            if (!isLocked) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_pIconTex->m_TexId);
                r->DrawQuad(colourWhite);  // always white (binary uses white singleton)
                glBindTexture(GL_TEXTURE_2D, 0);
            } else {
                // locked: draw locked_stroke.tex (static_block[+0x40])
                if (ShopScreen::s_TexLockedStroke.IsValid()) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexLockedStroke->m_TexId);
                    r->DrawQuad(colourWhite);  // always white
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
            }
        }

        // -----------------------------------------------------------------------
        // Part 6: scratch_deviders divider cell -- always drawn
        // Binary: Scale = Vec3(257.0f, 17.0f, 0.0f)   DAT_0015f198 = 257.0f
        //         divider_scale = *(float**)(GOT+0x7214) (runtime float, ~1.0f)
        //         scaled = Vec3(257,17,0) * divider_scale
        //         translate = scaled/2.0f + this->pos   (ORIGINAL pos, not local_d0)
        //         => (pos.x + 128.5f, pos.y + 8.5f, 0.0f)  when divider_scale=1.0
        //
        // Colour cache logic (static_block[+0x8C]):
        //   costType = (int)(int8_t)(*(ItemInfo+0x10)) = m_Type (sign-extended)
        //   if (s_costTypeCache == costType): Colour(255,255,255,200)
        //   else: s_costTypeCache = costType; Colour(128,128,128,255)
        //
        // DIFFERS: divider_scale (GOT+0x7214) not wired; using 1.0f (port approximation).
        // -----------------------------------------------------------------------
        {
            int32_t costType = (int32_t)(int8_t)m_pItemInfo->m_Type;
            Colour dividerColour;
            if (s_costTypeCache == costType) {
                dividerColour = Colour(255, 255, 255, 200);
            } else {
                s_costTypeCache = costType;   // update cache (static_block[+0x8C])
                dividerColour = Colour(128, 128, 128, 255);
            }

            // Scale: 257 wide x 17 tall (the divider quad's pixel size).
            float dividerW = 257.0f;           // DAT_0015f198
            float dividerH = 17.0f;
            Matrix44 matDiv = Matrix44::Scale44(dividerW, dividerH, 0.0f);
            // Translate: pos + (yAxisUnit * m_Height / 2). Binary computes
            //   tmp = (*GOT[0x52c]) * m_Height        (= (0,1,0) * 80 = (0,80,0))
            //   tmp = tmp / 2.0                        (= (0,40,0))
            //   final = pos + tmp                      (= (pos.x, pos.y + 40, pos.z))
            // i.e. divider 1 sits at the row's TOP edge (Y-up: pos.y + halfRowH).
            float halfRowH = m_Height * 0.5f;
            matDiv.GlobalTranslate44(pos.x, pos.y + halfRowH, 0.0f);
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matDiv);
            mm.UploadModelViewOnly();

            if (ShopScreen::s_TexScratch.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexScratch->m_TexId);
                r->DrawQuad(dividerColour);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Second divider (gate: m_bIsNew != 0)
            // Same scale as divider 1 but translate uses the SUBTRACT path:
            // final = pos - (yAxisUnit * m_Height / 2)
            //       = (pos.x, pos.y - halfRowH, pos.z)
            // i.e. divider 2 sits at the row's BOTTOM edge (Y-up).
            if (m_bIsNew) {
                float dividerW2 = 257.0f;      // DAT_0015f51c = 257.0f
                float dividerH2 = 17.0f;
                Matrix44 matDiv2 = Matrix44::Scale44(dividerW2, dividerH2, 0.0f);
                matDiv2.GlobalTranslate44(pos.x, pos.y - halfRowH, 0.0f);
                mm.GetWorldStack().Reset();
                mm.GetWorldStack().SetCurrentMatrix(matDiv2);
                mm.UploadModelViewOnly();

                if (ShopScreen::s_TexScratch.IsValid()) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexScratch->m_TexId);
                    r->DrawQuad(Colour(128, 128, 128, 255));  // always grey
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
            }
        }

        // -----------------------------------------------------------------------
        // Part 7: Description / cost text -- when m_CostAlpha > 0 and pointers valid
        // Binary: gate: *(this+0x58) != 0 && *(this+0x278) != 0
        //         alphaU = clamp((uint)(m_CostAlpha * 255.0f), 0, 255)
        //         if alphaU == 0: skip
        //         descBuf = (char*)(this+0x5c) = m_DescText
        //         Font shrink loop:
        //           descFontSize = 18.0f
        //           while (GetStringHeight(font, descBuf, descFontSize, 160.0f) > 82.5f)
        //               descFontSize -= 0.25f
        //         xPos = ShopScreen::GetDescriptionTextXPos()
        //         purchaseState = m_pItemInfo->m_RequirementType (+0x24)
        //
        // Colour: locked -> (255,255,255,alpha), unlocked -> (0x74,0x5D,0x3B,alpha)
        // DAT_0015f524 = 82.5f; DAT_0015f540 = 160.0f (wrap width)
        // -----------------------------------------------------------------------
        if (m_pShopScreen && m_pItemInfo) {
            uint32_t alphaU = (uint32_t)(m_CostAlpha * 255.0f);  // DAT_0015f520=255.0f
            if (alphaU > 0xFE) alphaU = 0xFF;
            alphaU &= ~((int32_t)alphaU >> 31);  // clamp negative to 0
            uint8_t descAlpha = (uint8_t)alphaU;

            if (descAlpha != 0) {
                const char* descStr = (m_DescText[0] != '\0')
                    ? m_DescText
                    : (m_pItemInfo->m_pDescText ? m_pItemInfo->m_pDescText : "");

                float descFontSize = 18.0f;

                // Font shrink loop: while height > 82.5f (DAT_0015f524), reduce by 0.25f
                // Binary: Font::GetStringHeight(font, descBuf, descFontSize, 160.0f)
                // Port: Font::GetStringHeight is available; wire if the method exists.
                // DIFFERS: Font::GetStringHeight not confirmed on port Font class; stubbed.
                // TODO: Replace with font->GetStringHeight(descStr, descFontSize, 160.0f) > 82.5f
                // when the method is verified.
                (void)descFontSize;  // suppress unused-variable warning until loop is wired
                descFontSize = 18.0f;
                // Stub: loop would be:
                //   float h = font->GetStringHeight(descStr, descFontSize, 160.0f);
                //   while (h > 82.5f) { descFontSize -= 0.25f; h = font->GetStringHeight(...); }

                float xPos = 65.0f;  // fallback
                if (m_pShopScreen) {
                    xPos = m_pShopScreen->GetDescriptionTextXPos();
                }

                int8_t purchaseState = m_pItemInfo->m_RequirementType;

                Colour descColour;
                if (isLocked) {
                    descColour = Colour(255, 255, 255, descAlpha);
                } else {
                    descColour = Colour(0x74, 0x5D, 0x3B, descAlpha);
                }

                // Description Y positions are ABSOLUTE world coords (the
                // description panel is centered on the screen, not anchored
                // to row pos.y). Binary loads y as a float literal at
                // 0x0015f5b6 etc. -- not a derived value.
                // Wrap width for the right-side description panel.
                // Binary: DAT_0015f540 = 160.0f -- text wider than this
                // gets broken onto multiple lines by Font::DrawString's
                // word-wrap path.
                static constexpr float DESC_WRAP_W = 160.0f;

                if (purchaseState == 0 || purchaseState == 3) {
                    // Case 0 or 3: normal description draw at y=0 (DAT_0015f53c)
                    Vec3 descPos(xPos, 0.0f, 0.0f);
                    font->DrawStringSized(descFontSize, DESC_WRAP_W, 0.0f,
                                     descStr, descPos,
                                     descColour,
                                     0xF);
                } else if (purchaseState == 1) {
                    // Case 1: cost-per-play
                    // Binary: line 1 at y=-20 (DAT_0015f4e6 = 0xC1A00000),
                    //         line 2 at y=+10 (DAT_0015f57a = 0x41200000).
                    // DIFFERS: FruitSaveData::PlayedModeToday and
                    //   IsDeviceUpsideDown not wired -- port draws stub text
                    //   in both slots. TODO: implement when FruitSaveData lands.
                    Vec3 descPos(xPos, -20.0f, 0.0f);
                    font->DrawStringSized(descFontSize * 0.8f, DESC_WRAP_W, 0.0f,
                                     descStr, descPos,
                                     Colour(0xBD, 0, 0, descAlpha),
                                     3);
                    Vec3 descPos2(xPos, 10.0f, 0.0f);
                    font->DrawStringSized(descFontSize * 0.9f, DESC_WRAP_W, 0.0f,
                                     descStr, descPos2,
                                     Colour(255, 255, 255, descAlpha),
                                     0xF);
                } else if (purchaseState == 2) {
                    // Case 2: played-mode-today
                    // Same Y layout as case 1 (line1 at -20, line2 at +10).
                    // Binary state-2 path multiplies by 0.9 TWICE (once at
                    // 0x0015f460 vmul s17,s16,0.9 and again at the shared
                    // second-line draw 0x0015f56c vmul s3,s17,0.9), giving
                    // s16 * 0.81. Case 1 only multiplies once (= 0.9).
                    // DIFFERS: FruitSaveData not wired.
                    Vec3 descPos(xPos, -20.0f, 0.0f);
                    font->DrawStringSized(descFontSize * 0.8f, DESC_WRAP_W, 0.0f,
                                     descStr, descPos,
                                     Colour(0xBD, 0, 0, descAlpha),
                                     3);
                    Vec3 descPos2(xPos, 10.0f, 0.0f);
                    font->DrawStringSized(descFontSize * 0.81f, DESC_WRAP_W, 0.0f,
                                     descStr, descPos2,
                                     Colour(255, 255, 255, descAlpha),
                                     0xF);
                }
            }
        }
    }  // end of onscreen block

    // -----------------------------------------------------------------------
    // Part 8: loading.tex new-badge stripes -- OUTSIDE the onscreen guard
    // Binary: gate is *(this+0x27E) != 0 (m_bIsNew), runs regardless of m_bVisible.
    //   Texture::Set(static_block2[+0x2C])  -- loading.tex
    //   Stripe 1: Scale(290,120,0), Translate(parent->pos.x - 2.0, 105.0, 0)
    //   Stripe 2: Scale(290,120,0), Translate(parent->pos.x - 2.0, -105.0, 0)
    //   Colour = (0,0,0,128)
    // Parent pos.x = *(float*)(*(this+0x10) + 8)  -- m_pParent->pos.x
    // -----------------------------------------------------------------------
    draw_part8:
    if (m_bIsNew) {
        // m_pParent->pos.x (binary: *(*(this+0x10) + 8))
        float parentX = m_pParent ? m_pParent->pos.x : pos.x;

        if (ShopScreen::s_TexLoading.IsValid()) {
            Mortar::MatrixManager& mm2 = Mortar::MatrixManager::GetInstance();
            Renderer* r2 = Renderer::GetInstance();
            if (r2) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexLoading->m_TexId);

                // Stripe 1 (top): Translate(parentX - 2.0, 105.0, 0)
                {
                    Matrix44 matTop = Matrix44::Scale44(290.0f, 120.0f, 0.0f);  // DAT_0015f718, DAT_0015f71c
                    matTop.GlobalTranslate44(parentX - 2.0f, 105.0f, 0.0f);    // DAT_0015f724=105.0f
                    mm2.GetWorldStack().Reset();
                    mm2.GetWorldStack().SetCurrentMatrix(matTop);
                    mm2.UploadModelViewOnly();
                    r2->DrawQuad(Colour(0, 0, 0, 128));  // (0,0,0,0x80)
                }
                // Stripe 2 (bottom): Translate(parentX - 2.0, -105.0, 0)
                {
                    Matrix44 matBot = Matrix44::Scale44(290.0f, 120.0f, 0.0f);  // DAT_0015f718, DAT_0015f71c
                    matBot.GlobalTranslate44(parentX - 2.0f, -105.0f, 0.0f);   // DAT_0015f728=-105.0f
                    mm2.GetWorldStack().Reset();
                    mm2.GetWorldStack().SetCurrentMatrix(matBot);
                    mm2.UploadModelViewOnly();
                    r2->DrawQuad(Colour(0, 0, 0, 128));
                }

                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    }
}
