#ifndef MORTAR_INPUT_EVENT_H
#define MORTAR_INPUT_EVENT_H

#include <cstddef>
#include <cstdint>

//
// InputEvent — the 0x14-byte value type every Mortar input path passes around.
//
// Ground truth is its two producers:
//   ASM-spec v1.6.1 Mortar::InputDevice::AxisEvent      @0x0027582c
//   ASM-spec v1.6.1 Mortar::InputDevice::ButtonPressed  @0x00275864
// Both pack the struct on the stack and tail into InputDevice::CheckActions
// @0x002757fc -> InputActionMapper::ProcessEvent @0x00275728.
//
//  off   size  field       axis event (AxisEvent)        button event (ButtonPressed)
//  ----  ----  ----------  ----------------------------  ----------------------------
//  0x00   4    m_Flags     mask | 0x20000                mask | 0x10000
//  0x04   2    m_Tag       param6                        (whole word 0x04 = param5)
//  0x06   2    m_KeyCode   uxtb(axisId)                  -- (part of the tag word)
//  0x08   4    m_Value     float axis value              key id (0x89 Touch1, ...)
//  0x0c   4    m_Delta     float delta                   float value arg
//  0x10   4    m_Stamp     param5                        param4
//
// NB on the parameter numbering above: AxisEvent/ButtonPressed params are counted
// 1-based EXCLUDING `this`. Only that reading makes the axis m_Stamp=param5 /
// m_Tag=param6 pair agree with the button m_Stamp=param4 / tag-word=param5 pair.
// The other reading silently swaps m_Delta and m_Stamp.
//
// m_Flags splits in half:
//   high 16 — arm selector. ProcessEvent switches on `m_Flags & 0xffff0000`.
//   low  16 — action mask (InputManager::ParseAction @0x00244060).
//
// Reading a value out of an event:
//   axis   — m_KeyCode names the axis, m_Value is the position, m_Delta the step.
//   button — m_Value is the key id; m_Flags' low half says pressed/down/released/up.
//
struct InputEvent {
    uint32_t m_Flags;      // +0x00  arm selector (hi16) | action mask (lo16)
    uint16_t m_Tag;        // +0x04
    uint16_t m_KeyCode;    // +0x06  axis events only; uxtb(axisId)
    union {                // +0x08
        float    m_Value;  //        axis events: the axis value
        uint32_t m_KeyId;  //        button events: the key id (0x89 = Touch1, ...)
    };
    float    m_Delta;      // +0x0c
    uint32_t m_Stamp;      // +0x10
};

#if defined(__bada__)
static_assert(offsetof(InputEvent, m_Flags)   == 0x00, "InputEvent::m_Flags offset");
static_assert(offsetof(InputEvent, m_Tag)     == 0x04, "InputEvent::m_Tag offset");
static_assert(offsetof(InputEvent, m_KeyCode) == 0x06, "InputEvent::m_KeyCode offset");
static_assert(offsetof(InputEvent, m_Value)   == 0x08, "InputEvent::m_Value offset");
static_assert(offsetof(InputEvent, m_KeyId)   == 0x08, "InputEvent::m_KeyId offset");
static_assert(offsetof(InputEvent, m_Delta)   == 0x0c, "InputEvent::m_Delta offset");
static_assert(offsetof(InputEvent, m_Stamp)   == 0x10, "InputEvent::m_Stamp offset");
static_assert(sizeof(InputEvent) == 0x14, "InputEvent size mismatch");
#endif

// --- m_Flags high half: arm selector ---------------------------------------
// ProcessEvent @0x00275728 switches on these three values.
static const uint32_t INPUT_ARM_BUTTON = 0x10000;  // ButtonPressed @0x00275864
static const uint32_t INPUT_ARM_AXIS   = 0x20000;  // AxisEvent @0x0027582c
// TODO: v1.6.1 0x00275728 (Mortar::InputActionMapper::ProcessEvent) — the third arm
//   selector. ProcessEvent has a compare against it, but neither AxisEvent nor
//   ButtonPressed can produce it, so its producer is unidentified. It was previously
//   mis-named INPUT_ACTION_UP; "up" is an action MASK (0x08) on the button arm, not
//   an arm of its own.
static const uint32_t INPUT_ARM_UNKNOWN = 0x80000;

// --- m_Flags low half: action mask -----------------------------------------
// v1.6.1 Mortar::InputManager::ParseAction @0x00244060, table @0x002d8fbc.
static const uint32_t INPUT_MASK_PRESSED  = 0x01;
static const uint32_t INPUT_MASK_DOWN     = 0x02;
static const uint32_t INPUT_MASK_RELEASED = 0x04;
static const uint32_t INPUT_MASK_UP       = 0x08;
static const uint32_t INPUT_MASK_ACTIVE   = 0x10;
static const uint32_t INPUT_MASK_MOVE     = 0x20;
static const uint32_t INPUT_MASK_DEAD     = 0x40;

// Composite m_Flags values Touch::SendIndividualTouchCallbacks @0x00242bc4 and
// InputDeviceBada::Update @0x00242f40 raise through AxisEvent / ButtonPressed.
static const uint32_t INPUT_ACTION_DOWN = INPUT_ARM_BUTTON | INPUT_MASK_DOWN;  // 0x10002
static const uint32_t INPUT_ACTION_MOVE = INPUT_ARM_AXIS   | INPUT_MASK_MOVE;  // 0x20020
static const uint32_t INPUT_ACTION_UP   = INPUT_ARM_BUTTON | INPUT_MASK_UP;    // 0x10008

// --- Key codes -------------------------------------------------------------
// v1.6.1 Mortar::InputManager::ParseKey @0x002438c8, table @0x002d8dd4.
// Touch/axis codes are contiguous per channel: Touch<n+1> = INPUT_KEY_TOUCH1 + n.
static const uint16_t INPUT_KEY_MOUSEBUTTON1 = 0x6c;
static const uint16_t INPUT_KEY_MOUSEAXISX   = 0x74;
static const uint16_t INPUT_KEY_MOUSEAXISY   = 0x75;
static const uint16_t INPUT_KEY_TOUCH1       = 0x89;  // Touch1..16      = 0x89..0x98
static const uint16_t INPUT_KEY_TOUCHAXISX1  = 0x99;  // TouchAxisX1..16 = 0x99..0xa8
static const uint16_t INPUT_KEY_TOUCHAXISY1  = 0xa9;  // TouchAxisY1..16 = 0xa9..0xb8

#if !defined(__bada__)
// Port specific: TEST-ONLY event builders. Live dispatch never uses these — it
// goes through Mortar::InputDevice::AxisEvent @0x0027582c /
// Mortar::InputDevice::ButtonPressed @0x00275864, which pack the very same words.
// They exist so a unit test can drive SlashEntity / a callback directly without
// standing up a device + mapper chain. Keep them byte-identical to what those two
// producers emit, or a test will pin a shape the game never sees.

// Touch<channel+1> button event, as ButtonPressed(0x89 + channel, mask, 1.0f, 0, 0)
// packs it. `flags` is INPUT_ACTION_DOWN or INPUT_ACTION_UP.
inline void FN_MakeTouchButtonEvent(InputEvent& ie, uint32_t flags, int channel) {
    ie.m_Flags   = flags;
    ie.m_Tag     = 0;
    ie.m_KeyCode = 0;
    ie.m_KeyId   = (uint32_t)(INPUT_KEY_TOUCH1 + channel);
    ie.m_Delta   = 1.0f;   // SendIndividualTouchCallbacks passes 1.0f as the value arg
    ie.m_Stamp   = 0;
}

// TouchAxisX<channel+1> / TouchAxisY<channel+1> axis event, as
// AxisEvent(0x99/0xa9 + channel, 0x20, value, delta, 0, 0) packs it.
inline void FN_MakeTouchAxisEvent(InputEvent& ie, int channel, bool yAxis,
                                  float value) {
    ie.m_Flags   = INPUT_ACTION_MOVE;
    ie.m_Tag     = 0;
    ie.m_KeyCode = (uint16_t)((yAxis ? INPUT_KEY_TOUCHAXISY1
                                     : INPUT_KEY_TOUCHAXISX1) + channel);
    ie.m_Value   = value;
    ie.m_Delta   = 0.0f;
    ie.m_Stamp   = 0;
}
#endif

#endif
