#ifndef FN_GAME_COIN_CHANCEINATOR_H
#define FN_GAME_COIN_CHANCEINATOR_H

// Coin chance system -- CoinsEnabled() always returns 0 in the shipped binary
// (v1.6.1 CoinsEnabled @0x0011a02c is a hard `return 0`), so every coin-spawn
// path downstream is dead by the binary's own doing, not by a port decision.
// COIN_CHANCEINATOR struct is defined in WaveStructs.h (binary-faithful 8-byte layout).
// This header provides only the free functions that operate on it.

#include "game/WaveStructs.h"

#include "engine/xml/TiXmlElement.h"

// Defunct: coin chance system -- no-op stub; v1.6.1 ParseCoinChanceinator @ 0x00129604
void ParseCoinChanceinator(COIN_CHANCEINATOR* pDst, TiXmlElement* pElem);

// ASM-spec v1.6.1 CoinsEnabled @0x0011a02c: body is `mov r0,#0; bx lr` -- the
// port's `return 0` matches the binary exactly.
int CoinsEnabled();

#endif // FN_GAME_COIN_CHANCEINATOR_H
