// Analysed: 2026-04-25T20:30
//
// ScrollingMenuItem stub implementation.
// Binary: ctor 0x0015b5dc, dtor 0x0015c3ac.
// Vtable fully matched per shop.md ScrollingMenuItem Class Layout section.
// Full rendering pipeline ported in ShopListItem::Draw.

#include "ScrollingMenuItem.h"
#include <cstring>

// Default item size from binary global DAT values (ctor 0x0015b5dc).
// DAT_0015b668 = 0x41c80000 = 25.0f (m_Width / m_Height default)
static const float SCROLL_ITEM_DEFAULT_SIZE = 25.0f;

ScrollingMenuItem::ScrollingMenuItem()
    : m_Colour(0xFFFFFFFF)
    , m_Width(SCROLL_ITEM_DEFAULT_SIZE)
    , m_Height(SCROLL_ITEM_DEFAULT_SIZE)
    , m_Depth(0.0f)
    , m_ParamWidth(SCROLL_ITEM_DEFAULT_SIZE)
    , m_ParamHeight(SCROLL_ITEM_DEFAULT_SIZE)
    , m_pText(nullptr)
    , m_field58(0)
    , m_pParent(nullptr)
    , m_bOnscreen(false)
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
    memset(m_DescText, 0, sizeof(m_DescText));
}

ScrollingMenuItem::~ScrollingMenuItem() {}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Callback) {
        m_Callback(this);
    }
}
