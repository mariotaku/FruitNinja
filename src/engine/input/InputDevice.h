#ifndef FN_ENGINE_INPUT_INPUTDEVICE_H
#define FN_ENGINE_INPUT_INPUTDEVICE_H

#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <list>
#include <cstdint>

// InputDevice base interface — v1.6.1 Mortar::InputDevice family @0x002756b0..0x00275fc7
// (the InputManager broadcaster that drives it lives at 0x0024371c..0x00244264).
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

// Mortar::InputActionMapper — non-polymorphic action-filter node.
// Binary sizeof = 0x44 (68 bytes). True binary layout (from ctor field stores):
//   +0x00  bool      m_Enabled
//   +0x04  uint32_t  m_ActionHash        (OR'd action+flag word, ctor param)
//   +0x08  uint32_t  m_ConfigSourceHash
//   +0x0c  uint32_t  m_ActionMask
//   +0x10  uint32_t  m_MatchValue        (hi16 = keycode/finger discriminator)
//   +0x14  uint32_t  m_KeyMask
//   +0x18  uint32_t  m_Param4
//   +0x1c  uint32_t  m_Param5
//   +0x20  32-byte   m_callback          (binary: StackAllocatedPointer<BaseDelegate,32ul>)
//   +0x40  bool      m_Flag40
// Port Delegate1 is 36 bytes (0x24), so it spans +0x20..+0x43 and the total
// is already 0x44 with no room for a separate m_Flag40.
// DIFFERS: binary m_callback = 32-byte inline StackAllocatedPointer<BaseDelegate,32ul>
//   with m_Flag40 bool at +0x40; port Delegate1 is 36 bytes so m_Flag40 (+0x40
//   in binary) is overlaid by the port Delegate1's trailing m_bEmpty/pad bytes.
//   InputActionMapper is only ever used through pointers so no ABI mismatch at
//   call sites. Total sizeof matches: both binary and port = 0x44.
// ActionMappers are held by pointer in InputDevice::m_ActionMappers
// (std::list<InputActionMapper*>). CheckActions iterates the list and calls
// ProcessEvent per element.
class InputActionMapper {
public:
    // v1.6.1 InputActionMapper::InputActionMapper @0x002756b0.
    // Binary has no default ctor -- every instance is fully initialised from
    // an InputEvent template + callback + two hashes. `ev` is only used as a
    // field-value source (action mask / keycode / mapper pointer), not stored.
    InputActionMapper(InputEvent ev, InputDeviceCallback cb,
                       unsigned long actionHash, unsigned long configSourceHash);

    // v1.6.1 InputActionMapper::ProcessEvent @0x00275728.
    // Called by InputDevice::CheckActions per mapper in m_ActionMappers list.
    // Compares the incoming event against this mapper's filter template and
    // fires m_callback(event) on a match.
    void ProcessEvent(InputEvent* event);

    bool      m_Enabled;           // +0x00  strb #1 in ctor
    uint32_t  m_ActionHash;        // +0x04  OR'd action+flag word (ctor param)
    uint32_t  m_ConfigSourceHash;  // +0x08
    uint32_t  m_ActionMask;        // +0x0c
    uint32_t  m_MatchValue;        // +0x10  hi16 = keycode/finger discriminator
    uint32_t  m_KeyMask;           // +0x14
    uint32_t  m_Param4;            // +0x18
    uint32_t  m_Param5;            // +0x1c
    // DIFFERS: binary = 32-byte StackAllocatedPointer<BaseDelegate,32ul> (+0x20..+0x3f)
    //   followed by bool m_Flag40 at +0x40. Port Delegate1 is 36 bytes (+0x20..+0x43),
    //   overlaying binary's m_Flag40 position. Total size still matches: 0x44.
    InputDeviceCallback m_callback; // +0x20  36 bytes in port, 32+1 in binary
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
    virtual InputDeviceTypes  GetDeviceType() = 0;

    // Port-side dispatch helper: fire all callbacks matching this event.
    // Called by InputManager::DispatchEvent.  Not a binary vtable slot —
    // InputTranslatorSDL drives dispatch by calling DispatchEvent on the
    // manager which routes here.
    virtual void              DispatchEvent(InputEvent* event) = 0;

    // ---- STUBS (binary) ----
    InputDevice();
    void AxisEvent(long, unsigned long, float, float, unsigned long, long);
    void ButtonPressed(unsigned long, unsigned long, float, unsigned long, long);

    // v1.6.1 InputDevice::CheckActions @0x00275fc7.
    // Iterates m_ActionMappers, calls ProcessEvent per element.
    void CheckActions(InputEvent* event);
    // ---- end STUBS ----

    // TODO: v1.6.1 InputDevice — confirm v1.6.1 data-layout addr (stale 0x001b3794 was v1.5.x).
    // +0x00: implicit vptr (port) / explicit fns* (binary) — layout equivalent
    // +0x04: std::list<InputActionMapper*> m_ActionMappers (8 bytes, Sourcery 2010q1)
    std::list<InputActionMapper*> m_ActionMappers;  // +0x04
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::InputActionMapper) == 0x44, "InputActionMapper size mismatch");
static_assert(sizeof(Mortar::InputDevice) == 12, "InputDevice size mismatch");
#endif

#endif // FN_ENGINE_INPUT_INPUTDEVICE_H
