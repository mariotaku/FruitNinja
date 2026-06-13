//
// ScrollingMenuItemRemoveAnimate : ScrollingMenuItem (size = 0x5c)
// Binary ctor @ 0x1b1738
// Binary Update @ 0x1af96c
//

#include "ScrollingMenuItemRemoveAnimate.h"
#include "ScrollingMenu.h"

// ctor @ 0x1b1738
// ScrollingMenuItem::ScrollingMenuItem(&base)  -- 0x10ba1c (PLT)
// this->+0x58 = index
// this->vptr  = 0x2ce060
ScrollingMenuItemRemoveAnimate::ScrollingMenuItemRemoveAnimate(int index)
    : ScrollingMenuItem()
    , m_ItemIndex(index)
{
}

// dtor D0 @ 0x1b1574 / dtor D1 @ 0x1b14e0
// set vptr, ScrollingMenuItem::~ (0x10b690), dealloc (0x108ecc)
ScrollingMenuItemRemoveAnimate::~ScrollingMenuItemRemoveAnimate() {
}

// Update(float dt)  slot 10  Binary @ 0x1af96c
// Remove/shrink animation. dt argument is IGNORED.
// Reads current scale via base vtable[+0x8] (slot 2 = GetHeight),
// multiplies by 0.75, writes back via base vtable[+0x10] (slot 4 = SetHeight).
// Completion threshold: scale <= 0.01f (DAT_1af9d0 = 0x3c23d70a).
// On completion: ScrollingMenu::RemoveItemImmediate(parent, itemIndex, 0) -> return 0.
// While animating: return 1.
int ScrollingMenuItemRemoveAnimate::Update(float dt) {
    (void)dt;

    // Reads current scale via base accessor (slot 2 = GetHeight).
    float cur = GetHeight();
    // Shrink: scale *= 0.75 each Update tick (frame-rate dependent, NOT dt-scaled).
    SetHeight(cur * 0.75f);

    // Re-read scale after write.
    float cur2 = GetHeight();
    if (cur2 <= 0.01f) {
        // Animation complete (scale <= 0.01f, DAT_1af9d0 = 0x3c23d70a).
        // Binary @ 0x1af9b8: r0=m_pParent(+0x10), r1=m_ItemIndex(+0x58), r2=0(erase=false).
        // RemoveItemImmediate with erase=false deletes items[index] (== this),
        // so capture members first and do not touch any member after the call.
        ScrollingMenu* parent = m_pParent;
        int idx = m_ItemIndex;
        parent->RemoveItemImmediate(idx, false);   // ScrollingMenu::RemoveItemImmediate @ 0x1af83c (PLT 0x10e85c)
        return 0;
    }
    return 1;
}
