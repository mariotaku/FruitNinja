#ifndef FN_ENGINE_INPUT_INPUTDEVICEBADA_H
#define FN_ENGINE_INPUT_INPUTDEVICEBADA_H

#include "input/InputDevice.h"
#include "input/Touch.h"
#include <list>
#include <cstdint>

// ASM-spec v1.6.1 InputDeviceBada @ 0x002427e0 — concrete InputDevice for Bada/touch.
// sizeof = 28 (0x1c), from operator new(0x1c) @ InputManager::Init 0x002447e0.
// Layout:
//   +0x00: vptr (port) / InputDevice base (binary fns*)
//   +0x04: std::list<InputActionMapper*> m_ActionMappers (8B, from InputDevice base)
//   +0x0c: uint32_t m_ActiveTouchId (ctor-zero)
//   +0x10: uint32_t m_LastTouchX    (ctor-zero)
//   +0x14: uint32_t m_LastTouchY    (ctor-zero)
//   +0x18: uint32_t m_EventStamp    (ctor-zero)
// Total: 12 (base) + 16 (derived) = 28.

namespace Mortar {

class InputDeviceBada : public InputDevice {
public:
    // ASM-spec v1.6.1 InputDeviceBada::InputDeviceBada @ 0x002427e0 — ctor (C1/C2 pair, byte-identical).
    InputDeviceBada();
    virtual ~InputDeviceBada();

    // InputDevice vtable (overrides matching binary slots):
    virtual void              Init(unsigned long flags);
    virtual void              Destroy();
    virtual void              Update(float dt);
    virtual void              AddActionMapper(InputActionMapper* mapper);
    virtual void              Reset();
    // Not overridden in the binary either: the Bada vtable (0x002d0468) shares the
    // base's (0x002d0f70) inline-empty `bx lr` bodies for these three. See the
    // SETTLED note in InputDeviceBada.cpp.
    virtual void              SetQueueEventsUntilUpdate(bool v);
    virtual void              SetSendDownCallbacksEachUpdate(bool v);
    virtual void              OnAxisExtentsChanged();
    virtual InputDeviceTypes  GetDeviceType();

    // Binary-faithful derived fields (ctor-zero). Names match the Ghidra
    // v1.6.1 InputDeviceBada struct (m_ActiveTouchId/m_LastTouchX/m_LastTouchY/
    // m_EventStamp); semantics confirmed by RE'ing InputDeviceBada::Update /
    // Reset:
    //   m_ActiveTouchId = active/tracked touch id (0 = none). Set from
    //               Touch::GetMostRecentTouch/GetAnyTouch; cleared on release.
    //   m_LastTouchX = last touch X (Ghidra types it as int/signed; kept as
    //               uint32_t to match the binary 4-byte slot, cast to int for
    //               delta math).
    //   m_LastTouchY = last touch Y (same signed/storage note as X).
    //   m_EventStamp = monotonically-incrementing event stamp; bumped once per
    //               Update() and passed as the timestamp arg to every emitted
    //               AxisEvent/ButtonPressed.
    uint32_t m_ActiveTouchId;  // +0x0c  active touch id (0 = none)
    uint32_t m_LastTouchX;     // +0x10  last touch X (signed in binary)
    uint32_t m_LastTouchY;     // +0x14  last touch Y (signed in binary)
    uint32_t m_EventStamp;     // +0x18  event stamp counter
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::InputDeviceBada) == 28, "InputDeviceBada size mismatch");
#endif

#endif // FN_ENGINE_INPUT_INPUTDEVICEBADA_H
