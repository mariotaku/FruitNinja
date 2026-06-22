#ifndef FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H

//
// FruitFactBonusFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x001743b8
//
// Layout (FruitFactPage base = 0x98 bytes; derived own-fields start at +0x98):
//   +0x98..+0xBB : 0x24 bytes padding (no named fields beyond base)
//   total: 0xBC
//
// ASM-verified: v1.6.1 FruitFactBonusFactPage @ 0x001743b8
//

#include "FruitFactPage.h"
#include <cstdint>

class FruitFactBonusFactPage : public FruitFactPage {
public:
    // Binary @ 0x001743b8
    explicit FruitFactBonusFactPage(FruitFactPageControl* pCtrl);
    ~FruitFactBonusFactPage() override;

    void Init() override;

private:
    // +0x98..+0xBB: 0x24 bytes padding (no named fields in binary beyond base)
    // ASM-verified: v1.6.1 FruitFactBonusFactPage @ 0x001743b8
    uint8_t _pad[0x24];
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(FruitFactBonusFactPage) == 0xBC,
    "FruitFactBonusFactPage sizeof must be 0xBC");
#endif

#endif // FN_SCREENS_FRUIT_FACT_BONUS_FACT_PAGE_H
