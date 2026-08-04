// InputDeviceBada — concrete Bada/touch InputDevice.
// ASM-spec v1.6.1 InputDeviceBada @ 0x002427e0.

#include "input/InputDeviceBada.h"
// Acceleration: small accelerometer singleton polled by InputDeviceBada::Update
// (binary @ 0x00195a2c). Header not yet ported — referenced faithfully here;
// see newMissingPorts. Binary: Acceleration::GetInstance, GetAccelAbs @
// 0x001955e0, GetAccelDelta @ 0x001955fc.
#include "input/Acceleration.h"

namespace Mortar {

// ---------------------------------------------------------------------------
// Global-pointer action dispatch (PointerMove / PointerPressed / PointerReleased)
// ---------------------------------------------------------------------------
//
// How v1.6.1 raises these, end to end (the port now does the same, no substitute):
//
//   InputDeviceBada::Update @0x00242f40 tracks ONE global pointer (the
//   most-recent touch, else any touch) and emits, per frame:
//     AxisEvent(0x74 MouseAxisX, 0x20, posX, dX, stamp, 0)
//     AxisEvent(0x75 MouseAxisY, 0x20, posY, dY, stamp, 0)
//     ButtonPressed(0x6c MouseButton1, mask, 1.0f, stamp, 0)
//       mask 1 on the acquire frame, 2 while held,
//       mask 4 then 8 on the frame the pointer is lost, 8 while idle.
//   InputDevice::AxisEvent @0x0027582c / InputDevice::ButtonPressed @0x00275864
//   pack a 0x14-byte InputEvent (word0 = mask | 0x20000 / | 0x10000, keycode at
//   +0x06, axis value or keycode at +0x08) and call InputDevice::CheckActions
//   @0x002757fc -> InputActionMapper::ProcessEvent @0x00275728, which fires the
//   callback bound to the matching action.
//
//   The mappers come from InputManager::LoadConfigFile @0x002442fc parsing
//   Data/input/input.txt. Key names resolve through InputManager::ParseKey
//   @0x002438c8 (MouseButton1 = 0x6c, MouseAxisX/Y = 0x74/0x75) and action names
//   through InputManager::ParseAction @0x00244060 (pressed=0x01, down=0x02,
//   released=0x04, up=0x08, active=0x10, move=0x20, dead=0x40).
//
//   The shipped input.txt binds the mouse keys to exactly three actions:
//     PointerMove:      MouseAxisX,MouseAxisY;   move
//     PointerPressed:   MouseButton1;            pressed
//     PointerReleased:  MouseButton1;            released
//   so mask 1 raises PointerPressed, mask 4 raises PointerReleased, and masks 2
//   ("down") and 8 ("up") raise NOTHING -- input.txt has no MouseButton1 line for
//   either. Per frame the global pointer actions therefore fire BEFORE the
//   per-finger Touch* actions, which Touch::SendIndividualTouchCallbacks
//   @0x00242bc4 emits afterwards from the tail of the same Update().
//
//   NB PointerMove's key ends up as 0x75, and ProcessEvent @0x00275728 treats an
//   axis key code BELOW 0x89 as a BITMASK (`(kc & evKc) != 0`) rather than an
//   exact compare. 0x75 overlaps 0x74 (MouseAxisX) -- which is how one mapper
//   covers both axes -- but it also overlaps every TouchAxis code, so
//   PointerMoveCallback runs a second time for each per-finger axis event. That
//   is binary behaviour and it is harmless: the second call recomputes the same
//   finger position from the same event.
//
//   input.txt's two remaining Pointer lines are DEAD in v1.6.1:
//     PointerMove:     X360_LStick_AxisX/Y;  active
//     PointerPressedX: X360_A;               down
//   ParseKey's 61-entry table holds only MouseButton1..8, MouseAxisX/Y,
//   Touch1..16, TouchAxisX/Y1..16 and AccelAxisX/Y/Z -- no X360_* name -- so it
//   returns 0 and LoadConfigFile's `if (key != 0 && action != 0)` guard skips the
//   mapper entirely. PointerDownXboxCallback @0x001cbec8 never runs on Bada, so
//   the port must not dispatch "PointerPressedX" either.
//
// The per-finger keycodes (Touch1..16 = 0x89..0x98, TouchAxisX1..16 =
// 0x99..0xa8, TouchAxisY1..16 = 0xa9..0xb8) come from
// Touch::SendIndividualTouchCallbacks @0x00242bc4, called from the tail of the
// same Update() -- so within a frame the global pointer actions always fire
// BEFORE the per-finger ones.

// ASM-spec v1.6.1 InputDeviceBada::InputDeviceBada @ 0x002427e0 — ctor: call InputDevice ctor at base, write
// InputDeviceBada vtable (fns*) at +0x00, zero four uint32_t fields.
InputDeviceBada::InputDeviceBada()
    : m_ActiveTouchId(0)
    , m_LastTouchX(0)
    , m_LastTouchY(0)
    , m_EventStamp(0)
{
}

InputDeviceBada::~InputDeviceBada() {
}

// v1.6.1 Mortar::InputManager::Init @0x002447d4 allocates the Bada HAL input-device handle (dev->fns->Init(dev)).
// Port specific: no SDL counterpart — SDL events reach the Touch singleton directly
// via InputTranslatorSDL, and the device is registered on InputManager's list by
// InputManager::Init (not here). Intentionally a no-op for the SDL backend.
void InputDeviceBada::Init(unsigned long /*flags*/) {
}

// ASM-spec v1.6.1 InputDeviceBada::Destroy @0x0024278c: tail-calls base InputDevice::Destroy, nothing else.
void InputDeviceBada::Destroy() {
    InputDevice::Destroy();
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
//
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
// So the empty bodies below are the faithful shape.
void InputDeviceBada::SetQueueEventsUntilUpdate(bool /*v*/) {
}

void InputDeviceBada::SetSendDownCallbacksEachUpdate(bool /*v*/) {
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

} // namespace Mortar
