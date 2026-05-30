#include "InputDevice.h"
#include <cstring>

namespace Mortar {

// Binary @ 0x001b356c — InputActionMapper ctor.
InputActionMapper::InputActionMapper()
    : m_enabled(true)
    , field4_0x4(0)
    , field5_0x8(0)
    , field6_0xc(0)
    , field7_0x10(0)
    , field8_0x14(0)
    , field9_0x18(0)
    , field10_0x1c(0)
{
}

// Binary @ 0x001b3508 — ProcessEvent.
// TODO: 0x001b3508 — fire m_callback if event matches; full body not yet ported.
void InputActionMapper::ProcessEvent(InputEvent* /*event*/) {
}

// Binary @ 0x001b3794 — InputDevice ctor: set fns ptr, list ctor, list clear.
// Port: standard C++ ctor (vptr set by compiler, actionMappers default-constructed).
InputDevice::InputDevice() {
}

// Binary @ 0x001b3744 — InputDevice dtor.
InputDevice::~InputDevice() {
}

// Binary @ 0x???? — Destroy stub.
void InputDevice::Destroy() {}

// Binary @ 0x???? — ClearActions stub.
void InputDevice::ClearActions(unsigned long, bool) {}

// Binary @ 0x???? — RegisterInputCallback stub.
void InputDevice::RegisterInputCallback(unsigned long, InputDeviceCallback) {}

// Binary @ 0x???? — AxisEvent stub.
void InputDevice::AxisEvent(long, unsigned long, float, float, unsigned long, long) {}

// Binary @ 0x???? — ButtonPressed stub.
void InputDevice::ButtonPressed(unsigned long, unsigned long, float, unsigned long, long) {}

// Binary @ 0x001b36b0 — CheckActions: iterate actionMappers list, call ProcessEvent.
void InputDevice::CheckActions(InputEvent* event) {
    for (std::list<InputActionMapper*>::iterator it = actionMappers.begin();
         it != actionMappers.end(); ++it) {
        (*it)->ProcessEvent(event);
    }
}

} // namespace Mortar
