// FruitFactLeaderboard -- v1.6.1 leaderboard fact page.
// Binary refs: ctor 0x00176980.
// Defunct: online-services -- leaderboard data population is no-op.

#include "FruitFactLeaderboard.h"

// Binary @ 0x00176980
FruitFactLeaderboard::FruitFactLeaderboard(FruitFactPageControl* pCtrl, bool isGlobal)
    : FruitFactPage(pCtrl)
    , m_bIsGlobal(isGlobal)
{
}

FruitFactLeaderboard::~FruitFactLeaderboard() {
}

// TODO: 0x00176980 -- BLOCKED on FNHighscore struct (3 embedded at +0xc8/+0x11c/+0x170,
//   each 0x54 bytes; ctor at its own addr), LeaderboardManager::GetInstance/ClearScores
//   (defunct-stub), IsProviderOnline/AreFriendsLoaded (defunct online -> return false),
//   LoadContent, and .tex strings at DAT_176d38 / DAT_176d48 from the binary string pool.
//   ctrl-B tex confirmed "leaderboard_vertical_divider_1.tex" (DAT_176d40).
//   LSTR ids: 0x7b (123, global LB title) / 0x363 (867, friends LB title).
// Defunct: online-services -- no-op stub; binary @ 0x00176980
void FruitFactLeaderboard::Init() {
}
