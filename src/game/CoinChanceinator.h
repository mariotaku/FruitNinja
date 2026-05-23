#ifndef FN_GAME_COIN_CHANCEINATOR_H
#define FN_GAME_COIN_CHANCEINATOR_H

// Defunct: coin chance system -- shipped binary has CoinsEnabled() always returns 0.
// CoinChanceinator @ 0x001230f0 (ParseCoinChanceinator).
// Struct is opaque; declared as empty so call sites compile.

#include <tinyxml2.h>

typedef tinyxml2::XMLElement TiXmlElement;

struct COIN_CHANCEINATOR {
    // Defunct: coin chance system -- no fields used; CoinsEnabled() == 0.
};

// Defunct: coin chance system -- no-op stub; binary @ 0x001230f0
void ParseCoinChanceinator(COIN_CHANCEINATOR* pDst, TiXmlElement* pElem);

// Defunct: coin chance system -- always returns 0; binary @ 0x0010a428
int CoinsEnabled();

#endif // FN_GAME_COIN_CHANCEINATOR_H
