// Analysed: 2026-04-25T23:30
//
// ScrollingMenuItem stub implementation.
// Binary: ctor 0x0015b5dc, dtor 0x0015c3ac.
// Vtable fully matched per shop.md ScrollingMenuItem Class Layout section.
// Full rendering pipeline ported in ShopListItem::Draw.

#include "ScrollingMenuItem.h"
#include <cstring>

// Default item size from binary global DAT values (ctor 0x0015b5dc).
// DAT_0015b668 = 0.0f (m_Width default). m_Height default = 25.0f (ctor literal).
// m_Size is loaded from a global Vec3 ptr (zeroed at load time = all 0.0f).

ScrollingMenuItem::ScrollingMenuItem()
    : m_pParent(nullptr)
    , m_Colour(0xFFFFFFFF)
    , m_Size{0.0f, 0.0f, 0.0f}
    , m_Height(25.0f)
    , m_Width(0.0f)
    // m_Delegate: struct default-ctor initialises _delegate_fn (std::function); header bytes zeroed below.
    , m_pText(nullptr)
    , m_field58(nullptr)
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
    // Zero the Delegate1 header bytes (binary: Delegate1 ctor zeroes its first 4 bytes).
    // m_Delegate._delegate_fn is already default-constructed (empty std::function).
    m_Delegate._cb_hdr0        = 0;
    m_Delegate.m_bOnscreen     = 0;
    m_Delegate._cb_hdr1[0]     = 0;
    m_Delegate._cb_hdr1[1]     = 0;
    m_Delegate._cb_tail[0]     = 0;
    m_Delegate._cb_tail[1]     = 0;
    m_Delegate._cb_tail[2]     = 0;
    m_Delegate._cb_tail[3]     = 0;
    memset(m_DescText, 0, sizeof(m_DescText));
}

ScrollingMenuItem::~ScrollingMenuItem() {
    // m_Delegate._delegate_fn is destroyed automatically via struct dtor.
}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Delegate._delegate_fn) {
        m_Delegate._delegate_fn(this);
    }
}
