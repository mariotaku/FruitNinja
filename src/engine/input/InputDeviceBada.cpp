// InputDeviceBada — concrete Bada/touch InputDevice.
// Binary @ 0x001958a4.

#include "input/InputDeviceBada.h"
// Acceleration: small accelerometer singleton polled by InputDeviceBada::Update
// (binary @ 0x00195a2c). Header not yet ported — referenced faithfully here;
// see newMissingPorts. Binary: Acceleration::GetInstance, GetAccelAbs @
// 0x001955e0, GetAccelDelta @ 0x001955fc.
#include "input/Acceleration.h"
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

// Binary @ 0x00196cc8 allocates the Bada HAL input-device handle (dev->fns->Init(dev)).
// Port specific: no SDL counterpart — the Touch singleton is bound in the ctor (m_touch),
// SDL events reach Touch directly via InputTranslatorSDL, and the device is registered on
// InputManager's list by InputManager::Init (not here). Intentionally a no-op for the SDL backend.
void InputDeviceBada::Init(unsigned long /*flags*/) {
}

void InputDeviceBada::Destroy() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    m_bindings.clear();
#endif
}

// Binary @ 0x00195a2c — InputDeviceBada::Update(float).
// Binary vtable slot +0x0c — broadcast target from InputManager::Update.
// Per-device touch poll: track the active touch in field_0xc, emit position
// AxisEvents (action 0x74/0x75) and lifecycle ButtonPressed (action 0x6c),
// then fan out SendIndividualTouchCallbacks and three accelerometer axes
// (action 0xb9/0xba/0xbb). field_0x18 is a monotonically-incrementing event
// stamp passed as the timestamp arg to every emitted event.
//
// Field map (binary +offset -> port member):
//   +0xc  field_0xc  = active touch id (0 = none)
//   +0x10 field_0x10 = last touch X
//   +0x14 field_0x14 = last touch Y
//   +0x18 field_0x18 = event stamp counter
void InputDeviceBada::Update(float /*dt*/) {
    Touch& touch = Touch::GetInstance();

    ++field_0x18;

    int posX = 0;
    int posY = 0;

    if (field_0xc == 0) {
        // No active touch yet: try to acquire one this frame.
        field_0xc = touch.GetMostRecentTouch();
        if (field_0xc == 0) {
            field_0xc = touch.GetAnyTouch();
        }
        if (field_0xc == 0) {
            // Nothing pressed: emit "up" (mask 8) and fall through to the
            // callbacks/accelerometer tail.
            ButtonPressed(0x6c, 8, 1.0f, field_0x18, 0);
            goto callbacks;
        }
        // Freshly acquired touch -> emit current position (press-edge).
        touch.GetTouchPos(field_0xc, posX, posY);
        AxisEvent(0x74, 0x20, (float)posX,
                  (float)(posX - (int)field_0x10), field_0x18, 0);
        AxisEvent(0x75, 0x20, (float)posY,
                  (float)(posY - (int)field_0x14), field_0x18, 0);
        // mask 1 = press-edge.
        ButtonPressed(0x6c, 1, 1.0f, field_0x18, 0);
        field_0x10 = (uint32_t)posX;
        field_0x14 = (uint32_t)posY;
    } else {
        // Already tracking a touch: verify it is still the most-recent/any
        // active touch and still has valid delta; otherwise release it.
        uint32_t recent = touch.GetMostRecentTouch();
        if (recent == 0) {
            recent = touch.GetAnyTouch();
        }
        uint32_t tracked = field_0xc;
        int dX = 0;
        int dY = 0;
        bool active = touch.GetTouchDelta(field_0xc, dX, dY) != 0;
        if (!active || recent != tracked) {
            // Lost/changed touch: emit release (mask 4) then up (mask 8),
            // clear the tracked id, and skip position events this frame.
            ButtonPressed(0x6c, 4, 1.0f, field_0x18, 0);
            ButtonPressed(0x6c, 8, 1.0f, field_0x18, 0);
            field_0xc = 0;
            goto callbacks;
        }
        touch.GetTouchPos(field_0xc, posX, posY);
        AxisEvent(0x74, 0x20, (float)posX,
                  (float)(posX - (int)field_0x10), field_0x18, 0);
        AxisEvent(0x75, 0x20, (float)posY,
                  (float)(posY - (int)field_0x14), field_0x18, 0);
        // mask 2 = held/move.
        ButtonPressed(0x6c, 2, 1.0f, field_0x18, 0);
        field_0x10 = (uint32_t)posX;
        field_0x14 = (uint32_t)posY;
    }

callbacks:
    Touch::GetInstance().SendIndividualTouchCallbacks(this);

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    Acceleration::GetInstance()->GetAccelAbs(&ax, &ay, &az);
    Acceleration::GetInstance()->GetAccelDelta(&dx, &dy, &dz);
    AxisEvent(0xb9, 0x20, ax, dx, field_0x18, 0);
    AxisEvent(0xba, 0x20, ay, dy, field_0x18, 0);
    AxisEvent(0xbb, 0x20, az, dz, field_0x18, 0);
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

// Binary @ 0x00195c00 — InputDeviceBada::Reset().
// Clear tracked-touch state (field_0xc id, field_0x10/0x14 last pos) and wipe
// the Touch singleton's pending state. (The InputManager-level broadcast that
// fans Reset() out to every device is InputManager::ResetDevices @ 0x00196194,
// a separate function — not this per-device override.)
void InputDeviceBada::Reset() {
    field_0x14 = 0;
    field_0xc  = 0;
    field_0x10 = 0;
    Touch::GetInstance().Clear();
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

// Binary @ 0x00195eb8 — InputDeviceBada::OnAxisExtentsChanged().
// Empty body in the binary (inherits the InputDevice base no-op; the touch
// device has no axis extents to recompute). The InputManager-level broadcast
// that fans this out across devices is InputManager::OnAxisExtentsChanged
// @ 0x001960bc — a separate function, not this per-device override.
void InputDeviceBada::OnAxisExtentsChanged() {
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
