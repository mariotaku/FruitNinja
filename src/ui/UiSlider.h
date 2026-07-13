#ifndef FN_UI_SLIDER_H
#define FN_UI_SLIDER_H

//
// UiSlider -- Port specific: clean-slate slider widget for the settings
// toolkit (src/ui/). NO binary counterpart -- see src/ui/UiWidget.h for why
// this toolkit exists instead of resurrecting the binary's dead
// SliderControl class (src/hud/SliderControl.h).
//
// Usage:
//   UiSlider sl(Vec3(x, y, 0), 0, 100, 50);
//   sl.SetBoxTexture(boxTex);        // required for DrawBox groove
//   sl.SetKnobTexture(knobTex);      // required for the knob to render
//   sl.SetOnChange(Delegate0<void>::Make(this, &Screen::OnSliderChanged));
//   // every frame: sl.Update(dt); sl.Draw(hudScale);
//
// Value <-> knob-position mapping: t = (value - min) / (max - min) in
// [0, 1]; knob travels along the track's usable span (trackW - knobD) so
// the knob never overhangs the track ends. Tap or drag inside the hit-rect
// (which is oversized to cover the knob's overhang above/below the thin
// track) recomputes the value from the live touch x and fires OnChange
// (installed via UiWidget::SetOnChange) whenever the value changes.
//
// SetSteps(n): quantizes the value to n equal stops across [min,max]; 0
// (default) leaves the slider continuous. SetDetent(v): a magnetic snap
// point + visual tick at value v; -1 (default) disables it. Both may be
// called after construction -- the current value is re-snapped immediately.
//

#include "UiWidget.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class UiSlider : public UiWidget {
public:
    UiSlider(const Vec3& pos, int minV = 0, int maxV = 100, int value = 0);
    virtual ~UiSlider();

    void Update(float dt) override;
    void Draw(float* hudScale) override;

    void SetRange(int minV, int maxV) { m_Min = minV; m_Max = maxV; SetValue(m_Value); }
    int GetValue() const { return m_Value; }
    void SetValue(int value);

    // Updates both the drawn track size and the hit-rect half-extents.
    void SetTrackSize(float w, float h);
    void SetKnobTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_KnobTex = tex; }
    void SetKnobSize(float d) { m_KnobD = d; SetTrackSize(m_TrackW, m_TrackH); }

    // segments == 0 (default): continuous, GetValue() returns any raw int
    // in [min,max]. segments > 0: [min,max] is divided into `segments`
    // equal stops and GetValue() only ever returns a stop.
    void SetSteps(int segments);
    // value == -1 (default): no detent. Otherwise a magnetic snap point +
    // visual tick drawn on the track at that value.
    void SetDetent(int value);

    void Release() override { m_KnobTex.SetNull(); UiWidget::Release(); }

private:
    // Knob centre X for the current m_Value, in the same centered-ortho
    // space as pos. See header doc above for the t/travel derivation.
    float ComputeKnobX() const;
    // Same t/travel math as ComputeKnobX but for an arbitrary value --
    // shared by ComputeKnobX(m_Value) and the detent tick (m_Detent).
    float ValueToTrackX(int value) const;
    // Quantizes rawValue to the step grid (if m_Steps > 0), then pulls to
    // m_Detent if within the magnetic window (if m_Detent >= 0). Result is
    // clamped to [m_Min, m_Max].
    int SnapValue(int rawValue) const;

    int m_Min;
    int m_Max;
    int m_Value;
    float m_TrackW;
    float m_TrackH;
    float m_KnobD;
    int m_Steps;
    int m_Detent;
    Mortar::SmartPtr<Mortar::Texture> m_KnobTex;
};

#endif // FN_UI_SLIDER_H
