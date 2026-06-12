// FruitFactBigClassicFactPage -- v1.6.1 big Classic-mode fact page.
// Binary refs: ctor 0x00172884.

#include "FruitFactBigClassicFactPage.h"

// Binary @ 0x00172884
FruitFactBigClassicFactPage::FruitFactBigClassicFactPage(
    FruitFactPageControl* pCtrl, int factIndex, int pageIndex)
    : FruitFactPage(pCtrl)
    , m_factIndex(factIndex)
    , m_pageIndex(pageIndex)
{
}

FruitFactBigClassicFactPage::~FruitFactBigClassicFactPage() {
}

// TODO: 0x00172884 -- build BakedStringBox + HUD children for big classic fact display
void FruitFactBigClassicFactPage::Init() {
}
