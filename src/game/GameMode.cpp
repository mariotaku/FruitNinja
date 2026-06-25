#include "GameMode.h"

// GetModeName -- binary: _Z11GetModeName9GAME_MODE v1.6.1 @0x0010b15c
// Returns the ASCII mode-name string used to construct per-mode stat keys.
const char* GetModeName(GAME_MODE gameMode) {
    static const char* s_Names[] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
    if ((unsigned)gameMode < 4) return s_Names[gameMode];
    return "UNKNOWN";
}
