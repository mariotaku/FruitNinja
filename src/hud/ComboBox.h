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
// blank_dialog_box.tex bar + the expand_arrow.tex arrow + the currently-selected
// item's text. Update (@0x00167f70) hit-tests the bar+arrow rect and, on a fresh
// press, creates a ListBox (operator new(0xDC)) positioned just below the bar,
// registers ComboBox::ListBoxClosed as the ListBox's commit callback, and
// AddControl's it to the HUD. When the ListBox commits it fires ListBoxClosed,
// which copies the selection back into m_SelectedIter and flags m_bCleanupPending
// so the next Update tears the ListBox down.
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
//   LoadContent         @ 0x00168b3c (blank_dialog_box.tex / expand_arrow.tex)
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

    // +0xA0: selected-item text colour (default white).
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

    // Read-only accessors (test/caller convenience).
    float DrawWidth()  const { return m_DrawWidth; }
    float DrawHeight() const { return m_DrawHeight; }
    std::string* SelectedIter() const { return m_SelectedIter; }

    // Static texture lifecycle. Binary @ 0x00168b3c.
    // Loads blank_dialog_box.tex -> s_bar, expand_arrow.tex -> s_expandArrow.
    static void LoadContent();
    static void UnloadContent();

    // Port/test-only: inject the bar + arrow textures. blank_dialog_box.tex ships,
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
    static_assert(sizeof(ComboBox) == 0xBC, "ComboBox size mismatch");               // v1.6.1 ctor @0x001682d4
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
