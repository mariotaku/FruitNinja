#ifndef FN_SCREENS_LEADERBOARD_ITEM_H
#define FN_SCREENS_LEADERBOARD_ITEM_H

// Defunct: LeaderboardItem -- online leaderboard row base (ScrollingMenuItem subclass).
// v1.6.1 LeaderboardItem::LeaderboardItem(char const*,int,int) @ 0x00190858 (thunk 0x00102f24).
// size 0x60 = base 0x58 + m_Score(+0x58) + m_Rank(+0x5c).
// FriendLeaderboardItem derives from this; its own members begin at +0x60.

#include "hud/ScrollingMenuItem.h"

class LeaderboardItem : public ScrollingMenuItem {
public:
    // Defunct: LeaderboardItem -- no-op stub; v1.6.1 LeaderboardItem::LeaderboardItem @ 0x00190858
    LeaderboardItem() : m_Score(0), m_Rank(-1) {}
    virtual ~LeaderboardItem() {}

    int m_Score; // +0x58  ctor: str r6,[r4,#0x58] = param_3 (score)
    int m_Rank;  // +0x5c  ctor: str r5,[r4,#0x5c] = param_2 (rank, 1-based; -1 on ERROR path)
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(LeaderboardItem) == 0x60, "LeaderboardItem size mismatch");
#endif

#endif // FN_SCREENS_LEADERBOARD_ITEM_H
