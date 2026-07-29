#ifndef FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H

//
// FruitFactBigClassicFactPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00172884 / 0x00172b68  (FruitFactControl*, int, int)  [C1/C2 variants]
//
// DEAD in v1.6.1 -- linked-but-unreferenced relic, NOT in-game UI.
// Evidence: the ctor has NO .plt thunk at all (.plt = 0x00102964-0x00116d13), its only
// xref is Ghidra's synthetic Entry Point [EXTERNAL], and its vtable @0x002ccff0 is
// referenced solely by its own ctor/dtor/dtor_deleting bodies. GameOverScreen::Update
// @0x00187220 is the sole FruitFactControl::RegisterPage call site and registers exactly
// three pages -- FruitFactZenPage, FruitFactBonusFactPage, FruitFactClassicFactPage.
// This class never appears. (Contrast the live sibling FruitFactBonusFactPage: ctor
// 0x00173d90, .plt thunk 0x0010c77c, called from 0x00187340.)
// Per the port's fidelity policy the test is "does v1.6.1 DRAW it", not "does the class
// exist", so this must NOT be instantiated to force visuals -- doing so would ADD
// divergence (the game-over nav-arrows precedent). Zero instantiation sites is CORRECT.
//

#include "FruitFactPage.h"

class FruitFactBigClassicFactPage : public FruitFactPage {
public:
    // Binary @ 0x00172884 -- ctor(FruitFactControl*, int factIndex, int pageIndex)
    FruitFactBigClassicFactPage(FruitFactControl* pCtrl, int factIndex, int pageIndex);
    ~FruitFactBigClassicFactPage() override;

private:
    int m_factIndex;
    int m_pageIndex;
};

#endif // FN_SCREENS_FRUIT_FACT_BIG_CLASSIC_FACT_PAGE_H
