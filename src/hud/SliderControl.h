#ifndef FN_HUD_SLIDER_CONTROL_H
#define FN_HUD_SLIDER_CONTROL_H

// Defunct: SliderControl -- orphaned in binary; binary vtable @ 0x001ea090.
// Zero internal call sites; every public symbol has only [EXTERNAL] xrefs.
// OptionsScreen was repurposed to PauseScreen, leaving the slider library
// code unused. Class shape preserved per stub-don't-skip policy.

#include "HUDControl3d.h"
#include "engine/math/Vec3.h"
#include "engine/util/Delegate.h"
#include <cstdint>

class SliderControl : public HUDControl3d {
public:
    // +0x7C: minimum value; binary: long
    int32_t  m_MinValue;

    // +0x80: maximum value; binary: long
    int32_t  m_MaxValue;

    // +0x84: font height (2-byte; binary: uint16/ushort)
    uint16_t m_FontSize;

    // +0x86: alignment pad
    uint16_t _pad86;

    // +0x88: current value; also written by UpdateTouchPosition
    int32_t  m_CurrentValue;

    // +0x8C: track horizontal half-extent; computed in ctor as font_glyph_w * size.x
    float    m_TrackWidth;

    // +0x90: track vertical half-extent; computed in ctor as font_glyph_h * size.y
    float    m_TrackHeight;

    // +0x94: thumb horizontal half-extent; computed from second font * size.x
    float    m_ThumbWidth;

    // +0x98: thumb vertical half-extent; computed from second font * size.y
    float    m_ThumbHeight;

    // +0x9C: label text (28 bytes).
    // DIFFERS: original = Mortar::Utf8StringIterator m_Label (28B); port uses
    // char[28] because Mortar::Utf8StringIterator is not yet ported. Binary ctor at 0x00160268.
    char     m_Label[28];

    // +0xB8: active touch slot; -1 when idle
    int32_t  m_TouchId;

    // +0xBC: live touch position (12 bytes)
    Vec3     m_TouchPos;

    // +0xC8: fires when m_CurrentValue changes (zero-arg, void return)
    // Binary: Mortar::Delegate0<void> (36 bytes)
    Delegate0<void> m_OnValueChanged;

    // Binary @ 0x00160268 (master ctor)
    // Signature: (Vec3 pos, Vec3 size, const char* label,
    //             long min_val, long max_val, ushort font_size, long initial_value)
    SliderControl(const Vec3& inPos, const Vec3& inSize,
                  const char* label,
                  int32_t minValue, int32_t maxValue,
                  uint16_t fontSize, int32_t initialValue);

    // Binary @ 0x001601a8 (D2) / 0x00160140 (D1/deleting)
    virtual ~SliderControl();

    // vtable slot 2 -- Binary @ 0x0015ffa0 (bx lr; no-op)
    void Init() override;

    // vtable slot 3 -- Binary @ 0x0015ffa4 (bx lr; no-op)
    void Release() override;

    // vtable slot 6 -- Binary @ 0x0015ffa8 (bx lr; no-op)
    void PreDraw(const Vec3& hudScale) override;

    // vtable slot 7 -- Binary @ 0x0016069c (~224 instructions; draws track+thumb quads + label)
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable slot 10 -- Binary @ 0x00160090 (touch state machine)
    void Update(float dt) override;

    // vtable slot 12 -- Binary @ 0x00160c8c (mov r0,#5; bx lr)
    int GetType() override;

    // Non-virtual. Binary @ 0x0016010c -- updates m_Label string
    void SetText(const char* str);

    // Static texture lifecycle. Binary @ 0x00160890 / 0x0016090c.
    // Loads "box.tex" + "slider_will.tex" via TextureManager (stub: no-op).
    static void LoadContent();
    static void UnloadContent();

private:
    // Non-virtual private helper; called by Update while touch is held.
    // Binary @ 0x0015ffb0 (~80 instructions; maps touch position to m_CurrentValue)
    void UpdateTouchPosition();
};

#ifdef __bada__
static_assert(sizeof(SliderControl) == 0xec, "SliderControl size mismatch");
static_assert(__builtin_offsetof(SliderControl, m_MinValue)       == 0x7C, "m_MinValue offset");
static_assert(__builtin_offsetof(SliderControl, m_MaxValue)       == 0x80, "m_MaxValue offset");
static_assert(__builtin_offsetof(SliderControl, m_FontSize)       == 0x84, "m_FontSize offset");
static_assert(__builtin_offsetof(SliderControl, m_CurrentValue)   == 0x88, "m_CurrentValue offset");
static_assert(__builtin_offsetof(SliderControl, m_TrackWidth)     == 0x8C, "m_TrackWidth offset");
static_assert(__builtin_offsetof(SliderControl, m_TrackHeight)    == 0x90, "m_TrackHeight offset");
static_assert(__builtin_offsetof(SliderControl, m_ThumbWidth)     == 0x94, "m_ThumbWidth offset");
static_assert(__builtin_offsetof(SliderControl, m_ThumbHeight)    == 0x98, "m_ThumbHeight offset");
static_assert(__builtin_offsetof(SliderControl, m_Label)          == 0x9C, "m_Label offset");
static_assert(__builtin_offsetof(SliderControl, m_TouchId)        == 0xB8, "m_TouchId offset");
static_assert(__builtin_offsetof(SliderControl, m_TouchPos)       == 0xBC, "m_TouchPos offset");
static_assert(__builtin_offsetof(SliderControl, m_OnValueChanged) == 0xC8, "m_OnValueChanged offset");
#endif

#endif // FN_HUD_SLIDER_CONTROL_H
