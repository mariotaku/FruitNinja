#ifndef FN_SCREENS_FRUIT_FACT_LITE_PAGE_H
#define FN_SCREENS_FRUIT_FACT_LITE_PAGE_H

//
// FruitFactLitePage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor   0x0017ad5c
//   Update 0x0017a220
//

#include "FruitFactPage.h"

class FruitFactLitePage : public FruitFactPage {
public:
    // Binary @ 0x0017ad5c
    explicit FruitFactLitePage(FruitFactPageControl* pCtrl);
    ~FruitFactLitePage() override;

    void Update(float dt) override;  // Binary @ 0x0017a220
};

#endif // FN_SCREENS_FRUIT_FACT_LITE_PAGE_H
