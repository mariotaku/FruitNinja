#ifndef FN_GAME_LEADERBOARD_LIST_H
#define FN_GAME_LEADERBOARD_LIST_H

// Defunct: LeaderboardList -- ScrollingMenu subclass for online leaderboard rows.
// Binary @ ~0x12C bytes (ScrollingMenu subclass; fields opaque).
// no-op stub: ScrollingMenu base provides the API surface used by FruitFactControl.
//
// Analysed: 2026-05-04T00:00

#include "hud/ScrollingMenu.h"
#include <cstdint>

class LeaderboardList : public ScrollingMenu {
public:
    LeaderboardList() {}
    ~LeaderboardList() override {}

    // Defunct: online leaderboards -- fields opaque.
    // Binary total: ~0x12C. On 64-bit port, sizeof(ScrollingMenu) may exceed 0x12C;
    // padding is omitted in that case (no negative-size array).
#ifdef __bada__
    char _pad[0x12C - sizeof(ScrollingMenu)];
#endif
};

#endif // FN_GAME_LEADERBOARD_LIST_H
