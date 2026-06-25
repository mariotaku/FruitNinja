#include "ItemParseUtil.h"
#include <cstdlib>
#include <cstdio>

// CompareWords -- returns 1 when a == b (strcmp == 0), 0 otherwise.
// Binary: _Z12CompareWordsPKcS0_ v1.6.1
int CompareWords(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return 0;
    return (strcmp(a, b) == 0) ? 1 : 0;
}

// ParseColour -- parse "R,G,B" or "R,G,B,A" into a Colour.
// Binary: _Z11ParseColourR6ColourPKc v1.6.1  (Colour& ref param)
void ParseColour(Colour& out, const char* str) {
    if (str == nullptr || str[0] == '\0') return;
    int vals[4] = { (int)out.r, (int)out.g, (int)out.b, (int)out.a };
    int count = 0;
    const char* p = str;
    while (*p && count < 4) {
        vals[count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    out.r = (uint8_t)vals[0];
    out.g = (uint8_t)vals[1];
    out.b = (uint8_t)vals[2];
    if (count >= 4) out.a = (uint8_t)vals[3];
}
