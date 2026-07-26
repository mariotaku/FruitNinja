#ifndef FN_GAME_LEADERBOARD_H
#define FN_GAME_LEADERBOARD_H

// Defunct: online leaderboard ID helpers -- no-op stubs (see Leaderboard.cpp).
// Free functions mapping game mode to online-leaderboard board IDs. All return
// 0 in the port; call sites keep the binary's call shape (the returned ID is
// passed straight into the NetworkManager::SetLeaderboardScore stub).

// Defunct: online leaderboard -- v1.6.1 GetCurrentModeLeaderboardID @0x00116e18
int GetCurrentModeLeaderboardID(int playerIdx);
// Defunct: online leaderboard -- v1.6.1 GetLeaderboardID @0x00116de4
int GetLeaderboardID(int mode, int variant);
// Defunct: online leaderboard -- v1.6.1 GetTotalFruitLeaderboardId @0x00116e80
int GetTotalFruitLeaderboardId(int variant);
// Defunct: online leaderboard -- v1.6.1 GetTweakLeaderboardId @0x00116e5c
int GetTweakLeaderboardId(int variant);

#endif // FN_GAME_LEADERBOARD_H
