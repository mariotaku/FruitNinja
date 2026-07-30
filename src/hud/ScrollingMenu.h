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
//  field_0xca         | +0xca         | m_bCollideEnabled
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

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Runs Phase 4 (velocity integrate), Phase 5 (layout), Phase 7 (spring-back)
    // dt-scaled, once per PRESENTED frame (Game::tickRealtimeUi via
    // HUD::UpdateRealtime), so shop-list scroll motion tracks the display's
    // actual present rate (60/90/120fps) instead of the fixed 60Hz sim tick.
    // Phases 1/2/3 (touch read) and Phase 6 (click-fire) stay in Update() at
    // 60Hz -- Phase 6 fires a one-shot click SFX on settle and would re-fire
    // up to 120x/s if run per-present. See ScrollingMenu.cpp for the phase
    // split and the SM_DECAY_F/SM_SPRING_F macros shared with the __bada__ path.
    // Phase 5 also calls item->AdvanceAnim(dtSeconds) once per present here
    // (NOT from Update()'s Phase 5) -- see ScrollingMenuItem::AdvanceAnim.
    //
    // INVARIANT (fixed momentum-runaway regression): Phase 4's velocity
    // integrate must never run on a present where the Phase-3B drag-delta
    // recompute (also in this function, immediately above Phase 4) did NOT
    // run. The binary's single-pass Update @0x001b03b4 pairs recompute+
    // integrate for free (same function, same frame); this split-cadence
    // port must re-derive that pairing explicitly (a local bool tracks
    // whether the recompute executed this call) so a stationary tap always
    // converges m_PendingVelocity -> ~0 instead of integrating a stale,
    // pre-tap-settle impulse into m_Velocity. Does not affect the
    // m_TouchId==-1 free-fling coast path, which must integrate every present
    // regardless (no recompute applies there -- that's the intended decay).
    void UpdateRealtime(float dtSeconds) override;
#endif

    // ScrollingMenu::Draw @ 0x0015af98
    // Pure iterator: calls vtable+0x2C (Draw) on each item in m_Items.
    // No scissor/clip, no per-frame positioning (that is done by Update).
    void Draw(float* hudScaleRaw) override;

    // ScrollingMenu::AddItem @ 0x0015be54
    // Appends item to m_Items, updates width/height accumulators,
    // calls item->SetParent(this).
    ScrollingMenuItem* AddItem(ScrollingMenuItem* item);

    // ScrollingMenu::RemoveItemImmediate @ 0x001af83c (PLT 0x0010e85c)
    // Immediately destroys the item at `index`; if it was the focused last row,
    // shifts m_ClosestIdx/m_Velocity.y up one row and kicks m_PendingVelocity.y=0.1f.
    // Recomputes m_TotalHeight over the remaining items. erase=true also removes
    // the vector slot; the RemoveAnimate completion path passes erase=false.
    void RemoveItemImmediate(int index, bool erase);

    // Returns count of items in m_Items.
    int GetNumItems();

    // v1.6.1 ScrollingMenu::GetItemClosestToZeroIdx @ 0x00191250
    // Returns m_ClosestIdx (field76_0xbc), closest-to-zero item index.
    // ShopScreen calls this to track selection changes.
    int GetItemClosestToZeroIdx();

    // v1.6.1 ScrollingMenu::GetItemClosestToZero @ 0x001912c0
    // Returns pointer to the item at m_ClosestIdx, or nullptr (see the DIFFERS
    // note on the definition: the binary indexes unguarded).
    ScrollingMenuItem* GetItemClosestToZero();

    // DestroyList — clears and deletes all items.
    void DestroyList();

    // ScrollingMenu::Collide(slot) @ 0x0015af4c
    // Walks m_Items calling vtable+0x38 (Slot13/hit-test, v1.6.1 slot 14) on each.
    // Returns the first item that returns non-null, or nullptr.
    // Only active when m_bCollideEnabled != 0.
    ScrollingMenuItem* Collide(int touchSlot);

    // Inline accessors — binary @ 0x00191248..0x001479dc
    int   GetType() override { return 8; }            // 0x00191248
    float GetHeight() const        { return m_Height; }            // 0x00147988 reads +0xa0
    float GetWidth()  const        { return m_ItemHeight; }        // 0x00147990 reads +0xa4 (port name swap)
    float GetItemHeight() const    { return m_Width; }             // 0x001479dc reads +0x9c (port name swap)

    // Width/height setters (vtable slots byte-proven 2026-06-06):
    //   SetHeight    vtable+0x4c -> +0xa0 (m_Height)   -- scroll-boundary clamp field
    //   SetWidth     vtable+0x50 -> +0xa4 (m_ItemHeight) + 4 derived region fields
    //   SetItemHeight vtable+0x54 -> +0x9c (m_Width)   -- item row height
    // Port field names (m_Width/m_Height/m_ItemHeight) are name-swapped vs binary semantics;
    // preserved to avoid mangled-symbol drift on the getter/setter method names.
    // ASM-verified: 2026-06-06 v1.6.1 binary @ 0x001479a0 (asm-inspector) -- SetWidth=vtable+0x50->+0xa4, SetHeight=vtable+0x4c->+0xa0, SetItemHeight=vtable+0x54->+0x9c. Shop: SetWidth(290)/SetHeight(80)/SetItemHeight(80).
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
    _Vector3<float> m_TouchAnchorPos;

    // +0x84..0x8c: velocity/scroll snapshot at finger-down
    //   .x = m_Velocity.x at anchor   (field_0x84)
    //   .y = m_Velocity.y at anchor   (field_0x88 = scroll offset at press)
    //   .z = m_Velocity.z at anchor   (field_0x8c)
    _Vector3<float> m_AnchorOffset;

    // +0x90..0x98: pending velocity Vec3 (accumulated drag delta; friction-scaled each tick)
    _Vector3<float> m_PendingVelocity;

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
    // +0xca: collision-enabled flag; gates the Collide() walk (ctor inits 1).
    //   Binary @ 0x001afed8 sets this->m_bCollideEnabled = 1; Collide @ 0x0015af4c
    //   early-returns nullptr when 0. Ghidra struct: m_bCollideEnabled.
    uint8_t m_bCollideEnabled;
    // +0xcb: padding (written 0 in ctor list, never read). Ghidra struct: _pad_cb.
    uint8_t m_padCB;

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
    _Vector3<float> m_Velocity;

    // +0xe0..0xec: outer touch region (4 floats relative to pos)
    //   [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM
    //   Binary: field100_0xe0, field101_0xe4, field102_0xe8, field103_0xec
    float m_OuterRegion[4];

    // +0xf0..0xfc: inner touch region (4 floats relative to pos)
    //   [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM
    //   Binary: field104_0xf0, field105_0xf4, field106_0xf8, field107_0xfc
    float m_InnerRegion[4];

#ifndef __bada__
    // Port specific: bridge fields, no binary counterpart. Phase 5 (layout)
    // publishes these each time it runs (Update() at 60Hz AND UpdateRealtime()
    // per-present); Phase 6 (click-fire, stays in 60Hz Update()) and Phase 7
    // (spring-back, moved to UpdateRealtime()) read them. Placed after every
    // offset-asserted field / the last static_assert below so __bada__
    // sizeof(ScrollingMenu) and field layout are completely unaffected --
    // these members do not exist at all under __bada__.
    ScrollingMenuItem* m_pClickTarget;  // Phase-5 dragTargetItem (binary local pSVar4)
    float m_ClosestSnapDelta;          // Phase-5 fVar7 = _Stack_6c.y - (pos.y - m_Velocity.y), SIGNED

    // Port specific: wheel-servo state for ScrollByPixels (no binary
    // counterpart). m_WheelTargetY is the clamped servo target scroll
    // position (same units as m_Velocity.y, the true scroll offset);
    // m_bWheelActive gates UpdateRealtime()'s wheel-servo arm and clears on
    // convergence or a Phase-2 finger press-edge acquire. Like the other
    // bridge members these do not exist at all under __bada__, so the
    // faithful layout/sizeof is unaffected.
    float   m_WheelTargetY;
    uint8_t m_bWheelActive;
#endif

public:
    // Binary @ 0x001af6a8 -- ClearTouch: m_TouchId = -1; m_bDragging = 0.
    void ClearTouch();

    // Binary @ 0x0015af4c -- Collide(long): walk m_Items, call vtable+0x34 (hit-test)
    // on each; return first non-null item, else nullptr. Gated by m_bCollideEnabled != 0.
    // Binary exports both int and long overloads (long is the type-resolver
    // delegate signature); port body forwards long -> int.
    ScrollingMenuItem* Collide(long touchSlot);
    // Binary @ 0x0015af28 -- Init: chains to vtable+0x10 (HUDControl::Init, a no-op).
    void Init() override;
    // Binary @ 0x0015af34 -- PreDraw(float*): empty no-op in binary (return only).
    void PreDraw(float* viewVec) override;
    // Binary @ 0x0015b034 -- Release: calls DestroyList() to clear+delete all items.
    void Release() override;
    // Binary @ 0x0015aeb8 -- Reset: m_DragTargetIdx=-1, m_TouchId=-1, m_pCollidedItem=0,
    // m_bNoDestructor=1, m_bConstrainedView=0, m_SnapDist=1.0f; zero Vec3 copied into
    // m_Velocity / m_TouchAnchorPos / m_AnchorOffset / m_PendingVelocity.
    // (Binary does NOT clear m_bDragging here.)
    void Reset() override;
    // Binary @ 0x0015af38 -- Skip: empty no-op in binary (return only).
    void Skip() override;

#ifndef __bada__
    // Port specific: no binary counterpart -- desktop mouse-wheel hover hit-test.
    // Tests an arbitrary game-space point against pos + m_OuterRegion[4]
    // (the SAME rect Update()'s Phase 2 touch-acquire scan uses), rather than
    // a specific touch slot -- mirrors HUDControl::TouchInRegion's rect but
    // for a point supplied directly (mouse position), not a live touch.
    bool ContainsPoint(float gx, float gy) const;

    // Port specific: no binary counterpart -- desktop mouse-wheel scroll.
    // Pins m_DragTargetIdx to (m_ClosestIdx + delta), clamped to
    // [0, GetNumItems()-1). Does NOT touch m_Velocity.y directly (that field
    // is the live scroll POSITION, not a physics velocity -- writing it
    // teleports the list). Phase 5's `m_DragTargetIdx == i` arm and Phase 7's
    // snap-spring (already running every Update/UpdateRealtime call) then
    // animate the position to the target item over several frames and settle,
    // exactly like a drag-release snap. delta>0 scrolls toward later items,
    // delta<0 toward earlier items. m_DragTargetIdx is cleared back to -1 on
    // the next touch press-edge acquire (Phase 2), so a subsequent drag is
    // unaffected by a prior wheel scroll. See .cpp for the full derivation.
    void ScrollByItems(int delta);

    // Port specific: no binary counterpart -- high-precision (fractional)
    // wheel/trackpad scroll for desktop and web. `dy` is in scroll-position
    // units (same units as m_Velocity.y, the true scroll offset); dy > 0
    // scrolls toward the top of the list (earlier items -- m_Velocity.y's
    // valid range is [m_Height - m_TotalHeight, 0], top = 0). Accumulates
    // into a range-clamped servo target (m_WheelTargetY); UpdateRealtime()
    // then drives m_PendingVelocity.y with the SAME drag-delta servo formula
    // a live finger-drag uses ((position - target) * DRAG_DELTA_FACTOR), so
    // friction (Phase 4), layout (Phase 5) and row-snap settle (Phase 7) all
    // come from the existing physics untouched, frame-rate consistent.
    // Clears any stale m_DragTargetIdx notch pin (Phase 5's closest arm and
    // Phase 7's bounds springs both require m_DragTargetIdx < 0). Ignored
    // while a finger is actively dragging (m_TouchId != -1); a new finger
    // press-edge (Phase 2) cancels the servo so the drag takes over cleanly.
    // Caller maps wheel deltas to units (GameSDL: one notch ~= one row
    // height, derived from GetItemClosestToZero()->GetHeight()).
    void ScrollByPixels(float dy);
#endif
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
static_assert(offsetof(ScrollingMenu, m_bCollideEnabled) == 0xca, "ScrollingMenu::m_bCollideEnabled offset");
static_assert(offsetof(ScrollingMenu, m_pCollidedItem)  == 0xcc, "ScrollingMenu::m_pCollidedItem offset");
#endif

// v1.6.1 DefaultClickedMenuItemCallback @0x1af5f4
// Default Mortar::Delegate1<ScrollingMenuItem*,ScrollingMenuItem*> item-click callback.
// Identity: returns the item unchanged (used as the default no-click handler).
ScrollingMenuItem* DefaultClickedMenuItemCallback(ScrollingMenuItem* item);

#endif // FN_SCROLLING_MENU_H
