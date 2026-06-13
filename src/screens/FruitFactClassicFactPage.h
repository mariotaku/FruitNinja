#ifndef FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H

//
// FruitFactClassicFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00174e30  (FruitFactControl*, int, int)
//

#include "FruitFactPage.h"

class FruitFactClassicFactPage : public FruitFactPage {
public:
    // Binary @ 0x00174e30 -- ctor(FruitFactControl*, int factIndex, int pageIndex)
    FruitFactClassicFactPage(FruitFactPageControl* pCtrl, int factIndex, int pageIndex);
    ~FruitFactClassicFactPage() override;

    void Init() override;  // Binary @ 0x00174e30 -- build icon GenericHUDControls

private:
    int m_factIndex;
    int m_pageIndex;
};

#endif // FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H
