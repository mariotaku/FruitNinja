#ifndef FN_ENGINE_INPUT_INPUTMANAGER_H
#define FN_ENGINE_INPUT_INPUTMANAGER_H

// InputManager — v1.6.1 Mortar::InputManager @0x0024371c..0x00244264.
// sizeof 0x14 (20 bytes):
//   vptr    (4)  — binary isPolymorphic=true; vtableAddr 0x001eb318;
//                  first vfunc slot 0x002436e8 (SetQueueEventsUntilUpdate).
//   field_0x4 (1, bool) — m_loadingConfig
//   field_0x5 (1, bool) — m_inUpdate
//   pad [0x06..0x07] (2)
//   m_inputDevices std::vector<InputDevice*> (12: begin/end/capEnd) — +0x08
//
// Architecture: broadcaster over std::vector<InputDevice*>.
// RegisterInputCallback forwards to each device (bindings live on device, not manager).
// Update gates on m_loadingConfig, sets m_inUpdate, broadcasts Update(dt).

#include "input/InputDevice.h"
#include "input/InputEvent.h"
#include "util/Delegate.h"
#include <vector>
#include <cstdint>

// Callback type alias (matches binary Mortar::Delegate1<bool, InputEvent*>).
typedef Mortar::Delegate1<bool, InputEvent*> InputCallback;

namespace Mortar {

class InputManager {
public:
    static InputManager* s_instance;

    // v1.6.1 Mortar::InputManager::InputManager @0x0024375c — ctor: both flags = 0.
    InputManager();
    // v1.6.1 Mortar::InputManager::~InputManager @0x0024371c — dtor: vector dtor only; does NOT call Destroy.
    // Virtual to match binary isPolymorphic=true (vptr at +0x00).
    // Binary vtable slot 0: 0x001eb318 area (dtor is implicitly first slot).
    virtual ~InputManager();

    static InputManager* GetInstance();

    // v1.6.1 Mortar::InputManager::Init @0x002447d4 — Init: alloc InputDeviceBada, dev->Init(flags), push_back.
    void Init(unsigned long flags);

    // v1.6.1 Mortar::InputManager::Destroy @0x00243798 — Destroy: clear flags, ClearActions(all=true) on first
    //   device, then Destroy+dtor on each device, vector.clear().
    void Destroy();

    // v1.6.1 Mortar::InputManager::Update @0x00243838 — Update: gate m_loadingConfig, m_inUpdate=true,
    //   broadcast Update(dt) via device vtable slot +0x0c, m_inUpdate=false.
    void Update(float dt);

    // Defunct: input config file — no-op stub; v1.6.1 Mortar::InputManager::LoadConfigFile @ 0x002442fc
    int LoadConfigFile(const char* path);

    // v1.6.1 Mortar::InputManager::AddActionMapper @0x00243894 — AddActionMapper: broadcast to devices.
    void AddActionMapper(InputActionMapper* mapper);

    // v1.6.1 Mortar::InputManager::ClearActions @0x002441e0 — broadcast
    // Mortar::InputDevice::ClearActions(hash, last=true on final).
    void ClearActions(unsigned long actionHash);

    // v1.6.1 Mortar::InputManager::HasInputDevice @0x00244298 — search the device
    // list for the first device whose GetDeviceType() matches `type`.
    // Returns true on a match, and writes that device to *out when out != NULL.
    // On no match it returns false and leaves *out UNTOUCHED — callers must
    // pre-initialise their out variable, the binary does not null it.
    bool HasInputDevice(InputDeviceTypes type, InputDevice** out);

    // v1.6.1 Mortar::InputManager::OnAxisExtentsChanged @0x00244238 — OnAxisExtentsChanged: broadcast.
    void OnAxisExtentsChanged();

    // v1.6.1 Mortar::InputManager::ParseAction @0x00244060 — lookup table of 7
    //   action-name hashes -> event-type flag.
    //   Dead unless config-file parsing enabled.
    unsigned long ParseAction(unsigned long hash) const;

    // v1.6.1 Mortar::InputManager::ParseKey @0x002438c8 — lookup of 61 (0x3d)
    //   key-name hashes -> KEY CODE (not a bitmask). Covers Mouse buttons/axes,
    //   Touch1..16, TouchAxisX/Y1..16 and AccelAxisX/Y/Z; see the .cpp for the table.
    //   Dead unless config-file parsing enabled.
    unsigned long ParseKey(unsigned long hash) const;

    // v1.6.1 Mortar::InputManager::RegisterInputCallback @0x0024475c — broadcast to devices.
    // NB: 2-param signature — NO actionFlags 3rd param (port previously had
    // a fictitious 3rd param; corrected here per RE evidence).
    void RegisterInputCallback(unsigned long actionHash, InputCallback cb);

    // v1.6.1 Mortar::InputManager::ResetDevices @0x0024380c — ResetDevices: broadcast Reset().
    void ResetDevices();

    // v1.6.1 Mortar::InputManager::SetQueueEventsUntilUpdate @0x002436e8 — broadcast.
    void SetQueueEventsUntilUpdate(bool v);

    // v1.6.1 Mortar::InputManager::SetSendDownCallbacksEachUpdate @0x00244264 — broadcast.
    void SetSendDownCallbacksEachUpdate(bool v);

    // Binary @ 0x00195fd8 — return (c - 0x20) < 0x90.
    // Non-static const instance method to match binary mangled ABI (_ZNK...ValidCharacterEh);
    // no callers currently invoke it, so the shape change is call-site-free.
    bool ValidCharacter(unsigned char c) const;

    // Port-side: dispatch an InputEvent through all devices.
    // Not a binary method — InputTranslatorSDL drives dispatch here.
    void DispatchEvent(InputEvent* event);

    // Port-side: dispatch to all devices (global event, no hash filter).
    void DispatchGlobal(InputEvent* event);

    // Struct fields matching binary layout (vptr occupies +0x00..+0x03):
    bool m_loadingConfig;                    // +0x04
    bool m_inUpdate;                         // +0x05
    // +0x06..0x07 padding (implicit)
    std::vector<InputDevice*> m_inputDevices; // +0x08 (12B: begin/end/capEnd)
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::InputManager) == 20, "InputManager size mismatch");
#endif

// Global-namespace free functions implemented in InputManager.cpp.
// Both are identity no-ops in v1.6.1 — real coordinate transform lives in InputDeviceBada.

// ASM-spec v1.6.1 DefaultKeyCallback @0x18cd6c: returns key unchanged (identity no-op).
const char* DefaultKeyCallback(const char* key);

// ASM-spec v1.6.1 TransformInput @0x1a03ac: binary body is bx lr — identity; returns ev unchanged.
// Raw-touch to game-coord rotation is done in the platform InputDevice, NOT here.
InputEvent* TransformInput(InputEvent* ev, float& x, float& y);

#endif // FN_ENGINE_INPUT_INPUTMANAGER_H
