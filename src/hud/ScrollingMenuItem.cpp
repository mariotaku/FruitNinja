// Analysed: 2026-04-25T14:00
//
// ScrollingMenuItem stub implementation.
// Binary: ctor 0x0015b5dc, dtor 0x0015c3ac.
// Full rendering not yet ported.

#include "ScrollingMenuItem.h"

// Default font scale and width from binary global DAT values (ctor 0x0015b5dc).
// DAT_0015b668 = 0x41c80000 = 25.0 (m_Width / m_Height default)
// Font scale copied from a global Vec3 (exact value not resolved here).
static const float SCROLL_ITEM_DEFAULT_SIZE = 25.0f;

ScrollingMenuItem::ScrollingMenuItem()
    : m_Colour(0xFFFFFFFF)
    , m_FontScaleX(1.0f)
    , m_FontScaleY(1.0f)
    , m_FontScaleZ(1.0f)
    , m_Width(SCROLL_ITEM_DEFAULT_SIZE)
    , m_Height(SCROLL_ITEM_DEFAULT_SIZE)
    , m_pText(nullptr)
    , m_field54(0)
    , m_pParent(nullptr)
    , m_bOnscreen(false)
{}

ScrollingMenuItem::~ScrollingMenuItem() {}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Callback) {
        m_Callback(this);
    }
}
