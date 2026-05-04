// Analysed: 2026-05-04T00:00

#include "input/InputManager.h"
#include "input/InputDeviceBada.h"
#include <cstddef>

InputManager* InputManager::s_instance = nullptr;

// Binary @ 0x00196980 — ctor: both flags = 0, list default-inits.
InputManager::InputManager()
    : m_loadingConfig(false)
    , m_inUpdate(false)
{
    s_instance = this;
}

// Binary @ 0x00196924 — dtor: list dtor only; does NOT call Destroy.
InputManager::~InputManager() {
    s_instance = nullptr;
    // Note: binary does NOT call Destroy() in dtor — list goes out of scope only.
    // Devices leaked intentionally (matches binary behavior at 0x00196924).
}

InputManager* InputManager::GetInstance() {
    return s_instance;
}

// Binary @ 0x00196cc8 — Init: alloc InputDeviceBada via new, dev->Init(flags), push_back.
void InputManager::Init(unsigned long flags) {
    InputDeviceBada* dev = new InputDeviceBada();
    dev->Init(flags);
    m_inputDevices.push_back(dev);
}

// Binary @ 0x001968a0 — Destroy: clear flags, ClearActions(all=true) on first
//   device only, then Destroy+dtor on each, list.clear().
void InputManager::Destroy() {
    m_loadingConfig = false;
    m_inUpdate = false;
    if (!m_inputDevices.empty()) {
        // Binary: ClearActions on first device only with all=true.
        // TODO: 0x001968a0 — clarify "all" param semantics for ClearActions on device
        InputDevice* first = m_inputDevices.front();
        first->ClearActions(0, true);
    }
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Destroy();
        delete *it;
    }
    m_inputDevices.clear();
}

// Binary @ 0x00196138 — Update: gate on m_loadingConfig, m_inUpdate=true,
//   broadcast Update(dt), m_inUpdate=false.
void InputManager::Update(float dt) {
    if (m_loadingConfig) return;
    m_inUpdate = true;
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Update(dt);
    }
    m_inUpdate = false;
}

// Binary @ 0x001969d8
int InputManager::LoadConfigFile(const char* path) {
    (void)path;
    // Defunct: input config file -- Bada-only; binary @ 0x001969d8
    return 1;
}

// Binary @ 0x001960f8 — broadcast to devices.
void InputManager::AddActionMapper(InputActionMapper* mapper) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->AddActionMapper(mapper);
    }
}

// Binary @ 0x001961d0 — broadcast ClearActions(hash, last=true on final).
void InputManager::ClearActions(unsigned long actionHash) {
    std::list<InputDevice*>::iterator it = m_inputDevices.begin();
    std::list<InputDevice*>::iterator end = m_inputDevices.end();
    while (it != end) {
        std::list<InputDevice*>::iterator next = it;
        ++next;
        bool last = (next == end);
        (*it)->ClearActions(actionHash, last);
        it = next;
    }
}

// Binary @ 0x00195fe8 — search devices by GetDeviceType.
bool InputManager::HasInputDevice(InputDeviceTypes type, InputDevice** out) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        if ((*it)->GetDeviceType() == type) {
            if (out) *out = *it;
            return true;
        }
    }
    if (out) *out = NULL;
    return false;
}

// Binary @ 0x00196bc8 — broadcast.
void InputManager::OnAxisExtentsChanged() {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->OnAxisExtentsChanged();
    }
}

// Binary @ 0x00196228 — lookup table of 6 action-name hashes -> action flag.
// Dead unless config-file parsing enabled.
unsigned long InputManager::ParseAction(unsigned long /*hash*/) {
    // TODO: 0x00196228 — action hash lookup table (6 entries)
    return 0;
}

// Binary @ 0x0019630c — lookup of 60 key-name hashes -> key bitmask.
// Dead unless config-file parsing enabled.
unsigned long InputManager::ParseKey(unsigned long /*hash*/) {
    // TODO: 0x0019630c — key hash lookup table (60 entries)
    return 0;
}

// Binary @ 0x0019683c — broadcast to devices (2-param; bindings live on device).
// DIFFERS: original = per-device binding store, see Binary @ 0x0019683c
void InputManager::RegisterInputCallback(unsigned long actionHash, InputCallback cb) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        InputDeviceCallback devcb;
        devcb = cb;
        (*it)->RegisterInputCallback(actionHash, devcb);
    }
}

// Binary @ 0x00196194 — broadcast Reset().
void InputManager::ResetDevices() {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Reset();
    }
}

// Binary @ 0x0019603c — broadcast.
void InputManager::SetQueueEventsUntilUpdate(bool v) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->SetQueueEventsUntilUpdate(v);
    }
}

// Binary @ 0x0019607c — broadcast.
void InputManager::SetSendDownCallbacksEachUpdate(bool v) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->SetSendDownCallbacksEachUpdate(v);
    }
}

// Binary @ 0x00195fd8 — return (c - 0x20) < 0x90.
bool InputManager::ValidCharacter(unsigned char c) {
    return (unsigned char)(c - 0x20u) < 0x90u;
}

// Port-side: dispatch through all devices.
void InputManager::DispatchEvent(InputEvent* event) {
    for (std::list<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->DispatchEvent(event);
    }
}

// Port-side: global dispatch (no hash filter — all bindings on all devices).
void InputManager::DispatchGlobal(InputEvent* event) {
    // Route same as DispatchEvent; device-side DispatchEvent filters by hash.
    // For global events (no specific hash), callers should set event->actionHash = 0
    // or use a dedicated broadcast path.
    // TODO: refine global dispatch semantics when full binary dispatch path is ported.
    DispatchEvent(event);
}
