#ifndef FN_ITEM_PARSE_UTIL_H
#define FN_ITEM_PARSE_UTIL_H

// Item XML parse helpers -- CloneString, CompareWords, ParseColour.
// Used by ItemInfo::Parse and SlashModInfo::Parse.
//
// Binary refs:
//   CloneString    0x001141c0
//   CompareWords   _Z12CompareWordsPKcS0_
//   ParseColour    _Z11ParseColourR6ColourPKc

#include "engine/math/Colour.h"
#include <cstdlib>
#include <cstring>

// CloneString @ 0x001141c0
// Binary: if src != NULL, heap-dup into *dst; else *dst = NULL
inline void CloneString(char** dst, const char* src) {
    if (dst == nullptr) return;
    if (src == nullptr) {
        *dst = nullptr;
    } else {
        *dst = strdup(src);
    }
}

// CompareWords -- returns 1 when strcmp(a, b) == 0 (strings equal), 0 otherwise.
// Binary: _Z12CompareWordsPKcS0_
int CompareWords(const char* a, const char* b);

// ParseColour -- parse "R,G,B" or "R,G,B,A" string into a Colour struct.
// Binary: _Z11ParseColourR6ColourPKc  (Colour passed by ref)
void ParseColour(Colour& out, const char* str);

#endif // FN_ITEM_PARSE_UTIL_H
