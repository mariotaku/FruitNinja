#ifndef FN_GAME_COIN_CHANCEINATOR_H
#define FN_GAME_COIN_CHANCEINATOR_H

// Defunct: coin chance system -- CoinsEnabled() always returns 0 in the shipped binary.
// COIN_CHANCEINATOR struct is defined in WaveStructs.h (binary-faithful 8-byte layout).
// This header provides only the free functions that operate on it.

#include "game/WaveStructs.h"

#include "engine/xml/TiXmlElement.h"

// Defunct: coin chance system -- no-op stub; v1.6.1 binary @ 0x001230f0
void ParseCoinChanceinator(COIN_CHANCEINATOR* pDst, TiXmlElement* pElem);

// Defunct: coin chance system -- always returns 0; binary @ 0x0010a428
int CoinsEnabled();

#endif // FN_GAME_COIN_CHANCEINATOR_H
