#ifndef FN_HUD_SLIDER_CONTROL_H
#define FN_HUD_SLIDER_CONTROL_H

//
// SliderControl : HUDControl3d (sizeof 0xDC on ARM32)
//
// A horizontal value slider. Dead code in v1.6.1 (OptionsScreen was repurposed
// to PauseScreen, so no live call site instantiates it) but the class carries a
// complete real implementation, ported here faithfully.
//
// Binary (v1.6.1):
//   ctor                 @ 0x001b7474
//   Update               @ 0x001b7248
//   UpdateTouchPosition  @ 0x001b713c
//   Draw                 @ 0x001b792c
//   LoadContent          @ 0x001b7bc0
//   GetType (-> 5)       @ 0x001b8148
//
// Field layout was re-derived from the ctor + UpdateTouchPosition instruction
// stream: Ghidra's pre-existing struct names were swapped. Ground truth:
//   * The LIVE current value is at +0x88 (written by UpdateTouchPosition, compared
//     for change, drives the thumb X and the m_OnValueChanged fire).
//   * +0x8C is the track's horizontal extent (read-only in UpdateTouchPosition as
//     s12, *0.5 = X half-extent), NOT the live value.
//
// Contract / gotchas:
//   * Track/thumb sizes are computed in the ctor from the LOADED texture dims
//     (s_box / s_slider width & height). LoadContent MUST run before construction.
//   * Update hit-region: X half-extent = m_TrackWidth*0.5, Y half-extent = size.y*60*0.5.
//   * UpdateTouchPosition maps (pos.x - finger.x) across the track to [min,max]; the
//     mapping is ratio*(min+max)/2 (only exactly correct when min==0) -- binary quirk.
//   * m_LayerFlags = 0x400.
//

#include "HUDControl3d.h"
#include "engine/math/Vec3.h"
#include "engine/util/Delegate.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "engine/util/StringTable.h"
#include <cstdint>

class SliderControl : public HUDControl3d {
    friend struct SliderControlLayoutAssert;

private:
    // +0x7C: minimum value (binary: long)
    int32_t  m_MinValue;

    // +0x80: maximum value (binary: long)
    int32_t  m_MaxValue;

    // +0x84: font height (binary: ushort)
    uint16_t m_FontSize;

    // +0x86: alignment pad
    uint16_t _pad86;

    // +0x88: LIVE current value; written by UpdateTouchPosition, drives thumb + delegate.
    int32_t  m_CurrentValue;

    // +0x8C: track horizontal extent; ctor = s_box->GetWidth() * size.x.
    float    m_TrackWidth;

    // +0x90: track vertical extent; ctor = s_box->GetHeight() * size.y.
    float    m_TrackHeight;

    // +0x94: thumb horizontal extent; ctor = s_slider->GetWidth() * size.x.
    float    m_ThumbWidth;

    // +0x98: thumb vertical extent; ctor = s_slider->GetHeight() * size.y.
    float    m_ThumbHeight;

    // +0x9C: label (12-byte slot).
    // DIFFERS: original = Mortar::Utf8StringIterator m_Label (flat 12B; cursor/src/
    //   codepoint). The port's Mortar::Utf8StringIterator is 0x10 (adds a port-only
    //   m_Begin), so it does NOT fit the 12-byte slot. Port stores only the source
    //   string pointer (== the binary iterator's cursor field) + 8 bytes pad; Draw
    //   reconstructs a Utf8StringIterator from it. v1.6.1 SliderControl @ 0x001b7474
    const char* m_Label;      // +0x9C
    uint32_t    _labelPad[2]; // +0xA0..+0xA7

    // +0xA8: active touch slot; -1 when idle
    int32_t  m_TouchId;

    // +0xAC: live touch position (12 bytes)
    Vec3     m_TouchPos;

    // +0xB8: fires when m_CurrentValue changes (zero-arg, void return); 36B -> ends 0xDC.
    Mortar::Delegate0<void> m_OnValueChanged;

public:
    // Binary @ 0x001b7474 (C2) / 0x001b7594 (C1) -- Itanium ABI complete-object /
    // base-object constructor pair for a SINGLE user-declared signature:
    //   SliderControl(Vec3, Vec3, char const*, long, long, ushort, long)
    // ASM-verified: 2026-07-11T00:00Z v1.6.1 SliderControl::SliderControl @ 0x001b7474 /
    //   0x001b7594 (re-analyst) -- both bodies are logically identical (same field
    //   writes, same s_box/s_slider reads); NEITHER calls GETSTRING_CAST_0. Earlier
    //   port notes read this C1/C2 pair as two DISTINCT overloads (char* vs
    //   LocalizedString) -- that was a misread; there is only one binary ctor.
    SliderControl(Vec3 inPos, Vec3 inSize,
                  const char* label,
                  int32_t minValue, int32_t maxValue,
                  uint16_t fontSize, int32_t initialValue);

    // Port convenience overload -- NOT a distinct binary ctor (see the correction
    // above). Provided for call-site symmetry with CheckBox's genuine
    // ctor(LocalizedString) (0x00166ab8); resolves via GETSTRING_CAST_0 the same
    // way every other GETSTRING_CAST_0 call site in the codebase does.
    SliderControl(Vec3 inPos, Vec3 inSize,
                  LocalizedString locLabel,
                  int32_t minValue, int32_t maxValue,
                  uint16_t fontSize, int32_t initialValue);

    virtual ~SliderControl();

    // vtable slot 2 -- bx lr; no-op
    void Init() override;
    // vtable slot 3 -- bx lr; no-op
    void Release() override;
    // vtable slot 6 -- bx lr; no-op
    void PreDraw(float* hudScale) override;
    // vtable slot 7 -- Binary @ 0x001b792c (draws track + thumb quads + label)
    void Draw(float* hudScaleRaw) override;
    // vtable slot 10 -- Binary @ 0x001b7248 (touch state machine)
    void Update(float dt) override;
    // vtable slot 12 -- Binary @ 0x001b8148 (returns 5)
    int GetType() override;

    // Non-virtual -- updates the label pointer.
    void SetText(const char* str);

    // Static texture lifecycle. Binary @ 0x001b7bc0 / UnloadContent.
    // Loads "box.tex" (track -- shared with ComboBox/ListBox) + "slider_will.tex" (thumb).
    static void LoadContent();
    static void UnloadContent();

    // Port/test-only: inject substitute track/thumb textures (the faithful art is not
    // shipped in v1.6.1 -- see .cpp DIFFERS). Must be called BEFORE constructing a
    // SliderControl, because the ctor reads the texture dims to size track/thumb.
    // Always compiled (static slots live in this TU); no binary counterpart.
    static void SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& track,
                                   const Mortar::SmartPtr<Mortar::Texture>& thumb);

    int GetValue() const { return m_CurrentValue; }

    // Port/test-only: install the value-changed callback. The binary binds
    // m_OnValueChanged at the (dead) OptionsScreen construction site; there is no
    // public setter. Lets an interactive harness observe drags. No binary counterpart.
    void SetOnValueChangedForTest(const Mortar::Delegate0<void>& cb) { m_OnValueChanged = cb; }

    // Read-only geometry accessors (test/caller convenience -- computed once in
    // the ctor from the loaded track/thumb texture dims; see class header note).
    float TrackWidth()  const { return m_TrackWidth; }
    float TrackHeight() const { return m_TrackHeight; }
    float ThumbWidth()  const { return m_ThumbWidth; }
    float ThumbHeight() const { return m_ThumbHeight; }

private:
    // Private non-virtual helper called by Update while touch is held.
    // Binary @ 0x001b713c -- maps finger position to m_CurrentValue, fires
    // m_OnValueChanged when the value changes.
    void UpdateTouchPosition();

public:
    // HUDControl override -- single bx lr; no-op.
    void UpdateFromGameWork();
};

#if defined(__bada__)
#include <cstddef>
struct SliderControlLayoutAssert {
    static_assert(sizeof(SliderControl) == 0xDC, "SliderControl size mismatch");   // v1.6.1 ctor @0x001b7474 (Delegate0 @ +0xB8)
    static_assert(offsetof(SliderControl, m_MinValue)       == 0x7C, "m_MinValue offset");
    static_assert(offsetof(SliderControl, m_MaxValue)       == 0x80, "m_MaxValue offset");
    static_assert(offsetof(SliderControl, m_FontSize)       == 0x84, "m_FontSize offset");
    static_assert(offsetof(SliderControl, m_CurrentValue)   == 0x88, "m_CurrentValue offset");
    static_assert(offsetof(SliderControl, m_TrackWidth)     == 0x8C, "m_TrackWidth offset");
    static_assert(offsetof(SliderControl, m_TrackHeight)    == 0x90, "m_TrackHeight offset");
    static_assert(offsetof(SliderControl, m_ThumbWidth)     == 0x94, "m_ThumbWidth offset");
    static_assert(offsetof(SliderControl, m_ThumbHeight)    == 0x98, "m_ThumbHeight offset");
    static_assert(offsetof(SliderControl, m_Label)          == 0x9C, "m_Label offset");
    static_assert(offsetof(SliderControl, m_TouchId)        == 0xA8, "m_TouchId offset");
    static_assert(offsetof(SliderControl, m_TouchPos)       == 0xAC, "m_TouchPos offset");
    static_assert(offsetof(SliderControl, m_OnValueChanged) == 0xB8, "m_OnValueChanged offset");
};
#endif

#endif // FN_HUD_SLIDER_CONTROL_H
