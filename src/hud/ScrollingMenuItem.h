#ifndef FN_SCROLLING_MENU_ITEM_H
#define FN_SCROLLING_MENU_ITEM_H

//
// ScrollingMenuItem -- single row in a ScrollingMenu.
// Binary refs:
//   ctor (5-param) 0x0015b228
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
// Struct layout (from ctor 0x0015b228, SetParent 0x0015aeb4, SetOnscreen 0x0013ce10):
//   +0x00  vtable*               (4 bytes on ARM32)
//   +0x04  float    pos.x        (set by Move)
//   +0x08  float    pos.y
//   +0x0C  float    pos.z
//   +0x10  ScrollingMenu* m_pParent   SetParent 0x0015aeb4: *(this+0x10) = param
//   +0x14  Colour   m_Colour     (4 bytes RGBA)
//   +0x18  float    m_Size.x      (NO binary symbol name; all ctors load this as
//   +0x1C  float    m_Size.y       Vec3[0..2] from a global default-size pointer.
//   +0x20  float    m_Size.z       FriendLeaderboardItem ctor @ 0x0013d210 writes
//                                  these explicitly as a scaled Vec3. ShopListItem::Move
//                                  @ 0x0015d1fc reads m_Size.x as an X sub-icon offset
//                                  (adds 35.2f to position a thumbnail). No subclass
//                                  uses m_Size.y or m_Size.z directly in observed code.)
//   +0x24  float    m_Height      (BINARY NAME confirmed via demangled symbol.
//                                  GetHeight()/SetHeight() target this. ROW PITCH for
//                                  ScrollingMenu::Update layout. Default 25.0f. ShopListItem
//                                  overrides to 80.0f. FriendLeaderboardItem sets 47.0f.)
//   +0x28  float    m_Width       (BINARY NAME confirmed via demangled symbol.
//                                  GetWidth()/SetWidth() target this. Default 0.0f.
//                                  ShopListItem sets 290.0f = divider span width.)
//   +0x2C  byte     _pre_del0    (padding; not part of Mortar::Delegate1)
//   +0x2D  byte     m_bOnscreen  SetOnscreen 0x0013ce10: this[0x2D] = param_bool
//   +0x2E  byte     _pre_del1    (padding)
//   +0x2F  byte     _pre_del2    (padding)
//   +0x30  Mortar::Delegate1  m_Delegate  (36 bytes on ARM32; Mortar::Delegate1<void,ScrollingMenuItem*>)
//          Binary ctor @ 0x0015b604: r6 = this+0x30; Mortar::Delegate1::Delegate1(r6) called.
//          Spans +0x30..+0x53 (36 bytes = 0x24). 4-byte header + 32-byte callable.
//   +0x54  char*    m_pText       SetText 0x0015b124: *(this+0x54)
//   +0x58  (end of ScrollingMenuItem on ARM32; total size 88 bytes)
//          m_field58 (+0x58) and m_DescText (+0x5C) are ShopListItem-own fields.
//
// Port status: vtable fully matched; Draw() and scrolling physics not yet ported.
//
// Analysed: 2026-04-25T23:30
//

#include "util/Delegate.h"
#include <cassert>
#include <cstdint>
#include <cstddef>

class ScrollingMenu;

// ASM-verified: 2026-04-29T00:00Z binary @ 0x0015b5dc + 0x001e9f00 (asm-inspector)
class ScrollingMenuItem {
public:
    // Matches ScrollingMenuItem::ScrollingMenuItem() @ 0x0015b5dc
    ScrollingMenuItem();
    virtual ~ScrollingMenuItem();

    // vtable +0x08 (slot 2): GetHeight
    // Binary 0x0013cdf0: returns *(this + 0x24) = m_Height (binary name, ROW PITCH for
    // layout). ShopListItem sets 80.0f; FriendLeaderboardItem sets 47.0f; default 25.0f.
    virtual float GetHeight() const { return m_Height; }

    // vtable +0x0C (slot 3): GetWidth
    // Binary 0x0013cdf8: returns *(this + 0x28) = m_Width (binary name).
    // NOT m_Size.x (+0x18) — those are separate display-size fields.
    virtual float GetWidth() const { return m_Width; }

    // vtable +0x10 (slot 4): SetHeight
    // Binary 0x0013ce00: writes to *(this + 0x24) = m_Height.
    virtual void SetHeight(float h) { m_Height = h; }

    // vtable +0x14 (slot 5): SetWidth
    // Binary 0x0013ce08: writes to *(this + 0x28) = m_Width.
    virtual void SetWidth(float w) { m_Width = w; }

    // vtable +0x18 (slot 6): Move(_Vector3) -- updates item world position
    // Binary: sets pos.x/y/z from incoming Vec3 argument.
    // ShopListItem overrides this at 0x0015d1fc.
    virtual void Move(float x, float y, float z);

    // vtable +0x1C (slot 7): Remove
    virtual void Remove() {}

    // vtable +0x20 (slot 8): SetParent(ScrollingMenu*)
    // Binary 0x0015aeb4: *(this+0x10) = param
    virtual void SetParent(ScrollingMenu* parent);

    // vtable +0x24 (slot 9): SetOnscreen(bool)
    // Binary 0x0013ce10: this[0x2D] = param  (byte at +0x2D, pre-Mortar::Delegate1 gap)
    virtual void SetOnscreen(bool onscreen) { m_bOnscreen = (uint8_t)onscreen; }

    // vtable +0x28 (slot 10): SetText(char*)
    // Binary: 0x0015b124 stores the pointer at +0x54.
    virtual void SetText(const char* text);

    // vtable +0x2C (slot 11): Draw() -- renders this item
    // ScrollingMenu::Draw dispatches here via vtable[+0x2C].
    // ShopListItem overrides at 0x0015eb00.
    virtual void Draw();

    // vtable +0x30 (slot 12): cancel-tap signal
    // Called when drag exceeds 5 units; clears pending tap highlight on the item.
    // Binary: 0x00147970 (base no-op)
    virtual void Slot12() {}

    // vtable +0x34 (slot 13): hit-test query (Collide)
    // Binary: 0x00147974. Called by ScrollingMenu::Collide for each item.
    // Returns non-null (self) if the given touch slot is over this item, null otherwise.
    // Base ScrollingMenuItem returns nullptr (no hit-test geometry in base class).
    // ShopListItem may override if hit-test is needed.
    virtual ScrollingMenuItem* Slot13(int /*touchSlot*/) { return nullptr; }

    // vtable +0x38 (slot 14): touch-release signal
    // Called when the tracked finger leaves the inner scroll region.
    // Binary: 0x00147978 (base no-op)
    virtual void Slot14() {}

    // 4-param ctor: float width, float height, char const*, Mortar::Delegate1<...>
    // Binary @ 0x0015b228: param1(width)->m_Height, param2(height)->m_Width,
    // m_Size from global default Vec3, m_Colour from global white singleton.
    ScrollingMenuItem(float, float, const char*, Mortar::Delegate1<void, ScrollingMenuItem*>);

    // CallClickedMenuItemCallback -- fires m_Callback
    void CallClickedMenuItemCallback();

    // --- Position (set by Move, read by Draw) ---
    // +0x04..+0x0F
    struct { float x, y, z; } pos;   // +0x04

    // +0x10: parent menu pointer
    // Binary offset confirmed: SetParent 0x0015aeb4 writes to *(this+0x10).
    ScrollingMenu* m_pParent;         // +0x10

    // +0x14: display colour
    unsigned int m_Colour;            // +0x14  (Colour RGBA 4 bytes)

    // +0x18..+0x20: display size Vec3 (no binary symbol name).
    // All ctors load these three floats from a global default-size Vec3 pointer.
    // FriendLeaderboardItem ctor (0x0013d210) writes them as a scaled Vec3 explicitly.
    // ShopListItem::Move (0x0015d1fc) reads m_Size.x as an X sub-icon position offset
    // (adds 35.2f to compute thumbnail position). m_Size.y and m_Size.z are set by
    // ctors but not observed to be read back in any compiled function.
    // ShopListItem ctor sets these indirectly via the base ctor (global default = 0,0,0).
    struct { float x, y, z; } m_Size; // +0x18..+0x23 (12 bytes)

    // +0x24: m_Height — BINARY NAME (demangled symbol confirmed in Ghidra struct).
    //        GetHeight()/SetHeight() target this field. ROW PITCH for
    //        ScrollingMenu::Update layout. Default 25.0f (ctor literal).
    //        ShopListItem sets 80.0f; FriendLeaderboardItem sets 47.0f.
    float m_Height;                   // +0x24

    // +0x28: m_Width — BINARY NAME (demangled symbol confirmed in Ghidra struct).
    //        GetWidth()/SetWidth() target this field. Default 0.0f.
    //        ShopListItem sets 290.0f (divider span width).
    float m_Width;                    // +0x28

    // +0x2C..+0x2F: 4-byte pre-Mortar::Delegate1 region.
    // Binary ASM: r6 = this+0x30 before Mortar::Delegate1::Delegate1 call (ctor 0x0015b5dc).
    // SetOnscreen 0x0013ce10 writes to this[0x2D] => byte at +0x2D = m_bOnscreen.
    // The 4 bytes +0x2C..+0x2F are NOT part of the Mortar::Delegate1 object.
    //   +0x2C  byte  _pre_del0   (padding byte before m_bOnscreen)
    //   +0x2D  byte  m_bOnscreen (SetOnscreen target)
    //   +0x2E  byte  _pre_del1
    //   +0x2F  byte  _pre_del2
    uint8_t _pre_del0;    // +0x2C
    uint8_t m_bOnscreen;  // +0x2D  SetOnscreen 0x0013ce10 writes here
    uint8_t _pre_del1;    // +0x2E
    uint8_t _pre_del2;    // +0x2F

    // +0x30..+0x53: Mortar::Delegate1<void,ScrollingMenuItem*> (36 bytes in binary).
    // Binary: Mortar::Delegate1::Delegate1 called with this+0x30 (r6 at 0x0015b604).
    // Port uses Mortar::Delegate1 directly -- same 36-byte ABI.
    Mortar::Delegate1<void, ScrollingMenuItem*> m_Delegate;

    // +0x54: display text pointer
    // Binary: SetText 0x0015b124 stores at *(this+0x54).
    const char* m_pText;              // +0x54

    // Binary ScrollingMenuItem ends here at +0x58 (total size 88 bytes on ARM32).
    // m_field58 (+0x58) and m_DescText (+0x5C) are ShopListItem fields, not base fields.
};

// Convenience accessor that matches binary SetOnscreen semantics.
// SetOnscreen 0x0013ce10 writes byte at +0x2D (pre-Mortar::Delegate1 gap, m_bOnscreen field).
inline void ScrollingMenuItem_SetOnscreen(ScrollingMenuItem* item, bool v) {
    item->m_bOnscreen = (uint8_t)v;
}

#endif // FN_SCROLLING_MENU_ITEM_H
