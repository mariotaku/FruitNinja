//
// UiSlider -- Port specific: see header for rationale. No binary
// counterpart; port-only glue code, no // ASM-verified markers apply.
//

#include "UiSlider.h"

#include <cmath>

UiSlider::UiSlider(const Vec3& inPos, int minV, int maxV, int value)
    : UiWidget()
    , m_Min(minV)
    , m_Max(maxV)
    , m_Value(value)
    , m_TrackW(120.0f)
    , m_TrackH(16.0f)
    , m_KnobD(32.0f)
    , m_KnobTex()
{
    pos = inPos;
    SetTrackSize(m_TrackW, m_TrackH);
}

UiSlider::~UiSlider() {
}

void UiSlider::SetTrackSize(float w, float h) {
    m_TrackW = w;
    m_TrackH = h;
    float halfH = m_TrackH > m_KnobD ? m_TrackH : m_KnobD;
    SetSize(m_TrackW * 0.5f, halfH * 0.5f);
}

void UiSlider::SetValue(int value) {
    if (value < m_Min) value = m_Min;
    if (value > m_Max) value = m_Max;
    m_Value = value;
}

float UiSlider::ComputeKnobX() const {
    float t = (m_Max > m_Min) ? (float)(m_Value - m_Min) / (float)(m_Max - m_Min) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return pos.x - m_TrackW * 0.5f + m_KnobD * 0.5f + t * (m_TrackW - m_KnobD);
}

void UiSlider::Update(float dt) {
    (void)dt;

    PressResult r = PollTouch();
    if (r == kPressed || r == kHeld) {
        float travel = m_TrackW - m_KnobD;
        float tt = (travel > 0.0f)
            ? (m_TouchCapture.x - (pos.x - m_TrackW * 0.5f + m_KnobD * 0.5f)) / travel
            : 0.0f;
        if (tt < 0.0f) tt = 0.0f;
        if (tt > 1.0f) tt = 1.0f;
        int v = m_Min + (int)(tt * (float)(m_Max - m_Min) + 0.5f);
        if (v != m_Value) {
            m_Value = v;
            if (m_OnChange) {
                m_OnChange();
            }
        }
    }
}

void UiSlider::Draw(float* hudScale) {
    (void)hudScale;

    DrawBox(pos.x, pos.y, m_TrackW, m_TrackH, m_Tint);

    if (m_KnobTex.IsValid()) {
        DrawGlyphQuad(m_KnobTex.Get(), ComputeKnobX(), pos.y, m_KnobD, m_KnobD, Colour::White);
    }
}
