#ifndef FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
#define FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H

//
// FruitFactRewardsPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x0017e4d8
//

#include "FruitFactPage.h"

class FruitFactRewardsPage : public FruitFactPage {
public:
    // Binary @ 0x0017e4d8
    explicit FruitFactRewardsPage(FruitFactPageControl* pCtrl);
    ~FruitFactRewardsPage() override;

    void Init() override;  // TODO: 0x0017e4d8 -- build rewards display HUD children
};

#endif // FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
