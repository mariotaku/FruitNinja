#ifndef FN_HUD_COMBO_BOX_H
#define FN_HUD_COMBO_BOX_H

//
// ComboBox : HUDControl3d (sizeof 0xBC on ARM32)
//
// The collapsed-bar half of the dead-code dropdown widget stack
// (ComboBox -> ListBox -> VerticalScroller). No live call site in v1.6.1
// instantiates a ComboBox, so the whole triple is dead code -- but carries a
// complete faithful implementation (CheckBox / SliderControl policy).
//
// Draw (@0x001687f4) renders a collapsed bar: a combo label (Yellow) + the
// box.tex bar (the SAME shared box.tex ListBox/SliderControl load -- Ghidra-
// confirmed string ref at each LoadContent) + the expand_arrow.tex arrow +
// the currently-selected item's text. Update (@0x00167f70) hit-tests the
// bar+arrow rect and, on a fresh
// press, creates a ListBox (operator new(0xDC)) positioned just below the bar,
// registers ComboBox::ListBoxClosed as the ListBox's commit callback, and
// AddControl's it to the HUD. When the ListBox commits it fires ListBoxClosed,
// which copies the selection back into m_SelectedIter and flags m_bCleanupPending
// so the next Update tears the ListBox down.
//
// CleanUpListBox (@0x00167f10, thunk 0x00113398) tears the ListBox down
// IMMEDIATELY: HUD::RemoveControl(hud, m_pListBox) then delete m_pListBox (the
// deleting dtor runs ListBox::Release, which in turn removes+deletes its own
// VerticalScroller). It is NOT SetPendingRemoval() -- that flag only defers to
// HUD::Update's end-of-frame sweep and does not, by itself, guarantee
// ListBox::Release runs in the same step as the cleanup request, which left
// the scroller (and its arrows) visibly orphaned in an earlier port pass.
// A second tap on the bar while a ListBox is open (m_pListBox != NULL) also
// calls CleanUpListBox directly -- closes without changing the selection.
// Release() (@0x001681b8) is likewise CleanUpListBox() + m_pFont=NULL, NOT a
// tail-call to HUDControl3d::Release -- a ComboBox destroyed while open must
// still tear its ListBox/scroller down.
//
// Field offsets were re-verified against the ctor @0x001682d4 instruction stream.
//
// Binary (v1.6.1):
//   ctor  @ 0x001682d4      dtor @ 0x0016822c
//   Init                @ 0x00167ddc
//   PreDraw             @ 0x00167e40
//   Release             @ 0x001681b8
//   Draw                @ 0x001687f4
//   Update              @ 0x00167f70
//   GetType (-> 5)      @ 0x001690fc
//   LoadContent         @ 0x00168b3c (box.tex / expand_arrow.tex)
//   ListBoxClosed       @ 0x00167de0
//

#include "HUDControl3d.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>
#include <vector>
#include <string>

namespace Mortar { class Font; }
class ListBox;

class ComboBox : public HUDControl3d {
    friend struct ComboBoxLayoutAssert;

private:
    // +0x7C: item model (binary: std::vector<std::string>&).
    std::vector<std::string>* m_pItems;

    // +0x80: current selection = items.begin() + defaultIdx. ListBoxClosed
    // rewrites it from the ListBox's committed row.
    // DIFFERS: original = __gnu_cxx::__normal_iterator; the port stores the raw
    //   std::string* (fixed 4 bytes on every host). v1.6.1 ComboBox @ 0x001682d4.
    std::string* m_SelectedIter;

    // +0x84: text flag (ctor arg); passed to the ListBox as its visibleRows.
    uint8_t  m_TextFlag;

    // +0x85: pad
    uint8_t  _pad85[3];

    // +0x88: the combo's own header label (drawn Yellow above the selection).
    const char* m_pComboLabel;

    // +0x8C: horizontal text scale (ctor arg); m_DrawWidth = this * size.x.
    uint16_t m_TextScaleX;

    // +0x8E: vertical text scale (ctor arg); m_DrawHeight = this * size.y.
    uint16_t m_TextScaleY;

    // +0x90: font-size param for the label DrawString (font size = this * size.x).
    uint16_t m_Width;

    // +0x92: pad
    uint16_t _pad92;

    // +0x94: bar/quad width = m_TextScaleX * size.x.
    float    m_DrawWidth;

    // +0x98: bar/quad height = m_TextScaleY * size.y.
    float    m_DrawHeight;

    // +0x9C: label font (game_work.pFontMain == fonts[1]).
    Mortar::Font* m_pFont;

    // +0xA0: selected-item text colour. Default-constructed = opaque BLACK
    // (Colour::Colour() @0x0011afa8, ASM-confirmed: b=g=r=0, a=0xFF) --
    // NOT white as earlier assumed. Callers must SetTextColour() explicitly
    // for legible rendering; the binary's own ComboBox::Update (@0x00167f70)
    // never propagates this colour to the ListBox it spawns either, so a
    // freshly-opened ListBox's row text also defaults to opaque black.
    Colour   m_TextColour;

    // +0xA4: live ListBox while expanded (null when collapsed).
    ListBox* m_pListBox;

    // +0xA8: set by ListBoxClosed; CleanUpListBox runs next Update.
    uint8_t  m_bCleanupPending;

    // +0xA9: pad
    uint8_t  _padA9[3];

    // +0xAC: active touch slot (-1 = none).
    int32_t  m_TouchIndex;

    // +0xB0..+0xBB: captured touch position (12 bytes, Vec3).
    Vec3     m_TouchPos;

    // Port specific: settable ListBox row-tint theme, cached here (appended
    // after the binary-faithful 0xBC layout) and applied to m_pListBox both
    // immediately (if open) and at creation time in Update -- see .cpp. No
    // binary counterpart.
    Colour   m_ListSelectedRowColour;
    Colour   m_ListHoverRowColour;
    Colour   m_ListTextColour;
    bool     m_bListSelectedRowColourSet;
    bool     m_bListHoverRowColourSet;
    bool     m_bListTextColourSet;

public:
    // Binary @ 0x001682d4
    ComboBox(Vec3 pos, Vec3 size, std::vector<std::string>& items,
             uint16_t defaultIdx, const char* comboLabel, uint8_t textFlag,
             uint16_t width, uint16_t textScaleX, uint16_t textScaleY);

    virtual ~ComboBox();

    // vtable slot 2 -- Binary @ 0x00167ddc
    void Init() override;
    // vtable slot 3 -- Binary @ 0x001681b8 (tail-calls HUDControl3d::Release)
    void Release() override;
    // vtable slot 6 -- Binary @ 0x00167e40
    void PreDraw(float* hudScale) override;
    // vtable slot 7 -- Binary @ 0x001687f4 (label + bar + arrow + selected label)
    void Draw(float* hudScaleRaw) override;
    // vtable slot 10 -- Binary @ 0x00167f70 (hit-test; opens/closes the ListBox)
    void Update(float dt) override;
    // vtable slot 12 -- Binary @ 0x001690fc (returns 5)
    int GetType() override;

    // Non-virtual. Binary @ 0x00167de0: copies the ListBox selection into
    // m_SelectedIter and flags m_bCleanupPending. Registered as the ListBox's
    // commit callback (via ListBox::SetCallback).
    void ListBoxClosed();

    // Non-virtual setters / helpers.
    void SetTextColour(Colour c);
    void SetFont(Mortar::Font* font);
    void SetPosition(float x, float y);
    void CleanUpListBox();
    void UpdateTouchPosition();

    // Port specific: no binary counterpart. The dropdown ListBox is created
    // lazily on tap (see Update, m_pListBox starts NULL) so callers can't reach
    // it directly; these forward the row-tint theme onto the ListBox once it
    // exists and cache it so a ListBox opened LATER also picks it up.
    void SetListSelectedRowColour(Colour c);
    void SetListHoverRowColour(Colour c);
    void SetListTextColour(Colour c);

    // Read-only accessors (test/caller convenience).
    float DrawWidth()  const { return m_DrawWidth; }
    float DrawHeight() const { return m_DrawHeight; }
    std::string* SelectedIter() const { return m_SelectedIter; }

    // Static texture lifecycle. Binary @ 0x00168b3c.
    // Loads box.tex -> s_bar (shared with ListBox/SliderControl), expand_arrow.tex -> s_expandArrow.
    static void LoadContent();
    static void UnloadContent();

    // Port/test-only: inject the bar + arrow textures. box.tex ships,
    // but expand_arrow.tex is NOT in FruitNinjaBada/Data (see .cpp DIFFERS).
    // No binary counterpart.
    static void SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& bar,
                                   const Mortar::SmartPtr<Mortar::Texture>& arrow);

    // vtable/per-frame hook -- Binary empty bx lr.
    // Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
    void UpdateFromGameWork();
};

#if defined(__bada__)
#include <cstddef>
struct ComboBoxLayoutAssert {
    // Binary-faithful prefix is 0xBC (v1.6.1 ctor @0x001682d4); +0xBC..+0xCC is
    // the port-specific ListBox row-tint cache appended above -- not present in
    // the binary layout.
    static_assert(sizeof(ComboBox) == 0xCC, "ComboBox size mismatch");
    static_assert(offsetof(ComboBox, m_ListSelectedRowColour)     == 0xBC, "ComboBox::m_ListSelectedRowColour offset");
    static_assert(offsetof(ComboBox, m_ListHoverRowColour)        == 0xC0, "ComboBox::m_ListHoverRowColour offset");
    static_assert(offsetof(ComboBox, m_ListTextColour)            == 0xC4, "ComboBox::m_ListTextColour offset");
    static_assert(offsetof(ComboBox, m_bListSelectedRowColourSet) == 0xC8, "ComboBox::m_bListSelectedRowColourSet offset");
    static_assert(offsetof(ComboBox, m_bListHoverRowColourSet)    == 0xC9, "ComboBox::m_bListHoverRowColourSet offset");
    static_assert(offsetof(ComboBox, m_bListTextColourSet)        == 0xCA, "ComboBox::m_bListTextColourSet offset");
    static_assert(offsetof(ComboBox, m_pItems)         == 0x7C, "ComboBox::m_pItems offset");
    static_assert(offsetof(ComboBox, m_SelectedIter)   == 0x80, "ComboBox::m_SelectedIter offset");
    static_assert(offsetof(ComboBox, m_TextFlag)       == 0x84, "ComboBox::m_TextFlag offset");
    static_assert(offsetof(ComboBox, m_pComboLabel)    == 0x88, "ComboBox::m_pComboLabel offset");
    static_assert(offsetof(ComboBox, m_TextScaleX)     == 0x8C, "ComboBox::m_TextScaleX offset");
    static_assert(offsetof(ComboBox, m_TextScaleY)     == 0x8E, "ComboBox::m_TextScaleY offset");
    static_assert(offsetof(ComboBox, m_Width)          == 0x90, "ComboBox::m_Width offset");
    static_assert(offsetof(ComboBox, m_DrawWidth)      == 0x94, "ComboBox::m_DrawWidth offset");
    static_assert(offsetof(ComboBox, m_DrawHeight)     == 0x98, "ComboBox::m_DrawHeight offset");
    static_assert(offsetof(ComboBox, m_pFont)          == 0x9C, "ComboBox::m_pFont offset");
    static_assert(offsetof(ComboBox, m_TextColour)     == 0xA0, "ComboBox::m_TextColour offset");
    static_assert(offsetof(ComboBox, m_pListBox)       == 0xA4, "ComboBox::m_pListBox offset");
    static_assert(offsetof(ComboBox, m_bCleanupPending)== 0xA8, "ComboBox::m_bCleanupPending offset");
    static_assert(offsetof(ComboBox, m_TouchIndex)     == 0xAC, "ComboBox::m_TouchIndex offset");
    static_assert(offsetof(ComboBox, m_TouchPos)       == 0xB0, "ComboBox::m_TouchPos offset");
};
#endif

#endif // FN_HUD_COMBO_BOX_H
