#ifndef FN_GAME_FN_HIGHSCORE_LIST_H
#define FN_GAME_FN_HIGHSCORE_LIST_H

// Defunct: FNHighscoreList -- online-leaderboard list; no-op stubs.
// Binary ctor @ 0x00111408. Size 0x10 (4 std::list bytes + 4 flags).

#include <cstddef>
#include <cstdint>

class FNHighscoreList {
public:
    FNHighscoreList() {}

    // Defunct: FNHighscoreList -- no-op stub; binary @ 0x00111874
    static void AddPlayerScore(long long /*score*/) {}

    void* GetFirst() { return nullptr; }
    void* GetNext()  { return nullptr; }
    size_t size() const { return 0; }
    void* PrepareForDataRetrieval() { return nullptr; }
    bool IsCurrentUser(void* /*entry*/) { return false; }

private:
    uint32_t m_data[4];
};

#endif // FN_GAME_FN_HIGHSCORE_LIST_H
