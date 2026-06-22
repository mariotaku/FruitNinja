#ifndef FN_GAME_LEADERBOARD_LIST_H
#define FN_GAME_LEADERBOARD_LIST_H

// Defunct: LeaderboardList -- ScrollingMenu subclass for online leaderboard rows.
// ASM-spec v1.6.1 LeaderboardList ctor @ 0x001906e8 / sizeof 0x12C (300):
//   +0x000  ScrollingMenu base (256 bytes = 0x100; last field m_InnerRegion[4] @0xf0..0xff)
//   +0x100  uint8_t m_bPopulated   (read by LeaderboardScreen::Update; ctor does NOT write it)
//   +0x101..+0x103  3 bytes natural alignment pad (before void*)
//   +0x104  void* m_pHighscoreData (ctor writes =0)
//   +0x108  Mortar::Delegate0<void> m_callback / m_OnPopulated (36 bytes; ctor default-constructs)
//   Total: 256 + 1 + 3 + 4 + 36 = 300 = 0x12C.

#include "hud/ScrollingMenu.h"
#include "engine/util/Delegate.h"
#include <cstdint>

class LeaderboardList : public ScrollingMenu {
public:
    // Defunct: LeaderboardList -- no-op stub; v1.6.1 LeaderboardList ctor @ 0x001906e8
    LeaderboardList() {}
    ~LeaderboardList() override {}

    // Defunct: online leaderboards -- no-op stub; binary address unknown.
    void Init() override {}

#ifdef __bada__
    // +0x100: populated flag (set by download/populate path; read by LeaderboardScreen::Update)
    uint8_t m_bPopulated;                    // +0x100
    // +0x101..+0x103: natural alignment pad (auto; do NOT add explicit pad array)
    // +0x104: highscore data pointer (ctor writes =0)
    void* m_pHighscoreData;                  // +0x104
    // +0x108..+0x12B: 0-arg void callback delegate (m_OnPopulated)
    Mortar::Delegate0<void> m_callback;      // +0x108
#endif
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(ScrollingMenu) == 0x100, "ScrollingMenu size mismatch");
static_assert(offsetof(LeaderboardList, m_bPopulated)    == 0x100, "LeaderboardList::m_bPopulated offset");
static_assert(offsetof(LeaderboardList, m_pHighscoreData) == 0x104, "LeaderboardList::m_pHighscoreData offset");
static_assert(offsetof(LeaderboardList, m_callback)      == 0x108, "LeaderboardList::m_callback offset");
static_assert(sizeof(LeaderboardList) == 0x12C, "LeaderboardList size mismatch");
#endif

#endif // FN_GAME_LEADERBOARD_LIST_H
