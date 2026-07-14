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
//   // If the caller has its own content glScissor/scroll-clip (a scrolling
//   // settings-style panel -- see SettingsScreen.cpp), call DrawBar()
//   // INSIDE that scissor (it's pure geometry, touches no GL scissor state,
//   // so the caller's clip governs it) and DrawPanel() AFTER the caller's
//   // scissor is disabled, so only the popup list is allowed to overflow
//   // the clipped content -- the bar itself must clip/fade like any other
//   // row. Draw() (bar+panel together, unclipped) remains correct for a
//   // dropdown with no surrounding scissor.
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
// Row content clip: a straddling row's highlight box is CUT at the panel's
// inner viewport TOP/BOTTOM edge (true glScissor, see Draw()), not
// geometrically shrunk -- a shrunk box reads as a squish, not a clip. The
// highlight-box scissor is VERTICAL-ONLY (full ortho width) -- it must never
// clip the box's left/right sides. Row TEXT additionally uses
// Font::DrawString's clipRect, which IS X-inset (past the box's rim) since
// per-glyph text clamping doesn't have the same squish/clip distinction.
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
//     the offset eases toward the finger instead of snapping to it 1:1. This
//     stays in Update() (60Hz) since it reads live touch state.
//   - Integrate + friction + spring-back (`m_PendingVel *= SCROLL_FRICTION`,
//     `m_ScrollOffset += m_PendingVel`, spring-back at the bounds) has moved
//     to UpdateRealtime(dtSeconds) -- once per PRESENTED frame, dt-scaled --
//     so a flick's coast/spring-back tracks the display refresh rate instead
//     of the fixed 60Hz sim tick (see UpdateRealtime()'s own .cpp comment;
//     mirrors SettingsScreen::UpdateRealtime's identical split). At
//     dtSeconds == 1/60 (f=1.0) this reproduces the exact per-tick literal
//     forms below:
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
//   - The row index is the UNCLAMPED floor((curYBase-fingerY)/rowH), REJECTED
//     (not selected, panel just closes) if it falls outside [0, itemCount) --
//     e.g. a press captured in the BAR band above the panel. Do not clamp
//     this index into range; a clamp aliases an above-panel press into row 0
//     (same rule for the live hover highlight during Held).
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
#ifndef __bada__
    // Port specific: no binary counterpart. Ticks the open-panel scroll
    // PHYSICS (friction decay + velocity integration + spring-back) once per
    // PRESENTED frame instead of once per 60Hz sim step, dt-scaled so the
    // same on-screen motion results at 60 and 120+ fps alike -- mirrors
    // SettingsScreen::UpdateRealtime's split of the identical model (see
    // SettingsScreen.h/.cpp). Touch acquire/track/drag-to-velocity (still in
    // Update(), which now only sets m_PendingVel) is UNCHANGED.
    //
    // UiDropdown is a CHILD WIDGET driven directly by its owning screen (NOT
    // AddControl'd to the HUD -- see the class header usage note), so
    // HUD::UpdateRealtime's control-list walk never reaches it. The owning
    // screen must forward its own per-present tick here explicitly while the
    // panel is open (see SettingsScreen::UpdateRealtime).
    void UpdateRealtime(float dtSeconds) override;
#endif
    // Composed convenience: DrawBar() then DrawPanel(). Callers that need
    // the bar to clip with scrolling content (see SettingsScreen.cpp) call
    // the two halves separately instead -- DrawBar() inside the content
    // scissor, DrawPanel() after it's disabled.
    void Draw(float* hudScale) override;
    // Draws ONLY the collapsed bar (DrawBox + value text + caret) -- pure
    // geometry, touches no GL scissor state, so the caller's own content
    // scissor clips it. Drawn whether open or closed (matches the old
    // Draw()'s unconditional bar draw before its open-panel branch).
    void DrawBar(float* hudScale);
    // Draws ONLY the open list panel (rows, highlights, fade edges) --
    // no-op if !IsOpen(). Manages its own internal row-viewport glScissor
    // (see .cpp); the caller must NOT have its own scissor enabled when
    // calling this (GL scissor state isn't stacked).
    void DrawPanel(float* hudScale);

    int GetSelected() const { return m_Selected; }
    void SetSelected(int idx);
    bool IsOpen() const { return m_Open; }

    void SetRowColours(Colour sel, Colour hover, Colour text);
    void SetCaretTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_CaretTex = tex; }
    // list_fade.tex -- top-rounded-corner fade band NineSlice-drawn at the
    // open panel's top edge normally, and at the bottom edge flipV=true (see
    // DrawFadeEdges). Optional: DrawFadeEdges no-ops if never set.
    void SetFadeTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_FadeTex = tex; }
    // list_item.tex -- borderless glossy vertical-gradient row highlight for
    // the selected/hover row (replaces the old bordered box.tex NineSlice --
    // see Draw()). Neutral/near-white art, MODULATE-tinted per state
    // (m_SelRowColour / m_HoverRowColour) via DrawGlyphQuad, sized to
    // m_BarW - 2*kGrooveOpeningInsetW (the rim OPENING's actual width, same
    // touch-point geometry DrawFadeEdges uses) so it fills right up to the
    // rim with no bare-groove margin. Sized directly, not scissor-trimmed --
    // the row-highlight glScissor is deliberately X-unbounded (vertical
    // clip only, see Draw()), so this quad's own width IS its left/right
    // extent. Falls back to the box.tex NineSlice highlight (own pad-based
    // width) if never set (m_BoxTex still valid).
    void SetItemTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_ItemTex = tex; }
    void SetRowHeight(float h) { m_RowH = h; }
    void SetTextScale(float s) { m_TextScale = s; }

    void Release() override {
        m_CaretTex.SetNull();
        m_FadeTex.SetNull();
        m_ItemTex.SetNull();
        UiWidget::Release();
    }

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

    // Top/bottom rounded-corner fade band for the open row list, GEOMETRICALLY
    // seated to coincide exactly with box.svg's inner-groove OPENING (the
    // hole its black rim stroke encloses), not an eyeballed inset. Full
    // derivation lives in list_fade.svg's header comment and this method's
    // .cpp comment; summary:
    //   - box.svg's groove is `rect x=6 y=6 w=52 h=28 rx=3.5 stroke-width=1`;
    //     since SVG strokes are path-centred, the OPENING inside the rim is
    //     the rect inset by half the stroke width: inset=6.5 texels,
    //     rx=3.0 texels (on box.svg's 64-wide canvas).
    //   - list_fade.svg is authored with those SAME numbers (inset=6.5,
    //     rx=3.0) on its own 64x6 canvas, so it can be NineSlice-drawn
    //     with kFadeSrcBorderPx/kFadeDestBorderX (this asset's OWN 9-slice
    //     X border -- box.tex's kBoxSrcBorderX=9 is sized for the OUTER rim
    //     and is too small to fully contain the groove opening's 9.5-texel
    //     arc extent without spillover) at the SAME per-texel scale
    //     (destBorder/srcBorder = 8/9, box.tex's corner-cell scale) that
    //     the panel's own box.tex draw uses -- so this asset's inset/radius
    //     map to the IDENTICAL world-space inset/radius the groove opening
    //     has relative to the panel rect.
    //   - HORIZONTAL-ONLY 9-slice (destBorderY=0/srcBorderYPx=0): the top/
    //     bottom border rows collapse to zero height so the middle row
    //     alone draws the whole image, stretched to kFadeHeight. Canvas
    //     HEIGHT (kFadeSvgCanvasH=6, INDEPENDENT of kFadeSrcBorderPx=10,
    //     the X-axis border) times the SAME 8/9 scale gives kFadeHeight --
    //     keeping the vertical scale equal to the horizontal corner-cell
    //     scale, so the rounded corner renders as a TRUE CIRCLE (not
    //     squashed into an ellipse) even after shrinking kFadeHeight (a
    //     40% reduction from the original ~8.889 to ~5.333, done by
    //     shrinking kFadeSvgCanvasH proportionally rather than touching
    //     kFadeHeight directly).
    //   - VERTICALLY seated flush against the PANEL RECT's own top/bottom
    //     edge (panelTopY / panelTopY-panelH, passed in -- NOT the
    //     row-viewport bounds, which use an unrelated `pad`=4.0 margin for
    //     row culling/scrolling), inset by kGrooveOpeningInsetW -- the
    //     groove opening's ACTUAL world-space touch-point offset (NOT
    //     kFadeDestBorderX, the whole NineSlice corner-CELL width; the
    //     opening's touch point sits at fraction 6.5/kFadeSrcBorderPx
    //     across that cell, a SMALLER value than the cell width itself --
    //     conflating the two seated the fade a few px past the rim, short
    //     of tucking into it). Applied directly to topCy/botCy since Y has
    //     no NineSlice border cell (destBorderY=0).
    //   - HORIZONTALLY, X extent is the FULL m_BarW (matching the panel's
    //     own DrawBox() call), NOT further inset by kGrooveOpeningInsetW --
    //     the corner CELL's own internal art (kFadeSrcBorderPx/
    //     kFadeDestBorderX, arc authored at texel x=6.5 within it) already
    //     positions the touch point at kGrooveOpeningInsetW from the
    //     quad's edge, mirroring how box.svg's own corner cell positions
    //     the groove relative to the PANEL's edge. Insetting fadeW by
    //     kGrooveOpeningInsetW too would double-apply that offset (an
    //     earlier bug).
    // Top band drawn upright; bottom band reuses the SAME texture
    // flipV=true so its rounded corners land at the panel's outer bottom
    // edge instead of a second asset. Drawn AFTER the row loop AND after
    // the row-highlight glScissor is disabled (see Draw()) so nothing
    // clips it. No-ops if m_FadeTex was never set.
    void DrawFadeEdges(float panelTopY, float panelH);
    // This asset's OWN 9-slice border in source texels (NOT kBoxSrcBorderX
    // -- box.tex's outer-rim border is too small to fully contain the
    // groove opening's 9.5-texel arc extent; see list_fade.svg's header).
    static const float kFadeSrcBorderPx;   // 10.0f
    // World-unit dest border for the NineSlice CORNER CELL (not the
    // opening's own touch-point inset -- see kGrooveOpeningInsetW), same
    // per-texel scale as box.tex's own corner cell (8 world units per 9
    // src texels: kFadeDestBorderX/kFadeSrcBorderPx == 8/9) so the fade's
    // authored inset/radius (identical numbers to the groove opening) land
    // at the groove opening's actual world-space position once mapped
    // through this cell.
    static const float kFadeDestBorderX;   // 10.0f * 8.0f/9.0f
    // list_fade.svg's own canvas HEIGHT in texels -- INDEPENDENT of
    // kFadeSrcBorderPx (the X corner-cell border; unrelated axis). kFadeHeight
    // must equal kFadeSvgCanvasH * (kFadeDestBorderX/kFadeSrcBorderPx) -- the
    // SAME 8/9 per-texel scale the X corner cells use -- or the horizontal-
    // only 9-slice's vertical stretch won't match the horizontal scale and
    // the rounded corner squashes into an ellipse.
    static const float kFadeSvgCanvasH;    // 6.0f
    static const float kFadeHeight;        // kFadeSvgCanvasH * 8.0f/9.0f -- keeps the corner circular (~5.333, 40% shorter than the original ~8.889)
    // The groove OPENING's actual world-space inset from the panel rect's
    // edge (box.svg groove-opening inset 6.5 texels, at fraction
    // 6.5/kFadeSrcBorderPx across the kFadeDestBorderX-wide corner cell) --
    // THIS is what DrawFadeEdges seats the band against, not the cell
    // width itself.
    static const float kGrooveOpeningInsetW;   // (6.5f/10.0f) * kFadeDestBorderX ~= 5.7778
    // The groove OPENING's world-space corner radius (box.svg groove
    // rx=3.0 opening radius, mapped through the same per-texel scale).
    static const float kGrooveOpeningRadiusW;  // (3.0f/10.0f) * kFadeDestBorderX ~= 2.6667

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

    // Caller-injected via SetFadeTexture() -- see DrawFadeEdges doc.
    Mortar::SmartPtr<Mortar::Texture> m_FadeTex;
    // Caller-injected via SetItemTexture() -- see SetItemTexture doc.
    Mortar::SmartPtr<Mortar::Texture> m_ItemTex;
};

#endif // FN_UI_DROPDOWN_H
