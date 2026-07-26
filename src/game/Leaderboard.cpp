#include "Leaderboard.h"

// Defunct: online leaderboard ID helpers -- no-op stubs.
// These free functions map game mode to leaderboard board IDs for the online
// leaderboard service (GameSpy/GameCenter). All defunct in port.

// Defunct: online leaderboard -- no-op stub; v1.6.1 GetCurrentModeLeaderboardID @0x00116e18
int GetCurrentModeLeaderboardID(int /*playerIdx*/) {
    return 0;
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 GetLeaderboardID @0x00116de4
int GetLeaderboardID(int /*mode*/, int /*variant*/) {
    return 0;
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 GetTotalFruitLeaderboardId @0x00116e80
int GetTotalFruitLeaderboardId(int /*variant*/) {
    return 0;
}

// Defunct: online leaderboard -- no-op stub; v1.6.1 GetTweakLeaderboardId @0x00116e5c
int GetTweakLeaderboardId(int /*variant*/) {
    return 0;
}
