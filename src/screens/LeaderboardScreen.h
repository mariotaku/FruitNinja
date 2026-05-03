#ifndef FN_SCREENS_LEADERBOARD_SCREEN_H
#define FN_SCREENS_LEADERBOARD_SCREEN_H

// Defunct: LeaderboardScreen -- online leaderboard UI; no-op stub.
// Binary ctor @ 0x001481c4. HUDControl3d-derived.

#include "hud/HUDControl3d.h"
#include <cstdint>

class LeaderboardScreen : public HUDControl3d {
public:
    LeaderboardScreen() {}
    ~LeaderboardScreen() override {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00148030
    static void LoadContent() {}

    // Defunct: LeaderboardScreen -- no-op stub
    static void UnLoadContent() {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00147cd0
    void OnLeaderboardListPopulated(void* /*list*/) {}

    // Defunct: LeaderboardScreen -- no-op stub; binary @ 0x00147d1c
    void LoadLeaderboards(int /*gameMode*/, int /*boardId*/) {}

    // Defunct: LeaderboardScreen -- no-op stub
    void Update(float /*dt*/) override {}

private:
    uint8_t pad[0xB0];
};

#endif // FN_SCREENS_LEADERBOARD_SCREEN_H
