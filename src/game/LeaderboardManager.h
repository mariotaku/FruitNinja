#ifndef FN_GAME_LEADERBOARD_MANAGER_H
#define FN_GAME_LEADERBOARD_MANAGER_H

// Analysed: 2026-04-30T00:00
//
// LeaderboardManager -- online leaderboard handler (OpenFeint / GameCenter).
// Defunct online service -- stub satisfies callers; no real network code.
// Size: ~0x80 bytes (ctor inits 0x10 bytes + BSS through ~0x80).
//
// Binary addresses:
//   ctor (real)    0x001113a8
//   ctor (alias)   0x001113c0
//   ctor thunk     0x00101a00
//   dtor (regular) 0x001113d8
//   dtor (empty)   0x001113dc
//   GetInstance    0x001114b8
//   RefreshLeaderboard 0x00111664
//   GetLeaderboard     0x001113e4
//   ClearScores        0x00111438
//
// NOTE: UpdateLeaderboard @ 0x0013afbc is FruitFactControl::UpdateLeaderboard,
//       not a method of this class. It was previously misidentified.

#include <cstdint>

class FNHighscoreList;

class LeaderboardManager {
public:
    static LeaderboardManager* GetInstance() {
        static LeaderboardManager s_instance;
        return &s_instance;
    }

    // Defunct: LeaderboardManager -- no-op stub; binary @ 0x00111664
    FNHighscoreList* RefreshLeaderboard(int /*gameMode*/, int /*boardId*/) { return nullptr; }

    // Defunct: LeaderboardManager -- no-op stub; binary @ 0x001113e4
    FNHighscoreList* GetLeaderboard(int /*gameMode*/, int /*boardId*/) { return nullptr; }

    // Defunct: LeaderboardManager -- no-op stub; binary @ 0x00111438
    void ClearScores(int /*gameMode*/, int /*boardId*/) {}

private:
    // ctor @ 0x001113a8
    LeaderboardManager() {}
    ~LeaderboardManager() {}

    // binary uses 0x10 ctor-init + BSS through ~0x80
    uint8_t m_data[0x80];
};

#endif // FN_GAME_LEADERBOARD_MANAGER_H
