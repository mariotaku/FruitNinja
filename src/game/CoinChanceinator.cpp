#include "game/CoinChanceinator.h"

// Defunct: coin chance system -- no-op stub; v1.6.1 ParseCoinChanceinator @ 0x00129604
void ParseCoinChanceinator(COIN_CHANCEINATOR* /*pDst*/, TiXmlElement* /*pElem*/) {
}

// ASM-spec v1.6.1 CoinsEnabled @0x0011a02c: whole body is `mov r0,#0; bx lr`.
// Returning 0 here is FAITHFUL, not a port-side stub decision. It is also an
// orphan -- zero callers in the image, so it gates nothing; coin drops are dead
// by DATA (FRUIT_INFO+0x328 m_CoinsMax == 0), see CoinChanceinator.h.
int CoinsEnabled() {
    return 0;
}
