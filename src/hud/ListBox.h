#ifndef FN_HUD_LIST_BOX_H
#define FN_HUD_LIST_BOX_H

//
// ListBox : HUDControl3d (sizeof 0xDC on ARM32)
//
// The dropdown-list half of the dead-code dropdown widget stack
// (ComboBox -> ListBox -> VerticalScroller). A ComboBox creates a ListBox on
// tap (operator new(0xDC)); the ListBox in turn creates a VerticalScroller
// (operator new(0xB4)) in its own ctor, but ONLY when items.size() > visibleRows.
// No live call site in v1.6.1 constructs a ComboBox, so the whole triple is dead
// code -- but carries a complete faithful implementation (CheckBox/Slider policy).
//
// Field offsets were re-verified against the ctor @0x00194a74 instruction stream.
// Ghidra's struct names for this class are swapped (it shows a phantom m_pItems
// at +0x78 and mislabels +0x84/+0x88); the layout below is the disasm ground truth.
//
// Composition wiring (ListBox::ListBox @0x00194a74, overflow branch):
//   scrollRange = items.size() - visibleRows                 (VS maxValue)
//   initScroll  = clamp((selIter - begin()) - 2, 0, scrollRange)
//   new VerticalScroller(scrollerPos, size, /*min*/0, scrollRange, /*step*/1,
//                        initScroll, /*reverse*/true, /*totalRows*/visibleRows,
//                        /*visibleHeight*/0, /*totalHeight*/visibleRows*16)
//   scroller->AdjustByWidth(); scroller->Init(); HUD::AddControl(scroller,false);
//
// ListBox::Draw @0x00194788 reads the scroller's live scroll offset as
//   m_pScroller->m_CurrentValue (+0x88)  -- confirmed ldr r3,[r3,#0x88] @0x001947d0
// and picks the top visible row = items.begin() + m_CurrentValue.
// ListBox::Update's row hover/commit hit-test adds the SAME m_CurrentValue
// offset before computing the target row -- omitting it (as an earlier port
// pass did) makes a scrolled list always hover/commit the row at the top of
// the visible window instead of the one under the finger. Update reads the
// scroller unconditionally (`ldr r3,[r4,#0x88]; cmp r3,#0` @0x00194364), i.e.
// it gates on the pointer alone, not on items.size() > visibleRows.
//
// Teardown: Release() (@0x00194528) owns tearing down the VerticalScroller --
// it is a SEPARATE HUD control the ctor AddControl'd, so nothing else removes
// it. Release() must HUD::RemoveControl + delete m_pScroller before the base
// HUDControl3d teardown, or the scroller (and its arrows) never disappears
// even after the ListBox itself is destroyed. ComboBox::CleanUpListBox must
// likewise HUD::RemoveControl + delete the ListBox IMMEDIATELY (not merely
// SetPendingRemoval, which defers to HUD::Update's sweep and does not by
// itself run ListBox::Release in the same step) so this chain actually fires.
//
// Binary (v1.6.1):
//   ctor  @ 0x00194a74 (C1) / 0x00194d18 (C2)
//   Init                @ 0x001941b8
//   PreDraw             @ 0x001941fc
//   Release             @ 0x00194528
//   Draw                @ 0x00194788
//   Update              @ 0x00194298
//   GetType (-> 5)      @ 0x001954a8
//   GetSelected         @ 0x00169104 (returns m_TopVisibleIt)
//   SetCallback         @ 0x001691b4 (assigns m_OnSelect)
//   LoadContent         @ 0x00194fdc (box.tex -> s_bar; shared with ComboBox/SliderControl)
//

#include "HUDControl3d.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "engine/util/Delegate.h"
#include <cstdint>
#include <vector>
#include <string>

namespace Mortar { class Font; }
class VerticalScroller;

class ListBox : public HUDControl3d {
    friend struct ListBoxLayoutAssert;

private:
    // +0x7C: item model (binary: std::vector<std::string>&).
    std::vector<std::string>* m_pItems;

    // +0x80: committed selection. GetSelected() returns this.
    // DIFFERS: original = __gnu_cxx::__normal_iterator (a std::string* on
    //   libstdc++). The port stores the raw std::string* so the field is a
    //   fixed 4 bytes on every host (MSVC iterators are fatter); the offset
    //   holds. v1.6.1 ListBox @ 0x00194a74.
    std::string* m_TopVisibleIt;

    // +0x84: row currently under the cursor (hover). end()-equivalent = null.
    std::string* m_HoverIt;

    // +0x88: owned scroller (created only when items.size() > visibleRows; else null).
    VerticalScroller* m_pScroller;

    // +0x8C: row text font (game_work.pFontMain == fonts[1]).
    Mortar::Font* m_pTextFont;

    // +0x90: row text colour. Default-constructed = opaque BLACK
    // (Colour::Colour() @0x0011afa8, ASM-confirmed: b=g=r=0, a=0xFF) --
    // NOT white as earlier assumed. Never written by ComboBox when it spawns
    // a ListBox (ComboBox::Update @0x00167f70 has no colour-propagation
    // call in the binary either), so freshly-opened dropdowns always render
    // row text in black; use SetTextColourForTest() to override for a
    // legible test-only rendering.
    Colour m_TextColour;

    // +0x94: cell-width param (ushort ctor arg); m_CellWidth = this * size.x.
    uint16_t m_CellWidthParam;

    // +0x96: font-scale param (ushort ctor arg); m_CellHeight = this * size.y.
    uint16_t m_FontScaleParam;

    // +0x98: cell-height param (ushort ctor arg); font draw size = this * size.x.
    uint16_t m_CellHeightParam;

    // +0x9A: alignment pad
    uint16_t _pad9A;

    // +0x9C: row quad width = m_CellWidthParam * size.x.
    float m_CellWidth;

    // +0xA0: row quad height / row stride = m_FontScaleParam * size.y.
    float m_CellHeight;

    // +0xA4: fires on commit (tap-release inside a row). 36 bytes -> ends 0xC8.
    Mortar::Delegate0<void> m_OnSelect;

    // +0xC8: held touch slot (-1 = none), written by the ctor and by Update's
    // touch state machine. This INT is the slot -- Update tests it with
    // `cmn r5,#1` (@0x001943ac). An earlier port pass overloaded the float
    // m_TouchX below as the slot, which lost the captured touch X entirely.
    int32_t m_ActiveTouchId;

    // +0xCC..+0xD7: the held slot's captured pointer state, copied as one
    // 12-byte block by UpdateTouchPosition (@0x001941bc: ldmia/stmia of
    // game_work.m_FingerSpawnPos[m_ActiveTouchId], GameWork+0xA4, stride 12).
    // Update range-checks BOTH x (against left/right) and y (against
    // bottom/top) on release before committing a row.
    float m_TouchX;      // +0xCC
    float m_TouchY;      // +0xD0
    float m_TouchPhase;  // +0xD4  (z slot of the finger record = phase; see IsTouchDown @0x001ca69c)

    // +0xD8: visible row count (ctor arg).
    uint8_t m_VisibleRows;

    // +0xD9..+0xDB: pad
    uint8_t _padD9[3];

    // Port specific: settable row tint overrides, appended after the binary-
    // faithful 0xDC layout so sizeof/offsetof asserts below stay intact. No
    // binary counterpart -- base ListBox is dead code with no live call site in
    // the port (the settings dropdown is now the from-scratch src/ui/UiDropdown,
    // not this binary widget). Default-initialised to the binary's hardcoded
    // literals so behaviour is identical unless a caller opts in.
    Colour m_SelectedRowColour;
    Colour m_HoverRowColour;

public:
    // Binary @ 0x00194a74 (C1) / 0x00194d18 (C2)
    ListBox(_Vector3<float> pos, _Vector3<float> size, std::vector<std::string>& items,
            std::string* selIter, uint8_t visibleRows,
            uint16_t cellHeightParam, uint16_t cellWidthParam, uint16_t fontScaleParam);

    virtual ~ListBox();

    // vtable slot 2 -- Binary @ 0x001941b8 (empty)
    void Init() override;
    // vtable slot 3 -- Binary @ 0x00194528 (tail-calls HUDControl3d::Release)
    void Release() override;
    // vtable slot 6 -- Binary @ 0x001941fc (empty)
    void PreDraw(float* hudScale) override;
    // vtable slot 7 -- Binary @ 0x00194788 (rows: bg quad + text, hover/selected tint)
    void Draw(float* hudScaleRaw) override;
    // vtable slot 10 -- Binary @ 0x00194298 (touch state machine; commit fires m_OnSelect)
    void Update(float dt) override;
    // vtable slot 12 -- Binary @ 0x001954a8 (returns 5)
    int GetType() override;

    // Non-virtual. Binary @ 0x00169104: returns m_TopVisibleIt (committed selection).
    std::string* GetSelected();

    // Non-virtual. Binary @ 0x001691b4: assigns m_OnSelect.
    void SetCallback(const Mortar::Delegate0<void>& cb);

    // Non-virtual, port-side. No binary counterpart -- v1.6.1 ComboBox::Update
    // never propagates its own font to the ListBox it spawns (see
    // ComboBox.cpp DIFFERS marker). Sets the row text font (m_pTextFont,
    // +0x8C; ctor default game_work.pFontMain).
    void SetFont(Mortar::Font* font);

    // Read-only accessors (test/caller convenience).
    VerticalScroller* Scroller()   const { return m_pScroller; }
    float             CellWidth()  const { return m_CellWidth; }
    float             CellHeight() const { return m_CellHeight; }

    // Port/test-only: force the committed/hover row and row text colour to drive
    // a specific visual state for screenshot tests (the binary only ever writes
    // these from Update's hover/commit paths).
    // No binary counterpart.
    void SetTopVisibleForTest(std::string* it) { m_TopVisibleIt = it; }
    void SetHoverForTest(std::string* it)      { m_HoverIt = it; }

    // Port specific: no binary counterpart -- see m_TextColour/m_SelectedRowColour/
    // m_HoverRowColour declarations above. SetTextColourForTest is an alias kept
    // for existing test call sites.
    void SetTextColour(Colour c)               { m_TextColour = c; }
    void SetTextColourForTest(Colour c)        { m_TextColour = c; }
    void SetSelectedRowColour(Colour c)        { m_SelectedRowColour = c; }
    void SetHoverRowColour(Colour c)           { m_HoverRowColour = c; }

private:
    // Binary @0x001941bc (reached via PLT veneer 0x00111a00 from Update).
    // No-op when m_ActiveTouchId == -1; else copies the finger record.
    void UpdateTouchPosition();

public:

    // Static texture lifecycle. Binary @ 0x00194fdc.
    // Loads box.tex -> s_bar (row background; shared with ComboBox/SliderControl).
    static void LoadContent();
    static void UnloadContent();

    // Port/test-only: inject the row-background texture (box.tex ships,
    // but the test uses a placeholder for isolation). No binary counterpart.
    static void SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& bar);

    // vtable/per-frame hook -- Binary empty bx lr.
    // Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
    void UpdateFromGameWork();
};

#if defined(__bada__)
#include <cstddef>
struct ListBoxLayoutAssert {
    // Binary-faithful prefix is 0xDC (v1.6.1 ctor @0x00194a74, operator new(0xDC));
    // +0xDC..+0xE4 is the port-specific m_SelectedRowColour/m_HoverRowColour
    // tail appended above -- not present in the binary layout.
    static_assert(sizeof(ListBox) == 0xE4, "ListBox size mismatch");
    static_assert(offsetof(ListBox, m_SelectedRowColour) == 0xDC, "ListBox::m_SelectedRowColour offset");
    static_assert(offsetof(ListBox, m_HoverRowColour)    == 0xE0, "ListBox::m_HoverRowColour offset");
    static_assert(offsetof(ListBox, m_pItems)        == 0x7C, "ListBox::m_pItems offset");
    static_assert(offsetof(ListBox, m_TopVisibleIt)  == 0x80, "ListBox::m_TopVisibleIt offset");
    static_assert(offsetof(ListBox, m_HoverIt)       == 0x84, "ListBox::m_HoverIt offset");
    static_assert(offsetof(ListBox, m_pScroller)     == 0x88, "ListBox::m_pScroller offset");
    static_assert(offsetof(ListBox, m_pTextFont)     == 0x8C, "ListBox::m_pTextFont offset");
    static_assert(offsetof(ListBox, m_TextColour)    == 0x90, "ListBox::m_TextColour offset");
    static_assert(offsetof(ListBox, m_CellWidthParam)  == 0x94, "ListBox::m_CellWidthParam offset");
    static_assert(offsetof(ListBox, m_FontScaleParam)  == 0x96, "ListBox::m_FontScaleParam offset");
    static_assert(offsetof(ListBox, m_CellHeightParam) == 0x98, "ListBox::m_CellHeightParam offset");
    static_assert(offsetof(ListBox, m_CellWidth)     == 0x9C, "ListBox::m_CellWidth offset");
    static_assert(offsetof(ListBox, m_CellHeight)    == 0xA0, "ListBox::m_CellHeight offset");
    static_assert(offsetof(ListBox, m_OnSelect)      == 0xA4, "ListBox::m_OnSelect offset");
    static_assert(offsetof(ListBox, m_ActiveTouchId) == 0xC8, "ListBox::m_ActiveTouchId offset");
    static_assert(offsetof(ListBox, m_TouchX)        == 0xCC, "ListBox::m_TouchX offset");
    static_assert(offsetof(ListBox, m_TouchY)        == 0xD0, "ListBox::m_TouchY offset");
    static_assert(offsetof(ListBox, m_VisibleRows)   == 0xD8, "ListBox::m_VisibleRows offset");
};
#endif

#endif // FN_HUD_LIST_BOX_H
