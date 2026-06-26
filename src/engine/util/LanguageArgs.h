#ifndef FN_ENGINE_UTIL_LANGUAGEARGS_H
#define FN_ENGINE_UTIL_LANGUAGEARGS_H

// ParseLanguageArg -- parse a lang= value to a language flag (0..14).
//
// Accepts either:
//   - An all-digit string: parsed via atoi, clamped to 0..14.
//   - A language name (e.g. "french", "korean"): matched case-insensitively
//     against the kLanguageSuffix table via StringTable::LanguageFlagFromName.
//
// Returns -1 if the value is empty, out of range, or not a recognised name.
// Port specific: test/debug helper; no binary counterpart.

#include "StringTable.h"
#include <cstdlib>
#include <cctype>

inline int ParseLanguageArg(const char* s) {
    if (!s || s[0] == '\0') return -1;

    // Check if all characters are digits (optional leading digits only -- no sign).
    bool all_digits = true;
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9') { all_digits = false; break; }
    }

    if (all_digits) {
        int flag = atoi(s);
        if (flag < 0 || flag > 14) return -1;
        return flag;
    }

    return Mortar::StringTable::LanguageFlagFromName(s);
}

#endif // FN_ENGINE_UTIL_LANGUAGEARGS_H
