#include "GameMode.h"
#include "engine/util/StringHash.h"
#include <cstring>

// v1.6.1 GetModeName @0x0011bac0 (_Z11GetModeName9GAME_MODE)
// Returns the ASCII mode-name string used to construct per-mode stat keys.
const char* GetModeName(GAME_MODE gameMode) {
    static const char* s_Names[] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
    if ((unsigned)gameMode < 4) return s_Names[gameMode];
    return "UNKNOWN";
}

// ASM-spec v1.6.1 ParseGameMode @0x0011bf6c (_Z13ParseGameModem)
// Hash -> mode index 0..3 (CLASSIC/CASINO/ARCADE/ZEN); unrecognized hash -> 4.
unsigned int ParseGameMode(unsigned long nameHash) {
    static unsigned long s_Names[4] = {
        StringHash("CLASSIC"), StringHash("CASINO"),
        StringHash("ARCADE"),  StringHash("ZEN")
    };
    for (int i = 0; i < 4; i++) {
        if (s_Names[i] == nameHash) return (unsigned int)i;
    }
    return 4u;
}

// ParseModeMask -- v1.6.1 ParseModeMask @0x0014f320.
// Parses a comma-separated mode-name string into a bitmask.
// Each recognized token ORs in its single mode bit. A non-empty token that
// matches none of the four names (binary: ParseGameMode returns sentinel 4,
// GetModeBitMask(4) == -1) ORs in the WILDCARD (all bits), not zero -- so an
// XML "mode" value like "ALL"/"ANY" allows every mode, matching the binary.
// Null or empty input returns 0xFFFFFFFF (all modes) via the early return.
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
        if (len > 0) {
            bool matched = false;
            for (int i = 0; i < s_Count; ++i) {
                if ((int)strlen(s_Tokens[i]) == len &&
                    strncmp(start, s_Tokens[i], (size_t)len) == 0) {
                    mask |= (1u << (unsigned)i);
                    matched = true;
                    break;
                }
            }
            if (!matched) mask |= 0xFFFFFFFFu;
        }
        if (*p == ',') ++p;
    }
    return mask;
}
