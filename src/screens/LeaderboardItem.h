#ifndef FN_SCREENS_LEADERBOARD_ITEM_H
#define FN_SCREENS_LEADERBOARD_ITEM_H

// Defunct: LeaderboardItem -- online leaderboard row base (ScrollingMenuItem subclass).
// Binary ctor @ 0x00101250. Size = sizeof(ScrollingMenuItem) = 0x58 (88 bytes on ARM32):
// ctor evidence shows no new fields beyond the inherited ScrollingMenuItem layout.
// FriendLeaderboardItem derives from this; its own members begin at +0x58.

#include "hud/ScrollingMenuItem.h"

class LeaderboardItem : public ScrollingMenuItem {
public:
    // Defunct: LeaderboardItem -- no-op stub; binary @ 0x00101250
    LeaderboardItem() {}
    virtual ~LeaderboardItem() {}
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(LeaderboardItem) == 0x58, "LeaderboardItem size mismatch");
#endif

#endif // FN_SCREENS_LEADERBOARD_ITEM_H
