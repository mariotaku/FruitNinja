#ifndef FN_SCREENS_FRUIT_FACT_LITE_PAGE_H
#define FN_SCREENS_FRUIT_FACT_LITE_PAGE_H

//
// FruitFactLitePage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor   0x0017ad5c / 0x0017a87c  [C1/C2 variants]
//   Update 0x0017a220
//
// DEAD in v1.6.1 -- linked-but-unreferenced relic, NOT in-game UI.
// Evidence: no .plt thunk exists for the ctor (.plt = 0x00102964-0x00116d13), its only
// xref is Ghidra's synthetic Entry Point [EXTERNAL], and its vtable @0x002cd2c8 is
// referenced solely by its own ctor/dtor/dtor_deleting bodies. GameOverScreen::Update
// @0x00187220, the sole FruitFactControl::RegisterPage call site, registers exactly three
// pages and never this one. (Contrast the live sibling FruitFactBonusFactPage: ctor
// 0x00173d90, .plt thunk 0x0010c77c, called from 0x00187340.)
// Do NOT instantiate to force visuals -- that would ADD divergence, per the game-over
// nav-arrows precedent. Zero instantiation sites is CORRECT.
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
