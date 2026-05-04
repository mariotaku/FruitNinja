#ifndef FN_ENGINE_INPUT_INPUTMANAGER_H
#define FN_ENGINE_INPUT_INPUTMANAGER_H

// Analysed: 2026-05-04T00:00

// InputManager — binary @ 0x00196980 area.
// sizeof 0x10: vfn-table(4) + m_loadingConfig(1) + m_inUpdate(1) + pad(2) +
//              m_inputDevices std::list<InputDevice*>(8, Sourcery 2010q1) = 0x10 total.
//
// Architecture: broadcaster over std::list<InputDevice*>.
// RegisterInputCallback forwards to each device (bindings live on device, not manager).
// Update gates on m_loadingConfig, sets m_inUpdate, broadcasts Update(dt).

#include "input/InputDevice.h"
#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <list>
#include <cstdint>

// Callback type alias (matches binary Delegate1<bool, InputEvent*>).
typedef Delegate1<bool, InputEvent*> InputCallback;

namespace Mortar {

class InputManager {
public:
    static InputManager* s_instance;

    // Binary @ 0x00196980 — ctor: both flags = 0.
    InputManager();
    // Binary @ 0x00196924 — dtor: list dtor only; does NOT call Destroy.
    ~InputManager();

    static InputManager* GetInstance();

    // Binary @ 0x00196cc8 — Init: alloc InputDeviceBada, dev->Init(flags), push_back.
    void Init(unsigned long flags);

    // Binary @ 0x001968a0 — Destroy: clear flags, ClearActions(all=true) on first
    //   device, then Destroy+dtor on each device, list.clear().
    void Destroy();

    // Binary @ 0x00196138 — Update: gate m_loadingConfig, m_inUpdate=true,
    //   broadcast Update(dt) via device vtable slot +0x0c, m_inUpdate=false.
    void Update(float dt);

    // Binary @ 0x001969d8 — Defunct: input config file — Bada-only; binary @ 0x001969d8
    int LoadConfigFile(const char* path);

    // Binary @ 0x001960f8 — AddActionMapper: broadcast to devices.
    void AddActionMapper(InputActionMapper* mapper);

    // Binary @ 0x001961d0 — ClearActions: broadcast InputDevice::ClearActions(hash, last=true on final).
    void ClearActions(unsigned long actionHash);

    // Binary @ 0x00195fe8 — HasInputDevice: search devices by GetDeviceType.
    bool HasInputDevice(InputDeviceTypes type, InputDevice** out);

    // Binary @ 0x00196bc8 — OnAxisExtentsChanged: broadcast.
    void OnAxisExtentsChanged();

    // Binary @ 0x00196228 — ParseAction: lookup table of 6 hashes -> flag.
    //   Dead unless config-file parsing enabled.
    unsigned long ParseAction(unsigned long hash);

    // Binary @ 0x0019630c — ParseKey: lookup of 60 key-name hashes -> bitmask.
    //   Dead unless config-file parsing enabled.
    unsigned long ParseKey(unsigned long hash);

    // Binary @ 0x0019683c — RegisterInputCallback: broadcast to devices.
    // NB: 2-param signature — NO actionFlags 3rd param (port previously had
    // a fictitious 3rd param; corrected here per RE evidence).
    // DIFFERS: original = per-device binding store, see Binary @ 0x0019683c
    void RegisterInputCallback(unsigned long actionHash, InputCallback cb);

    // Binary @ 0x00196194 — ResetDevices: broadcast Reset().
    void ResetDevices();

    // Binary @ 0x0019603c — SetQueueEventsUntilUpdate: broadcast.
    void SetQueueEventsUntilUpdate(bool v);

    // Binary @ 0x0019607c — SetSendDownCallbacksEachUpdate: broadcast.
    void SetSendDownCallbacksEachUpdate(bool v);

    // Binary @ 0x00195fd8 — return (c - 0x20) < 0x90.
    static bool ValidCharacter(unsigned char c);

    // Port-side: dispatch an InputEvent through all devices.
    // Not a binary method — InputTranslatorSDL drives dispatch here.
    void DispatchEvent(InputEvent* event);

    // Port-side: dispatch to all devices (global event, no hash filter).
    void DispatchGlobal(InputEvent* event);

    // Struct fields (Binary @ 0x00196980):
    bool m_loadingConfig;   // +0x04
    bool m_inUpdate;        // +0x05
    // +0x06..0x07 padding
    std::list<InputDevice*> m_inputDevices;  // +0x08 (8B, Sourcery 2010q1 pre-C++11)
};

// TODO: 0x00196980 — port InputManager lacks the binary's vptr at +0x00 (no
// virtual methods). Cross-build measures sizeof=16 vs binary's 20; m_inputDevices
// at +0x04 vs binary's +0x08. Adding a static_assert here today would fire.
// Convert to a polymorphic pseudo-vtable (Mortar fn-table at +0x00) per the
// binary's InputManagerFns layout to enable the assert.

} // namespace Mortar

#endif // FN_ENGINE_INPUT_INPUTMANAGER_H
