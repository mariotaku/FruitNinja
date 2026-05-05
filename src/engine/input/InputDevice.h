#ifndef FN_ENGINE_INPUT_INPUTDEVICE_H
#define FN_ENGINE_INPUT_INPUTDEVICE_H

// Analysed: 2026-05-04T00:00

#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <cstdint>

// Binary @ 0x00196980 area — InputDevice base interface.
// Concrete subclass on Bada: InputDeviceBada (composes a Mortar::Touch).
//
// The port uses this as a vtable-compatible base only; bindings live on
// InputDeviceBada.  Methods gated with // TODO: where body is not yet
// ported.

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

// Forward-declare InputActionMapper (internals not ported).
class InputActionMapper;

class InputDevice {
public:
    virtual ~InputDevice() {}

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
    virtual void              Destroy() = 0;
    virtual void              Update(float dt) = 0;
    virtual void              AddActionMapper(InputActionMapper* mapper) = 0;
    virtual void              ClearActions(unsigned long actionHash, bool last) = 0;
    virtual void              RegisterInputCallback(unsigned long actionHash,
                                                    InputDeviceCallback cb) = 0;
    virtual void              Reset() = 0;
    virtual void              SetQueueEventsUntilUpdate(bool v) = 0;
    virtual void              SetSendDownCallbacksEachUpdate(bool v) = 0;
    virtual void              OnAxisExtentsChanged() = 0;
    virtual InputDeviceTypes  GetDeviceType() const = 0;

    // Port-side dispatch helper: fire all callbacks matching this event.
    // Called by InputManager::DispatchEvent.  Not a binary vtable slot —
    // InputTranslatorSDL drives dispatch by calling DispatchEvent on the
    // manager which routes here.
    // TODO: 0x00195764 — route via SendIndividualTouchCallbacks once ported.
    virtual void              DispatchEvent(InputEvent* event) = 0;

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: InputDevice::AddAction -- auto stub from binary missing-symbol set
    void AddAction(InputActionMapper*);
    // STUB: InputDevice::AxisEvent -- auto stub from binary missing-symbol set
    void AxisEvent(int, unsigned int, float, float, unsigned int, int);
    // STUB: InputDevice::ButtonPressed -- auto stub from binary missing-symbol set
    void ButtonPressed(unsigned int, unsigned int, float, unsigned int, int);
    // ---- end AUTO-STUB MERGE ----
};

} // namespace Mortar

#endif // FN_ENGINE_INPUT_INPUTDEVICE_H
