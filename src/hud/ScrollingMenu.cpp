// Analysed: 2026-04-25T14:00
//
// ScrollingMenu stub implementation.
// Binary: ctor 0x0015b3b0, Update 0x0015b747 (377 lines), AddItem 0x0015be54.
// Full touch-based scrolling physics not yet ported — stub is sufficient
// for ShopScreen to compile and run with a static (non-scrolling) list.

#include "ScrollingMenu.h"
#include "ScrollingMenuItem.h"
#include <cstddef>

ScrollingMenu::ScrollingMenu()
    : m_Width(0.0f)
    , m_Height(0.0f)
    , m_ItemHeight(25.0f)
    , m_SelectedIdx(-1)
    , m_ClosestIdx(0)
    , m_bDragging(0)
    , m_bTouchProcessed(0)
    , m_fieldCA(1)
    , m_ScrollOffset(0.0f)
    , m_TouchId(-1)
    , m_TotalHeight(0.0f)
    , m_TotalWidth(0.0f)
{}

ScrollingMenu::~ScrollingMenu() {
    DestroyList();
}

void ScrollingMenu::Update(float /*dt*/) {
    // TODO: port full touch-drag scrolling from 0x0015b747
    m_bTouchProcessed = 0;
}

ScrollingMenuItem* ScrollingMenu::AddItem(ScrollingMenuItem* item) {
    // Matches ScrollingMenu::AddItem @ 0x0015be54:
    //   m_TotalHeight += item->GetHeight()   (via vtable+0x0c)
    //   m_TotalWidth  += item->GetWidth()    (via vtable+0x08)
    //   item->SetParent(this)                (via vtable+0x20)
    //   m_Items.push_back(item)
    if (!item) return nullptr;
    m_TotalHeight += item->GetHeight();
    m_TotalWidth  += item->GetWidth();
    item->SetParent(this);
    m_Items.push_back(item);
    return item;
}

int ScrollingMenu::GetNumItems() const {
    return (int)m_Items.size();
}

int ScrollingMenu::GetItemClosestToZeroIdx() const {
    // Binary: returns m_SelectedIdx (field_0xbc in binary, tracked by Update).
    // Stub: return first item if any, else -1.
    return m_ClosestIdx;
}

ScrollingMenuItem* ScrollingMenu::GetItemClosestToZero() const {
    int idx = GetItemClosestToZeroIdx();
    if (idx < 0 || idx >= (int)m_Items.size()) return nullptr;
    return m_Items[(size_t)idx];
}

void ScrollingMenu::DestroyList() {
    for (ScrollingMenuItem* item : m_Items) {
        delete item;
    }
    m_Items.clear();
    m_TotalHeight = 0.0f;
    m_TotalWidth  = 0.0f;
}
