#include "EntityTypes.h"
#include "util/StringHash.h"
#include <cstdint>

// ASM-spec v1.6.1 HashTypeConvert @ 0x001d8e64
//
// Binary layout: 7-entry static table at DAT_002d8ce4, each entry 12 bytes
//   {uint32_t hash, int32_t typeId, int32_t isValid}.
// TypeId and isValid fields are pre-initialized in .data; hash fields start as
// 0 and are filled at first call by StringHash("name"). Binary uses __cxa_guard
// for thread-safe one-time init; port uses a plain static bool (single-threaded
// SDL game loop makes the guard unnecessary).
long HashTypeConvert(unsigned long hash, bool& found_out) {
    struct Entry {
        uint32_t hash;
        int32_t  typeId;
        int32_t  isValid;
    };
    static Entry s_table[7] = {
        {0u, ENTITY_FRUIT,    1},  // "fruit"
        {0u, ENTITY_BOMB,     1},  // "bomb"
        {0u, ENTITY_SLASH,    1},  // "slash"
        {0u, ENTITY_BLAST,    1},  // "blast"
        {0u, ENTITY_COIN,     1},  // "coin"
        {0u, ENTITY_JIBLET,   1},  // "jiblet"
        {0u, ENTITY_FRUITRAY, 1},  // "fruitray"
    };
    static const char* const s_names[7] = {
        "fruit", "bomb", "slash", "blast", "coin", "jiblet", "fruitray"
    };
    static bool s_init = false;
    if (!s_init) {
        for (int i = 0; i < 7; i++)
            s_table[i].hash = StringHash(s_names[i]);
        s_init = true;
    }
    uint32_t h32 = (uint32_t)hash;
    for (int i = 0; i < 7; i++) {
        if (h32 == s_table[i].hash) {
            found_out = true;
            return (long)s_table[i].typeId;
        }
    }
    found_out = false;
    return -1L;
}
