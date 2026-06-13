// FruitFactBonusFactPage -- v1.6.1 bonus-mode fact page.
// Binary refs: ctor 0x001743b8.

#include "FruitFactBonusFactPage.h"

// Binary @ 0x001743b8
FruitFactBonusFactPage::FruitFactBonusFactPage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
{
}

FruitFactBonusFactPage::~FruitFactBonusFactPage() {
}

// Binary @ 0x001743b8 (ctor body)
// TODO: 0x001743b8 -- BLOCKED on BonusManager (GetInstance/GetFirstBestBonus/GetNextBestBonus),
//   Bonus struct (+0x3c count, +0x80 name), BakedStringBox::SetStroke (both 1-col and 3-col
//   forms), T_1035 (page-title getter from controller), LoadContent, and the .tex strings at
//   DAT_17484c (bg tex) / DAT_174854 (icon tex) from the binary string pool.
//   Also needs: row-colour array Colour[3] = {(0xad,0x7e,0), (0xa0,5,5), (1,0x5c,0x95)}.
void FruitFactBonusFactPage::Init() {
}
