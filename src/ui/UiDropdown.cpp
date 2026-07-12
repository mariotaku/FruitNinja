//
// UiDropdown -- Port specific: see header for rationale. No binary
// counterpart; port-only glue code, no // ASM-verified markers apply.
//

#include "UiDropdown.h"

#include "engine/input/Touch.h"
#include "game/GameWork.h"

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

UiDropdown::UiDropdown(const Vec3& inPos, std::vector<std::string>& items, int selected,
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
    // [-160,160], y in [-240,240] -- see docs/engine/coordinate-system.md)
    // means an outside press is latched here first; Release below already
    // closes without selecting when the captured position isn't inside the
    // panel rows.
    if (m_TouchId == -1) {
        int slot = TouchInRegion(-160.0f, 160.0f, -240.0f, 240.0f, -1);
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
        bool insidePanelRows =
            m_TouchCapture.y <= panelTopY - pad &&
            m_TouchCapture.y >= panelTopY - pad - panelRows * m_RowH &&
            m_TouchCapture.x >= openLeft && m_TouchCapture.x <= openRight;

        float absVel = m_PendingVel < 0.0f ? -m_PendingVel : m_PendingVel;
        bool isTap = !m_bDragging && m_DragDist < DRAG_CANCEL_DIST && absVel < CLICK_VEL_GATE;

        if (isTap && insidePanelRows) {
            int idx = ClampInt((int)std::floor((curYBase - m_TouchCapture.y) / m_RowH),
                                0, (int)m_pItems->size() - 1);
            m_Selected = idx;
            m_Open = false;
            if (m_OnChange) {
                m_OnChange();
            }
        } else if (!insidePanelRows && !m_bDragging) {
            // Released outside the panel without ever dragging -> close.
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
            int hoverRow = (int)std::floor((curYBase - currentY) / m_RowH);
            m_HoverRow = ClampInt(hoverRow, 0, panelRows - 1);
        } else {
            // A drag is scrolling, not hovering.
            m_HoverRow = -1;
        }
    }

    // --- Integrate + friction (every frame, held or not) ---
    m_PendingVel *= SCROLL_FRICTION;
    m_ScrollOffset += m_PendingVel;

    // --- Spring-back at bounds (only while not touching) ---
    // Past top (offset<0, before item 0) -> spring toward 0.
    // Past bottom (offset>maxScroll, past the last row) -> spring toward maxScroll.
    if (m_TouchId == -1) {
        float maxScroll = ComputeMaxScroll();
        if (m_ScrollOffset < 0.0f) {
            m_ScrollOffset *= SPRING_BACK_COEF;
        } else if (m_ScrollOffset > maxScroll) {
            m_ScrollOffset += (maxScroll - m_ScrollOffset) * SPRING_FWD_COEF;
        }
    }
}

void UiDropdown::Draw(float* hudScale) {
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
        DrawGlyphQuad(m_CaretTex.Get(), pos.x + m_BarW * 0.5f - caretHalf - 2.0f, pos.y,
                      caretSize, caretSize, Colour::White);
    }

    if (!m_Open) {
        return;
    }

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

    // Row text clip rect = panel inner viewport, in the same centered-ortho
    // world space Font::DrawString's pos/x/y already use (see Font::DrawString
    // @0x0024c7f0's clipRect path). The row-culling test above only skips rows
    // whose box is fully outside the viewport; a row straddling the top/bottom
    // edge still draws here, so its text needs a real per-glyph clip or it
    // spills past the panel box.
    Mortar::MortarRectangleT<float> textClip;
    textClip.left   = pos.x - m_BarW * 0.5f + textPad;
    textClip.right  = pos.x + m_BarW * 0.5f - textPad;
    textClip.top    = viewportTop;
    textClip.bottom = viewportBottom;

    for (int idx = 0; idx < (int)m_pItems->size(); ++idx) {
        float rowCy = curYBase - m_RowH * (idx + 0.5f);
        float rowTop = rowCy + m_RowH * 0.5f;
        float rowBottom = rowCy - m_RowH * 0.5f;
        if (rowTop < viewportBottom || rowBottom > viewportTop) {
            continue;
        }

        // Geometric clamp: NineSlice::Draw/Mesh::DrawQuadUnCached have no
        // scissor/clip param (raw quad geometry, unlike Font::DrawString's
        // per-glyph clipRect), so a row straddling the viewport top/bottom
        // edge is clamped here by shrinking the box to the visible band
        // (new centre/height from the clamped top/bottom) rather than
        // drawing its full m_RowH and spilling past the panel.
        float clampedTop = rowTop < viewportTop ? rowTop : viewportTop;
        float clampedBottom = rowBottom > viewportBottom ? rowBottom : viewportBottom;
        float clampedCy = (clampedTop + clampedBottom) * 0.5f;
        float clampedH = clampedTop - clampedBottom;

        if (m_HoverRow >= 0 && idx == firstVisibleIdx + m_HoverRow) {
            DrawBox(pos.x, clampedCy, m_BarW - 2.0f * pad, clampedH, m_HoverRowColour);
        } else if (idx == m_Selected) {
            DrawBox(pos.x, clampedCy, m_BarW - 2.0f * pad, clampedH, m_SelRowColour);
        }

        DrawText((*m_pItems)[idx].c_str(),
                  pos.x - m_BarW * 0.5f + textPad, rowCy + lineH * 0.5f,
                  m_TextScale, m_RowTextColour, &textClip);
    }
}
