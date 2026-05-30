#ifndef FN_ENGINE_INPUT_INPUTDEVICEBADA_H
#define FN_ENGINE_INPUT_INPUTDEVICEBADA_H

#include "input/InputDevice.h"
#include "input/Touch.h"
#include <list>
#include <cstdint>

// Binary @ 0x001958a4 — InputDeviceBada: concrete InputDevice for Bada/touch.
// sizeof = 28 (0x1c), from operator new(0x1c) @ InputManager::Init 0x00196cd4.
// Layout:
//   +0x00: vptr (port) / InputDevice base (binary fns*)
//   +0x04: std::list<InputActionMapper*> actionMappers (8B, from InputDevice base)
//   +0x0c: uint32_t field_0xc   (ctor-zero)
//   +0x10: uint32_t field_0x10  (ctor-zero)
//   +0x14: uint32_t field_0x14  (ctor-zero)
//   +0x18: uint32_t field_0x18  (ctor-zero)
// Total: 12 (base) + 16 (derived) = 28.

namespace Mortar {

// Port-side binding record — no binary counterpart on InputDeviceBada.
// Used only in the port's dispatch path (DispatchEvent, ClearActions,
// RegisterInputCallback). Fields live below the binary size assert guard.
struct InputDeviceBinding {
    unsigned long         actionHash;
    InputDeviceCallback   callback;
};

class InputDeviceBada : public InputDevice {
public:
    // Binary @ 0x001958a4 — ctor (C1/C2 pair, byte-identical).
    InputDeviceBada();
    virtual ~InputDeviceBada();

    // InputDevice vtable (overrides matching binary slots):
    virtual void              Init(unsigned long flags);
    virtual void              Destroy();
    virtual void              Update(float dt);
    virtual void              AddActionMapper(InputActionMapper* mapper);
    virtual void              ClearActions(unsigned long actionHash, bool last);
    virtual void              RegisterInputCallback(unsigned long actionHash,
                                                    InputDeviceCallback cb);
    virtual void              Reset();
    virtual void              SetQueueEventsUntilUpdate(bool v);
    virtual void              SetSendDownCallbacksEachUpdate(bool v);
    virtual void              OnAxisExtentsChanged();
    virtual InputDeviceTypes  GetDeviceType() const;

    // Port-side dispatch (not a binary vtable slot).
    virtual void              DispatchEvent(InputEvent* event);

    // Binary-faithful derived fields (ctor-zero; semantic types TBD).
    // TODO: 0x001958a4 — resolve semantic types of field_0xc..field_0x18 by
    //   RE'ing vtable slot+8 (fns->field2_0x8) called immediately after ctor
    //   in InputManager::Init @ 0x00196cd4.
    uint32_t field_0xc;    // +0x0c
    uint32_t field_0x10;   // +0x10
    uint32_t field_0x14;   // +0x14
    uint32_t field_0x18;   // +0x18

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
    static_assert(sizeof(InputDeviceBada) == 28, "InputDeviceBada size mismatch");
#endif

    // Port-only fields (tail; not counted in binary sizeof).
    // These implement the port-side callback dispatch path which in the
    // binary goes through InputActionMapper::ProcessEvent (not yet ported).
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    Mortar::Touch*                m_touch;
    std::list<InputDeviceBinding> m_bindings;
    bool                          m_queueUntilUpdate;
    bool                          m_sendDownEachUpdate;
#endif
};

} // namespace Mortar

#endif // FN_ENGINE_INPUT_INPUTDEVICEBADA_H
