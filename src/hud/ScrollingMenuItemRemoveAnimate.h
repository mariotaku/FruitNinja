#ifndef FN_HUD_SCROLLINGMENUITEMREMOVEANIMATE_H
#define FN_HUD_SCROLLINGMENUITEMREMOVEANIMATE_H

//
// ScrollingMenuItemRemoveAnimate : ScrollingMenuItem  (size = 0x5c)
// Binary ctor @ 0x1b1738  ScrollingMenuItemRemoveAnimate(int index)
// operator new(0x5c) @ ScrollingMenu::RemoveItem 0x1b01d4
// vtable base 0x2ce058, stored vptr = base+8 (0x2ce060)
//
// Replaces a live ScrollingMenuItem in the menu's items[] vector
// (ScrollingMenu+0xb0) during removal. Shrinks the render scale by
// 0.75 each Update tick until scale <= 0.01f, then calls
// ScrollingMenu::RemoveItemImmediate and signals completion (return 0).
//

#include "ScrollingMenuItem.h"
#include <cstddef>

class ScrollingMenu;

// vtable @ 0x2ce060 (relative offsets from vptr):
//   slot 0  (+0x00)  ~ScrollingMenuItemRemoveAnimate D1 dtor  0x1b14e0
//   slot 1  (+0x04)  ~ScrollingMenuItemRemoveAnimate D0 dtor  0x1b1574
//   slots 2..5       inherited ScrollingMenuItem accessor virtuals (GetHeight/GetWidth/SetHeight/SetWidth)
//   slot 6  (+0x18)  Move         inherited ScrollingMenuItem 0x1af5f8
//   slot 7  (+0x1c)  Remove       inherited ScrollingMenuItem 0x1792ec
//   slot 8  (+0x20)  SetParent    inherited 0x1af608: *(this+0x10) = menu
//   slot 9  (+0x24)  SetOnscreen  inherited
//   slot 10 (+0x28)  Update(float) -- remove/shrink animation  0x1af96c
//   slots 11..16     inherited
//
class ScrollingMenuItemRemoveAnimate : public ScrollingMenuItem {
public:
    // +0x58: item index passed to ScrollingMenu::RemoveItemImmediate on completion
    // ctor arg: ctor @ 0x1b1738 stores it at this+0x58
    int m_ItemIndex;                    // +0x58

    // ctor @ 0x1b1738: ScrollingMenuItem base ctor, store index, set vptr
    explicit ScrollingMenuItemRemoveAnimate(int index);
    virtual ~ScrollingMenuItemRemoveAnimate();

    // slot 10: Update(float) -- remove/shrink animation
    // Binary @ 0x1af96c
    // Reads current scale via base vtable[+0x8] (slot 2 = GetHeight),
    // multiplies by 0.75, writes back via base vtable[+0x10] (slot 4 = SetHeight).
    // When scale <= 0.01f (DAT_1af9d0 = 0x3c23d70a) calls
    //   ScrollingMenu::RemoveItemImmediate(parent@+0x10, itemIndex@+0x58, 0)
    // and returns 0 (animation done).
    // Returns 1 while still animating. dt arg is IGNORED.
    int Update(float dt) override;
};

#ifdef __bada__
#include <cstddef>
static_assert(__builtin_offsetof(ScrollingMenuItemRemoveAnimate, m_ItemIndex) == 0x58,
              "ScrollingMenuItemRemoveAnimate m_ItemIndex offset");
static_assert(sizeof(ScrollingMenuItemRemoveAnimate) == 0x5c,
              "ScrollingMenuItemRemoveAnimate sizeof mismatch");
#endif

#endif // FN_HUD_SCROLLINGMENUITEMREMOVEANIMATE_H
