#ifndef FN_INPUT_MANAGER_H
#define FN_INPUT_MANAGER_H

//
// InputManager — port of Mortar::InputManager
// Original: singleton at 0x19694c, LoadConfigFile at 0x1969d8,
//           RegisterInputCallback at 0x19683c
//
// In the port: SDL events are translated to InputEvents and dispatched
// through the same callback system. Config file parsing is replaced
// by direct registration in GameTaskInitInput.
//

#include "input/InputEvent.h"
#include <functional>
#include <vector>
#include <cstdint>
#include <cstddef>

// Callback type: matches original Delegate1<bool, InputEvent*>
// Returns true if event was consumed
typedef std::function<bool(InputEvent*)> InputCallback;

struct InputBinding {
    uint32_t actionHash;     // StringHash of action name
    uint32_t actionFlags;    // INPUT_ACTION_DOWN/MOVE/UP mask
    InputCallback callback;
};

class InputManager {
public:
    static InputManager* s_instance;

    InputManager() { s_instance = this; }
    ~InputManager() { s_instance = nullptr; }

    static InputManager* GetInstance() { return s_instance; }

    // Matches RegisterInputCallback (0x19683c)
    // actionHash = StringHash("TouchDown_0"), etc.
    // flags = which event types this callback handles
    void RegisterInputCallback(uint32_t actionHash, uint32_t actionFlags,
                               InputCallback callback) {
        InputBinding b;
        b.actionHash = actionHash;
        b.actionFlags = actionFlags;
        b.callback = callback;
        m_bindings.push_back(b);
    }

    // Matches ClearActions (0x1961d0)
    void ClearActions() {
        m_bindings.clear();
    }

    // Dispatch an InputEvent to all matching callbacks
    // Matches InputDevice::CheckActions → InputActionMapper::ProcessEvent
    void DispatchEvent(InputEvent* event) {
        for (size_t i = 0; i < m_bindings.size(); i++) {
            InputBinding& b = m_bindings[i];
            if (b.actionHash == event->actionHash &&
                (b.actionFlags & event->actionFlags)) {
                if (b.callback(event))
                    return;  // consumed
            }
        }
    }

    // Convenience: dispatch to all bindings matching an action flag pattern
    // (for global events like "TouchScreen" that don't have a specific hash)
    void DispatchGlobal(InputEvent* event) {
        for (size_t i = 0; i < m_bindings.size(); i++) {
            InputBinding& b = m_bindings[i];
            if (b.actionFlags & event->actionFlags) {
                b.callback(event);
            }
        }
    }

private:
    std::vector<InputBinding> m_bindings;
};

#endif
