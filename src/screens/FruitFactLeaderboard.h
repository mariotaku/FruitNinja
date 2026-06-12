#ifndef FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
#define FN_SCREENS_FRUIT_FACT_LEADERBOARD_H

//
// FruitFactLeaderboard : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00176980  (FruitFactControl*, bool)
//
// Note: the binary class name is FruitFactLeaderboard.
// The bool param selects a display variant (global vs friend scores).
//

#include "FruitFactPage.h"

class FruitFactLeaderboard : public FruitFactPage {
public:
    // Binary @ 0x00176980 -- ctor(FruitFactControl*, bool isGlobal)
    FruitFactLeaderboard(FruitFactPageControl* pCtrl, bool isGlobal);
    ~FruitFactLeaderboard() override;

    void Init() override;  // TODO: 0x00176980 -- build leaderboard display HUD children

private:
    bool m_bIsGlobal;
};

#endif // FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
