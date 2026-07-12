#ifndef FN_UI_DROPDOWN_H
#define FN_UI_DROPDOWN_H

//
// UiDropdown -- Port specific: clean-slate dropdown widget for the settings
// toolkit (src/ui/). NO binary counterpart -- see src/ui/UiWidget.h for why
// this toolkit exists instead of resurrecting the binary's dead
// ComboBox/ListBox/VerticalScroller stack (src/hud/ComboBox.h,
// src/hud/ListBox.h, src/hud/VerticalScroller.h): that stack spawns child
// controls added to HUD, which freezes under HUD's modal update gate (see
// UiWidget.h usage contract). UiDropdown is fully self-contained -- a
// collapsed bar that opens an in-place scrollable list below itself, with
// no spawned sub-controls and no AddControl.
//
// Usage:
//   // font_fruit_ninja.fnt has NO lowercase glyphs (92-char set: digits/
//   // uppercase/punctuation only) -- item strings must be ALL CAPS or
//   // lowercase letters silently drop (GetCharTemplate returns null).
//   std::vector<std::string> items; items.push_back("APPLE"); ...
//   UiDropdown dd(Vec3(x, y, 0), items, 0, 6, 160.0f, 28.0f);
//   dd.SetBoxTexture(boxTex);         // required for DrawBox to render anything
//   dd.SetCaretTexture(caretTex);     // optional; only drawn on the collapsed bar
//   dd.SetOnChange(Delegate0<void>::Make(this, &Screen::OnDropdownChanged));
//   // every frame: dd.Update(dt); ... dd.Draw(hudScale);  <- draw LAST among
//   // the screen's widgets so the open panel overlays siblings correctly.
//
// `items` is caller-owned and NOT copied -- the vector must outlive this
// widget and must not be resized/reallocated while GetSelected()'s index
// could point past its new end.
//
// Touch model: collapsed, this behaves like any other UiWidget (PollTouch);
// tap-release-inside opens the panel. Open, the widget hand-rolls its own
// latch over a WIDER region (bar + panel union) using the same
// TouchInRegion/IsTouchDown primitives PollTouch uses internally, since the
// base's single m_HalfW/m_HalfH hit-rect can't express two stacked rects.
// While held inside the panel, dragging above/below the row band scrolls
// one row per frame; releasing over a row selects it and fires OnChange
// (installed via UiWidget::SetOnChange); releasing elsewhere just closes.
//

#include "UiWidget.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Colour.h"
#include <cstdint>
#include <string>
#include <vector>

class UiDropdown : public UiWidget {
public:
    UiDropdown(const Vec3& pos, std::vector<std::string>& items, int selected,
               uint8_t visibleRows = 6, float barW = 160.0f, float barH = 28.0f);
    virtual ~UiDropdown();

    void Update(float dt) override;
    void Draw(float* hudScale) override;

    int GetSelected() const { return m_Selected; }
    void SetSelected(int idx);
    bool IsOpen() const { return m_Open; }

    void SetRowColours(Colour sel, Colour hover, Colour text);
    void SetCaretTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_CaretTex = tex; }
    void SetRowHeight(float h) { m_RowH = h; }
    void SetTextScale(float s) { m_TextScale = s; }

    void Release() override { m_CaretTex.SetNull(); UiWidget::Release(); }

    // Test-only: force-open the panel without a touch sequence, for render tests.
    void SetOpenForTest(bool open) {
        m_Open = open;
        if (open) {
            int maxScroll = ComputeMaxScroll();
            int st = m_Selected - (int)m_VisibleRows / 2;
            if (st < 0) st = 0;
            if (st > maxScroll) st = maxScroll;
            m_ScrollTop = st;
        }
    }
    // Test-only: force a hover row for the render-test screenshot.
    void SetHoverRowForTest(int row) { m_HoverRow = row; }
    // Test-only: force the scroll offset for the render-test screenshot.
    void SetScrollTopForTest(int scrollTop);

private:
    // Clamp of (items.size() - visibleRows) to >= 0 -- the largest valid
    // m_ScrollTop. Recomputed on demand rather than cached to avoid the old
    // ListBox stale-index class of bug.
    int ComputeMaxScroll() const;

    std::vector<std::string>* m_pItems;   // caller-owned, NOT copied/owned by this class
    int m_Selected;
    bool m_Open;
    int m_ScrollTop;
    uint8_t m_VisibleRows;
    int m_HoverRow;          // -1 = none
    float m_BarW, m_BarH, m_RowH;
    Colour m_SelRowColour, m_HoverRowColour, m_RowTextColour;
    Mortar::SmartPtr<Mortar::Texture> m_CaretTex;
    float m_TextScale;   // default 18.0f, matching SettingsScreen.cpp DrawSettingsLabel's convention
};

#endif // FN_UI_DROPDOWN_H
