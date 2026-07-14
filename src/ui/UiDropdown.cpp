//
// UiDropdown -- Port specific: see header for rationale. No binary
// counterpart; port-only glue code, no // ASM-verified markers apply.
//

#include "UiDropdown.h"

#include "engine/input/Touch.h"
#include "game/GameWork.h"
#include "render/NineSlice.h"
#include "render/Renderer.h"
#include "asset/Texture.h"

#include <cmath>

namespace {
    inline int ClampInt(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
    inline float ClampF(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
}

// Ported verbatim from ScrollingMenu (src/hud/ScrollingMenu.cpp) -- see that
// file's header comment for binary DAT provenance.
const float UiDropdown::SCROLL_FRICTION   = 0.9f;
const float UiDropdown::DRAG_DELTA_FACTOR = -0.5f;
const float UiDropdown::DRAG_THRESHOLD    = 0.001f;
const float UiDropdown::DRAG_CANCEL_DIST  = 5.0f;
const float UiDropdown::SPRING_BACK_COEF  = 0.75f;
const float UiDropdown::SPRING_FWD_COEF   = 0.25f;
const float UiDropdown::CLICK_VEL_GATE    = 0.5f;
// Derived, not eyeballed -- see list_fade.svg's header comment and
// DrawFadeEdges' comment for the full derivation against box.svg's
// inner-groove opening (rect x=6 y=6 w=52 h=28 rx=3.5 stroke-width=1,
// centred stroke -> opening inset=6.5/rx=3.0 texels).
const float UiDropdown::kFadeSrcBorderPx  = 10.0f;
const float UiDropdown::kFadeDestBorderX  = 10.0f * 8.0f / 9.0f;   // ~8.8889 (NineSlice CORNER-CELL width, X axis)
// list_fade.svg's own canvas HEIGHT (6.0 texels) -- INDEPENDENT of
// kFadeSrcBorderPx (10.0, the X corner-cell border, unchanged) since a
// horizontal-only 9-slice's Y scale is governed purely by
// kFadeHeight/kFadeSvgCanvasH, not by the X border at all. Kept as its own
// named constant (rather than a bare 6.0f literal in kFadeHeight's own
// derivation) so the circularity relationship stays legible: kFadeHeight
// MUST equal kFadeSvgCanvasH * (kFadeDestBorderX/kFadeSrcBorderPx), the
// SAME 8/9 ratio the X corner cells use, or the rounded corner squashes
// into an ellipse (see list_fade.svg's header comment).
const float UiDropdown::kFadeSvgCanvasH   = 6.0f;
const float UiDropdown::kFadeHeight       = 6.0f * 8.0f / 9.0f;    // ~5.3333 (40% shorter than the original ~8.889)
// The opening's touch point sits at fraction 6.5/kFadeSrcBorderPx across
// the kFadeDestBorderX-wide corner cell -- NOT at the cell's own edge.
// Using kFadeDestBorderX directly as the seating inset (an earlier bug)
// seated the fade a few px past the rim's actual opening, short of tucking
// into its corners.
const float UiDropdown::kGrooveOpeningInsetW  = (6.5f / 10.0f) * UiDropdown::kFadeDestBorderX;   // ~5.7778
const float UiDropdown::kGrooveOpeningRadiusW = (3.0f / 10.0f) * UiDropdown::kFadeDestBorderX;   // ~2.6667

UiDropdown::UiDropdown(const _Vector3<float>& inPos, std::vector<std::string>& items, int selected,
                       uint8_t visibleRows, float barW, float barH)
    : UiWidget()
    , m_pItems(&items)
    , m_Selected(selected)
    , m_Open(false)
    , m_ScrollOffset(0.0f)
    , m_TouchAnchorPos(0.0f, 0.0f, 0.0f)
    , m_AnchorOffset(0.0f)
    , m_PendingVel(0.0f)
    , m_bDragging(0)
    , m_DragDist(0.0f)
    , m_VisibleRows(visibleRows)
    , m_HoverRow(-1)
    , m_BarW(barW)
    , m_BarH(barH)
    , m_RowH(28.0f)
    , m_SelRowColour(0xF2, 0xC4, 0x00)
    , m_HoverRowColour(0xC9, 0x9A, 0x3A)
    , m_RowTextColour(0xEA, 0xD8, 0xB0)
    , m_CaretTex()
    // Mortar::Font::DrawString scale is font-native pixel size, not a 0-1/1.0
    // identity multiplier -- 18.0f matches SettingsScreen.cpp DrawSettingsLabel.
    , m_TextScale(18.0f)
{
    pos = inPos;
    SetSize(barW * 0.5f, barH * 0.5f);
}

UiDropdown::~UiDropdown() {
}

float UiDropdown::ComputeMaxScroll() const {
    int count = (int)m_pItems->size();
    int overflowRows = count - (int)m_VisibleRows;
    if (overflowRows <= 0) return 0.0f;
    return (float)overflowRows * m_RowH;
}

void UiDropdown::SetSelected(int idx) {
    if (m_pItems->empty()) {
        m_Selected = 0;
        return;
    }
    m_Selected = ClampInt(idx, 0, (int)m_pItems->size() - 1);
}

void UiDropdown::SetRowColours(Colour sel, Colour hover, Colour text) {
    m_SelRowColour = sel;
    m_HoverRowColour = hover;
    m_RowTextColour = text;
}

void UiDropdown::SetScrollOffsetForTest(float offset) {
    m_ScrollOffset = ClampF(offset, 0.0f, ComputeMaxScroll());
}

void UiDropdown::Update(float dt) {
    (void)dt;

    if (!m_Open) {
        if (PollTouch() == kReleasedInside) {
            m_Open = true;
            float maxScroll = ComputeMaxScroll();
            float st = (float)(m_Selected - (int)m_VisibleRows / 2) * m_RowH;
            m_ScrollOffset = ClampF(st, 0.0f, maxScroll);
            m_PendingVel = 0.0f;
            m_HoverRow = -1;
        }
        return;
    }

    int panelRows = m_pItems->size() < (size_t)m_VisibleRows
                        ? (int)m_pItems->size() : (int)m_VisibleRows;
    const float pad = 4.0f;
    float panelTopY = pos.y - m_BarH * 0.5f;
    // Sign convention: m_ScrollOffset >= 0, 0 = list top (item 0 first),
    // +maxScroll = list bottom. Row idx's centre is curYBase - rowH*(idx+0.5);
    // increasing m_ScrollOffset shifts every row UP (toward viewportTop),
    // revealing LATER items -- matches natural/content-follows-finger: finger
    // drags up (currentY increases) -> offset increases -> later items show
    // (mirrors ScrollingMenu's m_Velocity.y, which is the negation of this:
    // ScrollingMenu's finger-up drives m_Velocity.y DOWN via the same -0.5
    // damped-follow, and curY = pos.y - m_Velocity.y shifts items up for a
    // falling m_Velocity.y -- see src/hud/ScrollingMenu.cpp Phase 3B/5).
    float curYBase = (panelTopY - pad) + m_ScrollOffset;

    float openLeft = pos.x - m_BarW * 0.5f;
    float openRight = pos.x + m_BarW * 0.5f;

    // --- Acquire (press-edge, full-screen scrim while open) ---
    // Modal latch: while open, this widget must own EVERY touch press so an
    // outside tap can't fall through to the blade or a sibling widget on the
    // same frame it lands. Scanning only the bar+panel rect left presses
    // outside it unlatched -> the slot stayed visible to slice/blade code
    // (which reads touch phases directly, not gated by any widget's own
    // consumption) and, on the SAME press, to any sibling widget's own
    // TouchInRegion scan. Scanning the full centered-ortho screen (x in
    // [-240,240], y in [-160,160] -- SetupOrtho(160,-160,-240,240,...), see
    // docs/engine/coordinate-system.md) means an outside press is latched
    // here first; Release below already closes without selecting when the
    // captured position isn't inside the panel rows.
    if (m_TouchId == -1) {
        int slot = TouchInRegion(-240.0f, 240.0f, -160.0f, 160.0f, -1);
        if (slot == -1) {
            // Not touching: spring-back can still run below.
        } else if (IsTouchDown(slot) == 2) {
            const Mortar::TouchState* ts = Mortar::Touch::GetInstance().GetSlot(slot);
            if (ts) {
                m_TouchId = slot;
                m_TouchAnchorPos.x = ts->currX;
                m_TouchAnchorPos.y = ts->currY;
                m_TouchAnchorPos.z = (float)ts->phase;
                m_AnchorOffset = m_ScrollOffset;
                m_bDragging = 0;
                m_DragDist = 0.0f;
                m_TouchCapture = game_work.m_FingerSpawnPos[slot];
            }
        }
    } else if (IsTouchDown(m_TouchId) == 0) {
        // --- Release ---
        // Row-hit test: the finger-y must land INSIDE the actual visible row
        // viewport (below the bar, above the panel's bottom edge) -- NOT a
        // clamp-into-range of the row index. viewportTop/viewportBottom
        // mirror Draw()'s own viewport (panelTopY-pad down to
        // panelTopY-pad-panelRows*m_RowH), so this is the same visible band
        // the user actually sees rows drawn in. rawIdx is the absolute item
        // index (curYBase already folds in m_ScrollOffset, same formula
        // Draw() uses for each row's centre: curYBase - rowH*(idx+0.5)) --
        // it must ALSO be a valid item (0 <= rawIdx < itemCount); a bar-band
        // press fails the viewport-y test, a below-last-row press (partial
        // final row) fails the item-count test.
        float viewportTop = (panelTopY - pad);
        float viewportBottom = viewportTop - panelRows * m_RowH;
        int rawIdx = (int)std::floor((curYBase - m_TouchCapture.y) / m_RowH);
        bool insideRowList = m_TouchCapture.y <= viewportTop && m_TouchCapture.y >= viewportBottom &&
                              rawIdx >= 0 && rawIdx < (int)m_pItems->size() &&
                              m_TouchCapture.x >= openLeft && m_TouchCapture.x <= openRight;

        float absVel = m_PendingVel < 0.0f ? -m_PendingVel : m_PendingVel;
        bool isTap = !m_bDragging && m_DragDist < DRAG_CANCEL_DIST && absVel < CLICK_VEL_GATE;

        if (isTap && insideRowList) {
            m_Selected = rawIdx;
            m_Open = false;
            if (m_OnChange) {
                m_OnChange();
            }
        } else if (!insideRowList && !m_bDragging) {
            // Released outside the row list (bar band or off-panel) without
            // ever dragging -> close, never select.
            m_Open = false;
        }
        // A drag/fling release (m_bDragging or DragDist beyond the cancel
        // gate) keeps the panel open, coasting/springing on its own.
        m_TouchId = -1;
        m_HoverRow = -1;
    } else {
        // --- Held: damped-follow drag + hover tracking ---
        m_TouchCapture = game_work.m_FingerSpawnPos[m_TouchId];
        const Mortar::TouchState* ts = Mortar::Touch::GetInstance().GetSlot(m_TouchId);
        float currentY = ts ? ts->currY : m_TouchCapture.y;

        float delta = currentY - m_TouchAnchorPos.y;
        float prevAbsDelta = m_bDragging ? m_DragDist : 0.0f;
        float absDelta = delta < 0.0f ? -delta : delta;
        m_DragDist += (absDelta > prevAbsDelta) ? (absDelta - prevAbsDelta) : 0.0f;
        if (absDelta > DRAG_THRESHOLD) {
            m_bDragging = 1;
        }

        // Damped-follow: ease m_ScrollOffset toward the finger-tracked target.
        // Mirrors ScrollingMenu's `(m_Velocity.y - (m_AnchorOffset.y - delta))
        // * DRAG_DELTA_FACTOR` with delta negated to match m_ScrollOffset's
        // sign being the negation of ScrollingMenu's m_Velocity.y (see the
        // Update() header comment above): target = m_AnchorOffset + delta,
        // so a positive delta (finger up) pulls m_ScrollOffset UP too.
        m_PendingVel = (m_ScrollOffset - (m_AnchorOffset + delta)) * DRAG_DELTA_FACTOR;

        if (!m_bDragging) {
            // Stationary finger: drive hover highlight from the live row.
            // absIdx (curYBase - currentY)/m_RowH is the ABSOLUTE item index
            // (same formula Draw() uses for row centres); m_HoverRow is
            // consumed viewport-RELATIVE (Draw(): idx == firstVisibleIdx +
            // m_HoverRow), so subtract firstVisibleIdx before storing. Reject
            // (-1, no highlight) rather than clamp when the finger sits in
            // the BAR band or below the last visible row -- a clamp-into-
            // range here is what let a bar-band hold visually highlight
            // row 0 (see the Release-branch comment above for the matching
            // fix); comparing the RAW absolute index against [0,panelRows)
            // without the firstVisibleIdx offset (a prior version of this
            // fix) instead over-rejected every genuinely visible row once
            // scrolled.
            int firstVisibleIdx = (int)std::floor(m_ScrollOffset / m_RowH + 0.0001f);
            int absIdx = (int)std::floor((curYBase - currentY) / m_RowH);
            int hoverRow = absIdx - firstVisibleIdx;
            m_HoverRow = (hoverRow >= 0 && hoverRow < panelRows) ? hoverRow : -1;
        } else {
            // A drag is scrolling, not hovering.
            m_HoverRow = -1;
        }
    }

    // Integrate + friction + spring-back moved to UpdateRealtime() (see
    // below) -- runs once per PRESENTED frame instead of once per 60Hz sim
    // step, dt-scaled so it's rate-independent. Update() only tracks touch
    // and sets m_PendingVel now; it does NOT touch m_ScrollOffset.
}

#ifndef __bada__
// Port specific: no binary counterpart (see UiDropdown.h). Runs the scroll
// velocity->position integration (friction decay + spring-back) that
// Update()'s tail used to run inline at the fixed 60Hz sim rate. Called by
// the owning screen (SettingsScreen::UpdateRealtime) once per PRESENTED
// frame while the panel is open, with dtSeconds = real elapsed time since the
// last present -- dt-SCALED below so the same on-screen motion results at 60
// and 120 fps alike (see SettingsScreen::UpdateRealtime's identical comment
// for the f/pow derivation):
//   f = dtSeconds * 60                      -- frames-equivalent (1.0 at 60fps)
//   m_PendingVel   *= SCROLL_FRICTION^f      -- exponential decay, rate-independent
//   m_ScrollOffset += m_PendingVel * f       -- velocity integrated by frame-equivalents
//   spring-back: offset += (target - offset) * (1 - SPRING_COEF^f)
void UiDropdown::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    float f = dtSeconds * 60.0f;

    // --- Integrate + friction (rate-independent) ---
    m_PendingVel *= powf(SCROLL_FRICTION, f);
    m_ScrollOffset += m_PendingVel * f;

    // --- Spring-back at bounds (only while not touching) ---
    // Past top (offset<0, before item 0) -> spring toward 0.
    // Past bottom (offset>maxScroll, past the last row) -> spring toward maxScroll.
    if (m_TouchId == -1) {
        float maxScroll = ComputeMaxScroll();
        if (m_ScrollOffset < 0.0f) {
            m_ScrollOffset += (0.0f - m_ScrollOffset) * (1.0f - powf(SPRING_BACK_COEF, f));
        } else if (m_ScrollOffset > maxScroll) {
            m_ScrollOffset += (maxScroll - m_ScrollOffset) * (1.0f - powf(1.0f - SPRING_FWD_COEF, f));
        }
    }
}
#endif

// Top/bottom rounded-corner fade band for the open row list, geometrically
// seated on box.svg's inner-groove OPENING. See header doc for the full
// derivation; summary: box.svg's groove is `rect x=6 y=6 w=52 h=28 rx=3.5
// stroke-width=1` -- since strokes are path-centred, the OPENING inside the
// rim (excluding the stroke itself) is that rect inset by half the stroke
// width: inset=6.5 texels (kGrooveOpeningInsetW in world units), rx=3.0
// texels (kGrooveOpeningRadiusW) on the 64-wide canvas.
//
// list_fade.svg is authored with those SAME numbers (inset=6.5, rx=3.0) on
// its own 64x6 canvas, using its OWN 9-slice X border kFadeSrcBorderPx=10 /
// kFadeDestBorderX (NOT kBoxSrcBorderX=9 -- the groove opening's arc extent,
// inset+radius=9.5 texels, is larger than box.tex's OUTER-rim-sized 9-texel
// corner cell and would spill into the stretched middle column) at the
// SAME 8/9 per-texel scale box.tex's own corner cell uses. Its shape: at
// texture y=0 (source top / quad TOP once drawn), the outline is at its
// NARROWEST (x=9.5..54.5, inset by opening-inset+radius -- the corner
// ARC's peak); as y increases toward the canvas bottom it flares out to
// the FULL opening width (x=6.5..57.5) by y=kFadeSvgCanvasH. So the
// quad's TOP edge (narrow, arc-peak side) is what must land at the rim's
// actual corner peak, kGrooveOpeningInsetW inside the panel's own top
// edge -- NOT kFadeDestBorderX (that's the whole NineSlice corner-CELL
// width, a larger value; conflating the two -- an earlier bug -- seated
// the band a few px past the rim's opening instead of tucking into it).
// HORIZONTAL-ONLY 9-slice (destBorderY=0/srcBorderYPx=0): the top/bottom
// border rows collapse to zero height so the middle row alone draws the
// whole image, stretched to kFadeHeight = kFadeSvgCanvasH * 8/9 -- the
// SAME 8/9 scale as X (kFadeSvgCanvasH is the canvas HEIGHT, independent
// of kFadeSrcBorderPx, the X-axis border -- shrinking kFadeSvgCanvasH
// proportionally is how kFadeHeight got 40% shorter without squashing the
// corner into an ellipse), so the corner renders as a true circle, not an
// ellipse (a mismatched X/Y scale would squash the arc).
//
// VERTICALLY seated flush against the PANEL RECT's own edges (panelTopY /
// panelTopY-panelH, passed in), NOT the row-viewport bounds (viewportTop/
// Bottom, which use an unrelated `pad`=4.0 convention for row culling/
// scrolling -- see Draw()): the quad's outer (narrow) edge sits
// kGrooveOpeningInsetW inside the panel rect's own top/bottom edge (Y has
// no NineSlice border cell -- destBorderY=0 -- so this offset is applied
// directly to topCy/botCy).
//
// HORIZONTALLY, X extent is the FULL m_BarW (matching the panel's own
// DrawBox() call), NOT further inset by kGrooveOpeningInsetW -- the corner
// CELL's own internal art (kFadeSrcBorderPx/kFadeDestBorderX, arc authored
// at texel x=6.5 within it) already positions the touch point at
// kGrooveOpeningInsetW from the quad's edge, mirroring how box.svg's own
// corner cell positions the groove relative to the PANEL's edge -- the
// SAME mechanism the panel's own DrawBox() uses. Insetting fadeW by
// kGrooveOpeningInsetW too would double-apply that offset (an earlier
// bug: the corner cell started already inset, then its internal art added
// a second, similar-sized offset on top).
//
// Top band drawn upright (opaque + rounded corners toward the panel's outer
// TOP edge, fading down into the list). Bottom band reuses the SAME texture
// flipV=true (NineSlice::Draw's flipV mirrors the middle row too -- see its
// header doc -- since a horizontal-only 9-slice draws ONLY the middle row)
// so its rounded corners land at the panel's outer BOTTOM edge instead,
// fading UP into the list -- rather than authoring a second mirrored SVG,
// same reuse convention as arrow.svg (VerticalScroller's up arrow, V-
// flipped for the down arrow). Drawn AFTER the row loop AND after the
// row-highlight glScissor is disabled (see Draw()) so nothing clips the
// fade. No-ops if m_FadeTex was never injected (SetFadeTexture).
void UiDropdown::DrawFadeEdges(float panelTopY, float panelH) {
    if (!m_FadeTex.IsValid()) return;

    float panelBottomY = panelTopY - panelH;

    float fadeH = kFadeHeight;
    float halfSpan = panelH * 0.5f;
    if (fadeH > halfSpan) fadeH = halfSpan;   // don't let the two fades overlap-invert on a tiny panel
    if (fadeH <= 0.0f) return;

    // X extent is the FULL panel width, matching the panel's own DrawBox()
    // call (DrawBox(pos.x, panelCenterY, m_BarW, panelH, ...)) -- NOT
    // inset by kGrooveOpeningInsetW. The corner cell's OWN internal
    // geometry (kFadeSrcBorderPx/kFadeDestBorderX, the arc authored at
    // texel x=6.5 within it) already positions the touch point at
    // kGrooveOpeningInsetW from WHATEVER edge the quad's left/right border
    // cell starts at -- exactly mirroring how box.svg's own corner cell
    // positions the groove opening relative to the PANEL's edge. Insetting
    // fadeW by kGrooveOpeningInsetW here would double-apply that offset
    // (an earlier bug): the corner cell would start ALREADY inset by
    // kGrooveOpeningInsetW, then its internal art adds a SECOND
    // kGrooveOpeningInsetW-ish offset on top, landing well short of (or
    // past, depending on direction) the true corner.
    float fadeW = m_BarW;

    // Groove-opening-flush position: the quad's OUTER (narrow, arc-peak)
    // edge sits exactly kGrooveOpeningInsetW inside the panel rect's own
    // top/bottom edge -- that inset IS the groove opening's actual
    // world-space touch point (see derivation above).
    float openingPeakTop = panelTopY - kGrooveOpeningInsetW;
    float openingPeakBottom = panelBottomY + kGrooveOpeningInsetW;
    float topCy = openingPeakTop - fadeH * 0.5f;
    float botCy = openingPeakBottom + fadeH * 0.5f;

    Mortar::NineSlice::Draw(m_FadeTex.Get(), pos.x, topCy, fadeW, fadeH,
                            kFadeSrcBorderPx, 0.0f,
                            kFadeDestBorderX, 0.0f, Colour::White);
    Mortar::NineSlice::Draw(m_FadeTex.Get(), pos.x, botCy, fadeW, fadeH,
                            kFadeSrcBorderPx, 0.0f,
                            kFadeDestBorderX, 0.0f, Colour::White,
                            /*flipV=*/true);
}

void UiDropdown::Draw(float* hudScale) {
    DrawBar(hudScale);
    DrawPanel(hudScale);
}

void UiDropdown::DrawBar(float* hudScale) {
    (void)hudScale;

    DrawBox(pos.x, pos.y, m_BarW, m_BarH, m_Tint);

    const float textPad = 8.0f;
    // Mortar::Font::DrawString's pos.y is a glyph-top anchor (glyph extends
    // downward by GetLineHeight(scale); see Font.h). To center a line of
    // text on centerY: yTop = centerY + lineH * 0.5f.
    Mortar::Font* font = m_pFont ? m_pFont : game_work.pFontMain.Get();
    const float lineH = font ? font->GetLineHeight(m_TextScale) : m_TextScale;
    if (m_Selected >= 0 && m_Selected < (int)m_pItems->size()) {
        DrawText((*m_pItems)[m_Selected].c_str(),
                  pos.x - m_BarW * 0.5f + textPad, pos.y + lineH * 0.5f,
                  m_TextScale, m_TextColour);
    }

    if (m_CaretTex.IsValid()) {
        float caretSize = ClampF(m_BarH * 0.6f, 0.0f, 16.0f);
        float caretHalf = caretSize * 0.5f;
        // Right margin matches textPad (the left text margin, 8.0f) rather
        // than a bare 2.0f -- the old margin sat the caret right against
        // the bar's inner rim with almost no breathing room; textPad gives
        // it the same comfortable gap the value text has on the left.
        DrawGlyphQuad(m_CaretTex.Get(), pos.x + m_BarW * 0.5f - caretHalf - textPad, pos.y,
                      caretSize, caretSize, Colour::White);
    }
}

void UiDropdown::DrawPanel(float* hudScale) {
    (void)hudScale;

    if (!m_Open) {
        return;
    }

    const float textPad = 8.0f;
    // Mortar::Font::DrawString's pos.y is a glyph-top anchor (glyph extends
    // downward by GetLineHeight(scale); see Font.h). To center a line of
    // text on centerY: yTop = centerY + lineH * 0.5f.
    Mortar::Font* font = m_pFont ? m_pFont : game_work.pFontMain.Get();
    const float lineH = font ? font->GetLineHeight(m_TextScale) : m_TextScale;

    int panelRows = m_pItems->size() < (size_t)m_VisibleRows
                        ? (int)m_pItems->size() : (int)m_VisibleRows;
    const float pad = 4.0f;
    float panelH = panelRows * m_RowH + 2.0f * pad;
    float panelTopY = pos.y - m_BarH * 0.5f;
    float panelCenterY = panelTopY - panelH * 0.5f;
    float curYBase = panelTopY - pad + m_ScrollOffset;

    DrawBox(pos.x, panelCenterY, m_BarW, panelH, m_Tint);

    // Clip to the panel viewport: only draw rows whose full row-height band
    // overlaps [panelTopY-pad, panelTopY-pad-panelRows*m_RowH]. Content is
    // indexed by item index (curYBase - rowH*(idx+0.5)), not by a scroll-top
    // row index, since m_ScrollOffset is a continuous float.
    float viewportTop = panelTopY - pad;
    float viewportBottom = viewportTop - panelRows * m_RowH;
    // First fully-visible item index, so a live m_HoverRow (viewport-relative
    // slot, set in Update) can be compared against the absolute item index.
    int firstVisibleIdx = (int)std::floor(m_ScrollOffset / m_RowH + 0.0001f);

    // Text clip rect (Font::DrawString's clipRect, per-glyph): the panel's
    // inner viewport, INSET by kBoxDestBorderX on X (past the box's own
    // reserved corner/rim border -- see UiWidget::kBoxDestBorderX/DrawBox
    // doc) so straddling row TEXT never overlaps the dark inner border of
    // the panel. Y sits inside the border via `pad` (viewportTop/Bottom =
    // panelTopY -+ pad, the panel-box-to-row-viewport gap set up above).
    float clipLeft   = pos.x - m_BarW * 0.5f + kBoxDestBorderX;
    float clipRight  = pos.x + m_BarW * 0.5f - kBoxDestBorderX;
    float clipTop    = viewportTop;
    float clipBottom = viewportBottom;

    Mortar::MortarRectangleT<float> textClip;
    textClip.left   = clipLeft;
    textClip.right  = clipRight;
    textClip.top    = clipTop;
    textClip.bottom = clipBottom;

    // True hardware clip for the row highlight boxes: a straddling row's
    // DrawBox() quad must render at its FULL size and be CUT at the edge,
    // not geometrically shrunk (a resize reads as a squish, not a clip --
    // see DrawFadeEdges/this function's git history). NineSlice::Draw /
    // Mesh::DrawQuadUnCached have no clip param of their own (unlike Font::
    // DrawString's clipRect), so a real glScissor is the only way to cut a
    // NineSlice box's geometry without resizing it. Mapping mirrors
    // BakedStringBox.cpp's established worldspace->scissor conversion
    // (DIFFERS: original clips via CPU ClipAgainstPlanes, no glScissor
    // equivalent in the binary -- port-only, GLES2 has no fixed-function
    // user clip planes): centered ortho SetupOrtho(top=160,bottom=-160,
    // left=-240,right=240) -> pixel = (world+halfExtent)/fullExtent *
    // viewportPx + viewportOrigin; glScissor's origin is bottom-left (GL
    // convention) while world Y is top-positive, hence the bottom-edge flip.
    //
    // X bounds are the FULL ortho width (-240..240), NOT clipLeft/clipRight
    // -- the row highlight box must only ever be clipped VERTICALLY (top/
    // bottom viewport edges); an X-inset scissor here would cut the
    // selected/hover box's left/right sides, which is wrong (the box's own
    // width, m_BarW - 2*pad, already sits inside the panel and needs no
    // horizontal clip).
    //
    // Y bounds are the RIM OPENING (panelTopY/panelBottomY inset by
    // kGrooveOpeningInsetW -- the SAME groove-opening geometry
    // DrawFadeEdges seats the fade against, see its derivation), NOT the
    // tighter row-viewport (viewportTop/Bottom, `pad`=4.0-based). Using the
    // row-viewport here left a bare-groove GAP between a top/bottom row's
    // highlight gradient and the rim -- the highlight must fill all the
    // way up to the rim opening, not just up to the row-viewport margin.
    // Row TEXT is unaffected (it uses textClip, its own separate per-glyph
    // clipRect, still row-viewport-bounded so it doesn't spill under the
    // fade).
    // Host/SDL+GLES2 only: Renderer::SetClipRect no-ops on __bada__ / FN_GL_STUB
    // (glGetIntegerv/GL_VIEWPORT aren't in the asm-verify cross-build's GL shim
    // or the unit-test GL stub), so this call is unconditional here.
    float rimOpeningTop = panelTopY - kGrooveOpeningInsetW;
    float rimOpeningBottom = panelTopY - panelH + kGrooveOpeningInsetW;
    if (Renderer* r = Renderer::GetInstance()) {
        r->SetClipRect(-240.0f, rimOpeningTop, 240.0f, rimOpeningBottom);
    }

    for (int idx = 0; idx < (int)m_pItems->size(); ++idx) {
        float rowCy = curYBase - m_RowH * (idx + 0.5f);
        float rowTop = rowCy + m_RowH * 0.5f;
        float rowBottom = rowCy - m_RowH * 0.5f;
        if (rowTop < viewportBottom || rowBottom > viewportTop) {
            continue;
        }

        bool isHover = m_HoverRow >= 0 && idx == firstVisibleIdx + m_HoverRow;
        bool isSelected = idx == m_Selected;
        if (isHover || isSelected) {
            Colour rowTint = isHover ? m_HoverRowColour : m_SelRowColour;
            // iOS/iPod-style borderless glossy gradient (list_item.tex,
            // neutral art MODULATE-tinted per state) replaces the old
            // bordered box.tex NineSlice highlight -- a plain stretched
            // quad, no rim, since the gradient is vertical-only (see
            // SetItemTexture doc). X extent is m_BarW - 2*kGrooveOpeningInsetW
            // -- the rim-OPENING's actual width (same touch-point geometry
            // DrawFadeEdges/the row-highlight scissor use), NOT
            // kBoxDestBorderX (that undershoots -- kBoxDestBorderX is the
            // panel's OUTER-rim-sized corner cell, 8.0, wider than the
            // groove opening's real touch point, kGrooveOpeningInsetW
            // ~5.778 -- leaving a bare-groove margin between the gradient
            // and the rim). Sized directly rather than drawn full-width and
            // scissor-trimmed: unlike DrawFadeEdges' NineSlice draw (whose
            // corner CELL has its own internal touch-point offset baked
            // into its art), this is a plain quad with no such internal
            // positioning, and the row-highlight scissor is deliberately
            // X-unbounded (see its comment -- clips vertically only, so a
            // straddling row's box is cut at the top/bottom viewport edge
            // without being cut on its sides), so this quad's OWN width is
            // what determines its left/right extent. Falls back to the
            // NineSlice box highlight (its own pre-existing pad-based
            // width) if the caller never injected list_item.tex.
            if (m_ItemTex.IsValid()) {
                float itemW = m_BarW - 2.0f * kGrooveOpeningInsetW;
                DrawGlyphQuad(m_ItemTex.Get(), pos.x, rowCy, itemW, m_RowH, rowTint);
            } else {
                DrawBox(pos.x, rowCy, m_BarW - 2.0f * pad, m_RowH, rowTint);
            }
        }

        DrawText((*m_pItems)[idx].c_str(),
                  pos.x - m_BarW * 0.5f + textPad, rowCy + lineH * 0.5f,
                  m_TextScale, m_RowTextColour, &textClip);
    }

    if (Renderer* r = Renderer::GetInstance()) r->ClearClipRect();

    DrawFadeEdges(panelTopY, panelH);
}

