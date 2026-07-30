#ifndef FN_ENGINE_INPUT_INPUTDEVICE_H
#define FN_ENGINE_INPUT_INPUTDEVICE_H

#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <list>
#include <cstdint>

// InputDevice base interface — v1.6.1 Mortar::InputDevice family @0x002756b0..0x00275bdc
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
//   +0x20  36-byte   m_callback          (binary: Delegate1 = StackAllocatedPointer<BaseDelegate,32ul>
//                                         32-byte inline payload +0x20..+0x3f, then the
//                                         delegate's OWN m_bInline byte at delegate+0x20 = +0x40)
// The byte at +0x40 is NOT a separate InputActionMapper field: it is the
// Delegate1's m_bInline (see Delegate.h m_bEmpty @ +0x20 of the delegate). Port
// Delegate1 is byte-exact with the binary's, so the whole class matches at 0x44.
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
    // Returns the handler's bool on the MOVE/UP arms (binary tail-calls
    // Delegate1<bool,InputEvent*>::Call @0x002757d4) and false everywhere else,
    // including the DOWN arm. CheckActions discards it, so it is unobservable --
    // the return exists for shape fidelity, NOT to drive a chain-consume.
    bool ProcessEvent(InputEvent* event);

    bool      m_Enabled;           // +0x00  strb #1 in ctor
    uint32_t  m_ActionHash;        // +0x04  OR'd action+flag word (ctor param)
    uint32_t  m_ConfigSourceHash;  // +0x08
    // +0x0c..+0x1c are a verbatim copy of the ctor's by-value InputEvent words
    // 0..4 (ctor @0x002756b0: `ldmia lr!,{r0-r3} / stmia r12!,{r0-r3}` into
    // this+0x0c, then word 4 into this+0x1c). ProcessEvent therefore compares
    // template-word-N against event-word-N, field for field.
    uint32_t  m_ActionMask;        // +0x0c  event word 0 (type bits | device mask)
    uint32_t  m_MatchValue;        // +0x10  event word 1; hi16 = keycode/finger discriminator
    uint32_t  m_KeyMask;           // +0x14  event word 2 (button/key id on DOWN events)
    // TODO: v1.6.1 0x002756b0 (Mortar::InputActionMapper::InputActionMapper) — the ctor
    //   copies InputEvent words 3 and 4 (binary InputEvent +0x0c / +0x10) into these two
    //   fields; the port's InputEvent has no counterpart for those words (its layout is
    //   remapped, see InputEvent.h), so the ctor hardcodes 0. Port a layout-faithful
    //   InputEvent before wiring them.
    uint32_t  m_Param4;            // +0x18  binary: InputEvent word 3
    uint32_t  m_Param5;            // +0x1c  binary: InputEvent word 4
    InputDeviceCallback m_callback; // +0x20  36 bytes (0x24) both sides; its m_bInline lands at +0x40
};

class InputDevice {
public:
    virtual ~InputDevice();

    // Binary vtable — 12 slots (base table 0x002d0f70, InputDeviceBada 0x002d0468):
    // slot +0x00  — dtor
    // slot +0x04  — dtor_deleting
    // slot +0x08  — Init(unsigned long)
    // slot +0x0c  — Update(float)         [broadcast target in InputManager::Update]
    // slot +0x10  — Destroy()                        @0x00275938
    // slot +0x14  — Reset()                          base body @0x00275bdc (bare `bx lr`)
    // slot +0x18  — GetDeviceType() -> InputDeviceTypes
    // slot +0x1c  — OnAxisExtentsChanged()            {} inline-empty in the base header
    // slot +0x20  — SetSendDownCallbacksEachUpdate(bool)  {} inline-empty
    // slot +0x24  — SetQueueEventsUntilUpdate(bool)       {} inline-empty
    // slot +0x28  — IsDown(...)                           {} inline-empty
    // slot +0x2c  — AddActionMapper(InputActionMapper*)  @0x00275894
    //
    // NON-VIRTUAL base methods in the binary (no vtable slot):
    //   ClearActions @0x002758b0, RegisterInputCallback @0x002759f4,
    //   CheckActions @0x002757fc, AxisEvent @0x0027582c, ButtonPressed @0x00275864.
    //
    // The four inline-empty slots (+0x1c..+0x28) resolve to shared 4-byte `bx lr`
    // bodies (0x00243550 / 0x00243554 / 0x00243558 / 0x0024355c); InputDeviceBada's
    // vtable points at the SAME addresses, i.e. it does NOT override any of them.

    virtual void              Init(unsigned long flags) = 0;
    virtual void              Destroy();
    virtual void              Update(float dt) = 0;
    virtual void              AddActionMapper(InputActionMapper* mapper) = 0;
    // DIFFERS: ClearActions/RegisterInputCallback are NON-VIRTUAL base methods in the
    //   binary (see the slot list above). The port declares them virtual and overrides
    //   them in InputDeviceBada purely to host the m_bindings dispatch substitute that
    //   stands in for InputManager::LoadConfigFile (a Defunct stub here, and the binary's
    //   only creator of InputActionMappers). Drop the `virtual` together with m_bindings
    //   when LoadConfigFile is ported — not before, or every registration is lost.
    virtual void              ClearActions(unsigned long actionHash, bool last);
    virtual void              RegisterInputCallback(unsigned long actionHash,
                                                    InputDeviceCallback cb);
    virtual void              Reset() = 0;
    virtual void              SetQueueEventsUntilUpdate(bool v) = 0;
    virtual void              SetSendDownCallbacksEachUpdate(bool v) = 0;
    virtual void              OnAxisExtentsChanged() = 0;
    virtual InputDeviceTypes  GetDeviceType() = 0;

    // DIFFERS: PORT-INVENTED — no binary counterpart, in the vtable or otherwise.
    //   The binary dispatches via the non-virtual CheckActions -> InputActionMapper::
    //   ProcessEvent. This slot exists only for the m_bindings substitute path
    //   (InputTranslatorSDL -> InputManager::DispatchEvent -> here) and is removed
    //   with it once LoadConfigFile is ported.
    virtual void              DispatchEvent(InputEvent* event) = 0;

    // ---- STUBS (binary) ----
    // v1.6.1 InputDevice::InputDevice @0x002759a8.
    InputDevice();
    // v1.6.1 InputDevice::AxisEvent @0x0027582c.
    void AxisEvent(long, unsigned long, float, float, unsigned long, long);
    // v1.6.1 InputDevice::ButtonPressed @0x00275864.
    void ButtonPressed(unsigned long, unsigned long, float, unsigned long, long);

    // v1.6.1 InputDevice::CheckActions @0x002757fc.
    // Iterates m_ActionMappers, calls ProcessEvent per element.
    void CheckActions(InputEvent* event);
    // ---- end STUBS ----

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
