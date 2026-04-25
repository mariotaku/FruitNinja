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
// Port status: STUB — scrolling and touch handling not yet implemented.
// AddItem, GetNumItems, GetItemClosestToZeroIdx are functional stubs used
// by ShopScreen to query state.
//
// Key fields used by ShopScreen:
//   +0x70  std::vector<ScrollingMenuItem*>  m_Items
//   +0x74  int    m_TouchId            (-1 = none)
//   +0x88  float  field_0x88           (scroll position)
//   +0xbc  int    field_0xbc           (closest-to-zero idx, init 0)
//   +0xc0  int    m_SelectedIdx        (-1 = none; vtable+0x3c returns this)
//   +0xc8  byte   field_0xc8           (drag mode)
//   +0xc9  byte   m_TouchProcessed     (cleared each Update)
//
// Analysed: 2026-04-25T14:00
//

#include "HUDControl.h"
#include <vector>

class ScrollingMenuItem;

class ScrollingMenu : public HUDControl {
public:
    // Matches ScrollingMenu::ScrollingMenu() @ 0x0015b3b0
    ScrollingMenu();
    ~ScrollingMenu() override;

    // HUDControl overrides
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override {}

    // ScrollingMenu::AddItem @ 0x0015be54
    // Appends item to m_Items, updates width/height accumulators,
    // calls item->SetParent(this).
    ScrollingMenuItem* AddItem(ScrollingMenuItem* item);

    // ScrollingMenu::GetNumItems @ (vtable +0x14 = GetWidth of ScrollingMenu)
    // Returns count of items in m_Items.
    // Binary: vtable+0x14 on ScrollingMenu returns iVar1 (item count).
    int GetNumItems() const;

    // ScrollingMenu::GetItemClosestToZeroIdx @ 0x00147980
    // Returns m_SelectedIdx (the item index closest to scroll position 0).
    // ShopScreen calls this via vtable+0x3c to track selection changes.
    int GetItemClosestToZeroIdx() const;

    // ScrollingMenu::GetItemClosestToZero @ 0x001479ec
    // Returns pointer to the item at m_SelectedIdx, or nullptr.
    ScrollingMenuItem* GetItemClosestToZero() const;

    // DestroyList — clears and deletes all items.
    void DestroyList();

    // Width/height of the scrollable area
    void SetWidth(float w)  { m_Width = w; }
    void SetHeight(float h) { m_Height = h; }

    // +0x9c: menu area width
    float m_Width;
    // +0xa0: menu area height
    float m_Height;
    // +0xa4: item row height
    float m_ItemHeight;

    // +0xc0: closest-to-zero item index (-1 = none)
    int m_SelectedIdx;

    // +0xbc: field_0xbc (init 0)
    int m_ClosestIdx;

    // +0xc8: drag mode flag
    uint8_t m_bDragging;
    // +0xc9: touch-processed flag (cleared each Update)
    uint8_t m_bTouchProcessed;
    // +0xca: field_0xca (init 1)
    uint8_t m_fieldCA;

    // +0x88: scroll offset
    float m_ScrollOffset;

    // +0x74: touch id tracking
    int m_TouchId;

private:
    // +0x70: item list
    std::vector<ScrollingMenuItem*> m_Items;

    // Accumulators updated by AddItem
    float m_TotalHeight;   // +0xac
    float m_TotalWidth;    // +0xa8
};

#endif // FN_SCROLLING_MENU_H
