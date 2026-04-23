#ifndef MORTAR_INPUT_MANAGER_H
#define MORTAR_INPUT_MANAGER_H

#include "input/InputEvent.h"
#include <functional>
#include <vector>
#include <cstdint>
#include <cstddef>

// Callback type: matches original Delegate1<bool, InputEvent*>
typedef std::function<bool(InputEvent*)> InputCallback;

struct InputBinding {
    uint32_t actionHash;
    uint32_t actionFlags;
    InputCallback callback;
};

// Matches original Mortar::InputManager singleton
// Action-hash callback dispatch system
class InputManager {
public:
    static InputManager* s_instance;

    InputManager() { s_instance = this; }
    ~InputManager() { s_instance = nullptr; }

    static InputManager* GetInstance() { return s_instance; }

    // Matches RegisterInputCallback (0x19683c)
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

    // Dispatch to matching callbacks
    void DispatchEvent(InputEvent* event) {
        for (size_t i = 0; i < m_bindings.size(); i++) {
            InputBinding& b = m_bindings[i];
            if (b.actionHash == event->actionHash &&
                (b.actionFlags & event->actionFlags)) {
                if (b.callback(event))
                    return;
            }
        }
    }

    // Dispatch to all bindings matching flag pattern (global events)
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
