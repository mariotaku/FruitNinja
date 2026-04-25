#ifndef FN_SCROLLING_MENU_ITEM_H
#define FN_SCROLLING_MENU_ITEM_H

//
// ScrollingMenuItem -- single row in a ScrollingMenu.
// Binary refs:
//   ctor (5-param) 0x0015b194
//   ctor (0-param) 0x0015b5dc
//   dtor           0x0015c3ac / 0x0015c3e8
//
// Vtable (from binary @ 0x001e9f00):
//   slot  0 (+0x00)  ~ScrollingMenuItem dtor1  0x0015c3ac
//   slot  1 (+0x04)  ~ScrollingMenuItem dtor2  0x0015c3e8
//   slot  2 (+0x08)  GetHeight                 0x0013cdf0
//   slot  3 (+0x0C)  GetWidth                  0x0013cdf8
//   slot  4 (+0x10)  SetHeight                 0x0013ce00
//   slot  5 (+0x14)  SetWidth                  0x0013ce08
//   slot  6 (+0x18)  Move(_Vector3)            0x0015aea8
//   slot  7 (+0x1C)  Remove                    0x0013d14c
//   slot  8 (+0x20)  SetParent(ScrollingMenu*) 0x0015aeb4
//   slot  9 (+0x24)  SetOnscreen(bool)         0x0013ce10
//   slot 10 (+0x28)  SetText(char*)            0x0015b124
//   slot 11 (+0x2C)  Draw()                    0x0015b480  <-- ScrollingMenu::Draw dispatches here
//   slot 12 (+0x30)  ?                         0x00147970
//   slot 13 (+0x34)  ?                         0x00147974
//   slot 14 (+0x38)  ?                         0x00147978
//
// Struct layout (from ctor 0x0015b228):
//   +0x00  vtable*
//   +0x04  float    pos.x  (set by Move)
//   +0x08  float    pos.y  (set by Move)
//   +0x0C  float    pos.z  (set by Move)
//   +0x10  ScrollingMenu*  m_pParent
//   +0x14  Colour   m_Colour  (4 bytes RGBA, from MakeColourFromGlobal_ScrollMenu)
//   +0x18  float    m_Width   (from GOT-cached Vec3[0])
//   +0x1C  float    m_Height  (from GOT-cached Vec3[1])
//   +0x20  float    m_Depth   (from GOT-cached Vec3[2])
//   +0x24  float    m_ParamWidth   (width ctor param)
//   +0x28  float    m_ParamHeight  (height ctor param)
//   +0x2C  Delegate1  m_Callback  (40 bytes, Delegate1<void,ScrollingMenuItem*>)
//   +0x54  char*    m_pText   (label string ptr; SetText stores here)
//   +0x58  (unknown)
//   +0x5C  char[..] m_DescText  (inline description text buffer, ShopListItem::Draw reads in_r0+0x5c)
//
// Port status: vtable fully matched; Draw() and scrolling physics not yet ported.
//
// Analysed: 2026-04-25T20:30
//

#include <functional>

class ScrollingMenu;

class ScrollingMenuItem {
public:
    // Matches ScrollingMenuItem::ScrollingMenuItem() @ 0x0015b5dc
    ScrollingMenuItem();
    virtual ~ScrollingMenuItem();

    // vtable +0x08 (slot 2): GetHeight -- returns m_Height
    virtual float GetHeight() const { return m_Height; }

    // vtable +0x0C (slot 3): GetWidth -- returns m_Width
    virtual float GetWidth() const { return m_Width; }

    // vtable +0x10 (slot 4): SetHeight
    virtual void SetHeight(float h) { m_Height = h; }

    // vtable +0x14 (slot 5): SetWidth
    virtual void SetWidth(float w) { m_Width = w; }

    // vtable +0x18 (slot 6): Move(_Vector3) -- updates item world position
    // Binary: sets pos.x/y/z from incoming Vec3 argument.
    // ShopListItem overrides this at 0x0015d9fc.
    virtual void Move(float x, float y, float z) { pos.x = x; pos.y = y; pos.z = z; }

    // vtable +0x1C (slot 7): Remove
    virtual void Remove() {}

    // vtable +0x20 (slot 8): SetParent(ScrollingMenu*)
    virtual void SetParent(ScrollingMenu* parent) { m_pParent = parent; }

    // vtable +0x24 (slot 9): SetOnscreen(bool)
    virtual void SetOnscreen(bool onscreen) { m_bOnscreen = onscreen; }

    // vtable +0x28 (slot 10): SetText(char*)
    // Binary: 0x0015b124 stores the pointer at +0x54.
    virtual void SetText(const char* text) { m_pText = text; }

    // vtable +0x2C (slot 11): Draw() -- renders this item
    // ScrollingMenu::Draw dispatches here via vtable[+0x2C].
    // ShopListItem overrides at 0x0015eb00.
    virtual void Draw() {}

    // vtable +0x30..+0x38 (slots 12-14): unknown
    virtual void Slot12() {}
    virtual void Slot13() {}
    virtual void Slot14() {}

    // CallClickedMenuItemCallback -- fires m_Callback
    void CallClickedMenuItemCallback();

    // --- Position (set by Move, read by Draw) ---
    // +0x04
    struct { float x, y, z; } pos;

    // +0x14: display colour
    unsigned int m_Colour;   // Colour RGBA (4 bytes)

    // +0x18..+0x20: item bounding dimensions (from GOT-cached Vec3)
    float m_Width;
    float m_Height;
    float m_Depth;

    // +0x24..+0x28: ctor params
    float m_ParamWidth;
    float m_ParamHeight;

    // +0x2C..+0x53: click callback (Delegate1<void,ScrollingMenuItem*> in binary; port: std::function)
    std::function<void(ScrollingMenuItem*)> m_Callback;

    // +0x54: display text pointer
    const char* m_pText;

    // +0x58: unknown field, init 0
    int m_field58;

    // +0x5C: inline description text buffer (ShopListItem::Draw reads this as in_r0+0x5c)
    // Size unknown; a 128-byte buffer is safe for the port.
    char m_DescText[128];

protected:
    ScrollingMenu* m_pParent;
    bool m_bOnscreen;
};

#endif // FN_SCROLLING_MENU_ITEM_H
