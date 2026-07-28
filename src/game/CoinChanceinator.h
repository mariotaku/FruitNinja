#ifndef FN_GAME_COIN_CHANCEINATOR_H
#define FN_GAME_COIN_CHANCEINATOR_H

// Coin chance system. COIN_CHANCEINATOR itself is defined in WaveStructs.h
// (binary-faithful 8-byte layout); this header provides only the free functions
// that operate on it.
//
// WHY COIN DROPS NEVER HAPPEN -- it is DATA, not code. Do not attribute it to
// CoinsEnabled():
//   * The real gate is FRUIT_INFO+0x328 (m_CoinsMax). Fruit::CollisionResponse
//     @0x001de780 does `ldr r3,[r0,#0x328]; cmp #0; ble` and only reaches its
//     MakeCoins call @0x001de95c when the field is > 0. SlashEntity::Update
//     @0x001e9204 tests the same field per combo fruit but still CALLS MakeCoins
//     @0x001e930c, which returns at its own `if (0 < count)` -- so that path
//     consumes no RNG either way, which matters for RNG-accounting fidelity.
//   * FRUIT_INFO::FRUIT_INFO @0x001e3d98 initialises +0x324 = 0 and +0x328 = 0,
//     and FruitNinjaBada/Data/xml/fruitlist.xml sets no coin attribute on any
//     fruit, so m_CoinsMax is 0 for every fruit in the shipped data.
//
// COINS THEMSELVES ARE LIVE -- only the fruit/combo DROP path above is dead.
// BonusScreen::AwardScores @0x0016393c spawns coins on every game-over via
// Coin::MakeCoins with the AddToScoreOnArrival @0x00162ab8 delegate (not
// CoinArrived). Reading "coins are dead" without that distinction leads to the
// wrong conclusion about the live RNG/score accounting on the bonus screen.
//
// WaveManager::RequestCoins @0x001233b0 (the only consumer of COIN_CHANCEINATOR
// data) has zero callers and discards GetCoins()'s return anyway, so the table
// is parsed-but-never-read -- same shape as CRITICAL_DISAPPEAR_SPEED. The
// functions are kept so the call graph and struct shape match the binary.

#include "game/WaveStructs.h"

#include "engine/xml/TiXmlElement.h"

// Defunct: coin chance system -- no-op stub; v1.6.1 ParseCoinChanceinator @ 0x00129604
void ParseCoinChanceinator(COIN_CHANCEINATOR* pDst, TiXmlElement* pElem);

// ASM-spec v1.6.1 CoinsEnabled @0x0011a02c: body is `mov r0,#0; bx lr` -- the
// port's `return 0` matches the binary exactly. NOTE it is an ORPHAN: it has
// ZERO callers in the whole image (no PLT thunk, just the body plus an
// exported-symbol entry), so it gates nothing. It is ported for symbol
// completeness only -- see the file header for what actually kills coin drops.
int CoinsEnabled();

#endif // FN_GAME_COIN_CHANCEINATOR_H
