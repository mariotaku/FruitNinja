#ifndef FN_ENGINE_UTIL_LANGUAGEARGS_H
#define FN_ENGINE_UTIL_LANGUAGEARGS_H

// ParseLanguageArg -- parse a lang= value to a language flag (0..14).
//
// Accepts any of three forms, tried in order:
//   - An all-digit string: parsed via atoi, clamped to 0..14.
//   - A short ISO code (e.g. "zh", "de", "en_uk"): matched case-insensitively
//     against the table below; only flags 0..13 are mapped (those are the only
//     loadable translation slots). Short codes match the kLangShort[] spelling
//     used in test screenshot filenames so --lang=<code> produces <code>.png.
//   - A full language name (e.g. "french", "korean"): matched case-insensitively
//     against the kLanguageSuffix table via StringTable::LanguageFlagFromName.
//
// Returns -1 if the value is empty, out of range, or not a recognised name/code.
// Port specific: test/debug helper; no binary counterpart.

#include "StringTable.h"
#include <cstdlib>
#include <cstring>

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

    // Lowercase the input once; used by both short-code and long-name checks.
    char lower[32];
    int li = 0;
    while (s[li] && li < 31) {
        char c = s[li];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        lower[li] = c;
        ++li;
    }
    lower[li] = '\0';

    // Short ISO codes -- flags 0..13 only (flags 14+ have no loadable data).
    // Ordering mirrors kLangShort[] in test screenshot builders so
    // --lang=<code> round-trips to the output filename <code>.png.
    struct ShortEntry { const char* code; int flag; };
    static const ShortEntry kShortCodes[] = {
        { "en",    0  },  // english_us
        { "en_uk", 1  },  // english_uk
        { "fr",    2  },  // french
        { "es",    3  },  // spanish
        { "de",    4  },  // german
        { "it",    5  },  // italian
        { "nl",    6  },  // dutch
        { "sv",    7  },  // swedish
        { "da",    8  },  // danish
        { "nb",    9  },  // norwegian
        { "fi",    10 },  // finnish
        { "ko",    11 },  // korean
        { "ja",    12 },  // japanese
        { "zh",    13 },  // chinese
    };
    static const int kShortCodeCount = 14;
    for (int j = 0; j < kShortCodeCount; ++j) {
        if (strcmp(lower, kShortCodes[j].code) == 0)
            return kShortCodes[j].flag;
    }

    // Fall back to full language name (e.g. "french", "english_us").
    // LanguageFlagFromName does its own lowercase conversion internally.
    return Mortar::StringTable::LanguageFlagFromName(s);
}

#endif // FN_ENGINE_UTIL_LANGUAGEARGS_H
