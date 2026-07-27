#ifndef FN_GAME_FN_HIGHSCORE_LIST_H
#define FN_GAME_FN_HIGHSCORE_LIST_H

// Defunct: FNHighscoreList -- online-leaderboard score list (OpenFeint/GameCenter).
// Binary ctor @ 0x00111408. Size 0x10 (16 bytes), proven by operator_new(0x10) at
// LeaderboardManager::RefreshLeaderboard @ 0x00111664.
// Non-polymorphic: ctor writes no vtable. No base class.
//
// Binary field layout:
//   +0x00: std::list<FNHighscore>  (8 bytes, Sourcery 2010q1 sentinel-only layout)
//   +0x08: bool m_flag8            (ctor: strb #0)
//   +0x09: bool m_flag9            (ctor: strb #0)
//   +0x0A: bool m_flagA            (ctor: strb #0)
//   +0x0B: bool m_isGlobal         (ctor: strb #0; set to 1 in RefreshLeaderboard for kinds 0,2,3)
//   +0x0C: bool m_flagC            (ctor: strb #0)
//   +0x0D-+0x0F: tail padding
//   total: 16 bytes

#include "game/FNHighscore.h"
#include <list>
#include <cstddef>
#include <cstdint>

class FNHighscoreList {
public:
    // Defunct: FNHighscoreList -- no-op stub; v1.6.1 FNHighscoreList::FNHighscoreList @0x00136d30
    FNHighscoreList()
        : m_flag8(false)
        , m_flag9(false)
        , m_flagA(false)
        , m_isGlobal(false)
        , m_flagC(false)
    {}

    // Defunct: FNHighscoreList -- no-op stub; v1.6.1 FNHighscoreList::AddPlayerScore @ 0x00137444
    static void AddPlayerScore(long long /*score*/) {}

    void* GetFirst() { return nullptr; }
    void* GetNext()  { return nullptr; }
    size_t size() const { return 0; }
    void* PrepareForDataRetrieval() { return nullptr; }
    bool IsCurrentUser(void* /*entry*/) { return false; }

private:
    std::list<FNHighscore> m_scores;   // +0x00 (8 bytes, std::list sentinel-only)
    bool m_flag8;                       // +0x08
    bool m_flag9;                       // +0x09
    bool m_flagA;                       // +0x0A
    bool m_isGlobal;                    // +0x0B
    bool m_flagC;                       // +0x0C
    // +0x0D..+0x0F: tail padding (compiler-generated)
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(FNHighscoreList) == 16, "FNHighscoreList size mismatch");
static_assert(offsetof(FNHighscoreList, m_flag8)    == 0x08, "FNHighscoreList::m_flag8");
static_assert(offsetof(FNHighscoreList, m_isGlobal) == 0x0B, "FNHighscoreList::m_isGlobal");
#endif

#endif // FN_GAME_FN_HIGHSCORE_LIST_H
