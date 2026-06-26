#include "GameMode.h"
#include <cstring>

// GetModeName -- binary: _Z11GetModeName9GAME_MODE v1.6.1 @0x0010b15c
// Returns the ASCII mode-name string used to construct per-mode stat keys.
const char* GetModeName(GAME_MODE gameMode) {
    static const char* s_Names[] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
    if ((unsigned)gameMode < 4) return s_Names[gameMode];
    return "UNKNOWN";
}

// ParseModeMask -- v1.6.1 @0x00116674.
// Parses a comma-separated mode-name string into a bitmask.
// Each token is compared against mode names; matching modes set their bit.
// Null or empty input returns 0xFFFFFFFF (all modes).
uint32_t ParseModeMask(const char* modeStr) {
    if (!modeStr || modeStr[0] == '\0') return 0xFFFFFFFFu;

    static const char* s_Tokens[] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
    static const int   s_Count = 4;

    uint32_t mask = 0;
    const char* p = modeStr;
    while (*p) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') ++p;
        // Extract token up to comma or end
        const char* start = p;
        while (*p && *p != ',') ++p;
        int len = (int)(p - start);
        // Trim trailing whitespace
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) --len;
        // Match against known tokens
        for (int i = 0; i < s_Count; ++i) {
            if ((int)strlen(s_Tokens[i]) == len &&
                strncmp(start, s_Tokens[i], (size_t)len) == 0) {
                mask |= (1u << (unsigned)i);
                break;
            }
        }
        if (*p == ',') ++p;
    }
    return mask;
}
