// InputDeviceBada — concrete Bada/touch InputDevice.
// Binary @ 0x001958a4.

#include "input/InputDeviceBada.h"
#include <cstring>

namespace Mortar {

// Binary @ 0x001958a4 — ctor: call InputDevice ctor at base, write
// InputDeviceBada vtable (fns*) at +0x00, zero four uint32_t fields.
InputDeviceBada::InputDeviceBada()
    : field_0xc(0)
    , field_0x10(0)
    , field_0x14(0)
    , field_0x18(0)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_touch(&Mortar::Touch::GetInstance())
    , m_queueUntilUpdate(false)
    , m_sendDownEachUpdate(false)
#endif
{
}

InputDeviceBada::~InputDeviceBada() {
}

// Binary @ 0x00196cc8 (partial — full body: alloc + dev->fns->Init(dev) + push_back on manager)
void InputDeviceBada::Init(unsigned long /*flags*/) {
}

void InputDeviceBada::Destroy() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    m_bindings.clear();
#endif
}

// Binary vtable slot +0x0c — broadcast target from InputManager::Update.
// TODO: 0x00195764 — call Touch::Update + SendIndividualTouchCallbacks when ported.
void InputDeviceBada::Update(float dt) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (m_touch) {
        m_touch->Update(dt);
    }
#else
    (void)dt;
#endif
}

void InputDeviceBada::AddActionMapper(InputActionMapper* mapper) {
    actionMappers.push_back(mapper);
}

// Binary @ 0x001961d0 — ClearActions: clear matching bindings.
// last=true on final device in iteration (from InputManager::ClearActions).
void InputDeviceBada::ClearActions(unsigned long actionHash, bool /*last*/) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    for (std::list<InputDeviceBinding>::iterator it = m_bindings.begin();
         it != m_bindings.end(); ) {
        if (it->actionHash == actionHash) {
            it = m_bindings.erase(it);
        } else {
            ++it;
        }
    }
#else
    (void)actionHash;
#endif
}

// Binary @ 0x0019683c — per-device binding store.
// DIFFERS: original = per-device binding via InputActionMapper; port uses
//   direct InputDeviceBinding list for SDL dispatch path (no InputActionMapper
//   ctor ported yet). Binary @ 0x0019683c.
void InputDeviceBada::RegisterInputCallback(unsigned long actionHash,
                                            InputDeviceCallback cb) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    InputDeviceBinding b;
    b.actionHash = actionHash;
    b.callback   = cb;
    m_bindings.push_back(b);
#else
    (void)actionHash;
    (void)cb;
#endif
}

void InputDeviceBada::Reset() {
    // TODO: 0x00196194 — ResetDevices broadcast body
}

void InputDeviceBada::SetQueueEventsUntilUpdate(bool v) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    m_queueUntilUpdate = v;
#else
    (void)v;
#endif
}

void InputDeviceBada::SetSendDownCallbacksEachUpdate(bool v) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    m_sendDownEachUpdate = v;
#else
    (void)v;
#endif
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
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    for (std::list<InputDeviceBinding>::iterator it = m_bindings.begin();
         it != m_bindings.end(); ++it) {
        if (it->actionHash == (unsigned long)event->actionHash) {
            if (it->callback(event)) {
                return;
            }
        }
    }
#else
    (void)event;
#endif
}

} // namespace Mortar
