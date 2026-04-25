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
//   +0x2C  Delegate1  m_Callback   (40 bytes; Delegate1<void,ScrollingMenuItem*>)
//          Binary ctor @ 0x0015b228: operator= target = (this+0x30); next field m_pText at +0x54
//          => Delegate1 spans +0x2C..+0x53 (40 bytes = 0x28)
//          SetOnscreen 0x0013ce10 writes: this[0x2D] = param_bool
//          => m_bOnscreen is byte 1 inside the Delegate1 header block
//   +0x54  char*    m_pText       SetText 0x0015b124: *(this+0x54)
//   +0x58  void*    m_field58     Draw 0x0015eb00: *(int*)(in_r0+0x58) non-null check
//                                 dereferences as ptr->field_0xb8 (likely ShopScreen*)
//   +0x5C  char[]   m_DescText    Draw: pcVar23 = (char*)(in_r0+0x5c) (inline text buffer)
//
// Port status: vtable fully matched; Draw() and scrolling physics not yet ported.
//
// Analysed: 2026-04-25T23:30
//

#include <functional>
#include <cassert>
#include <cstdint>
#include <cstddef>

class ScrollingMenu;

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
    virtual void Move(float x, float y, float z) { pos.x = x; pos.y = y; pos.z = z; }

    // vtable +0x1C (slot 7): Remove
    virtual void Remove() {}

    // vtable +0x20 (slot 8): SetParent(ScrollingMenu*)
    // Binary 0x0015aeb4: *(this+0x10) = param
    virtual void SetParent(ScrollingMenu* parent) { m_pParent = parent; }

    // vtable +0x24 (slot 9): SetOnscreen(bool)
    // Binary 0x0013ce10: this[0x2D] = param  (byte 1 of Delegate1 header)
    virtual void SetOnscreen(bool onscreen) { m_Delegate.m_bOnscreen = (uint8_t)onscreen; }

    // vtable +0x28 (slot 10): SetText(char*)
    // Binary: 0x0015b124 stores the pointer at +0x54.
    virtual void SetText(const char* text) { m_pText = text; }

    // vtable +0x2C (slot 11): Draw() -- renders this item
    // ScrollingMenu::Draw dispatches here via vtable[+0x2C].
    // ShopListItem overrides at 0x0015eb00.
    virtual void Draw() {}

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

    // +0x2C..+0x53: Delegate1<void,ScrollingMenuItem*> (40 bytes in binary).
    //
    // Binary layout of Delegate1 at +0x2C:
    //   +0x2C  byte  _cb_hdr0    (Delegate1 header byte 0)
    //   +0x2D  byte  m_bOnscreen (SetOnscreen 0x0013ce10 writes this[0x2D])
    //   +0x2E  byte  _cb_hdr1[0]
    //   +0x2F  byte  _cb_hdr1[1]
    //   +0x30..+0x4F  std::function<> callable (32 bytes; port uses std::function here)
    //   +0x50..+0x53  _cb_tail[4] (pad to reach +0x54)
    //
    // Port accesses m_bOnscreen via SetOnscreen()/getter; m_Callback via the
    // std::function stored at _delegate_fn (offset +0x30 within the 40-byte block).
    // sizeof(std::function<void(ScrollingMenuItem*)>) == 32 on x86_64 (verified).
    // Total region: 1+1+2+32+4 = 40 bytes. ✓
    struct {
        uint8_t _cb_hdr0;                                    // +0x2C  (Delegate1 header byte 0)
        uint8_t m_bOnscreen;                                 // +0x2D  SetOnscreen writes here
        uint8_t _cb_hdr1[2];                                 // +0x2E..+0x2F
        std::function<void(ScrollingMenuItem*)> _delegate_fn; // +0x30..+0x4F (32 bytes)
        uint8_t _cb_tail[4];                                 // +0x50..+0x53 (pad to 40 bytes)
    } m_Delegate;   // +0x2C, 40 bytes total

    // +0x54: display text pointer
    // Binary: SetText 0x0015b124 stores at *(this+0x54).
    const char* m_pText;              // +0x54

    // +0x58: opaque pointer (set by ShopScreen; checked non-null before desc/cost draw)
    // Binary Draw 0x0015eb00: if(*(int*)(in_r0+0x58)!=0) then dereference ->field_0xb8
    // Type is a pointer to an object with field at +0xB8 (likely ShopScreen* or similar).
    void* m_field58;                  // +0x58

    // +0x5C: inline description text buffer (ShopListItem::Draw reads this as in_r0+0x5c)
    // Size unknown; a 128-byte buffer is sufficient for the port.
    char m_DescText[128];             // +0x5C..+0xDB
};

// Convenience accessors that match binary SetOnscreen / GetOnscreen semantics.
// SetOnscreen writes byte at +0x2D (inside Delegate1 header, m_bOnscreen field).
inline void ScrollingMenuItem_SetOnscreen(ScrollingMenuItem* item, bool v) {
    item->m_Delegate.m_bOnscreen = (uint8_t)v;
}

#endif // FN_SCROLLING_MENU_ITEM_H
