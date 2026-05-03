#ifndef FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H
#define FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H

// Defunct: FriendLeaderboardItem -- online friend-leaderboard row.
// Binary @ 0x0013d210. Size 0x28C.

#include "hud/ScrollingMenuItem.h"
#include <cstdint>

class FriendLeaderboardItem : public ScrollingMenuItem {
public:
    FriendLeaderboardItem(const char* /*name*/, int /*rank*/, int /*score*/,
                          void* /*userdata*/, const char* /*url*/) {}
    ~FriendLeaderboardItem() {}

    // Defunct: FriendLeaderboardItem -- geometry stub; binary @ 0x0013d304
    bool CollideWithButton(long /*touchIdx*/) { return false; }

private:
    // Size 0x28C - base ScrollingMenuItem (0x58 on ARM32)
    uint8_t pad[0x28C - 0x58];
};

#endif // FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H
