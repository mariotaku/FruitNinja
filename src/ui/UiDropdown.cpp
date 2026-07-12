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

UiDropdown::UiDropdown(const Vec3& inPos, std::vector<std::string>& items, int selected,
                       uint8_t visibleRows, float barW, float barH)
    : UiWidget()
    , m_pItems(&items)
    , m_Selected(selected)
    , m_Open(false)
    , m_ScrollTop(0)
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

int UiDropdown::ComputeMaxScroll() const {
    int count = (int)m_pItems->size();
    int maxScroll = count - (int)m_VisibleRows;
    return maxScroll > 0 ? maxScroll : 0;
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

void UiDropdown::SetScrollTopForTest(int scrollTop) {
    m_ScrollTop = ClampInt(scrollTop, 0, ComputeMaxScroll());
}

void UiDropdown::Update(float dt) {
    (void)dt;

    if (!m_Open) {
        if (PollTouch() == kReleasedInside) {
            m_Open = true;
            m_ScrollTop = ClampInt(m_Selected - (int)m_VisibleRows / 2, 0, ComputeMaxScroll());
            m_HoverRow = -1;
        }
        return;
    }

    int panelRows = m_pItems->size() < (size_t)m_VisibleRows
                        ? (int)m_pItems->size() : (int)m_VisibleRows;
    const float pad = 4.0f;
    float panelH = panelRows * m_RowH + 2.0f * pad;
    float panelTopY = pos.y - m_BarH * 0.5f;

    float openLeft = pos.x - m_BarW * 0.5f;
    float openRight = pos.x + m_BarW * 0.5f;
    float openTop = pos.y + m_BarH * 0.5f;
    float openBottom = panelTopY - panelH;

    if (m_TouchId == -1) {
        int slot = TouchInRegion(openLeft, openRight, openBottom, openTop, -1);
        if (slot == -1) {
            return;
        }
        if (IsTouchDown(slot) != 2) {
            return;
        }
        m_TouchId = slot;
        m_TouchCapture = game_work.m_FingerSpawnPos[slot];
        return;
    }

    if (IsTouchDown(m_TouchId) == 0) {
        bool insidePanelRows =
            m_TouchCapture.y <= panelTopY - pad &&
            m_TouchCapture.y >= panelTopY - pad - panelRows * m_RowH &&
            m_TouchCapture.x >= openLeft && m_TouchCapture.x <= openRight;

        if (insidePanelRows) {
            int row = ClampInt((int)std::floor((panelTopY - pad - m_TouchCapture.y) / m_RowH),
                                0, panelRows - 1);
            m_Selected = m_ScrollTop + row;
            m_Open = false;
            if (m_OnChange) {
                m_OnChange();
            }
        } else {
            m_Open = false;
        }
        m_TouchId = -1;
        m_HoverRow = -1;
    } else {
        m_TouchCapture = game_work.m_FingerSpawnPos[m_TouchId];

        int maxScroll = ComputeMaxScroll();
        float rowsTop = panelTopY - pad;
        float rowsBottom = panelTopY - pad - panelRows * m_RowH;

        if (m_TouchCapture.y > rowsTop) {
            m_HoverRow = -1;
            if (m_ScrollTop > 0) {
                m_ScrollTop = ClampInt(m_ScrollTop - 1, 0, maxScroll);
            }
        } else if (m_TouchCapture.y < rowsBottom) {
            m_HoverRow = -1;
            if (m_ScrollTop < maxScroll) {
                m_ScrollTop = ClampInt(m_ScrollTop + 1, 0, maxScroll);
            }
        } else {
            int hoverRow = (int)std::floor((rowsTop - m_TouchCapture.y) / m_RowH);
            m_HoverRow = ClampInt(hoverRow, 0, panelRows - 1);
        }
    }

    m_ScrollTop = ClampInt(m_ScrollTop, 0, ComputeMaxScroll());
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

    DrawBox(pos.x, panelCenterY, m_BarW, panelH, m_Tint);

    for (int i = 0; i < panelRows; ++i) {
        int idx = m_ScrollTop + i;
        float rowCy = panelTopY - pad - m_RowH * (i + 0.5f);

        if (i == m_HoverRow) {
            DrawBox(pos.x, rowCy, m_BarW - 2.0f * pad, m_RowH, m_HoverRowColour);
        } else if (idx == m_Selected) {
            DrawBox(pos.x, rowCy, m_BarW - 2.0f * pad, m_RowH, m_SelRowColour);
        }

        if (idx >= 0 && idx < (int)m_pItems->size()) {
            DrawText((*m_pItems)[idx].c_str(),
                      pos.x - m_BarW * 0.5f + textPad, rowCy + lineH * 0.5f,
                      m_TextScale, m_RowTextColour);
        }
    }
}
