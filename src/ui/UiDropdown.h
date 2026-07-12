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
// latch using the same TouchInRegion/IsTouchDown primitives PollTouch uses
// internally, since the base's single m_HalfW/m_HalfH hit-rect can't express
// two stacked rects. The open-state acquire scan is MODAL -- it latches a
// press-edge touch ANYWHERE on screen (full-screen scrim), not just over the
// bar+panel, so an outside tap can't fall through to the blade or a sibling
// widget on the same frame it lands; release then closes the panel (without
// selecting) unless the captured press was inside the row list.
//
// OPEN-list scrolling mirrors ScrollingMenu's (src/hud/ScrollingMenu.cpp)
// kinetic drag/fling/spring-back model, re-scoped to a single float content
// offset instead of ScrollingMenu's per-item layout accumulation (this
// widget has no ScrollingMenuItem children -- rows are drawn directly from
// m_ScrollOffset each frame).
//
// Sign convention: m_ScrollOffset >= 0; 0 = list top (item 0 first row),
// +maxScroll = list bottom (maxScroll = (itemCount-visibleRows)*rowH).
// Increasing m_ScrollOffset shifts every row UP the panel, revealing LATER
// items -- natural/content-follows-finger, same rule as the shop list: drag
// up -> later items, drag down -> earlier items. (m_ScrollOffset is the
// negation of ScrollingMenu's m_Velocity.y, which decreases for the same
// up-drag; see Update()'s implementation comment for the formula mapping.)
//   - Held: m_PendingVel damped-follows the finger via
//     `(m_ScrollOffset - (m_AnchorOffset + delta)) * DRAG_DELTA_FACTOR`
//     (DRAG_DELTA_FACTOR=-0.5, delta = currentY - anchorY) every frame, so
//     the offset eases toward the finger instead of snapping to it 1:1.
//   - Every frame (held or not): `m_PendingVel *= SCROLL_FRICTION` (0.9),
//     then `m_ScrollOffset += m_PendingVel` -- this is what makes a flick
//     coast after release (last frame's velocity decays at 0.9/frame).
//   - Spring-back only runs while NOT touching (m_TouchId == -1): past the
//     top (offset<0) -> `offset *= SPRING_BACK_COEF` (0.75); past the
//     bottom (offset>maxScroll) -> `offset += (maxScroll-offset) *
//     SPRING_FWD_COEF` (0.25).
//   - Release selects a row ONLY on a tap: !m_bDragging AND
//     m_DragDist < DRAG_CANCEL_DIST (5.0) AND |m_PendingVel| < CLICK_VEL_GATE
//     (0.5). A drag or fling release never selects -- it just leaves the
//     panel open and coasting/springing.
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
            float maxScroll = ComputeMaxScroll();
            float st = (float)(m_Selected - (int)m_VisibleRows / 2) * m_RowH;
            if (st < 0.0f) st = 0.0f;
            if (st > maxScroll) st = maxScroll;
            m_ScrollOffset = st;
        }
        m_PendingVel = 0.0f;
    }
    // Test-only: force a hover row for the render-test screenshot.
    void SetHoverRowForTest(int row) { m_HoverRow = row; }
    // Test-only: force the scroll offset (world-Y content shift; 0 = list
    // top/item 0, +maxScroll = list bottom -- see m_ScrollOffset doc below)
    // for the render-test screenshot.
    void SetScrollOffsetForTest(float offset);

private:
    // Largest valid m_ScrollOffset (content scrolled to its last row):
    // (items.size() - visibleRows) * m_RowH, clamped to >= 0. Recomputed
    // on demand rather than cached to avoid the old ListBox stale-index
    // class of bug.
    float ComputeMaxScroll() const;

    // Drag/fling/spring-back constants -- ported verbatim from ScrollingMenu
    // (src/hud/ScrollingMenu.cpp); see that file's header comment for the
    // binary DAT provenance of each value.
    static const float SCROLL_FRICTION;    // 0.9f  -- per-frame velocity decay
    static const float DRAG_DELTA_FACTOR;  // -0.5f -- damped-follow easing
    static const float DRAG_THRESHOLD;     // 0.001f -- |delta| to enter drag mode
    static const float DRAG_CANCEL_DIST;   // 5.0f  -- accumulated drag dist that cancels a tap
    static const float SPRING_BACK_COEF;   // 0.75f -- spring multiplier past top
    static const float SPRING_FWD_COEF;    // 0.25f -- spring multiplier past bottom
    static const float CLICK_VEL_GATE;     // 0.5f  -- |m_PendingVel| gate for tap-select

    std::vector<std::string>* m_pItems;   // caller-owned, NOT copied/owned by this class
    int m_Selected;
    bool m_Open;

    // Kinetic scroll state (mirrors ScrollingMenu's touch-physics fields).
    float m_ScrollOffset;     // world-Y content shift; >= 0, 0 = list top, +maxScroll = list bottom
    Vec3  m_TouchAnchorPos;   // finger (x, y, phase) latched at press
    float m_AnchorOffset;     // m_ScrollOffset at press time
    float m_PendingVel;       // decaying drag/fling velocity
    uint8_t m_bDragging;      // 1 once |delta| has exceeded DRAG_THRESHOLD this touch
    float m_DragDist;         // accumulated |delta| since press, for tap-vs-drag gate

    uint8_t m_VisibleRows;
    int m_HoverRow;          // -1 = none
    float m_BarW, m_BarH, m_RowH;
    Colour m_SelRowColour, m_HoverRowColour, m_RowTextColour;
    Mortar::SmartPtr<Mortar::Texture> m_CaretTex;
    float m_TextScale;   // default 18.0f, matching SettingsScreen.cpp DrawSettingsLabel's convention
};

#endif // FN_UI_DROPDOWN_H
