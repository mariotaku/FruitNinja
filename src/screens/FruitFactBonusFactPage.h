#ifndef FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H

//
// FruitFactBonusFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x001743b8
//

#include "FruitFactPage.h"

class FruitFactBonusFactPage : public FruitFactPage {
public:
    // Binary @ 0x001743b8
    explicit FruitFactBonusFactPage(FruitFactPageControl* pCtrl);
    ~FruitFactBonusFactPage() override;

    void Init() override;  // TODO: 0x001743b8 -- build BakedStringBox + HUD children
};

#endif // FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H
