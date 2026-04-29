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

// ASM-verified: 2026-04-29T00:00Z binary @ 0x0015b5dc (asm-inspector)
ScrollingMenuItem::ScrollingMenuItem()
    : m_pParent(nullptr)
    , m_Colour(0xFFFFFFFF)
    , m_Size{0.0f, 0.0f, 0.0f}
    , m_Height(25.0f)
    , m_Width(0.0f)
    , _pre_del0(0)
    , m_bOnscreen(0)
    , _pre_del1(0)
    , _pre_del2(0)
    // m_Delegate: struct default-ctor initialises _delegate_fn (std::function); header bytes zeroed below.
    , m_pText(nullptr)
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
    // Zero the Delegate1 header bytes (binary: Delegate1 ctor zeroes its first 4 bytes).
    // m_Delegate._delegate_fn is already default-constructed (empty std::function).
    m_Delegate._hdr[0] = 0;
    m_Delegate._hdr[1] = 0;
    m_Delegate._hdr[2] = 0;
    m_Delegate._hdr[3] = 0;
}

ScrollingMenuItem::~ScrollingMenuItem() {
    // m_Delegate._delegate_fn is destroyed automatically via struct dtor.
}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Delegate._delegate_fn) {
        m_Delegate._delegate_fn(this);
    }
}

