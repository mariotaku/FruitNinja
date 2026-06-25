#include "StringHash.h"

// Case-insensitive Jenkins lookup3 hash.
// v1.6.1 StringHash(const char*, int) @ 0x00252a10 — real impl; len is the byte length
// v1.6.1 StringHash(const char*)      @ 0x00119328 — 1-arg helper (inline in header)
// ASM-verified: 2026-06-12 v1.6.1 binary @ 0x00252a10 / 0x0019c5d4 (re-analyst)
// Verified test vectors (case-insensitive):
//   "watermelon"  -> 0x158bc245
//   "apple_red"   -> 0xdac1f38f
//   "banana"      -> 0x5ff2eb92
//
uint32_t StringHash(const char* str, int len) {
    uint32_t a, b, c;
    a = b = 0x9e3779b9;
    c = 0x805;                          // NO + len here

    int remaining = len;
    const char* p = str;
    uint8_t buf[12];

    while (remaining > 11) {
        for (int i = 0; i < 12; i++) {
            uint8_t ch = (uint8_t)p[i];
            if (ch >= 'A' && ch <= 'Z') ch += 0x20;
            buf[i] = ch;
        }
        p += 12;

        a += buf[0] + ((uint32_t)buf[1] << 8) + ((uint32_t)buf[2] << 16) + ((uint32_t)buf[3] << 24);
        b += buf[4] + ((uint32_t)buf[5] << 8) + ((uint32_t)buf[6] << 16) + ((uint32_t)buf[7] << 24);
        c += buf[8] + ((uint32_t)buf[9] << 8) + ((uint32_t)buf[10] << 16) + ((uint32_t)buf[11] << 24);

        // Binary mix: both subtractions before the XOR (single expression each).
        a = ((a - c) - b) ^ (c >> 13);
        b = ((b - c) - a) ^ (a << 8);
        c = ((c - a) - b) ^ (b >> 13);
        a = ((a - b) - c) ^ (c >> 12);
        b = ((b - c) - a) ^ (a << 16);
        c = ((c - a) - b) ^ (b >> 5);
        a = ((a - b) - c) ^ (c >> 3);
        b = ((b - c) - a) ^ (a << 10);
        c = ((c - a) - b) ^ (b >> 15);

        remaining -= 12;
    }

    c += (uint32_t)len;                 // the ONLY len addition

    for (uint32_t i = 0; i < (uint32_t)remaining; i++) {
        uint8_t ch = (uint8_t)p[i];
        if (ch >= 'A' && ch <= 'Z') ch += 0x20;
        buf[i] = ch;
    }

    switch (remaining) {
        case 11: c += (uint32_t)buf[10] << 24; // fall through
        case 10: c += (uint32_t)buf[9]  << 16; // fall through
        case 9:  c += (uint32_t)buf[8]  << 8;  // fall through
        case 8:  b += (uint32_t)buf[7]  << 24; // fall through
        case 7:  b += (uint32_t)buf[6]  << 16; // fall through
        case 6:  b += (uint32_t)buf[5]  << 8;  // fall through
        case 5:  b += (uint32_t)buf[4];         // fall through
        case 4:  a += (uint32_t)buf[3]  << 24; // fall through
        case 3:  a += (uint32_t)buf[2]  << 16; // fall through
        case 2:  a += (uint32_t)buf[1]  << 8;  // fall through
        case 1:  a += (uint32_t)buf[0];
    }

    // Final mix — uses a temp variable `t` per binary rotation.
    a = ((a - b) - c) ^ (c >> 13);
    uint32_t t = ((b - c) - a) ^ (a << 8);
    b = ((c - a) - t) ^ (t >> 13);
    a = ((a - t) - b) ^ (b >> 12);
    t = ((t - b) - a) ^ (a << 16);
    c = ((b - a) - t) ^ (t >> 5);
    a = ((a - t) - c) ^ (c >> 3);
    b = ((t - c) - a) ^ (a << 10);
    return ((c - a) - b) ^ (b >> 15);
}
