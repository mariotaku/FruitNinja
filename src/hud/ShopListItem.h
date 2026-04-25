#ifndef FN_SHOP_LIST_ITEM_H
#define FN_SHOP_LIST_ITEM_H

//
// ShopListItem : ScrollingMenuItem
// Binary refs:
//   ctor (0-param) 0x0015f9e8
//   ctor (5-param) 0x0015f734  (takes ItemInfo* + texture data)
//   dtor           0x0015cf50 / 0x0015cfb4 / 0x0015d018
//
// A single row in the shop's item list. Extends ScrollingMenuItem with
// shop-specific fields: item info pointer, thumbnail texture, lock state,
// alpha and colour data.
//
// Layout (from ctor 0x0015f9e8 — 0-param):
//   +0x00..+0x57  ScrollingMenuItem base
//   +0x258        (ScrollingMenuItem size is ~0x58, ShopListItem starts at 0x258??)
//
// Actually from the ctor: offsets accessed are 0x25c, 0x260, 0x264, 0x274,
// 0x278, 0x27c, 0x27d, 0x27e, 0x280. This means ShopListItem has a large
// intermediate (likely the 5-param ctor data), OR the offset is from the
// ScrollingMenuItem base struct (size 0x58) plus the extended fields.
//
// From binary offsets in ShopScreen::Update using ShopListItem:
//   base + 0x278   ItemInfo*   m_pItemInfo    (checked for null, queried for type)
//   base + 0x264   float       m_Alpha        (set to 0.25 = 0x3e800000 on locked click)
//   base + 0x260   float       m_field260     (init 0)
//   base + 0x25c   float       m_field25c     (init 0)
//   base + 0x274   SmartPtr<Texture> m_TexThumb (thumbnail texture)
//   base + 0x280   float       m_field280     (init from DAT)
//   base + 0x27c   byte        m_fieldMenu    (init 1)
//   base + 0x27d   byte        m_field27d     (init 0)
//   base + 0x27e   byte        m_field27e     (init 0)
//
// Wait — those are very large offsets for a struct. Given ScrollingMenuItem
// size ~0x58 and ShopListItem extending it, the extra fields start at +0x58.
// Binary offsets seen are 0x25c, 0x260, etc. which are offsets from the
// ShopListItem THIS pointer, meaning ShopListItem size is at least 0x284 bytes.
// This implies there's a LOT more intermediate data (possibly a per-item
// rendering buffer or name storage). Since ScrollingMenuItem is ~0x58 bytes,
// the gap from +0x58 to +0x25c (= 0x204 bytes) is unaccounted for.
//
// Rather than guessing the intermediate layout, we place the known fields
// at the correct binary offsets (relative to ShopListItem base) by using
// padding. This keeps the struct binary-faithful for the pointer arithmetic
// ShopScreen does via field offsets.
//
// Port status: STUB — thumbnail rendering and full field layout pending.
// The key binary-facing fields (m_pItemInfo at +0x278, m_Alpha at +0x264)
// are properly positioned via padding.
//
// Analysed: 2026-04-25T14:00
//

#include "ScrollingMenuItem.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class ItemInfo;

class ShopListItem : public ScrollingMenuItem {
public:
    // Matches ShopListItem::ShopListItem() @ 0x0015f9e8
    ShopListItem();
    ~ShopListItem() override;

    // --- Extended fields (binary offsets from ShopListItem* this) ---
    // Padding from end of ScrollingMenuItem (+0x58) to +0x25c = 0x204 bytes.
    // Binary analysis shows intermediate data (text buffers, sub-structs)
    // that are not yet fully RE'd. Filled with zeros until a full RE pass.
    char _pad[0x204];                // +0x58..+0x25b  (unknown intermediate)

    // +0x25c: float field (init 0.0)
    float m_field25c;

    // +0x260: float field (init 0.0)
    float m_field260;

    // +0x264: alpha value (set to 0x3e800000 = 0.25 on locked-item tap)
    float m_Alpha;

    // +0x268..+0x273: padding
    char _pad2[0x0c];

    // +0x274: thumbnail texture SmartPtr (4 bytes)
    SmartPtr<Mortar::Texture> m_TexThumb;

    // +0x278: pointer to the ItemInfo for this list entry (null = no item)
    ItemInfo* m_pItemInfo;

    // +0x27c: byte field (init 1)
    uint8_t m_fieldMenu;

    // +0x27d: byte field (init 0)
    uint8_t m_field27d;

    // +0x27e: byte field (init 0)
    uint8_t m_field27e;

    // +0x27f: pad
    uint8_t _pad3;

    // +0x280: float field (init from DAT — not yet resolved)
    float m_field280;
};

#endif // FN_SHOP_LIST_ITEM_H
