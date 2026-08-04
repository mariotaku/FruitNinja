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
//   +0x20  36-byte   m_Callback          (binary: Delegate1 = StackAllocatedPointer<BaseDelegate,32ul>
//                                         32-byte inline payload +0x20..+0x3f, then the
//                                         delegate's OWN m_bInline byte at delegate+0x20 = +0x40)
// The byte at +0x40 is NOT a separate InputActionMapper field: it is the
// Delegate1's m_bInline (see Delegate.h m_bEmpty @ +0x20 of the delegate). Port
// Delegate1 is byte-exact with the binary's, so the whole class matches at 0x44.
//
// UNBOUND MAPPERS ARE NORMAL, NOT AN ERROR. InputManager::LoadConfigFile builds
// one mapper per Input/Input.txt action line (67 of them) with an EMPTY delegate,
// and only ~54 ever get a callback bound -- the 16 TouchReleased_<i> mappers never
// do. CheckActions @0x002757fc walks ALL of them with no boundness filter, so
// invoking an empty delegate is a designed, once-per-frame path. The no-op lives
// inside the delegate itself (v1.6.1 Delegate1<bool,InputEvent*>::Call @0x002757d4
// null-checks the resolved pointer and returns 0). Never add a port-side
// "skip unbound mappers" guard on top of it.
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

    // v1.6.1 InputActionMapper::SetCallback @0x00275690 — two instructions:
    //   add r0,r0,#0x20 ; b StackAllocatedPointer<BaseDelegate,32>::operator=
    // i.e. a plain OVERWRITE of the single 36-byte delegate at +0x20. There is no
    // append: one mapper holds exactly ONE callback, so binding a second handler to
    // the same action name silently replaces the first.
    void SetCallback(const InputDeviceCallback& cb);

    bool      m_Enabled;           // +0x00  strb #1 in ctor
    uint32_t  m_ActionHash;        // +0x04  OR'd action+flag word (ctor param)
    uint32_t  m_ConfigSourceHash;  // +0x08
    // +0x0c..+0x1c are a verbatim copy of the ctor's by-value InputEvent words
    // 0..4 (ctor @0x002756b0: `ldmia lr!,{r0-r3} / stmia r12!,{r0-r3}` into
    // this+0x0c, then word 4 into this+0x1c). ProcessEvent therefore compares
    // template-word-N against event-word-N, field for field.
    uint32_t  m_ActionMask;        // +0x0c  event word 0 = InputEvent::m_Flags
    uint32_t  m_MatchValue;        // +0x10  event word 1 = m_Tag | m_KeyCode<<16
    uint32_t  m_KeyMask;           // +0x14  event word 2 = m_Value / m_KeyId
    uint32_t  m_Param4;            // +0x18  event word 3 = m_Delta (float bits)
    uint32_t  m_Param5;            // +0x1c  event word 4 = m_Stamp
    // +0x20  StackAllocatedPointer<BaseDelegate,32> — 32-byte inline buffer
    //        (+0x20..+0x3f) plus its own mode flag at +0x40. Empty = flag set /
    //        resolved pointer NULL; bound = object placement-constructed IN the
    //        buffer. Mortar::Delegate1 is that layout, so this is NOT a
    //        BaseDelegate* and must not be modelled as one.
    InputDeviceCallback m_Callback; // +0x20  36 bytes (0x24) both sides; its m_bInline lands at +0x40
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
    virtual void              Reset() = 0;
    virtual void              SetQueueEventsUntilUpdate(bool v) = 0;
    virtual void              SetSendDownCallbacksEachUpdate(bool v) = 0;
    virtual void              OnAxisExtentsChanged() = 0;
    virtual InputDeviceTypes  GetDeviceType() = 0;

    // v1.6.1 InputDevice::InputDevice @0x002759a8.
    InputDevice();

    // ---- NON-VIRTUAL base methods (no vtable slot in the binary) ----

    // v1.6.1 InputDevice::AxisEvent @0x0027582c.
    // Packs a 0x14-byte axis InputEvent and runs it through CheckActions:
    //   m_Flags = mask | 0x20000, m_Tag = tag, m_KeyCode = (uint8)axisId,
    //   m_Value = value, m_Delta = delta, m_Stamp = stamp.
    // Params are the binary's, 1-based EXCLUDING `this` — only that reading makes
    // the axis stamp/tag pair agree with ButtonPressed's (see InputEvent.h).
    void AxisEvent(long axisId, unsigned long mask, float value, float delta,
                   unsigned long stamp, long tag);

    // v1.6.1 InputDevice::ButtonPressed @0x00275864.
    // Packs a 0x14-byte button InputEvent and runs it through CheckActions:
    //   m_Flags = mask | 0x10000, event word 1 (m_Tag|m_KeyCode<<16) = tagWord,
    //   m_KeyId = key, m_Delta = value, m_Stamp = stamp.
    void ButtonPressed(unsigned long key, unsigned long mask, float value,
                       unsigned long stamp, long tagWord);

    // v1.6.1 InputDevice::CheckActions @0x002757fc.
    // Iterates m_ActionMappers, calls ProcessEvent per element. UNCONDITIONAL:
    // no boundness filter, no short-circuit on a true return.
    void CheckActions(InputEvent* event);

    // v1.6.1 InputDevice::ClearActions @0x002758b0.
    // Removes every mapper whose m_ConfigSourceHash matches `configSourceHash`
    // (or all of them when the argument is 0). `last` is true on the final device
    // of the InputManager broadcast; only that device deletes the mappers, because
    // InputManager::AddActionMapper @0x00243894 handed the SAME pointer to each
    // device. NOT the same symbol as InputManager::ClearActions @0x002441e0.
    void ClearActions(unsigned long configSourceHash, bool last);

    // v1.6.1 InputDevice::RegisterInputCallback @0x002759f4.
    // Walks m_ActionMappers and calls SetCallback on EVERY mapper whose
    // m_ActionHash equals `actionHash`. There is no insert-on-miss: an action name
    // that Input/Input.txt does not declare binds nothing and is silently dropped.
    void RegisterInputCallback(unsigned long actionHash, InputDeviceCallback cb);

    // +0x00: implicit vptr (port) / explicit fns* (binary) — layout equivalent
    // +0x04: std::list<InputActionMapper*> m_ActionMappers (8 bytes, Sourcery 2010q1)
    std::list<InputActionMapper*> m_ActionMappers;  // +0x04
};

} // namespace Mortar

#if defined(__bada__)
static_assert(offsetof(Mortar::InputActionMapper, m_Enabled)          == 0x00, "InputActionMapper::m_Enabled offset");
static_assert(offsetof(Mortar::InputActionMapper, m_ActionHash)       == 0x04, "InputActionMapper::m_ActionHash offset");
static_assert(offsetof(Mortar::InputActionMapper, m_ConfigSourceHash) == 0x08, "InputActionMapper::m_ConfigSourceHash offset");
static_assert(offsetof(Mortar::InputActionMapper, m_ActionMask)       == 0x0c, "InputActionMapper::m_ActionMask offset");
static_assert(offsetof(Mortar::InputActionMapper, m_MatchValue)       == 0x10, "InputActionMapper::m_MatchValue offset");
static_assert(offsetof(Mortar::InputActionMapper, m_KeyMask)          == 0x14, "InputActionMapper::m_KeyMask offset");
static_assert(offsetof(Mortar::InputActionMapper, m_Param4)           == 0x18, "InputActionMapper::m_Param4 offset");
static_assert(offsetof(Mortar::InputActionMapper, m_Param5)           == 0x1c, "InputActionMapper::m_Param5 offset");
// +0x20 is the StackAllocatedPointer<BaseDelegate,32> itself; its own mode flag
// lands at +0x40, which the 0x44 total below pins (0x20 + 0x24 == 0x44).
static_assert(offsetof(Mortar::InputActionMapper, m_Callback)         == 0x20, "InputActionMapper::m_Callback offset");
static_assert(sizeof(Mortar::InputActionMapper) == 0x44, "InputActionMapper size mismatch");
static_assert(sizeof(Mortar::InputDevice) == 12, "InputDevice size mismatch");
#endif

#endif // FN_ENGINE_INPUT_INPUTDEVICE_H
