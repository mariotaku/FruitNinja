#ifndef FN_UI_CHECKBOX_H
#define FN_UI_CHECKBOX_H

//
// UiCheckbox -- Port specific: clean-slate checkbox widget for the settings
// toolkit (src/ui/). NO binary counterpart -- see src/ui/UiWidget.h for why
// this toolkit exists instead of resurrecting the binary's dead CheckBox
// class (src/hud/CheckBox.h): that class's hit-rect is hardcoded independent
// of its drawn size, forcing empirical padding-offset workarounds. UiCheckbox
// instead uses ITS OWN pos +/- half-extent as both the drawn box size and the
// hit-rect, so there's nothing to back-solve.
//
// Usage:
//   UiCheckbox cb(Vec3(x, y, 0), 32.0f, false);
//   cb.SetBoxTexture(boxTex);       // required for DrawBox to render anything
//   cb.SetCheckGlyph(checkTex);     // optional; only drawn when checked
//   cb.SetOnChange(Delegate0<void>::Make(this, &Screen::OnToggle));
//   // every frame: cb.Update(dt); cb.Draw(hudScale);
//
// Tap-release inside the box toggles m_Checked and fires the OnChange
// delegate (installed via UiWidget::SetOnChange). No binary counterpart --
// this whole file is port-only glue code.
//

#include "UiWidget.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstdint>

class UiCheckbox : public UiWidget {
public:
    UiCheckbox(const Vec3& pos, float side = 32.0f, bool checked = false);
    virtual ~UiCheckbox();

    void Update(float dt) override;
    void Draw(float* hudScale) override;

    void SetChecked(bool checked) { m_Checked = checked ? 1 : 0; }
    bool IsChecked() const { return m_Checked != 0; }

    void SetCheckGlyph(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_CheckGlyph = tex; }

    // Alias for UiWidget::SetOnChange -- reads clearer at checkbox call sites.
    void SetOnToggle(const Mortar::Delegate0<void>& cb) { SetOnChange(cb); }

    void Release() override { m_CheckGlyph.SetNull(); UiWidget::Release(); }

private:
    float   m_Side;
    uint8_t m_Checked;
    Mortar::SmartPtr<Mortar::Texture> m_CheckGlyph;
};

#endif // FN_UI_CHECKBOX_H
