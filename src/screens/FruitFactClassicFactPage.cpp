// FruitFactClassicFactPage -- v1.6.1 Classic-mode fact page (one fact card).
// Binary refs: ctor 0x00174e30.

#include "FruitFactClassicFactPage.h"

// Binary @ 0x00174e30
FruitFactClassicFactPage::FruitFactClassicFactPage(
    FruitFactPageControl* pCtrl, int factIndex, int pageIndex)
    : FruitFactPage(pCtrl)
    , m_factIndex(factIndex)
    , m_pageIndex(pageIndex)
{
}

FruitFactClassicFactPage::~FruitFactClassicFactPage() {
}

// TODO: 0x00174e30 -- build BakedStringBox + HUD children for classic fact display
void FruitFactClassicFactPage::Init() {
}
