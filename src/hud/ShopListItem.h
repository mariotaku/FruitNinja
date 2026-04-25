#ifndef FN_SHOP_LIST_ITEM_H
#define FN_SHOP_LIST_ITEM_H

//
// ShopListItem : ScrollingMenuItem
// Binary refs:
//   ctor (0-param) 0x0015f9e8
//   ctor (5-param) 0x0015f734  (takes ItemInfo* + texture data)
//   dtor           0x0015cf50 / 0x0015cfb4 / 0x0015d018
//
// Vtable overrides (from 0x001ea030):
//   slot  0 (+0x00)  ~ShopListItem dtor1  0x0015cfb4
//   slot  1 (+0x04)  ~ShopListItem dtor2  0x0015d018
//   slot  6 (+0x18)  ShopListItem::Move   0x0015d9fc
//   slot 11 (+0x2C)  ShopListItem::Draw   0x0015eb00
//
// Additional fields (appended after ScrollingMenuItem base, ~0x58 bytes):
//   +0x25C  float  m_NewItemAlpha   >0 -> draw new_item_sml badge; fades from init
//   +0x260  float  m_SelectedAlpha  >0 -> draw selected_sml highlight ring
//   +0x264  float  (unknown float)  init 0.0
//   +0x268  (unknown, size 0xC)
//   +0x274  SmartPtr<Texture>  m_pIconTex  item icon texture; SetNull in ctor
//   +0x278  ItemInfo*  m_pItemInfo          ptr to item info; 0 in ctor
//   +0x27C  byte   m_bOnscreenItem  1 in ctor; 0 = off-screen, Draw early-exits
//   +0x27D  byte   m_bSelected      0 in ctor; 1 = resets static colour cache
//   +0x27E  byte   m_bIsNew         0 in ctor; non-zero = draw loading.tex badge
//   +0x27F  (pad)
//   +0x280  float  m_CostAlpha      (m_CostAlpha * 255.0f) -> byte alpha for cost/desc text
//
// Gap from end of ScrollingMenuItem (~+0x58) to +0x25C = 0x204 bytes.
// Intermediate layout not yet fully RE'd. Filled with zeros until a full RE pass.
//
// Analysed: 2026-04-25T20:30
//

#include "ScrollingMenuItem.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class ItemInfo;
class ShopScreen;

class ShopListItem : public ScrollingMenuItem {
public:
    // Matches ShopListItem::ShopListItem() @ 0x0015f9e8
    ShopListItem();
    ~ShopListItem() override;

    // vtable slot 6 (+0x18): Move override
    // Binary 0x0015d9fc: sets pos.x/y/z from incoming Vec3.
    void Move(float x, float y, float z) override {
        pos.x = x; pos.y = y; pos.z = z;
    }

    // vtable slot 11 (+0x2C): Draw override -- renders the row
    // Binary 0x0015eb00, ~450 instructions, 5 Font::DrawString calls.
    void Draw() override;

    // --- Extended fields (binary offsets from ShopListItem* this) ---
    // Padding from end of ScrollingMenuItem (+0x58) to +0x25c = 0x204 bytes.
    // Binary analysis shows intermediate data (text buffers, sub-structs)
    // that are not yet fully RE'd. Filled with zeros until a full RE pass.
    char _pad[0x204];                // +0x58..+0x25b  (unknown intermediate)

    // +0x25c: new-item badge alpha (>0 => draw new_item_sml badge)
    float m_NewItemAlpha;

    // +0x260: selected-ring alpha (>0 => draw selected_sml highlight)
    float m_SelectedAlpha;

    // +0x264: flash alpha (set to 0.25 = 0x3e800000 on locked-item tap in binary)
    // Binary offset confirmed from ShopScreen::ClickedOnShopItem.
    float m_LockFlashAlpha;

    // +0x268..+0x273: padding (unknown 0xC bytes -- likely a Vec3 for icon translate)
    char _pad2[0x0c];

    // +0x274: item icon texture SmartPtr (4 bytes)
    SmartPtr<Mortar::Texture> m_pIconTex;

    // +0x278: pointer to the ItemInfo for this list entry (null = no item)
    ItemInfo* m_pItemInfo;

    // +0x27c: onscreen flag (1 = on-screen; 0 = Draw early-exits)
    uint8_t m_bOnscreenItem;

    // +0x27d: selected flag (1 = resets static colour cache)
    uint8_t m_bSelected;

    // +0x27e: new-item flag (non-zero = draw loading.tex badge stripes)
    uint8_t m_bIsNew;

    // +0x27f: pad
    uint8_t _pad3;

    // +0x280: cost text alpha (m_CostAlpha * 255.0f clamped -> byte alpha)
    float m_CostAlpha;

    // Port-only: back-pointer to ShopScreen so Draw can call GetDescriptionTextXPos().
    // Binary accesses m_TransitionAlpha via GOT; port uses explicit pointer.
    // Set by ShopScreen when adding the item to the list.
    ShopScreen* m_pShopScreen;
};

#endif // FN_SHOP_LIST_ITEM_H
