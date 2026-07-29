#include "InputDevice.h"
#include <cstring>

namespace Mortar {

// v1.6.1 InputActionMapper::InputActionMapper @0x002756b0.
// ev is a by-value template event: only its actionFlags/keycode/m_mapper
// fields feed the filter, matching the binary reading its 3 relevant words
// out of the by-value InputEvent (0x14 bytes -- see InputEvent.h).
// TODO: v1.6.1 0x002756b0 (Mortar::InputActionMapper::InputActionMapper) --
// the binary also copies InputEvent words 3 and 4 (binary +0x0c / +0x10) into
// m_Param4 / m_Param5. The port's InputEvent is remapped and carries no field
// for those words, so both are hardcoded 0 here; wire them once InputEvent is
// made layout-faithful.
InputActionMapper::InputActionMapper(InputEvent ev, InputDeviceCallback cb,
                                      unsigned long actionHash,
                                      unsigned long configSourceHash)
    : m_Enabled(true)
    , m_ActionHash(actionHash)
    , m_ConfigSourceHash(configSourceHash)
    , m_ActionMask(ev.actionFlags)
    , m_MatchValue(ev.keycode << 16)
    , m_KeyMask((uint32_t)(uintptr_t)ev.m_mapper)
    , m_Param4(0)
    , m_Param5(0)
    , m_callback(cb)
{
}

// v1.6.1 InputActionMapper::ProcessEvent @0x00275728.
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
// DIFFERS: the binary reads a 0x14-byte InputEvent (combined action-word at +0x00,
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

// v1.6.1 InputDevice::InputDevice @0x002759a8 — set fns ptr, list ctor, list clear.
// Port: standard C++ ctor (vptr set by compiler, m_ActionMappers default-constructed).
InputDevice::InputDevice() {
}

// v1.6.1 InputDevice::~InputDevice @0x00275958 (deleting variant @0x0027598c).
InputDevice::~InputDevice() {
}

// ASM-spec v1.6.1 InputDevice::Destroy @0x00275938: m_ActionMappers.clear() (list nodes only, payloads borrowed).
void InputDevice::Destroy() {
    m_ActionMappers.clear();
}

// v1.6.1 InputDevice::ClearActions @0x002758b0 — non-virtual in the binary (see
// InputDevice.h DIFFERS for why the port declares it virtual). Body not ported.
void InputDevice::ClearActions(unsigned long, bool) {}

// v1.6.1 InputDevice::RegisterInputCallback @0x002759f4 — non-virtual in the binary
// (see InputDevice.h DIFFERS). Body not ported.
void InputDevice::RegisterInputCallback(unsigned long, InputDeviceCallback) {}

// v1.6.1 InputDevice::AxisEvent @0x0027582c — stub.
void InputDevice::AxisEvent(long, unsigned long, float, float, unsigned long, long) {}

// v1.6.1 InputDevice::ButtonPressed @0x00275864 — stub.
void InputDevice::ButtonPressed(unsigned long, unsigned long, float, unsigned long, long) {}

// v1.6.1 InputDevice::CheckActions @0x002757fc. Iterate m_ActionMappers list, call ProcessEvent.
void InputDevice::CheckActions(InputEvent* event) {
    for (std::list<InputActionMapper*>::iterator it = m_ActionMappers.begin();
         it != m_ActionMappers.end(); ++it) {
        (*it)->ProcessEvent(event);
    }
}

} // namespace Mortar
