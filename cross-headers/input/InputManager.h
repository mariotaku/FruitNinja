// Cross-build stub for input/InputManager.h.
// libstdc++ 4.5.3 has a known bug with vector<InputBinding>::push_back when
// std::function is the value type; avoid instantiating those templates here.
// We don't care about InputManager's behavior for asm-verifying WaveManager
// or other game-logic .cpp -- we only need the symbols/types.
#ifndef MORTAR_INPUT_MANAGER_H
#define MORTAR_INPUT_MANAGER_H

#include "input/InputEvent.h"
#include <cstdint>
#include <cstddef>

struct InputBinding;
typedef bool (*InputCallback)(InputEvent*);

class InputManager {
public:
    static InputManager* s_instance;
    InputManager();
    ~InputManager();
    static InputManager* GetInstance();
    void RegisterInputCallback(uint32_t, uint32_t, InputCallback);
    void ClearActions();
    void DispatchEvent(InputEvent*);
    void DispatchGlobal(InputEvent*);
};

#endif
