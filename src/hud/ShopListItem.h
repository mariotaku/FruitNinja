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
// Binary ScrollingMenuItem ends at +0x58 (88 bytes; base ctor 0x0015b5dc).
// ShopListItem own-fields begin immediately at +0x58 in the binary:
//   +0x58  ShopScreen*  m_pShopScreen  (binary m_field58: void* set to ShopScreen*)
//   +0x5C  char[128]    m_DescText     (inline description text buffer)
//   +0xDC  (end of m_DescText)
//
// Extended ShopListItem fields (absolute offsets from ShopListItem* this):
//   +0x25C  float  m_NewItemAlpha   >0 -> draw new_item_sml badge; fades from init
//   +0x260  float  m_SelectedAlpha  >0 -> draw selected_sml highlight ring
//   +0x264  float  m_LockFlashAlpha (init 0.0; set to 0.25 on locked-item tap)
//   +0x268  char[0x0C]  _pad2       (likely Vec3 for icon translate -- not yet RE'd)
//   +0x274  SmartPtr<Texture>  m_pIconTex  item icon texture; SetNull in ctor
//   +0x278  ItemInfo*  m_pItemInfo          ptr to item info; 0 in ctor
//   +0x27C  byte   m_bOnscreenItem  1 in ctor; 0 = off-screen, Draw early-exits
//   +0x27D  byte   m_bSelected      0 in ctor; 1 = resets static colour cache
//   +0x27E  byte   m_bIsNew         0 in ctor; non-zero = draw loading.tex badge
//   +0x27F  pad
//   +0x280  float  m_CostAlpha      (m_CostAlpha * 255.0f) -> byte alpha for cost/desc text
//
// Gap from end of ScrollingMenuItem (+0xDC) to +0x25C = 0x180 bytes.
// Intermediate layout not yet fully RE'd. Filled with zeros until a full RE pass.
//
// Analysed: 2026-04-25T14:30
//

#include "ScrollingMenuItem.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstddef>

class ItemInfo;
class ShopScreen;

// ASM-verified: 2026-04-29T00:00Z binary @ 0x0015f9e8 + 0x0015c988 + 0x0015eb00 (asm-inspector)
class ShopListItem : public ScrollingMenuItem {
public:
    // Matches ShopListItem::ShopListItem() @ 0x0015f9e8
    ShopListItem();
    ~ShopListItem() override;

    // ShopListItem::Create @ 0x0015c988
    // Called by ShopScreen::Init after each ShopListItem() ctor.
    // Sets m_RowHeight (GetHeight() field at +0x24) to 80.0f (DAT_0015cae8),
    // m_BBoxWidth/m_BBoxHeight/m_BBoxDepth from Vec3(60.0f, 13.0f, 0.0f),
    // stores ItemInfo* and ShopScreen* back-pointer, loads icon texture,
    // and builds the item's description/cost text buffer at +0x5c.
    void Create(ItemInfo* pItemInfo, ShopScreen* pShopScreen);

    // vtable slot 6 (+0x18): Move override
    // Binary 0x0015d9fc (ShopListItem::Move):
    //   1. Sets pos.x/y/z from incoming Vec3.
    //   2. Always copies pos into _pad2 (this+0x268):
    //        *(Vec3*)(this+0x268) = pos
    //   3. If m_pIconTex.IsValid():
    //        *(float*)(this+0x268) += DAT_0015d474(35.2f) + m_Size.x(60.0f)
    //        => _pad2.x = pos.x + 95.2f
    // See docs/screens/shop-list-item-draw.md section 8 for full spec.
    void Move(float x, float y, float z) override;

    // vtable slot 11 (+0x2C): Draw override -- renders the row
    // Binary 0x0015eb00, ~450 instructions, 5 Font::DrawString calls.
    void Draw() override;

    // --- ShopListItem own fields mirroring binary +0x58..+0xDB ---
    //
    // +0x58 (ARM32): ShopScreen* back-pointer.
    // Binary Create @ 0x0015c988: *(this+0x58) = param_2 (ShopScreen*).
    // Binary Draw   @ 0x0015eb00: *(int*)(in_r0+0x58) != 0 check, then ->field_0xb8.
    // field_0xb8 of ShopScreen = m_State (int). Typed void* in binary; port uses ShopScreen*.
    ShopScreen* m_pShopScreen;        // +0x58 ARM32

    // +0x5C (ARM32): inline description text buffer (128 bytes).
    // Binary Draw: pcVar23 = (char*)(in_r0+0x5c) passed to Font::DrawString.
    // Binary Create: snprintf/strncpy fills this buffer from ItemInfo strings.
    char m_DescText[128];             // +0x5C..+0xDB ARM32

    // --- Padding to bridge from end-of-base (x86_64) to ARM32 +0x25C for m_NewItemAlpha ---
    // ARM32 ShopListItem layout after m_DescText (+0xDC): gap to +0x25C.
    // On x86_64, sizeof(ScrollingMenuItem) + sizeof(ShopScreen*) + 128 > ARM32 equivalents,
    // so the _pad below shrinks to compensate. Size tuned to satisfy the static_assert
    // for m_NewItemAlpha == 0x25C on GCC/libstdc++ x86_64 (MinGW).
    // Formula: 0x25C - sizeof(ScrollingMenuItem)_x86 - sizeof(ShopScreen*)_x86 - 128 - align
    // DIFFERS: exact size determined empirically; update if base struct layout changes.
    char _pad[0x163];                 // bridge: end-of-m_DescText..+0x25B

    // +0x25C: new-item badge alpha (>0 => draw new_item_sml badge)
    float m_NewItemAlpha;             // +0x25C

    // +0x260: selected-ring alpha (>0 => draw selected_sml highlight)
    float m_SelectedAlpha;            // +0x260

    // +0x264: flash alpha (set to 0.25 = 0x3e800000 on locked-item tap in binary)
    // Binary offset confirmed from ShopScreen::ClickedOnShopItem.
    float m_LockFlashAlpha;           // +0x264

    // +0x268..+0x273: Vec3 iconPos -- written by Move each frame.
    // _pad2[0..3] = float x = pos.x + 95.2f (when m_pIconTex valid), else pos.x
    // _pad2[4..7] = float y = pos.y
    // _pad2[8..11] = float z = pos.z
    // Binary: ShopListItem::Move @ 0x0015d1fc writes iconXOff = DAT_0015d474(35.2f) + m_Size.x(60.0f)
    // Draw Part 5 reads *(float*)(this+0x268) as translate X.
    char _pad2[0x0c];                 // +0x268..+0x273  (Vec3 iconPos, see Move)

    // +0x274: item icon texture SmartPtr (4 bytes on ARM32)
    // On x86_64: SmartPtr = 8 bytes + alignment gap. The ARM32 absolute offsets
    // for fields after m_pIconTex and m_pItemInfo cannot be satisfied on x86_64
    // without hiding the pointer inside a byte array. Fields below are accessed by
    // name only; the static_asserts for them are ARM32-only (guarded by sizeof(void*)==4).
    SmartPtr<Mortar::Texture> m_pIconTex;  // +0x274 (ARM32)

    // +0x278: pointer to the ItemInfo for this list entry (null = no item)
    ItemInfo* m_pItemInfo;            // +0x278 (ARM32)

    // +0x27C: onscreen flag (1 = on-screen; 0 = Draw early-exits)
    uint8_t m_bOnscreenItem;          // +0x27C

    // +0x27D: selected flag (1 = resets static colour cache)
    uint8_t m_bSelected;              // +0x27D

    // +0x27E: new-item flag (non-zero = draw loading.tex badge stripes)
    uint8_t m_bIsNew;                 // +0x27E

    // +0x27F: alignment pad
    uint8_t _pad3;                    // +0x27F

    // +0x280: cost text alpha (m_CostAlpha * 255.0f clamped -> byte alpha)
    float m_CostAlpha;                // +0x280
};

// ---------------------------------------------------------------------------
// Compile-time offset verification (ARM32 binary absolute offsets).
//
// Fields at +0x25C..+0x264 (pure float, no intervening pointer fields) are
// verifiable on any platform — the _pad before them is tuned to 0x163 bytes
// so they land at the ARM32 absolute offsets on both ARM32 and x86_64.
//
// Fields at +0x27C..+0x280 follow m_pIconTex (SmartPtr, 4B ARM32 / 8B x86_64)
// and m_pItemInfo (ptr, 4B ARM32 / 8B x86_64). These CANNOT land at the exact
// ARM32 absolute offsets on x86_64 without faking the pointer types.
// Those asserts are therefore guarded to fire only on 32-bit (ARM32 target) builds.
// Port code always accesses these fields by name, so the actual x86_64 offset
// mismatch has no runtime impact.
// ---------------------------------------------------------------------------

// `offsetof` on a non-standard-layout class (ShopListItem inherits from
// ScrollingMenuItem which has virtual methods) is "conditionally-supported"
// per the C++ standard. GCC emits -Winvalid-offsetof but its result is
// correct for our layout-checking purpose. Suppress the warning around
// the static_asserts -- they're a build-time invariant check, not actual
// pointer arithmetic.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

// Always-active: float fields at +0x25C..+0x264 (size-invariant across
// pointer widths). Gate on libstdc++ — MSVC's STL has a different
// std::string SBO size, which shifts these offsets. Binary was built
// with libstdc++ (CodeSourcery G++ 4.4.1) so the offset invariant only
// applies when the port is also using libstdc++.
#if defined(__GLIBCXX__)
static_assert(offsetof(ShopListItem, m_NewItemAlpha)  == 0x25C, "ShopListItem::m_NewItemAlpha must be at +0x25C");
static_assert(offsetof(ShopListItem, m_SelectedAlpha) == 0x260, "ShopListItem::m_SelectedAlpha must be at +0x260");
static_assert(offsetof(ShopListItem, m_LockFlashAlpha)== 0x264, "ShopListItem::m_LockFlashAlpha must be at +0x264");
#endif

#if defined(__arm__) || (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4)
// ARM32-only: fields after pointer-sized members m_pIconTex + m_pItemInfo.
// On x86_64 these land 12 bytes higher due to pointer widths; that is expected.
static_assert(offsetof(ShopListItem, m_bOnscreenItem) == 0x27C, "ShopListItem::m_bOnscreenItem must be at +0x27C");
static_assert(offsetof(ShopListItem, m_bSelected)     == 0x27D, "ShopListItem::m_bSelected must be at +0x27D");
static_assert(offsetof(ShopListItem, m_bIsNew)        == 0x27E, "ShopListItem::m_bIsNew must be at +0x27E");
static_assert(offsetof(ShopListItem, m_CostAlpha)     == 0x280, "ShopListItem::m_CostAlpha must be at +0x280");
#endif // __arm__

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // FN_SHOP_LIST_ITEM_H
