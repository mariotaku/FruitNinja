#include "InputDevice.h"
#include <cstring>

namespace Mortar {

// Binary @ 0x001b356c — InputActionMapper ctor.
InputActionMapper::InputActionMapper()
    : m_Enabled(true)
    , m_ActionHash(0)
    , m_ConfigSourceHash(0)
    , m_ActionMask(0)
    , m_MatchValue(0)
    , m_KeyMask(0)
    , m_Param4(0)
    , m_Param5(0)
{
}

// Binary @ 0x001b3508 — ProcessEvent.
// Filters the incoming event against this mapper's template and fires
// m_callback(event) when it matches. Control flow ported 1:1 from the binary:
//
//   eventWord = *(uint32*)event                      (InputEvent +0x00)
//   typeBits  = eventWord & 0xffff0000               (DOWN/MOVE/UP)
//   if ((typeBits & m_actionFilter) == 0) return;            // type must overlap
//   if ((eventWord & m_actionFilter & 0xffff) == 0) return;  // device mask must overlap
//   switch (typeBits):
//     0x20000 (MOVE): kc = m_matchValue >> 16;
//                     if (kc < 0x89)  match = (kc & event->keycode) != 0;  // bitmask finger set
//                     else            match = (event->keycode == kc);      // exact keycode
//     0x80000 (UP):   match = (event->keycode == (m_matchValue >> 16));
//     0x10000 (DOWN): match = (event->m_mapper == m_matchMapper);
//     default:        return;
//   if (match) m_callback(event);
//
// DIFFERS: the binary reads a 12-byte InputEvent (combined action-word at +0x00,
//   ushort keycode at +0x06, InputActionMapper* at +0x08). The port's InputEvent
//   reinterprets those bytes into named fields. eventWord maps to actionFlags
//   (carries the 0x10000/0x20000/0x80000 type bits), the +0x06 keycode maps to
//   InputEvent::keycode, and the +0x08 pointer maps to InputEvent::m_mapper.
//   The low-16 device-mask half of the binary's action-word lives in
//   InputEvent::actionFlags' low half. The whole InputActionMapper path is
//   preserved per stub-don't-skip; the port's live dispatch uses
//   InputDeviceBada's InputDeviceBinding list (see InputDeviceBada.cpp DIFFERS).
void InputActionMapper::ProcessEvent(InputEvent* event) {
    uint32_t eventWord = event->actionFlags;
    uint32_t typeBits  = eventWord & 0xffff0000u;

    if ((typeBits & m_ActionMask) == 0) {
        return;
    }
    if ((eventWord & m_ActionMask & 0xffffu) == 0) {
        return;
    }

    bool match;
    if (typeBits == INPUT_ACTION_MOVE) {            // 0x20000
        uint16_t kc = (uint16_t)(m_MatchValue >> 16);
        if (kc < 0x89) {
            match = ((kc & (uint16_t)event->keycode) != 0);
        } else {
            match = ((uint16_t)event->keycode == kc);
        }
    } else if (typeBits == INPUT_ACTION_UP) {       // 0x80000
        match = ((uint16_t)event->keycode == (uint16_t)(m_MatchValue >> 16));
    } else if (typeBits == INPUT_ACTION_DOWN) {     // 0x10000
        match = ((uint32_t)(uintptr_t)event->m_mapper == this->m_KeyMask);
    } else {
        return;
    }

    if (match) {
        m_callback(event);
    }
}

// Binary @ 0x001b3794 — InputDevice ctor: set fns ptr, list ctor, list clear.
// Port: standard C++ ctor (vptr set by compiler, m_ActionMappers default-constructed).
InputDevice::InputDevice() {
}

// Binary @ 0x001b3744 — InputDevice dtor.
InputDevice::~InputDevice() {
}

// ASM-spec v1.6.1 InputDevice::Destroy @0x00275938: m_ActionMappers.clear() (list nodes only, payloads borrowed).
void InputDevice::Destroy() {
    m_ActionMappers.clear();
}

// Binary @ 0x???? — ClearActions stub.
void InputDevice::ClearActions(unsigned long, bool) {}

// Binary @ 0x???? — RegisterInputCallback stub.
void InputDevice::RegisterInputCallback(unsigned long, InputDeviceCallback) {}

// Binary @ 0x???? — AxisEvent stub.
void InputDevice::AxisEvent(long, unsigned long, float, float, unsigned long, long) {}

// Binary @ 0x???? — ButtonPressed stub.
void InputDevice::ButtonPressed(unsigned long, unsigned long, float, unsigned long, long) {}

// Binary @ 0x001b36b0 — CheckActions: iterate m_ActionMappers list, call ProcessEvent.
void InputDevice::CheckActions(InputEvent* event) {
    for (std::list<InputActionMapper*>::iterator it = m_ActionMappers.begin();
         it != m_ActionMappers.end(); ++it) {
        (*it)->ProcessEvent(event);
    }
}

} // namespace Mortar
