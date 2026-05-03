#ifndef FN_GAME_FN_HIGHSCORE_H
#define FN_GAME_FN_HIGHSCORE_H

// FNHighscore -- single leaderboard score entry (online, Defunct).
// Binary sizeof = 81 bytes (0x51).
// Used inline (not heap-allocated) in FruitFactControl.
//
// Analysed: 2026-05-04T00:00

#include <cstdint>
#include <cstring>

struct FNHighscore {
    // Opaque 81-byte storage; online-leaderboard entry format not fully RE'd.
    // Defunct: online scores -- no-op stub; binary @ unknown.
    char m_data[81];

    FNHighscore() { memset(m_data, 0, sizeof(m_data)); }
};

#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4
static_assert(sizeof(FNHighscore) == 81, "FNHighscore size mismatch");
#endif

#endif // FN_GAME_FN_HIGHSCORE_H
