#ifndef MORTAR_STRING_HASH_H
#define MORTAR_STRING_HASH_H

#include <cstdint>

// Jenkins lookup3 hash with case-folding.
// Binary: 0x00252a10 (v1.6.1) / 0x0019c5d4 (v1.0)
// ASM-verified: 2026-06-12 v1.6.1 binary @ 0x00252a10 / 0x0019c5d4 (re-analyst)
uint32_t StringHash(const char* str);

#endif // MORTAR_STRING_HASH_H
