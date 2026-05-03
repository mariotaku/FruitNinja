#ifndef FN_HUD_KEYBOARD_CONTROL_H
#define FN_HUD_KEYBOARD_CONTROL_H

// Defunct: KeyboardControl — Bada native on-screen keyboard.
// Binary ctor @ 0x0014649c; sizeof 0xD4.
// Port specific: bypass entirely on host platforms; if a future caller needs
// text input, route through SDL2 SDL_StartTextInput() instead. This stub
// preserves the call shape so callers compile but provides no UI.

#include "HUDControl3d.h"
#include <cstdint>

class KeyboardControl : public HUDControl3d {
public:
    KeyboardControl() {}
    ~KeyboardControl() override {}

private:
    // Binary sizeof is 0xD4; HUDControl3d is 0x7C. Round up to 0xE0 for safety.
    static const int kPadSize = 0xE0 - sizeof(HUDControl3d);
    uint8_t pad[kPadSize];
    static_assert(sizeof(HUDControl3d) <= 0xD4, "HUDControl3d exceeds binary KeyboardControl size");
};

#endif // FN_HUD_KEYBOARD_CONTROL_H
