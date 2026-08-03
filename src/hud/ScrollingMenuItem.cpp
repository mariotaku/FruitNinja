// Analysed: 2026-04-25T23:30
//
// ScrollingMenuItem stub implementation.
// Binary: ctor 0x0015b5dc, dtor 0x0015c3ac.
// Vtable fully matched per shop.md ScrollingMenuItem Class Layout section.
// Full rendering pipeline ported in ShopListItem::Draw.

#include "ScrollingMenuItem.h"
#include "ScrollingMenu.h"
#include "engine/render/Font.h"
#include "engine/math/_Vector2.h"
#include "engine/math/_Vector3.h"
#include "engine/math/Colour.h"
#include "game/GameWork.h"
#include <cstring>

// Default item size from binary global DAT values (ctor 0x0015b5dc).
// DAT_0015b668 = 0.0f (m_Width default). m_Height default = 25.0f (ctor literal).
// m_Size is loaded from a global Vec3 ptr (zeroed at load time = all 0.0f).

// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0015b5dc (asm-inspector)
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

// ASM-verified: 2026-07-04T00:00Z v1.6.1 ScrollingMenuItem::~ScrollingMenuItem @ 0x001b1488 (asm-inspector)
ScrollingMenuItem::~ScrollingMenuItem() {
    if (m_pText != nullptr) {
        delete[] m_pText;
        m_pText = nullptr;
    }
    // m_Delegate destroyed automatically.
}

// v1.6.1 ScrollingMenuItem::Move @ 0x001af5f8: void Move(_Vector3<float> v) by value.
void ScrollingMenuItem::Move(_Vector3<float> v) {
    pos.x = v.x; pos.y = v.y; pos.z = v.z;
}

void ScrollingMenuItem::SetParent(ScrollingMenu* parent) {
    m_pParent = parent;
}

// ASM-verified: 2026-07-04T00:00Z v1.6.1 ScrollingMenuItem::SetText @ 0x001afb14 (asm-inspector)
void ScrollingMenuItem::SetText(const char* text) {
    if (m_pText != nullptr) {
        delete[] m_pText;
        m_pText = nullptr;
    }
    if (text != nullptr) {
        char* buf = new char[strlen(text) + 1];
        strcpy(buf, text);
        m_pText = buf;
    }
}

// Binary @ 0x001afd40 -- ScrollingMenuItem::Draw (v1.6.1 vtable slot 12, +0x30).
//
// Renders the item's text label, optionally clipped to the parent menu's
// visible rect. Disasm-confirmed sequence:
//   1. if (m_Text == NULL) return;
//   2. textPos = pos + m_Size            (Vector3::operator+, hidden r2=&m_Size)
//   3. clipRect = NULL
//      if (m_Parent != NULL):
//          h = parent->GetHeight()       (vtable +0x44, reads ScrollingMenu +0xa0)
//          w = parent->GetWidth()        (vtable +0x48, reads ScrollingMenu +0xa4)
//          clipRect.top    = parent.pos.y + h * 0.5    (sp+0x30)
//          clipRect.bottom = parent.pos.y + h * -0.5   (sp+0x38)
//          clipRect.left   = parent.pos.x + w * -0.5   (sp+0x2c)
//          clipRect.right  = parent.pos.x + w * 0.5    (sp+0x34)
//          clipRect = &{left, top, right, bottom}      (memory order at sp+0x2c)
//   4. font  = game_work.pFontMain  (*(Font**)(game_work + 0x58))
//      iter  = Utf8StringIterator(m_Text)
//      colour = m_Colour
//      maxWH = *globalDefaultMaxWH (GOT+0x78c0 -> BSS Vec2, load-time (0,0))
//      Font_DrawString(font, iter, pos=textPos, colour, maxWH,
//                      scale=30.0f (s0), yLineFactor=1.0f (s1), rotZ=0.0f (s2),
//                      alignment=0xF, clipRect)
void ScrollingMenuItem::Draw() {
    // (1) No text -> nothing to render.
    if (m_pText == nullptr) {
        return;
    }

    // (2) Text base position = pos + m_Size (binary Vector3::operator+).
    _Vector3<float> textPos(pos.x + m_Size.x, pos.y + m_Size.y, pos.z + m_Size.z);

    // (3) Clip rect derived from the parent menu's centre + width/height.
    // Only built when this item has a parent; otherwise no clipping.
    Mortar::MortarRectangleT<float> clipRect;
    Mortar::MortarRectangleT<float>* pClip = nullptr;
    if (m_pParent != nullptr) {
        float h = m_pParent->GetHeight();   // vtable +0x44 -> ScrollingMenu +0xa0
        float w = m_pParent->GetWidth();    // vtable +0x48 -> ScrollingMenu +0xa4
        clipRect.top    = m_pParent->pos.y + h * 0.5f;
        clipRect.bottom = m_pParent->pos.y + h * -0.5f;
        clipRect.left   = m_pParent->pos.x + w * -0.5f;
        clipRect.right  = m_pParent->pos.x + w * 0.5f;
        pClip = &clipRect;
    }

    // (4) Resolve the main font from game_work. @0x001afe14: ldr r7,[r3,#0x58] =
    // pFontMain, unguarded. (The +0x54 load nearby is ldr r1,[r4,#0x54] = this->m_pText.)
    Mortar::Font* font = game_work.pFontMain.Get();

    Mortar::Utf8StringIterator iter(m_pText);
    // m_Colour is the packed 4-byte BGRA field (binary Colour::Colour copy-ctor
    // from this+0x14). Reinterpret the bytes into a Colour byte-for-byte.
    Colour colour;
    memcpy(&colour, &m_Colour, sizeof(colour));

    // maxWH from the global default-size Vec2 (GOT+0x78c0 -> BSS, load-time (0,0)).
    // maxWH.x <= 0 means "no word-wrap"; the parent clip rect bounds the text.
    _Vector2<float> maxWH(0.0f, 0.0f);

    // Binary scale=30.0f, yLineFactor=1.0f, rotZ=0.0f (DAT_0015b5a0), alignment=0xF.
    font->DrawString(30.0f, 1.0f, 0.0f,
                     iter, textPos, colour,
                     maxWH, 0xF, 0.0f, pClip);
}

// Binary @ 0x001afbb0 -- 4-param ctor: ScrollingMenuItem(float width, float height,
// char const* text, Mortar::Delegate1<void,ScrollingMenuItem*> callback).
//
// Binary sequence (disasm-confirmed):
//   *(this) = vtable+8                          (set in member-init order by C++)
//   Colour::Colour(&m_Colour)                   default-construct
//   Delegate1::Delegate1(&m_Delegate);          default-ctor then operator= copy
//   Delegate1::operator=(&m_Delegate, callback)
//   m_Text = 0                                  (cleared before SetText)
//   m_Size = *(Vec3*)global_default_size_ptr    (GOT+DAT_0015b2b8 -> BSS Vec3,
//                                                load-time value (0,0,0))
//   SetText(this, text)                         stores text at +0x54
//   MakeColourFromGlobal_ScrollMenu(tmp, &m_Colour)  copies the global white
//                                                colour singleton (GOT+0x73a4 =
//                                                {255,255,255,255}) into m_Colour.
//   m_Height = width   (param1 -> +0x24, vstr s16 at 0x001afc40)
//   m_Width  = height  (param2 -> +0x28, vstr s17 at 0x001afc44)
//
// Param naming is "swapped": the first float (width) lands in m_Height (the
// ROW PITCH field, GetHeight target) and the second (height) lands in m_Width.
// This matches the binary's vstr layout exactly -- not a typo.
ScrollingMenuItem::ScrollingMenuItem(float width, float height, const char* text, Mortar::Delegate1<void, ScrollingMenuItem*> delegate)
    : m_pParent(nullptr)
    , m_Colour(0xFFFFFFFF)   // overwritten below by MakeColourFromGlobal_ScrollMenu
    , m_Size{0.0f, 0.0f, 0.0f}
    , m_Height(width)        // param1 (width) -> +0x24 m_Height (v1.6.1 ctor @0x001afbb0, vstr s16,[r4,#0x24] @0x001afc40)
    , m_Width(height)        // param2 (height) -> +0x28 m_Width  (v1.6.1 ctor @0x001afbb0, vstr s17,[r4,#0x28] @0x001afc44)
    , _pre_del0(0)
    , m_bOnscreen(0)
    , _pre_del1(0)
    , _pre_del2(0)
    , m_Delegate(delegate)
    , m_pText(nullptr)       // binary clears m_Text before SetText
{
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;

    // m_Size from global default-size Vec3 (GOT-relative DAT_0015b2b8 -> BSS,
    // zero at load time). Left as (0,0,0) above to match the load-time value.

    SetText(text);   // binary calls SetText (vtable slot) to store the pointer.

    // MakeColourFromGlobal_ScrollMenu @ 0x0015b154: copies the global white
    // colour singleton (GOT+0x73a4 = {255,255,255,255}) into m_Colour.
    m_Colour = 0xFFFFFFFF;   // white {255,255,255,255}
}

void ScrollingMenuItem::CallClickedMenuItemCallback() {
    if (m_Delegate) {
        m_Delegate(this);
    }
}

// v1.6.1 ScrollingMenuItem::SetClickedFocusedCallback (called per-row from
// ShopScreen::Init @0x001b42ac, 0x1b43e0-0x1b4424).
void ScrollingMenuItem::SetClickedFocusedCallback(Mortar::Delegate1<void, ScrollingMenuItem*> callback) {
    m_Delegate = callback;
}

