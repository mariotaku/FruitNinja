#ifndef MORTAR_STRING_HASH_H
#define MORTAR_STRING_HASH_H

#include <cstdint>
#include <cstring>

// Jenkins lookup3 hash with case-folding.
// Two overloads matching the binary:
//   v1.6.1 StringHash(const char*, int) @ 0x00252a10 — real impl; len is the byte length
//   v1.6.1 StringHash(const char*)      @ 0x00119328 — 1-arg helper calls StringHash(s, strlen(s))
// ASM-verified: 2026-06-12 v1.6.1 binary @ 0x00252a10 / 0x0019c5d4 (re-analyst)
uint32_t StringHash(const char* str, int len);

inline uint32_t StringHash(const char* str) { return StringHash(str, (int)strlen(str)); }

#endif // MORTAR_STRING_HASH_H
