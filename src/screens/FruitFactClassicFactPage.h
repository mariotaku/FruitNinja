#ifndef FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H

//
// FruitFactClassicFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00174e30  (FruitFactControl*, int headIdx, int bodyIdx)
//
// Layout (FruitFactPage base = 0x98 bytes; derived own-fields start at +0x98):
//   +0x98 : BakedStringBox* m_pTitleBox  (lazy, init NULL)
//   +0x9C : BakedStringBox* m_pBodyBox   (lazy, init NULL)
//   total: 0xA0
//
// The ctor int args headIdx / bodyIdx are consumed locally; they are NOT stored.
//
// ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
//

#include "FruitFactPage.h"

namespace Mortar { class BakedStringBox; }

class FruitFactClassicFactPage : public FruitFactPage {
public:
    // Binary @ 0x00174e30 -- ctor(FruitFactControl*, int headIdx, int bodyIdx)
    FruitFactClassicFactPage(FruitFactControl* pCtrl, int headIdx, int bodyIdx);
    ~FruitFactClassicFactPage() override;

    void Init() override;  // Binary @ 0x00174e30 -- build icon GenericHUDControls
    void DrawOrder(float* hudScaleRaw, int layerMask) override; // Binary @ 0x00175250

    // Test support: delete and null both lazy BakedStringBox members so DrawOrder
    // re-bakes them from the controller's current m_FactText/m_FactColour on the
    // next frame. Used by test_fruitfact --fact= override after-the-fact patching.
    // The game never calls this; it is a no-op stub from the binary's perspective.
    void ResetBakedTextBoxes();

private:
    // +0x98: lazy-initialized title BakedStringBox pointer (init NULL)
    // ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
    Mortar::BakedStringBox* m_pTitleBox;   // @+0x98
    // +0x9C: lazy-initialized body BakedStringBox pointer (init NULL)
    // ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
    Mortar::BakedStringBox* m_pBodyBox;    // @+0x9C

#ifdef __bada__
    friend struct FruitFactClassicFactPageLayoutAssert;
#endif
};

#ifdef __bada__
#include <cstddef>
struct FruitFactClassicFactPageLayoutAssert {
    static_assert(sizeof(FruitFactClassicFactPage) == 0xA0,
        "FruitFactClassicFactPage sizeof must be 0xA0");
    static_assert(offsetof(FruitFactClassicFactPage, m_pTitleBox) == 0x98,
        "FruitFactClassicFactPage::m_pTitleBox must be at +0x98");
    static_assert(offsetof(FruitFactClassicFactPage, m_pBodyBox) == 0x9C,
        "FruitFactClassicFactPage::m_pBodyBox must be at +0x9C");
};
#endif

#endif // FN_SCREENS_FRUIT_FACT_CLASSIC_FACT_PAGE_H
