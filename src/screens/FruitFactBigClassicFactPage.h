#ifndef FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H

//
// FruitFactBigClassicFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00172884  (FruitFactControl*, int, int)
//

#include "FruitFactPage.h"

class FruitFactBigClassicFactPage : public FruitFactPage {
public:
    // Binary @ 0x00172884 -- ctor(FruitFactControl*, int factIndex, int pageIndex)
    FruitFactBigClassicFactPage(FruitFactPageControl* pCtrl, int factIndex, int pageIndex);
    ~FruitFactBigClassicFactPage() override;

    void Init() override;  // TODO: 0x00172884 -- build BakedStringBox + HUD children

private:
    int m_factIndex;
    int m_pageIndex;
};

#endif // FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H
