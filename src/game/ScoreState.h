#ifndef FN_GAME_SCORESTATE_H
#define FN_GAME_SCORESTATE_H

// Analysed: 2026-05-02T00:00

// g_ComboCount: v1.6.1 Fruit::s_consecutiveCount @ 0x00332a2c, GOT[0x774c].
// Written by Fruit::CollisionResponse (increment), Fruit::KillFruit (zero),
// TimeControl::Update (zero on game-over), WaveManager::Reset (zero).
extern int g_ComboCount;

// g_ComboFruitType: v1.6.1 Fruit::s_consecutiveType @ 0x002d8d64, GOT[0x71b4].
// Fruit-TYPE (m_FruitType, Fruit+0x3c) of the last fruit that continued the
// current combo streak -- NOT a player index and NOT the digit/combo count.
// Sentinel -1 = no fruit has continued a streak yet (cold-boot / after game-over).
// Binary uses 0xFFFFFFFF (= -1 as signed int) in TimeControl game-over branch
// and 1 in WaveManager::Reset; port uses -1 throughout for consistency with
// cold-boot sentinel.
extern int g_ComboFruitType;

// Identity pass-throughs used as score-accumulation helpers.
// v1.6.1 AddScoreNomal @0x1478b4, AddScoreNomals @0x1adee0
int AddScoreNomal(int x);
int AddScoreNomals(int x);

#endif // FN_GAME_SCORESTATE_H
