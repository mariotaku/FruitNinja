#ifndef FN_SCROLLING_MENU_ITEM_H
#define FN_SCROLLING_MENU_ITEM_H

//
// ScrollingMenuItem — single row in a ScrollingMenu.
// Binary refs:
//   ctor (5-param) 0x0015b194
//   ctor (0-param) 0x0015b5dc
//   dtor           0x0015c3ac / 0x0015c3e8
//
// Struct size: ~0x58 (from ctor analysis + ShopListItem offset 0x25c extends past it)
//
// Layout (0-param ctor 0x0015b5dc):
//   +0x00  vtable ptr
//   +0x04  int         field_0x04 (unknown, from 5-param ctor)
//   +0x08-0x10        unknown padding
//   +0x14  Colour      m_Colour       (4 bytes)
//   +0x18  float       m_FontScaleX
//   +0x1c  float       m_FontScaleY
//   +0x20  float       m_FontScaleZ
//   +0x24  float       m_Width        (default 25.0 from ctor = 0x41c80000)
//   +0x28  float       m_Height       (from DAT)
//   +0x2c-0x30        char* m_pText
//   +0x30  Delegate1<void,ScrollingMenuItem*>  m_Callback  (size ~0x24)
//   +0x54  int         field_0x54     (init 0)
//
// Port status: STUB — scrolling list is not yet rendered. Public API stubs
// only. Full implementation requires Draw pipeline (ScrollingMenu::Draw calls
// each item's vtable+0x1c Draw).
//
// Analysed: 2026-04-25T14:00
//

#include <functional>

class ScrollingMenu;

class ScrollingMenuItem {
public:
    // Matches ScrollingMenuItem::ScrollingMenuItem() @ 0x0015b5dc
    ScrollingMenuItem();
    virtual ~ScrollingMenuItem();

    // vtable +0x08: GetHeight — returns m_Height
    virtual float GetHeight() const { return m_Height; }

    // vtable +0x0c: GetWidth — returns m_Width
    virtual float GetWidth() const { return m_Width; }

    // vtable +0x10: (unknown — appears in AddItem as vtable+0x10)
    virtual float GetWidth2() const { return m_Width; }

    // vtable +0x18: Move(Vec3) — updates item position
    virtual void Move(float x, float y, float z) { (void)x; (void)y; (void)z; }

    // vtable +0x20: SetParent(ScrollingMenu*)
    virtual void SetParent(ScrollingMenu* parent) { m_pParent = parent; }

    // vtable +0x24: SetOnscreen(bool)
    virtual void SetOnscreen(bool onscreen) { m_bOnscreen = onscreen; }

    // vtable +0x28: (Draw — renders item, called by ScrollingMenu::Update)
    virtual void Draw() {}

    // vtable +0x30: CollideWithButton(long) — returns 1 if collides
    virtual int CollideWithButton(long /*touchId*/) { return 0; }

    // vtable +0x34: (CancelCollision)
    virtual void CancelCollision() {}

    // vtable +0x38: Poke
    virtual void Poke() {}

    // CallClickedMenuItemCallback — fires m_Callback
    void CallClickedMenuItemCallback();

    // SetText(const char*)
    void SetText(const char* text) { m_pText = text; }

    // +0x14: display colour
    unsigned int m_Colour;     // Colour RGBA (4 bytes)

    // +0x18..+0x20: font scale
    float m_FontScaleX;
    float m_FontScaleY;
    float m_FontScaleZ;

    // +0x24: item width (default 25.0)
    float m_Width;

    // +0x28: item height (from global)
    float m_Height;

    // +0x2c: display text
    const char* m_pText;

    // +0x30: click callback (Delegate1<void,ScrollingMenuItem*> in binary)
    // Port: std::function
    std::function<void(ScrollingMenuItem*)> m_Callback;

    // +0x54: unknown field, init 0
    int m_field54;

protected:
    ScrollingMenu* m_pParent;
    bool m_bOnscreen;
};

#endif // FN_SCROLLING_MENU_ITEM_H
