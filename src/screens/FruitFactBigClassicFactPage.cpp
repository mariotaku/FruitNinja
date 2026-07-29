// FruitFactBigClassicFactPage -- v1.6.1 big Classic-mode fact page.
// Binary refs: ctor 0x00172884.

#include "FruitFactBigClassicFactPage.h"

// Binary @ 0x00172884
FruitFactBigClassicFactPage::FruitFactBigClassicFactPage(
    FruitFactControl* pCtrl, int factIndex, int pageIndex)
    : FruitFactPage(pCtrl)
    , m_factIndex(factIndex)
    , m_pageIndex(pageIndex)
{
}

FruitFactBigClassicFactPage::~FruitFactBigClassicFactPage() {
}
