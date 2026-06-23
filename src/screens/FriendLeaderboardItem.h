#ifndef FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H
#define FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H

// Defunct: FriendLeaderboardItem -- online friend-leaderboard row.
// Binary ctor @ 0x0013d210. Size 0x28C (652 bytes).
// Base: LeaderboardItem (ScrollingMenuItem subclass) @ offset 0; ctor @ 0x00101250.

#include "screens/LeaderboardItem.h"
#include <cstdint>

class FriendLeaderboardItem : public LeaderboardItem {
public:
    // Defunct: FriendLeaderboardItem -- no-op stub; v1.6.1 binary @ 0x0013d210
    FriendLeaderboardItem(const char* /*name*/, int /*rank*/, int /*score*/,
                          void* /*userdata*/, const char* /*url*/) {}
    ~FriendLeaderboardItem() {}

    // Defunct: FriendLeaderboardItem -- geometry stub; v1.6.1 binary @ 0x0013d304
    bool CollideWithButton(long /*touchIdx*/) { return false; }

private:
    // Own fields at +0x58..+0x28B (652 - 88 = 564 bytes of opaque layout).
    // Confirmed members (binary ctor evidence): m_Score(+0x58), m_Rank(+0x5c),
    // flags(+0x60..+0x63), m_Name[0x200](+0x64), m_AltName[0x1f](+0x264),
    // field_0x283, field_0x284, m_UserData(+0x288). Total to +0x28C = 652.
    uint8_t m_pad[0x28C - 0x58];
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(FriendLeaderboardItem) == 0x28C, "FriendLeaderboardItem size mismatch");
#endif

#endif // FN_SCREENS_FRIEND_LEADERBOARD_ITEM_H
