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

// TODO: 0x00176980 -- build leaderboard display HUD children
// Defunct: online-services -- no-op stub; binary @ 0x00176980
void FruitFactLeaderboard::Init() {
}
