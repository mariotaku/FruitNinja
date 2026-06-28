#ifndef FN_GAME_SCORESTATE_H
#define FN_GAME_SCORESTATE_H

// Analysed: 2026-05-02T00:00

// g_ComboCount: BSS @ 0x0024d764, GOT[0x78f8] -> combo count int.
// Written by Fruit::CollisionResponse (increment), Fruit::KillFruit (zero),
// TimeControl::Update (zero on game-over), WaveManager::Reset (zero).
extern int g_ComboCount;

// g_LastSlasher: BSS @ 0x001f3e4c, GOT[0x7478] -> last-slasher player index.
// Sentinel -1 = no player has slashed yet (cold-boot / after game-over).
// Binary uses 0xFFFFFFFF (= -1 as signed int) in TimeControl game-over branch
// and 1 in WaveManager::Reset; port uses -1 throughout for consistency with
// cold-boot sentinel.
extern int g_LastSlasher;

// Identity pass-throughs used as score-accumulation helpers.
// v1.6.1 AddScoreNomal @0x1478b4, AddScoreNomals @0x1adee0
int AddScoreNomal(int x);
int AddScoreNomals(int x);

#endif // FN_GAME_SCORESTATE_H
