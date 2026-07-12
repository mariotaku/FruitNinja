//
// UiCheckbox -- Port specific: see header for rationale. No binary
// counterpart; port-only glue code, no // ASM-verified markers apply.
//

#include "UiCheckbox.h"

UiCheckbox::UiCheckbox(const Vec3& inPos, float side, bool checked)
    : UiWidget()
    , m_Side(side)
    , m_Checked(checked ? 1 : 0)
    , m_CheckGlyph()
{
    pos = inPos;
    SetSize(side * 0.5f, side * 0.5f);
}

UiCheckbox::~UiCheckbox() {
}

void UiCheckbox::Update(float dt) {
    (void)dt;
    switch (PollTouch()) {
        case kReleasedInside:
            m_Checked = (uint8_t)(m_Checked ^ 1);
            if (m_OnChange) {
                m_OnChange();
            }
            break;
        default:
            break;
    }
}

void UiCheckbox::Draw(float* hudScale) {
    (void)hudScale;

    // Checked state renders a slightly warmer tint on the box; tune later.
    Colour boxTint = m_Checked ? Colour(0xC0, 0xA0, 0x60) : m_Tint;
    DrawBox(pos.x, pos.y, m_Side, m_Side, boxTint);

    if (m_Checked && m_CheckGlyph.IsValid()) {
        DrawGlyphQuad(m_CheckGlyph.Get(), pos.x, pos.y, m_Side * 0.8f, m_Side * 0.8f, Colour::White);
    }
}
