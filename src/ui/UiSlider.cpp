//
// UiSlider -- Port specific: see header for rationale. No binary
// counterpart; port-only glue code, no // ASM-verified markers apply.
//

#include "UiSlider.h"

#include <cmath>

UiSlider::UiSlider(const _Vector3<float>& inPos, int minV, int maxV, int value)
    : UiWidget()
    , m_Min(minV)
    , m_Max(maxV)
    , m_Value(value)
    , m_TrackW(120.0f)
    , m_TrackH(16.0f)
    , m_KnobD(32.0f)
    , m_Steps(0)
    , m_Detent(-1)
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
    m_Value = SnapValue(value);
}

void UiSlider::SetSteps(int segments) {
    m_Steps = segments;
    m_Value = SnapValue(m_Value);
}

void UiSlider::SetDetent(int value) {
    m_Detent = value;
    m_Value = SnapValue(m_Value);
}

int UiSlider::SnapValue(int rawValue) const {
    int v = rawValue;
    if (v < m_Min) v = m_Min;
    if (v > m_Max) v = m_Max;

    if (m_Steps > 0 && m_Max > m_Min) {
        float step = (float)(m_Max - m_Min) / (float)m_Steps;
        float t = (float)(v - m_Min) / step;
        int stopIdx = (int)(t + 0.5f);
        v = m_Min + (int)(stopIdx * step + 0.5f);
        if (v < m_Min) v = m_Min;
        if (v > m_Max) v = m_Max;

        if (m_Detent >= 0) {
            float window = step;
            if (std::fabs((float)(v - m_Detent)) <= window) {
                v = m_Detent;
            }
        }
    }

    return v;
}

float UiSlider::ValueToTrackX(int value) const {
    float t = (m_Max > m_Min) ? (float)(value - m_Min) / (float)(m_Max - m_Min) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return pos.x - m_TrackW * 0.5f + m_KnobD * 0.5f + t * (m_TrackW - m_KnobD);
}

float UiSlider::ComputeKnobX() const {
    return ValueToTrackX(m_Value);
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
        int v = SnapValue(m_Min + (int)(tt * (float)(m_Max - m_Min) + 0.5f));
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

    if (m_Detent >= 0 && m_BoxTex.IsValid()) {
        static const Colour kDetentTint(0x3A, 0x22, 0x0C, 0xC0);
        DrawGlyphQuad(m_BoxTex.Get(), ValueToTrackX(m_Detent), pos.y, 3.0f, m_TrackH, kDetentTint);
    }

    if (m_KnobTex.IsValid()) {
        DrawGlyphQuad(m_KnobTex.Get(), ComputeKnobX(), pos.y, m_KnobD, m_KnobD, Colour::White);
    }
}
