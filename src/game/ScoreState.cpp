// Analysed: 2026-05-02T00:00

#include "ScoreState.h"

// BSS @ 0x0024d764 (GOT[0x78f8]): combo count, zero-initialised.
int g_ComboCount = 0;

// BSS @ 0x001f3e4c (GOT[0x7478]): last-slasher player index.
// Binary sentinel is -1 (0xFFFFFFFF) at cold-boot / after game-over.
// TODO (Tier-2): FruitSaveData +0x74 (last-slasher) / +0x78 (combo count)
// save/restore via WaveManager::Resume and SaveCurrentData -- see
// docs/engine/scorecontrol-combo-source.md.
int g_LastSlasher = -1;

// ASM-spec v1.6.1 AddScoreNomal @0x1478b4: identity pass-through (single bx lr in Thumb).
int AddScoreNomal(int x) { return x; }

// ASM-spec v1.6.1 AddScoreNomals @0x1adee0: identity pass-through.
int AddScoreNomals(int x) { return x; }
