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
    // m_Delegate default-ctor: empty Mortar::Delegate1.
    , m_pText(nullptr)
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
}

ScrollingMenuItem::~ScrollingMenuItem() {
    // m_Delegate destroyed automatically.
}

void ScrollingMenuItem::Move(float x, float y, float z) {
    pos.x = x; pos.y = y; pos.z = z;
}

void ScrollingMenuItem::SetParent(ScrollingMenu* parent) {
    m_pParent = parent;
}

void ScrollingMenuItem::SetText(const char* text) {
    m_pText = text;
}

void ScrollingMenuItem::Draw() {
    // TODO: 0x0015b480 -- render item text and highlight
}

// TODO: 0x0015b228 -- 4-param ctor: load m_Size Vec3 from global default-size ptr
// (DAT_0015b2b8) instead of zeroing; width->m_Height, height->m_Width per binary;
// run MakeColourFromGlobal_ScrollMenu on m_Colour. Port currently uses fixed defaults.
ScrollingMenuItem::ScrollingMenuItem(float, float, const char* text, Mortar::Delegate1<void, ScrollingMenuItem*> delegate)
    : m_pParent(nullptr)
    , m_Colour(0xFFFFFFFF)
    , m_Size{0.0f, 0.0f, 0.0f}
    , m_Height(25.0f)
    , m_Width(0.0f)
    , _pre_del0(0)
    , m_bOnscreen(0)
    , _pre_del1(0)
    , _pre_del2(0)
    , m_Delegate(delegate)
    , m_pText(text)
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Delegate) {
        m_Delegate(this);
    }
}

