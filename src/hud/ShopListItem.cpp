// Analysed: 2026-04-25T20:30
//
// ShopListItem implementation.
// Binary: ctor (0-param) 0x0015f9e8, ctor (5-param) 0x0015f734.
// Draw @ 0x0015eb00 -- ~450 instructions, 5 Font::DrawString calls.
//
// See docs/screens/shop.md "ShopListItem::Draw" for full spec.

#include "ShopListItem.h"
#include "ScrollingMenu.h"
#include "game/ItemInfo.h"
#include "screens/ShopScreen.h"
#include "Game.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Renderer.h"
#include "engine/render/Font.h"
#include "engine/math/Matrix44.h"
#include "engine/math/Colour.h"
#include "engine/math/Vec3.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
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
// ShopListItem::Draw @ 0x0015eb00
//
// Render order (binary-faithful):
//   Guard: m_bOnscreenItem == 0 -> return
//   Part 1: Title text (2 draws: shadow + fill)
//   Part 2: Cost hint text (2 draws: shadow + fill)
//   Part 3: new_item_sml badge (when m_NewItemAlpha > 0)
//   Part 4: selected_sml highlight ring (when m_SelectedAlpha > 0)
//   Part 5: Item icon texture (when m_pIconTex valid)
//   Part 6: scratch_deviders divider cell (always)
//   Part 7: Description text (1 draw, when m_CostAlpha > 0)
//   Part 8: loading.tex new-badge stripes (when m_bIsNew)
// ---------------------------------------------------------------------------
void ShopListItem::Draw() {
    // --- Static colour cache (static_block+0x8C in binary) ---
    // Reset when m_bSelected is set (binary: static_block[+0x8C] = 0xFFFFFFFF)
    static uint32_t s_colourCache = 0xFFFFFFFF;

    // --- Guard: m_bSelected resets the colour cache ---
    if (m_bSelected) {
        s_colourCache = 0xFFFFFFFF;
    }

    // --- Guard: skip if off-screen ---
    // Binary: if (*(char*)(in_r0 + 0x27C) == 0) return;
    if (!m_bOnscreenItem) return;

    // --- Guard: no item info, nothing to render ---
    if (!m_pItemInfo) return;

    Game* g = Game::GetInstance();
    if (!g) return;

    Mortar::Font* font = g->pFontMain.IsValid() ? g->pFontMain.Get() : nullptr;
    if (!font) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // White/full-alpha colour from GOT (*(Colour**)(GOT+0x73a4) at runtime)
    // Assumed {255,255,255,255} per spec.
    const Colour colourWhite(255, 255, 255, 255);

    // Item colour: white if unlocked, grey if locked.
    // Binary: if (ItemInfo::IsLocked) itemColour = Colour(200,200,200,255)
    Colour itemColour = colourWhite;
    bool isLocked = (m_pItemInfo->IsLocked() != 0);
    if (isLocked) {
        itemColour = Colour(200, 200, 200, 255);
    }

    // basePos from ScrollingMenuItem::pos (set by Move each frame by Update).
    // Binary: Vec3 basePos = this->pos  (in_r0 + 0x04)
    Vec3 basePos(pos.x, pos.y, pos.z);

    // HD mode: Game.field_0x03 == '\f' (0x0C).
    // Binary: if HD -> scale=20 else scale=25
    bool isHD = (g->languageFlag == 0x0C);  // field_0x03 in binary = languageFlag at +0x03
    float titleScale = isHD ? 20.0f : 25.0f;  // DAT text scale

    // ---------------------------------------------------------------------------
    // Part 1: Title text (2 draws: shadow + fill)
    // Binary: Font::Font_DrawString(scale,1.0,0.0, font, iter, pos, colour, vec2, 0xE, 0)
    // ---------------------------------------------------------------------------
    const char* titleStr = m_pItemInfo->m_pTitle ? m_pItemInfo->m_pTitle : "";

    // Scale-to-fit check (HD mode only)
    if (isHD) {
        float measured = font->MeasureWidth(1.0f, titleStr);
        if (measured * titleScale > 175.0f) {
            float textScale = 175.0f / (measured * titleScale);
            float scaled = textScale * 20.0f;
            titleScale = (scaled > 0.0f) ? scaled : 0.0f;
        }
    }

    {
        // Shadow draw: offset (+4, -4, 0) from basePos
        Vec3 shadowPos(basePos.x + 4.0f, basePos.y - 4.0f, basePos.z);
        font->DrawStringSized(titleScale, 0.0f, 0.0f,
                         titleStr, shadowPos,
                         Colour(0, 0, 0, 64),
                         0xE);  // flags 0xE = right+bottom alignment

        // Actual draw at basePos
        font->DrawStringSized(titleScale, 0.0f, 0.0f,
                         titleStr, basePos,
                         itemColour,
                         0xE);
    }

    // ---------------------------------------------------------------------------
    // Part 2: Cost hint text (2 draws: shadow + fill)
    // Binary: picks cost string from static_block[+0x1C + costTypeIndex*4].
    // Cost type index = *(char*)(ItemInfo + 0x10) = m_pItemInfo->m_Type.
    // Static block stores 4 cost strings; not yet fully ported.
    //
    // Port stub: format the cost value directly from m_pItemInfo->m_Cost.
    // "FREE" when m_Cost <= 0 (matches binary -1=purchased/free behaviour).
    // DIFFERS: binary uses pre-cached cost strings from static_block[+0x1C..+0x28].
    // TODO: wire actual cost strings from ItemManager/ItemInfo when available.
    // ---------------------------------------------------------------------------
    float costScale = isHD ? (titleScale * 0.8f) : 20.0f;  // 20.0 = 0x41a00000 in binary

    char costBuf[32];
    if (m_pItemInfo->m_Cost <= 0) {
        snprintf(costBuf, sizeof(costBuf), "FREE");
    } else {
        snprintf(costBuf, sizeof(costBuf), "%d", (int)m_pItemInfo->m_Cost);
    }

    {
        Vec3 cShadowPos(basePos.x + 4.0f, basePos.y - 4.0f, basePos.z);
        font->DrawStringSized(costScale, 0.0f, 0.0f,
                         costBuf, cShadowPos,
                         Colour(0, 0, 0, 64),
                         0xE);

        font->DrawStringSized(costScale, 0.0f, 0.0f,
                         costBuf, basePos,
                         itemColour,
                         0xE);
    }

    // ---------------------------------------------------------------------------
    // Part 3: new_item_sml badge -- when m_NewItemAlpha > 0
    // Binary: Scale = Vec3(65.0f, 33.0f, 0.0f) * alpha^2
    //         Translate: (basePos.x - title_width*costScale - 4.0f,
    //                     34.0f + basePos.y + static_block[+0x6C], 0.0f)
    // DAT_0015f188 = 64.0f (icon scale), badge scale is 65x33.
    // DIFFERS: translate X uses stubbed cost width (0) for now.
    // ---------------------------------------------------------------------------
    if (m_NewItemAlpha > 0.0f) {
        float alphaS = m_NewItemAlpha * m_NewItemAlpha;
        Matrix44 matBadge = Matrix44::Scale44(65.0f * alphaS, 33.0f * alphaS, 0.0f);
        // DIFFERS: title_width*costScale not measured; using basePos.x - 4.0 as placeholder
        matBadge.GlobalTranslate44(basePos.x - 4.0f, 34.0f + basePos.y, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matBadge);
        mm.UploadModelViewOnly();

        if (ShopScreen::s_TexNewItemSmlBadge.IsValid()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexNewItemSmlBadge->m_TexId);
            r->DrawQuad(itemColour);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // ---------------------------------------------------------------------------
    // Part 4: selected_sml highlight ring -- when m_SelectedAlpha > 0
    // Binary: Scale = Vec3(65.0f, 33.0f, 0.0f) * alpha^2
    //         Translate: (basePos.x - cached_costWidth - 32.0f, basePos.y, 0.0f)
    // DIFFERS: cached cost width not available; using basePos.x - 32.0f.
    // ---------------------------------------------------------------------------
    if (m_SelectedAlpha > 0.0f) {
        float alphaS = m_SelectedAlpha * m_SelectedAlpha;
        Matrix44 matSel = Matrix44::Scale44(65.0f * alphaS, 33.0f * alphaS, 0.0f);
        // DIFFERS: costTypeIndex width cache not ported; using -32.0 placeholder
        matSel.GlobalTranslate44(basePos.x - 32.0f, basePos.y, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matSel);
        mm.UploadModelViewOnly();

        if (ShopScreen::s_TexSelectedSml.IsValid()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexSelectedSml->m_TexId);
            r->DrawQuad(itemColour);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // ---------------------------------------------------------------------------
    // Part 5: Item icon texture -- when m_pIconTex is non-null
    // Binary: Scale = Vec3(64.0f, 64.0f, 0.0f)   DAT_0015f188 = 64.0f
    //         Translate from cached Vec3 at GOT+0x73ec plus (in_r0+0x268)
    //         If not locked: draw m_pIconTex.
    //         If locked: draw locked_stroke.tex (greyed-out overlay).
    // TODO: translate from _pad2 (+0x268 cache) not yet resolved; using basePos.
    // ---------------------------------------------------------------------------
    if (m_pIconTex.IsValid()) {
        Matrix44 matIcon = Matrix44::Scale44(64.0f, 64.0f, 0.0f);  // DAT_0015f188 = 64.0f
        // DIFFERS: translate uses basePos; binary adds GOT+0x73ec cached Vec3 + _pad2 Vec3
        matIcon.GlobalTranslate44(basePos.x, basePos.y, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matIcon);
        mm.UploadModelViewOnly();

        if (!isLocked) {
            // Binary: Texture::Set(*(in_r0 + 0x274))  -- m_pIconTex
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_pIconTex->m_TexId);
            r->DrawQuad(itemColour);
            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            // Binary: Texture::Set(static_block[+0x40])  -- locked_stroke.tex
            if (ShopScreen::s_TexLockedStroke.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexLockedStroke->m_TexId);
                r->DrawQuad(itemColour);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Part 6: scratch_deviders divider cell -- always drawn
    // Binary: Scale = Vec3(DAT_0015f198, 17.0f, 0.0f) * *(float**)(GOT+0x7214) / 2.0f
    //         Translate based on pos + parent scroll.
    // Colour: white (255,255,255,200) if selected type matches, grey (128,128,128,255) otherwise.
    // DIFFERS: divider size multiplier (GOT+0x7214) not ported; using 1.0f.
    // DIFFERS: position uses basePos; binary adds parent scroll offset.
    // ---------------------------------------------------------------------------
    {
        // Colour cache: if s_colourCache matches current costTypeIndex -> white, else grey
        // Binary uses static_block[+0x8C] as the cached "current type" selector.
        // Port stub: pick colour based on m_bSelected.
        Colour dividerColour = m_bSelected
            ? Colour(255, 255, 255, 200)
            : Colour(128, 128, 128, 255);

        // DAT_0015f198 = ? (width for divider row cell; not resolved yet)
        // DIFFERS: using 290.0f as placeholder (matches loading.tex width DAT_0015f718)
        float dividerW = 290.0f;  // DIFFERS: DAT_0015f198 not resolved
        Matrix44 matDiv = Matrix44::Scale44(dividerW, 17.0f, 0.0f);
        matDiv.GlobalTranslate44(basePos.x, basePos.y, 0.0f);
        mm.GetWorldStack().Reset();
        mm.GetWorldStack().SetCurrentMatrix(matDiv);
        mm.UploadModelViewOnly();

        if (ShopScreen::s_TexScratch.IsValid()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexScratch->m_TexId);
            r->DrawQuad(dividerColour);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Second divider draw when m_bIsNew (binary: uses operator- for translate)
        if (m_bIsNew) {
            // DIFFERS: translate X uses negative offset (operator-); using basePos.x - dividerW
            Matrix44 matDiv2 = Matrix44::Scale44(dividerW, 17.0f, 0.0f);
            matDiv2.GlobalTranslate44(basePos.x - dividerW, basePos.y, 0.0f);
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matDiv2);
            mm.UploadModelViewOnly();

            if (ShopScreen::s_TexScratch.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexScratch->m_TexId);
                r->DrawQuad(Colour(128, 128, 128, 255));
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Part 7: Description / lock text -- when m_CostAlpha > 0
    // Binary: alpha = clamp((uint)(m_CostAlpha * 255.0f), 0, 255)
    //         descBuf = (char*)(in_r0 + 0x5c)  -- m_DescText inline buffer
    //         descFontSize = 18.0f, shrunk to fit DAT_0015f524 = 65.0f height
    //         xPos = ShopScreen::GetDescriptionTextXPos()
    //         purchaseState = ItemInfo + 0x24 = m_RequirementType
    //
    // purchaseState==0 or ==3: draw description text normally.
    // purchaseState==1: "cost per play" mode (stub -- use normal description).
    // purchaseState==2: FruitSaveData::PlayedModeToday check (stub -- normal desc).
    //
    // Colour: locked -> (255,255,255,alpha), unlocked -> (0x74,0x5D,0x3B,alpha).
    // DAT_0015f524 = 65.0f (max description height).
    // ---------------------------------------------------------------------------
    {
        uint32_t alphaU = (uint32_t)(m_CostAlpha * 255.0f);
        if (alphaU > 255) alphaU = 255;
        uint8_t descAlpha = (uint8_t)alphaU;

        if (descAlpha != 0) {
            // descBuf: inline text buffer at +0x5c of ScrollingMenuItem base.
            // Binary reads description from ItemInfo::m_pDescText or the inline
            // buffer depending on parse state. Use m_DescText if non-empty,
            // otherwise fall back to m_pItemInfo->m_pDescText.
            const char* descStr = (m_DescText[0] != '\0')
                ? m_DescText
                : (m_pItemInfo->m_pDescText ? m_pItemInfo->m_pDescText : "");

            float descFontSize = 18.0f;

            // Shrink descFontSize until text height fits within 65.0f.
            // Binary: while (Font::GetStringHeight(font, descBuf, descFontSize, ...) > 65.0f)
            //             descFontSize -= 0.25f;
            // Port: approximate -- reduce if text is long.
            // TODO: port Font::GetStringHeight once available.
            // DIFFERS: height check stubbed; using fixed 18.0f.

            // xPos from GetDescriptionTextXPos (ShopScreen slide formula - 80.0f offset)
            float xPos = 65.0f;  // fallback at alpha=1 (145.0f - 80.0f)
            if (m_pShopScreen) {
                xPos = m_pShopScreen->GetDescriptionTextXPos();
            }

            // purchaseState (ItemInfo + 0x24) = m_RequirementType
            int8_t purchaseState = m_pItemInfo->m_RequirementType;

            // Colour: locked -> white+alpha, unlocked -> 0x74,0x5D,0x3B,alpha
            Colour descColour;
            if (isLocked) {
                descColour = Colour(255, 255, 255, descAlpha);
            } else {
                descColour = Colour(0x74, 0x5D, 0x3B, descAlpha);
            }

            if (purchaseState == 0 || purchaseState == 3) {
                // Normal description draw
                Vec3 descPos(xPos, basePos.y, basePos.z);
                font->DrawStringSized(descFontSize, 0.0f, 0.0f,
                                 descStr, descPos,
                                 descColour,
                                 0xF);  // flags 0xF per spec
            }
            // purchaseState==1 and ==2: stubbed -- same as normal for now
            // DIFFERS: binary has special cost-per-play / playedToday handling.
            // TODO: implement purchaseState==1 and ==2 paths when FruitSaveData is wired.
            else {
                Vec3 descPos(xPos, basePos.y, basePos.z);
                font->DrawStringSized(descFontSize, 0.0f, 0.0f,
                                 descStr, descPos,
                                 descColour,
                                 0xF);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Part 8: loading.tex new-badge overlay -- when m_bIsNew
    // Binary: two semi-transparent black quads (top + bottom stripes):
    //   Scale = Vec3(290.0f, 120.0f, 0.0f)  DAT_0015f718=290, DAT_0015f71c=120
    //   Translate 1: (parent->pos.x - 2.0f,  105.0f, 0.0f)  DAT_0015f724=105
    //   Translate 2: (parent->pos.x - 2.0f, -105.0f, 0.0f)  DAT_0015f728=-105
    //   Colour = Colour(0,0,0,128)
    //
    // The binary reads parent->pos.x from the ScrollingMenu parent.
    // Port: uses m_pParent->pos.x if available, else basePos.x.
    // ---------------------------------------------------------------------------
    if (m_bIsNew) {
        float parentX = m_pParent ? m_pParent->pos.x : basePos.x;

        if (ShopScreen::s_TexLoading.IsValid()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ShopScreen::s_TexLoading->m_TexId);

            // Top stripe
            {
                Matrix44 matTop = Matrix44::Scale44(290.0f, 120.0f, 0.0f);  // DAT_0015f718, DAT_0015f71c
                matTop.GlobalTranslate44(parentX - 2.0f, 105.0f, 0.0f);    // DAT_0015f724=105.0f
                mm.GetWorldStack().Reset();
                mm.GetWorldStack().SetCurrentMatrix(matTop);
                mm.UploadModelViewOnly();
                r->DrawQuad(Colour(0, 0, 0, 128));
            }
            // Bottom stripe
            {
                Matrix44 matBot = Matrix44::Scale44(290.0f, 120.0f, 0.0f);  // DAT_0015f718, DAT_0015f71c
                matBot.GlobalTranslate44(parentX - 2.0f, -105.0f, 0.0f);   // DAT_0015f728=-105.0f
                mm.GetWorldStack().Reset();
                mm.GetWorldStack().SetCurrentMatrix(matBot);
                mm.UploadModelViewOnly();
                r->DrawQuad(Colour(0, 0, 0, 128));
            }

            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}
