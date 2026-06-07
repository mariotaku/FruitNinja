#ifndef FN_SCROLLING_MENU_H
#define FN_SCROLLING_MENU_H

//
// ScrollingMenu : HUDControl  (NOT HUDControl3d)
// Binary refs:
//   ctor    0x0015b3b0
//   dtor    0x0015b03c / 0x0015b08c / 0x0015b0d8
//   Update  0x0015b747 (377 lines)
//   AddItem 0x0015be54
//
// Scrollable list of ScrollingMenuItems with touch-based drag/swipe.
// Struct size: ~0x100
//
// Field map (binary offsets are from decompile; port fields listed in
// declaration order to match the closest feasible C++ layout):
//
//  Binary name        | Binary offset | Port field
//  -------------------|---------------|---------------------
//  field22_0x74       | +0x74         | m_TouchId
//  field_0x78..0x80   | +0x78         | m_TouchAnchorPos (Vec3)
//  field_0x84..0x8c   | +0x84         | m_AnchorOffset (Vec3; copy of velocity at anchor-down)
//  field_0x90..0x98   | +0x90         | m_PendingVelocity (Vec3)
//  field59_0x9c       | +0x9c         | m_Width   (DAT_0015b468 = 320.0f)
//  field60_0xa0       | +0xa0         | m_Height  (DAT_0015b46c = 240.0f)
//  field61_0xa4       | +0xa4         | m_ItemHeight (DAT_0015b470 = -120.0f)
//  field41_0xa8       | +0xa8         | m_TotalHeight  (AddItem += GetHeight())
//  field42_0xac       | +0xac         | m_TotalWidth   (AddItem += GetWidth())
//  (std::vector)      | +0xb0..+0xbb  | m_Items
//  field76_0xbc       | +0xbc         | m_ClosestIdx
//  field78_0xc4       | +0xc4         | m_SnapDist  (snap-dist acc; init 1.0f)
//  field77_0xc0       | +0xc0         | m_DragTargetIdx  (ephemeral; NOT persistent selection)
//  field_0xc8         | +0xc8         | m_bDragging
//  field_0xc9         | +0xc9         | m_bTouchProcessed
//  field_0xca         | +0xca         | m_fieldCA
//  field83_0xcc       | +0xcc         | m_pCollidedItem
//  field_0xd0         | +0xd0         | m_bConstrainedView
//  field_0xd4..0xdc   | +0xd4         | m_Velocity (Vec3; m_Velocity.y = TRUE scroll offset)
//  (true scroll off)  | +0xd8         | m_Velocity.y  (was wrongly called m_ScrollOffset@+0x88)
//  field100..103      | +0xe0..0xec   | m_OuterRegion[4]  (touch outer rect, relative to pos.x)
//  field104..107      | +0xf0..0xfc   | m_InnerRegion[4]  (touch inner rect, relative to pos.x)
//
// The TRUE scroll offset is m_Velocity.y (binary field_0xd8).
// The old m_ScrollOffset at +0x88 was incorrect — +0x88 is m_AnchorOffset.y
// (the scroll position latched when the finger first pressed down).
//
// Analysed: 2026-04-25T21:45
//

#include "HUDControl.h"
#include "ScrollingMenuItem.h"
#include <vector>

class ScrollingMenu : public HUDControl {
public:
    // Matches ScrollingMenu::ScrollingMenu() @ 0x0015b3b0
    ScrollingMenu();
    ~ScrollingMenu() override;

    // HUDControl overrides
    void Update(float dt) override;

    // ScrollingMenu::Draw @ 0x0015af98
    // Pure iterator: calls vtable+0x2C (Draw) on each item in m_Items.
    // No scissor/clip, no per-frame positioning (that is done by Update).
    void Draw(const Vec3& hudScale, int layerMask) override;

    // ScrollingMenu::AddItem @ 0x0015be54
    // Appends item to m_Items, updates width/height accumulators,
    // calls item->SetParent(this).
    ScrollingMenuItem* AddItem(ScrollingMenuItem* item);

    // Returns count of items in m_Items.
    int GetNumItems() const;

    // ScrollingMenu::GetItemClosestToZeroIdx @ 0x00147980
    // Returns m_ClosestIdx (field76_0xbc), closest-to-zero item index.
    // ShopScreen calls this to track selection changes.
    int GetItemClosestToZeroIdx() const;

    // ScrollingMenu::GetItemClosestToZero @ 0x001479ec
    // Returns pointer to the item at m_ClosestIdx, or nullptr.
    ScrollingMenuItem* GetItemClosestToZero() const;

    // DestroyList — clears and deletes all items.
    void DestroyList();

    // ScrollingMenu::Collide(slot) @ 0x0015af4c
    // Walks m_Items calling vtable+0x34 (Slot13/hit-test) on each.
    // Returns the first item that returns non-null, or nullptr.
    // Only active when m_fieldCA != 0.
    ScrollingMenuItem* Collide(int touchSlot);

    // Inline accessors — binary @ 0x0014797c..0x001479dc
    int   GetType() override { return 8; }            // 0x0014797c
    float GetHeight() const        { return m_Height; }            // 0x00147988 reads +0xa0
    float GetWidth()  const        { return m_ItemHeight; }        // 0x00147990 reads +0xa4 (port name swap)
    float GetItemHeight() const    { return m_Width; }             // 0x001479dc reads +0x9c (port name swap)

    // Width/height setters (vtable slots byte-proven 2026-06-06):
    //   SetHeight    vtable+0x4c -> +0xa0 (m_Height)   -- scroll-boundary clamp field
    //   SetWidth     vtable+0x50 -> +0xa4 (m_ItemHeight) + 4 derived region fields
    //   SetItemHeight vtable+0x54 -> +0x9c (m_Width)   -- item row height
    // Port field names (m_Width/m_Height/m_ItemHeight) are name-swapped vs binary semantics;
    // preserved to avoid mangled-symbol drift on the getter/setter method names.
    // ASM-verified: 2026-06-06 binary @ 0x001479a0 (asm-inspector) -- SetWidth=vtable+0x50->+0xa4, SetHeight=vtable+0x4c->+0xa0, SetItemHeight=vtable+0x54->+0x9c. Shop: SetWidth(290)/SetHeight(80)/SetItemHeight(80).
    void SetWidth(float w);
    void SetHeight(float h)     { m_Height = h; }
    // SetItemHeight @ 0x001479d4: vtable+0x54, writes +0x9c (port: m_Width -- field-name swap).
    void SetItemHeight(float h) { m_Width = h; }

    // --- Fields at documented binary offsets ---
    // (binary offset +0x74 relative to object start)

    // +0x74: touch slot index (-1 = no active touch)
    int m_TouchId;

    // +0x78..0x80: touch anchor position Vec3
    //   .x = touch[slot].x at finger-down  (field_0x78)
    //   .y = touch[slot].y at finger-down  (field_0x7c)
    //   .z = touch[slot].state at finger-down (field_0x80)
    Vec3 m_TouchAnchorPos;

    // +0x84..0x8c: velocity/scroll snapshot at finger-down
    //   .x = m_Velocity.x at anchor   (field_0x84)
    //   .y = m_Velocity.y at anchor   (field_0x88 = scroll offset at press)
    //   .z = m_Velocity.z at anchor   (field_0x8c)
    Vec3 m_AnchorOffset;

    // +0x90..0x98: pending velocity Vec3 (accumulated drag delta; friction-scaled each tick)
    Vec3 m_PendingVelocity;

    // +0x9c: scroll area width (DAT_0015b468 = 320.0f)
    float m_Width;
    // +0xa0: visible window height (DAT_0015b46c = 240.0f)
    float m_Height;
    // +0xa4: outer-region half-height used in touch rect setup (DAT_0015b470 = -120.0f)
    float m_ItemHeight;

    // +0xa8: total scroll height accumulator (field41_0xa8, updated by AddItem += GetHeight())
    float m_TotalHeight;
    // +0xac: total scroll width accumulator (field42_0xac, updated by AddItem += GetWidth())
    float m_TotalWidth;

    // +0xb0..+0xbb: item list (Sourcery pre-C++11 std::vector = 12 bytes)
    std::vector<ScrollingMenuItem*> m_Items;

    // +0xbc: closest-to-zero item index (field76_0xbc; what ShopScreen reads)
    int m_ClosestIdx;

    // +0xc0: ephemeral drag target index (field77_0xc0; NOT persistent selection index)
    // This tracks which item is being dragged/hovered, not the final selected item.
    // Renamed from m_SelectedIdx to clarify: ShopScreen uses m_ClosestIdx for selection.
    int m_DragTargetIdx;

    // +0xc4: closest-to-snap distance accumulator (field78_0xc4; init 1.0f)
    float m_SnapDist;

    // +0xc8: drag mode flag (1 while finger has moved past drag threshold)
    uint8_t m_bDragging;
    // +0xc9: touch-processed flag (cleared each Update; set when tap fires)
    uint8_t m_bTouchProcessed;
    // +0xca: enables Collide() walk (init 1)
    uint8_t m_fieldCA;
    // +0xcb: padding
    uint8_t m_fieldCB;

    // +0xcc: pointer to item collided at touch-acquire time (field83_0xcc)
    ScrollingMenuItem* m_pCollidedItem;

    // +0xd0: constrained-view mode flag
    //   0 = default visible range [-160, 160]
    //   1 = range [pos.y - m_Height, pos.y]
    uint8_t m_bConstrainedView;
    // +0xd1..0xd3: padding
    uint8_t m_pad_d1[3];

    // +0xd4..0xdc: velocity Vec3
    //   .y component (field_0xd8) = TRUE scroll offset (items positioned relative to this)
    // Binary: field_0xd4..field_0xdc
    Vec3 m_Velocity;

    // +0xe0..0xec: outer touch region (4 floats relative to pos)
    //   [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM
    //   Binary: field100_0xe0, field101_0xe4, field102_0xe8, field103_0xec
    float m_OuterRegion[4];

    // +0xf0..0xfc: inner touch region (4 floats relative to pos)
    //   [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM
    //   Binary: field104_0xf0, field105_0xf4, field106_0xf8, field107_0xfc
    float m_InnerRegion[4];

public:
    // Binary @ 0x0015af3c -- ClearTouch: m_TouchId = -1; m_bDragging = 0.
    void ClearTouch();

    // Binary @ 0x0015af4c -- Collide(long): walk m_Items, call vtable+0x34 (hit-test)
    // on each; return first non-null item, else nullptr. Gated by m_fieldCA != 0.
    // Binary exports both int and long overloads (long is the type-resolver
    // delegate signature); port body forwards long -> int.
    ScrollingMenuItem* Collide(long touchSlot);
    // Binary @ 0x0015af28 -- Init: chains to vtable+0x10 (HUDControl::Init, a no-op).
    void Init() override;
    // Binary @ 0x0015af34 -- PreDraw(float*): empty no-op in binary (return only).
    void PreDraw(float* viewVec);
    // Binary @ 0x0015b034 -- Release: calls DestroyList() to clear+delete all items.
    void Release() override;
    // Binary @ 0x0015aeb8 -- Reset: m_DragTargetIdx=-1, m_TouchId=-1, m_pCollidedItem=0,
    // m_bNoDestructor=1, m_bConstrainedView=0, m_SnapDist=1.0f; zero Vec3 copied into
    // m_Velocity / m_TouchAnchorPos / m_AnchorOffset / m_PendingVelocity.
    // (Binary does NOT clear m_bDragging here.)
    void Reset() override;
    // Binary @ 0x0015af38 -- Skip: empty no-op in binary (return only).
    void Skip() override;
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(ScrollingMenu, m_TotalHeight)    == 0xa8, "ScrollingMenu::m_TotalHeight offset");
static_assert(offsetof(ScrollingMenu, m_TotalWidth)     == 0xac, "ScrollingMenu::m_TotalWidth offset");
static_assert(offsetof(ScrollingMenu, m_Items)          == 0xb0, "ScrollingMenu::m_Items offset");
static_assert(offsetof(ScrollingMenu, m_ClosestIdx)     == 0xbc, "ScrollingMenu::m_ClosestIdx offset");
static_assert(offsetof(ScrollingMenu, m_DragTargetIdx)  == 0xc0, "ScrollingMenu::m_DragTargetIdx offset");
static_assert(offsetof(ScrollingMenu, m_SnapDist)       == 0xc4, "ScrollingMenu::m_SnapDist offset");
static_assert(offsetof(ScrollingMenu, m_bDragging)      == 0xc8, "ScrollingMenu::m_bDragging offset");
static_assert(offsetof(ScrollingMenu, m_bTouchProcessed) == 0xc9, "ScrollingMenu::m_bTouchProcessed offset");
static_assert(offsetof(ScrollingMenu, m_fieldCA)        == 0xca, "ScrollingMenu::m_fieldCA offset");
static_assert(offsetof(ScrollingMenu, m_pCollidedItem)  == 0xcc, "ScrollingMenu::m_pCollidedItem offset");
#endif

#endif // FN_SCROLLING_MENU_H
