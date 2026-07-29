// InputDeviceBada — concrete Bada/touch InputDevice.
// ASM-spec v1.6.1 InputDeviceBada @ 0x002427e0.

#include "input/InputDeviceBada.h"
// Acceleration: small accelerometer singleton polled by InputDeviceBada::Update
// (binary @ 0x00195a2c). Header not yet ported — referenced faithfully here;
// see newMissingPorts. Binary: Acceleration::GetInstance, GetAccelAbs @
// 0x001955e0, GetAccelDelta @ 0x001955fc.
#include "input/Acceleration.h"
#include <cstring>

namespace Mortar {

// ASM-spec v1.6.1 InputDeviceBada::InputDeviceBada @ 0x002427e0 — ctor: call InputDevice ctor at base, write
// InputDeviceBada vtable (fns*) at +0x00, zero four uint32_t fields.
InputDeviceBada::InputDeviceBada()
    : m_ActiveTouchId(0)
    , m_LastTouchX(0)
    , m_LastTouchY(0)
    , m_EventStamp(0)
#if !defined(__bada__)
    , m_touch(&Mortar::Touch::GetInstance())
    , m_queueUntilUpdate(false)
    , m_sendDownEachUpdate(false)
#endif
{
}

InputDeviceBada::~InputDeviceBada() {
}

// v1.6.1 Mortar::InputManager::Init @0x002447d4 allocates the Bada HAL input-device handle (dev->fns->Init(dev)).
// Port specific: no SDL counterpart — the Touch singleton is bound in the ctor (m_touch),
// SDL events reach Touch directly via InputTranslatorSDL, and the device is registered on
// InputManager's list by InputManager::Init (not here). Intentionally a no-op for the SDL backend.
void InputDeviceBada::Init(unsigned long /*flags*/) {
}

// ASM-spec v1.6.1 InputDeviceBada::Destroy @0x0024278c: tail-calls base InputDevice::Destroy, nothing else.
void InputDeviceBada::Destroy() {
    InputDevice::Destroy();
#if !defined(__bada__)
    m_bindings.clear();  // Port specific: SDL dispatch map, no binary counterpart
#endif
}

// ASM-spec v1.6.1 InputDeviceBada::Update @ 0x00242f40 — InputDeviceBada::Update(float).
// Binary vtable slot +0x0c — broadcast target from InputManager::Update.
// Per-device touch poll: track the active touch in m_ActiveTouchId, emit
// position AxisEvents (action 0x74/0x75) and lifecycle ButtonPressed (action
// 0x6c), then fan out SendIndividualTouchCallbacks and three accelerometer axes
// (action 0xb9/0xba/0xbb). m_EventStamp is a monotonically-incrementing event
// stamp passed as the timestamp arg to every emitted event.
//
// Field map (binary +offset -> port member, names per Ghidra v1.6.1 struct):
//   +0xc  m_ActiveTouchId = active touch id (0 = none)
//   +0x10 m_LastTouchX    = last touch X
//   +0x14 m_LastTouchY    = last touch Y
//   +0x18 m_EventStamp    = event stamp counter
void InputDeviceBada::Update(float /*dt*/) {
    Touch& touch = Touch::GetInstance();

    ++m_EventStamp;

    float posX = 0.0f;
    float posY = 0.0f;

    if (m_ActiveTouchId == 0) {
        // No active touch yet: try to acquire one this frame.
        m_ActiveTouchId = touch.GetMostRecentTouch();
        if (m_ActiveTouchId == 0) {
            m_ActiveTouchId = touch.GetAnyTouch();
        }
        if (m_ActiveTouchId == 0) {
            // Nothing pressed: emit "up" (mask 8) and fall through to the
            // callbacks/accelerometer tail.
            ButtonPressed(0x6c, 8, 1.0f, m_EventStamp, 0);
            goto callbacks;
        }
        // Freshly acquired touch -> emit current position (press-edge).
        touch.GetTouchPos(m_ActiveTouchId, posX, posY);
        AxisEvent(0x74, 0x20, posX,
                  posX - (float)m_LastTouchX, m_EventStamp, 0);
        AxisEvent(0x75, 0x20, posY,
                  posY - (float)m_LastTouchY, m_EventStamp, 0);
        // mask 1 = press-edge.
        ButtonPressed(0x6c, 1, 1.0f, m_EventStamp, 0);
        m_LastTouchX = (uint32_t)posX;
        m_LastTouchY = (uint32_t)posY;
    } else {
        // Already tracking a touch: verify it is still the most-recent/any
        // active touch and still has valid delta; otherwise release it.
        uint32_t recent = touch.GetMostRecentTouch();
        if (recent == 0) {
            recent = touch.GetAnyTouch();
        }
        uint32_t tracked = m_ActiveTouchId;
        float dX = 0.0f;
        float dY = 0.0f;
        bool active = touch.GetTouchDelta(m_ActiveTouchId, dX, dY) != 0;
        if (!active || recent != tracked) {
            // Lost/changed touch: emit release (mask 4) then up (mask 8),
            // clear the tracked id, and skip position events this frame.
            ButtonPressed(0x6c, 4, 1.0f, m_EventStamp, 0);
            ButtonPressed(0x6c, 8, 1.0f, m_EventStamp, 0);
            m_ActiveTouchId = 0;
            goto callbacks;
        }
        touch.GetTouchPos(m_ActiveTouchId, posX, posY);
        AxisEvent(0x74, 0x20, posX,
                  posX - (float)m_LastTouchX, m_EventStamp, 0);
        AxisEvent(0x75, 0x20, posY,
                  posY - (float)m_LastTouchY, m_EventStamp, 0);
        // mask 2 = held/move.
        ButtonPressed(0x6c, 2, 1.0f, m_EventStamp, 0);
        m_LastTouchX = (uint32_t)posX;
        m_LastTouchY = (uint32_t)posY;
    }

callbacks:
    Touch::GetInstance().SendIndividualTouchCallbacks(this);

    // TODO: v1.6.1 -- InputDeviceBada::Update @0x00242f40 has NO accelerometer tail;
    // the binary likely polls Acceleration in a separate input device. Confirm where
    // (Acceleration::GetAccel* @stale 0x001955e0/0x001955fc need re-RE) before removing
    // this block, so upside-down scoring isn't lost. Stale addrs unverified.
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    Acceleration::GetInstance()->GetAccelAbs(&ax, &ay, &az);
    Acceleration::GetInstance()->GetAccelDelta(&dx, &dy, &dz);
    AxisEvent(0xb9, 0x20, ax, dx, m_EventStamp, 0);
    AxisEvent(0xba, 0x20, ay, dy, m_EventStamp, 0);
    AxisEvent(0xbb, 0x20, az, dz, m_EventStamp, 0);
}

void InputDeviceBada::AddActionMapper(InputActionMapper* mapper) {
    m_ActionMappers.push_back(mapper);
}

// v1.6.1 Mortar::InputDevice::ClearActions @0x002758b0 — NON-VIRTUAL base method in
// the binary, so InputDeviceBada has nothing to override there. This override exists
// only to service the port's m_bindings substitute (see InputDevice.h DIFFERS); it
// goes away with m_bindings once InputManager::LoadConfigFile is ported. Distinct
// symbol from the manager-side broadcaster Mortar::InputManager::ClearActions
// @0x002441e0. last=true on the final device in iteration.
void InputDeviceBada::ClearActions(unsigned long actionHash, bool /*last*/) {
#if !defined(__bada__)
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

// v1.6.1 Mortar::InputDevice::RegisterInputCallback @0x002759f4 — NON-VIRTUAL base
// method in the binary (no vtable slot), which walks m_ActionMappers, matches
// m_ActionHash and installs the callback. (Manager-side broadcaster: @0x0024475c.)
// DIFFERS: original = per-device binding via InputActionMapper; port overrides the
//   method and uses a direct InputDeviceBinding list, because the binary's only
//   producer of InputActionMappers is InputManager::LoadConfigFile @0x002442fc,
//   which is a Defunct stub here — so m_ActionMappers is always empty and the
//   binary-faithful body would register nothing. Remove this override together
//   with m_bindings when LoadConfigFile is ported.
void InputDeviceBada::RegisterInputCallback(unsigned long actionHash,
                                            InputDeviceCallback cb) {
#if !defined(__bada__)
    InputDeviceBinding b;
    b.actionHash = actionHash;
    b.callback   = cb;
    m_bindings.push_back(b);
#else
    (void)actionHash;
    (void)cb;
#endif
}

// ASM-spec v1.6.1 InputDeviceBada::Reset @ 0x00243174 — InputDeviceBada::Reset().
// Clear tracked-touch state (m_ActiveTouchId, m_LastTouchX/m_LastTouchY) and wipe
// the Touch singleton's pending state. (The InputManager-level broadcast that
// fans Reset() out to every device is InputManager::ResetDevices @ 0x0024380c,
// a separate function — not this per-device override.)
void InputDeviceBada::Reset() {
    m_LastTouchY = 0;
    m_ActiveTouchId  = 0;
    m_LastTouchX = 0;
    Touch::GetInstance().Clear();
}

// SETTLED (do not re-litigate): InputDeviceBada does NOT override
// SetQueueEventsUntilUpdate / SetSendDownCallbacksEachUpdate. Both vtables — base
// 0x002d0f70 and Bada 0x002d0468 — point at the SAME 4-byte `bx lr` bodies
// (0x00243554 and 0x00243558), emitted from inline empty `{}` in the base header.
// Same story for OnAxisExtentsChanged (0x00243550) and IsDown (0x0024355c).
// So the empty __bada__ arm below is ALREADY FAITHFUL; the !__bada__ arms writing
// m_queueUntilUpdate / m_sendDownEachUpdate are port inventions belonging to the
// m_bindings substitute (see InputDevice.h DIFFERS) and are deleted with it.
void InputDeviceBada::SetQueueEventsUntilUpdate(bool v) {
#if !defined(__bada__)
    m_queueUntilUpdate = v;
#else
    (void)v;
#endif
}

void InputDeviceBada::SetSendDownCallbacksEachUpdate(bool v) {
#if !defined(__bada__)
    m_sendDownEachUpdate = v;
#else
    (void)v;
#endif
}

// v1.6.1 InputDevice::OnAxisExtentsChanged @0x00243550 — a shared inline-empty `bx lr`
// body, NOT an InputDeviceBada override: the Bada vtable (0x002d0468) slot +0x1c holds
// the same address as the base vtable (0x002d0f70). The touch device has no axis
// extents to recompute. The InputManager-level broadcast that fans this out across
// devices is InputManager::OnAxisExtentsChanged @0x00244238 — a separate function.
void InputDeviceBada::OnAxisExtentsChanged() {
}

InputDeviceTypes InputDeviceBada::GetDeviceType() {
    return INPUT_DEVICE_TOUCH;
}

// Port-side: dispatch an InputEvent to all matching callbacks on this device.
// Binary equivalent: InputDevice::CheckActions -> InputActionMapper::ProcessEvent
// -> fires callbacks matching the action hash.
void InputDeviceBada::DispatchEvent(InputEvent* event) {
#if !defined(__bada__)
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
