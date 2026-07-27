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

    // Defunct: LeaderboardManager -- no-op stub; v1.6.1 LeaderboardManager::RefreshLeaderboard @ 0x0013713c
    FNHighscoreList* RefreshLeaderboard(int /*gameMode*/, int /*boardId*/) { return nullptr; }

    // Defunct: LeaderboardManager -- no-op stub; v1.6.1 LeaderboardManager::GetLeaderboard @ 0x00136d0c
    FNHighscoreList* GetLeaderboard(int /*gameMode*/, int /*boardId*/) { return nullptr; }

    // Defunct: LeaderboardManager -- no-op stub; v1.6.1 LeaderboardManager::ClearScores @ 0x00136da4
    void ClearScores(int /*gameMode*/, int /*boardId*/) {}

private:
    // ctor @ 0x001113a8
    LeaderboardManager() {}
    ~LeaderboardManager() {}

    // Binary ctor zero-fills this+0x00..+0x3F (16 uint32_t fields, 64 bytes total).
    // Evidence: `add r1,r0,#0x40; for (p=this; p!=this+0x40; p+=0x10) p[0..3]=0`.
    uint8_t m_data[0x40];
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(LeaderboardManager) == 0x40, "LeaderboardManager size mismatch");
#endif

#endif // FN_GAME_LEADERBOARD_MANAGER_H
