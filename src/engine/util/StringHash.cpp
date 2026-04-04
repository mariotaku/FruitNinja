#include "StringHash.h"
#include <cstring>

// Case-insensitive Jenkins lookup3 hash (0x0019c5d4)
// Lowercases A-Z before hashing
uint32_t StringHash(const char* str) {
    size_t len = strlen(str);
    uint32_t a, b, c;
    a = b = 0x9e3779b9; // golden ratio
    c = 0x805 + (uint32_t)len;

    size_t remaining = len;
    const char* p = str;
    uint8_t buf[12];

    // Process 12 bytes at a time
    while (remaining > 11) {
        for (int i = 0; i < 12; i++) {
            uint8_t ch = p[i];
            if (ch >= 'A' && ch <= 'Z') ch += 0x20;
            buf[i] = ch;
        }
        p += 12;

        a += buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
        b += buf[4] + (buf[5] << 8) + (buf[6] << 16) + (buf[7] << 24);
        c += buf[8] + (buf[9] << 8) + (buf[10] << 16) + (buf[11] << 24);

        // mix
        a = (a - c) ^ (c >> 13); a -= b;
        b = (b - a) ^ (a << 8);  b -= c;
        c = (c - b) ^ (b >> 13); c -= a;
        a = (a - c) ^ (c >> 12); a -= b;
        b = (b - a) ^ (a << 16); b -= c;
        c = (c - b) ^ (b >> 5);  c -= a;
        a = (a - c) ^ (c >> 3);  a -= b;
        b = (b - a) ^ (a << 10); b -= c;
        c = (c - b) ^ (b >> 15); c -= a;

        remaining -= 12;
    }

    // Add remaining bytes (case-folded)
    c += (uint32_t)len;
    for (uint32_t i = 0; i < remaining; i++) {
        uint8_t ch = p[i];
        if (ch >= 'A' && ch <= 'Z') ch += 0x20;
        buf[i] = ch;
    }

    // Fall-through switch for remaining bytes
    switch (remaining) {
        case 11: c += buf[10] << 24; // fall through
        case 10: c += buf[9] << 16;  // fall through
        case 9:  c += buf[8] << 8;   // fall through
        case 8:  b += buf[7] << 24;  // fall through
        case 7:  b += buf[6] << 16;  // fall through
        case 6:  b += buf[5] << 8;   // fall through
        case 5:  b += buf[4];        // fall through
        case 4:  a += buf[3] << 24;  // fall through
        case 3:  a += buf[2] << 16;  // fall through
        case 2:  a += buf[1] << 8;   // fall through
        case 1:  a += buf[0];
    }

    // Final mix
    a = (a - b) ^ (c >> 13); a -= b;
    b = (b - c) ^ (a << 8);  b -= a;
    c = (c - a) ^ (b >> 13); c -= b;
    a = (a - b) ^ (c >> 12); a -= b;
    b = (b - c) ^ (a << 16); b -= a;
    c = (c - a) ^ (b >> 5);  c -= b;
    a = (a - b) ^ (c >> 3);  a -= b;
    b = (b - c) ^ (a << 10); b -= a;
    c = (c - a) ^ (b >> 15); c -= b;

    return c;
}
