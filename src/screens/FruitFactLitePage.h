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
    explicit FruitFactLitePage(FruitFactControl* pCtrl);
    ~FruitFactLitePage() override;

    void Update(float dt) override;  // Binary @ 0x0017a220

protected:
    // FruitFactLitePage-specific fields (offsets relative to object base).
    // ctor @ 0x0017ad5c writes +0x98, +0x9c, +0xa0; Update @ 0x0017a220 uses +0xa0, +0xa4.
    int   m_LiteState;      // +0x98 : init 0 in ctor
    float m_RandomSeed;     // +0x9c : init VectorUnsignedToFloat(rand) in ctor
    float m_ScrollPhase;    // +0xa0 : init 0.0f (DAT_0017b11c); += dt/3, wraps at 5.0
    float m_ElapsedTime;    // +0xa4 : total elapsed-time accumulator (+= dt)
};

#endif // FN_SCREENS_FRUIT_FACT_LITE_PAGE_H
