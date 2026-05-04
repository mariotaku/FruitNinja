// Analysed: 2026-05-04T00:00

#include "input/InputDeviceBada.h"
#include <cstring>

namespace Mortar {

InputDeviceBada::InputDeviceBada()
    : m_touch(&Mortar::Touch::GetInstance())
    , m_queueUntilUpdate(false)
    , m_sendDownEachUpdate(false)
{
}

InputDeviceBada::~InputDeviceBada() {
}

// Binary @ 0x00196cc8 (partial — full body: alloc + dev->fns->Init(dev) + push_back on manager)
void InputDeviceBada::Init(unsigned long /*flags*/) {
}

void InputDeviceBada::Destroy() {
    m_bindings.clear();
}

// Binary vtable slot +0x0c — broadcast target from InputManager::Update.
// TODO: 0x00195764 — call Touch::Update + SendIndividualTouchCallbacks when ported.
void InputDeviceBada::Update(float dt) {
    if (m_touch) {
        m_touch->Update(dt);
    }
}

void InputDeviceBada::AddActionMapper(InputActionMapper* /*mapper*/) {
    // TODO: 0x001960f8 — AddActionMapper broadcast body
}

// Binary @ 0x001961d0 — ClearActions: clear matching bindings.
// last=true on final device in iteration (from InputManager::ClearActions).
void InputDeviceBada::ClearActions(unsigned long actionHash, bool /*last*/) {
    for (std::list<InputDeviceBinding>::iterator it = m_bindings.begin();
         it != m_bindings.end(); ) {
        if (it->actionHash == actionHash) {
            it = m_bindings.erase(it);
        } else {
            ++it;
        }
    }
}

// Binary @ 0x0019683c — per-device binding store.
// DIFFERS: original = per-device binding store, see Binary @ 0x0019683c
void InputDeviceBada::RegisterInputCallback(unsigned long actionHash,
                                            InputDeviceCallback cb) {
    InputDeviceBinding b;
    b.actionHash = actionHash;
    b.callback   = cb;
    m_bindings.push_back(b);
}

void InputDeviceBada::Reset() {
    // TODO: 0x00196194 — ResetDevices broadcast body
}

void InputDeviceBada::SetQueueEventsUntilUpdate(bool v) {
    m_queueUntilUpdate = v;
}

void InputDeviceBada::SetSendDownCallbacksEachUpdate(bool v) {
    m_sendDownEachUpdate = v;
}

void InputDeviceBada::OnAxisExtentsChanged() {
    // TODO: 0x00196bc8 — OnAxisExtentsChanged broadcast body
}

InputDeviceTypes InputDeviceBada::GetDeviceType() const {
    return INPUT_DEVICE_TOUCH;
}

// Port-side: dispatch an InputEvent to all matching callbacks on this device.
// Binary equivalent: InputDevice::CheckActions -> InputActionMapper::ProcessEvent
// -> fires callbacks matching the action hash.
void InputDeviceBada::DispatchEvent(InputEvent* event) {
    for (std::list<InputDeviceBinding>::iterator it = m_bindings.begin();
         it != m_bindings.end(); ++it) {
        if (it->actionHash == (unsigned long)event->actionHash) {
            if (it->callback(event)) {
                return;
            }
        }
    }
}

} // namespace Mortar
