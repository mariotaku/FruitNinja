#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/ResBlock.h"
#include <cstdio>
#include <cstring>

namespace fn {
namespace wii {

namespace {
    int s_CurrentBlockMask = RES_BLOCK_NONE;
}

void SetCurrentBlock(ResBlockFlag block) {
    s_CurrentBlockMask = (int)block;
}

void AddCurrentBlock(ResBlockFlag block) {
    s_CurrentBlockMask |= (int)block;
}

int GetCurrentBlockMask() {
    return s_CurrentBlockMask;
}

const char* GetCurrentBlockName() {
    static char buf[48];
    if (s_CurrentBlockMask == RES_BLOCK_NONE) {
        return "NONE";
    }
    buf[0] = '\0';
    bool first = true;
    if (s_CurrentBlockMask & RES_BLOCK_MENU) {
        strcat(buf, first ? "MENU" : "|MENU");
        first = false;
    }
    if (s_CurrentBlockMask & RES_BLOCK_SHOP) {
        strcat(buf, first ? "SHOP" : "|SHOP");
        first = false;
    }
    if (s_CurrentBlockMask & RES_BLOCK_INGAME) {
        strcat(buf, first ? "INGAME" : "|INGAME");
        first = false;
    }
    if (s_CurrentBlockMask & RES_BLOCK_GAMEOVER) {
        strcat(buf, first ? "GAMEOVER" : "|GAMEOVER");
        first = false;
    }
    return buf;
}

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII
