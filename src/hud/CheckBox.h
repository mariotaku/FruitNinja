#ifndef FN_HUD_CHECKBOX_H
#define FN_HUD_CHECKBOX_H

//
// CheckBox : HUDControl3d (sizeof 0xB8 on ARM32)
//
// A latching two-state toggle. Dead code in v1.6.1 (the settings screen that
// used it was repurposed to PauseScreen) but the class carries a complete real
// implementation, ported here faithfully.
//
// Binary (v1.6.1):
//   ctor(char*)          @ 0x00166a10
//   ctor(LocalizedString)@ 0x00166ab8
//   Update               @ 0x00166c24
//   UpdateTouchPosition  @ 0x001667b8
//   Draw                 @ 0x001672f8
//   GetType (-> 5)       @ 0x001674d8
//   LoadContent          @ 0x00167214
//
// Not in namespace Mortar -- symbol-diff confirms bare CheckBox:: prefix.
//
// Contract / gotchas:
//   * Hit-rect is HARDCODED pos.x +/- 36, pos.y +/- 28.5 (NOT pos +/- size).
//   * Update captures the PRESS position (game_work.m_FingerSpawnPos[slot]) into
//     m_TouchCapture every held frame; the toggle on release tests that CAPTURED
//     press position against the rect (not the live release position), then fires
//     m_OnToggle.
//   * Draw scales a HARDCODED 128x64 quad at pos, texture = checked/unchecked.
//   * LoadContent must run before Draw or the textures are null (Draw no-ops).
//

#include "HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "engine/util/StringTable.h"
#include "engine/math/Vec3.h"
#include "engine/util/Delegate.h"
#include <cstdint>

class CheckBox : public HUDControl3d {
public:
    // Binary @ 0x00166a10
    CheckBox(Vec3 pos, Vec3 size, const char* label);

    // Binary @ 0x00166ab8 -- LocalizedString overload (second binary ctor).
    // ASM-verified 0x00166ab8: resolves `loc` via GETSTRING_CAST_0(loc), then
    // tail-calls the char* ctor with the resolved string (same pattern every
    // other GETSTRING_CAST_0 call site in the codebase uses).
    // LocalizedString here is the engine's string-table-ID enum
    // (engine/util/StringTable.h), NOT a port-local wrapper type.
    CheckBox(Vec3 pos, Vec3 size, LocalizedString loc);

    virtual ~CheckBox();

    // Vtable overrides -- match HUDControl vtable slot order.
    virtual void   Init()    override;                              // Binary @ 0x00166964 area (no-op)
    virtual void   Release() override;                              // no-op
    virtual void   PreDraw(float* hudScaleRaw) override;            // no-op
    virtual void   Update(float dt) override;                       // Binary @ 0x00166c24
    virtual void   Draw(float* hudScaleRaw) override;               // Binary @ 0x001672f8
    virtual int    GetType() override;                              // Binary @ 0x001674d8 (returns 5)

    // Non-virtual helpers
    void UpdateTouchPosition();   // Binary @ 0x001667b8
    void UpdateFromGameWork();    // empty in binary too

    // Statics
    static void LoadContent();    // Binary @ 0x00167214
    static void UnloadContent();

    // Port/test-only: inject substitute textures into the static slots (the faithful
    // checked.tex/unchecked.tex art is not shipped in v1.6.1 -- see .cpp DIFFERS).
    // Always compiled (the static slots live in this TU; a test built without the
    // library's compile flags must still resolve this symbol). No binary counterpart.
    static void SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& checked,
                                   const Mortar::SmartPtr<Mortar::Texture>& unchecked);

    // Query current toggle state (test / caller convenience).
    bool IsChecked() const { return m_Checked != 0; }

    // Port/test-only: force the toggle state (binary has no public setter; the
    // state only flips via the touch-release toggle in Update). No binary counterpart.
    void SetCheckedForTest(bool checked) { m_Checked = checked ? 1 : 0; }

    // Port/test-only: install the on-toggle callback. The binary binds m_OnToggle
    // at the (dead) OptionsScreen construction site; there is no public setter.
    // Lets an interactive harness observe the real toggle event. No binary counterpart.
    void SetOnToggleForTest(const Mortar::Delegate0<void>& cb) { m_OnToggle = cb; }

    friend struct CheckBoxLayoutAssert;

private:
    uint8_t     m_Checked;         // +0x7C  default 1 = CHECKED
    uint8_t     _pad7D[3];         // +0x7D..+0x7F
    const char* m_Label;           // +0x80
    int32_t     m_TouchId;         // +0x84 (-1 = none)
    Vec3        m_TouchCapture;    // +0x88 captured press pos (game_work.m_FingerSpawnPos[id])
    Mortar::Delegate0<void> m_OnToggle;  // +0x94 fires on toggle (36B) -> ends 0xB8

    static Mortar::SmartPtr<Mortar::Texture> s_checked;
    static Mortar::SmartPtr<Mortar::Texture> s_unchecked;
};

#if defined(__bada__)
#include <cstddef>
struct CheckBoxLayoutAssert {
    static_assert(sizeof(CheckBox) == 0xB8, "CheckBox size mismatch");            // v1.6.1 ctor @0x00166a10 (Delegate0 @ +0x94)
    static_assert(offsetof(CheckBox, m_Checked)      == 0x7C, "CheckBox::m_Checked offset");
    static_assert(offsetof(CheckBox, m_Label)        == 0x80, "CheckBox::m_Label offset");
    static_assert(offsetof(CheckBox, m_TouchId)      == 0x84, "CheckBox::m_TouchId offset");
    static_assert(offsetof(CheckBox, m_TouchCapture) == 0x88, "CheckBox::m_TouchCapture offset");
    static_assert(offsetof(CheckBox, m_OnToggle)     == 0x94, "CheckBox::m_OnToggle offset");
};
#endif

#endif // FN_HUD_CHECKBOX_H
