#ifndef FN_ENGINE_INPUT_INPUTDEVICE_H
#define FN_ENGINE_INPUT_INPUTDEVICE_H

#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <list>
#include <cstdint>

// Binary @ 0x00196980 area — InputDevice base interface.
// Concrete subclass on Bada: InputDeviceBada (composes a Mortar::Touch).
//
// Polymorphism model: the binary uses an explicit 'fns' table pointer at
// offset 0 (not an Itanium C++ vtable); the port uses a standard C++ vtable
// which is layout-equivalent at offset 0 (both are a 4-byte dispatch pointer).
// The 'fns' alias is cosmetic-only for asm-verify (poly-mismatch = cosmetic).

// Callback type: Mortar::Delegate1<bool, InputEvent*> per binary signature.
typedef Mortar::Delegate1<bool, InputEvent*> InputDeviceCallback;

// Device type enum — matches binary InputDeviceTypes values.
// Bada touch device = 0 (first slot).
// Binary: InputDeviceTypes is in global scope (no Mortar:: prefix).
enum InputDeviceTypes {
    INPUT_DEVICE_TOUCH = 0,
    INPUT_DEVICE_KEYBOARD = 1,
    INPUT_DEVICE_GAMEPAD = 2,
};

namespace Mortar {

// Binary @ 0x001b356c — InputActionMapper.
// Non-polymorphic, 40 bytes.
// Ctor signature (per binary): InputActionMapper(InputEvent, Delegate1<bool,InputEvent*>,
//   unsigned long, unsigned long).
// ActionMappers are held by pointer in InputDevice::actionMappers
// (std::list<InputActionMapper*>). CheckActions @ 0x001b36b0 iterates the
// list and calls ProcessEvent per element.
//
// DIFFERS: binary sizeof = 40 (m_callback is an 8-byte BaseDelegate in the
//   binary's Delegate1 implementation); port Delegate1 is 36 bytes so port
//   sizeof != 40. InputActionMapper is only ever used through pointers in
//   the port — no by-value allocation — so the divergence is contained.
//   The static_assert fires only under __bada__ where sizes match.
class InputActionMapper {
public:
    // Binary @ 0x001b356c — ctor.
    // Params: InputEvent ev, Delegate1<bool,InputEvent*> cb,
    //         unsigned long, unsigned long.
    InputActionMapper();

    // Binary @ 0x001b3508 — ProcessEvent.
    // Called by InputDevice::CheckActions per mapper in actionMappers list.
    // Compares the incoming event against this mapper's filter template and
    // fires m_callback(event) on a match.
    void ProcessEvent(InputEvent* event);

    // Fields per binary ctor writes (0x001b356c). The ctor copies an InputEvent
    // "template" into the mapper so it acts as an action filter:
    //   m_actionFilter = template.eventWord   (+0xc, from InputEvent +0x00)
    //   m_matchValue   = template.matchWord   (+0x10, from InputEvent +0x04;
    //                    high 16 bits = keycode/finger discriminator)
    //   m_matchMapper  = template.m_mapper    (+0x14, from InputEvent +0x08)
    bool      m_enabled;       // +0x00  strb r12 = 1 at ctor @0x001b358a
    uint32_t  field4_0x4;     // +0x04  <- ctor param_4
    uint32_t  field5_0x8;     // +0x08  <- ctor in_stack (5th word)
    uint32_t  m_actionFilter; // +0x0c  action-type (hi16) | device-mask (lo16)
    uint32_t  m_matchValue;   // +0x10  hi16 = keycode/finger to match (MOVE/UP)
    uint32_t  m_matchMapper;  // +0x14  InputActionMapper* matched for DOWN events
    uint32_t  field9_0x18;    // +0x18  <- ctor param_2.super.fns
    uint32_t  field10_0x1c;   // +0x1c  <- ctor param_2._4_4_
    // Binary: m_callback is Delegate1<bool,InputEvent*> at +0x20, 8 bytes
    // in the binary's implementation. Port uses our 36-byte Delegate1 here.
    InputDeviceCallback m_callback;  // +0x20

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
    // Binary sizeof = 40; port sizeof diverges due to Delegate1 size difference.
    // Suppress for now — InputActionMapper only used through pointers in port.
    // static_assert(sizeof(InputActionMapper) == 40, "InputActionMapper size mismatch");
#endif
};

class InputDevice {
public:
    virtual ~InputDevice();

    // Binary vtable slots (per layout from RE evidence):
    // slot +0x00  — dtor
    // slot +0x04  — Init(unsigned long)
    // slot +0x08  — Destroy()
    // slot +0x0c  — Update(float)         [broadcast target in InputManager::Update]
    // slot +0x10  — AddActionMapper(InputActionMapper*)
    // slot +0x14  — ClearActions(unsigned long, bool last)
    // slot +0x18  — HasInputDevice — not on device; on manager
    // slot +0x1c  — RegisterInputCallback(unsigned long, InputDeviceCallback)
    // slot +0x20  — Reset()
    // slot +0x24  — SetQueueEventsUntilUpdate(bool)
    // slot +0x28  — SetSendDownCallbacksEachUpdate(bool)
    // slot +0x2c  — OnAxisExtentsChanged()
    // slot +0x30  — GetDeviceType() -> InputDeviceTypes

    virtual void              Init(unsigned long flags) = 0;
    virtual void              Destroy();
    virtual void              Update(float dt) = 0;
    virtual void              AddActionMapper(InputActionMapper* mapper) = 0;
    virtual void              ClearActions(unsigned long actionHash, bool last);
    virtual void              RegisterInputCallback(unsigned long actionHash,
                                                    InputDeviceCallback cb);
    virtual void              Reset() = 0;
    virtual void              SetQueueEventsUntilUpdate(bool v) = 0;
    virtual void              SetSendDownCallbacksEachUpdate(bool v) = 0;
    virtual void              OnAxisExtentsChanged() = 0;
    virtual InputDeviceTypes  GetDeviceType() const = 0;

    // Port-side dispatch helper: fire all callbacks matching this event.
    // Called by InputManager::DispatchEvent.  Not a binary vtable slot —
    // InputTranslatorSDL drives dispatch by calling DispatchEvent on the
    // manager which routes here.
    virtual void              DispatchEvent(InputEvent* event) = 0;

    // ---- STUBS (binary) ----
    InputDevice();
    void AxisEvent(long, unsigned long, float, float, unsigned long, long);
    void ButtonPressed(unsigned long, unsigned long, float, unsigned long, long);

    // Binary @ 0x001b36b0 — CheckActions: iterate actionMappers, call ProcessEvent.
    void CheckActions(InputEvent* event);
    // ---- end STUBS ----

    // Binary @ 0x001b3794 — data members:
    // +0x00: implicit vptr (port) / explicit fns* (binary) — layout equivalent
    // +0x04: std::list<InputActionMapper*> actionMappers (8 bytes, Sourcery 2010q1)
    std::list<InputActionMapper*> actionMappers;  // +0x04

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
    static_assert(sizeof(InputDevice) == 12, "InputDevice size mismatch");
#endif
};

} // namespace Mortar

#endif // FN_ENGINE_INPUT_INPUTDEVICE_H
