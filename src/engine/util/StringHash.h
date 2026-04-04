#ifndef MORTAR_STRING_HASH_H
#define MORTAR_STRING_HASH_H

#include <cstdint>

// Jenkins lookup3 hash with case-folding (0x0019c5d4)
uint32_t StringHash(const char* str);

#endif // MORTAR_STRING_HASH_H
