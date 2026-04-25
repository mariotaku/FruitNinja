#ifndef FN_ITEM_PARSE_UTIL_H
#define FN_ITEM_PARSE_UTIL_H

// Analysed: 2026-04-25T10:30
//
// Item XML parse helpers — CloneString, CompareWords, ParseColour,
// GETSTRING_CAST_0_STR.  Used by ItemInfo::Parse and SlashModInfo::Parse.
//
// Binary refs:
//   CloneString          0x001141c0
//   CompareWords         (used in ItemInfo::Parse for bool attrs)
//   ParseColour          (parses "R,G,B" or "R,G,B,A" into Colour)
//   GETSTRING_CAST_0_STR (localisation lookup — pass-through stub)
//

#include "engine/math/Colour.h"
#include "engine/util/Localisation.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// CloneString @ 0x001141c0
// Binary: if src != NULL, heap-dup into *dst; else *dst = NULL
// (effectively strdup with null guard)
inline void CloneString(char** dst, const char* src) {
    if (dst == nullptr) return;
    if (src == nullptr) {
        *dst = nullptr;
    } else {
        *dst = strdup(src);
    }
}

// CompareWords — returns non-zero (1) when strcmp(a, b) == 0 (i.e. strings equal).
// Used as: if (CompareWords(trueStr, attr) != 0) → means attr == "true".
// Binary: strcmp-based, returns 0 when NOT equal, 1 when equal.
inline int CompareWords(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return 0;
    return (strcmp(a, b) == 0) ? 1 : 0;
}

// GETSTRING_CAST_0_STR — localisation lookup for a key string.
// Binary: calls GETSTRING_STR(key, 0) -> Mortar::StringTable::GetInfo binary search
// then GetString(HeaderLookup*) -> str_blob offset.  Returns key on miss.
// Port: delegates to Localisation::Get which replicates the same algorithm.
// Refs: GETSTRING_CAST_0_STR @ 0x00109ec0, GETSTRING_STR @ 0x0011fb40
inline const char* GETSTRING_CAST_0_STR(const char* key) {
    return Localisation::Get(key);
}

// ParseColour — parse "R,G,B" or "R,G,B,A" string into a Colour struct.
// Binary stores as BGRA (Colour layout b,g,r,a).
// If str is NULL or empty, leaves *out unchanged.
inline void ParseColour(Colour* out, const char* str) {
    if (out == nullptr || str == nullptr || str[0] == '\0') return;
    int vals[4] = { (int)out->r, (int)out->g, (int)out->b, (int)out->a };
    int count = 0;
    const char* p = str;
    while (*p && count < 4) {
        vals[count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    out->r = (uint8_t)vals[0];
    out->g = (uint8_t)vals[1];
    out->b = (uint8_t)vals[2];
    if (count >= 4) out->a = (uint8_t)vals[3];
}

#endif // FN_ITEM_PARSE_UTIL_H
