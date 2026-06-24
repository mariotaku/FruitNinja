#include "game/CoinChanceinator.h"

// Defunct: coin chance system -- no-op stub; v1.6.1 binary @ 0x001230f0
void ParseCoinChanceinator(COIN_CHANCEINATOR* /*pDst*/, TiXmlElement* /*pElem*/) {
}

// Defunct: coin chance system -- always returns 0; v1.6.1 ParseCoinChanceinator @ 0x00113650
int CoinsEnabled() {
    return 0;
}
