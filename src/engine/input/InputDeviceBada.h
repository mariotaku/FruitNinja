#ifndef FN_ENGINE_INPUT_INPUTDEVICEBADA_H
#define FN_ENGINE_INPUT_INPUTDEVICEBADA_H

// Analysed: 2026-05-04T00:00

#include "input/InputDevice.h"
#include "input/Touch.h"
#include <list>
#include <cstdint>

// Binary @ 0x00196cc8 — InputDeviceBada: concrete InputDevice for Bada/touch.
// Composes a Mortar::Touch for state tracking, and holds the binding list
// that RegisterInputCallback pushes into.
//
// Bindings (per binary @ 0x0019683c): callbacks are stored per-device,
// not on the manager. The manager broadcasts RegisterInputCallback to each
// device in m_inputDevices.

struct InputDeviceBinding {
    unsigned long         actionHash;
    InputDeviceCallback   callback;
};

class InputDeviceBada : public InputDevice {
public:
    // Binary @ 0x00196cc8 — Init allocs this; touch is composed in-place.
    InputDeviceBada();
    virtual ~InputDeviceBada();

    // InputDevice vtable
    virtual void              Init(unsigned long flags);                         // Binary @ 0x00196cc8 (partial)
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

    // Port-side dispatch.
    virtual void              DispatchEvent(InputEvent* event);

    // The composed Mortar::Touch instance.
    // Binary: InputDeviceBada owns/wraps the Touch; struct layout not
    // fully decompiled — port keeps a pointer to the global Touch singleton
    // until the full struct is RE'd.
    // TODO: 0x00196cc8 — replace with owned Touch once InputDeviceBada layout known.
    Mortar::Touch*            m_touch;

private:
    std::list<InputDeviceBinding> m_bindings;
    bool                          m_queueUntilUpdate;
    bool                          m_sendDownEachUpdate;
};

#endif // FN_ENGINE_INPUT_INPUTDEVICEBADA_H
