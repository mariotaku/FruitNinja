// FruitFactLitePage -- v1.6.1 Lite-mode fruit-fact page.
// Binary refs: ctor 0x0017ad5c, Update 0x0017a220.

#include "FruitFactLitePage.h"

// Binary @ 0x0017ad5c
FruitFactLitePage::FruitFactLitePage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
    , m_LiteState(0)
    , m_RandomSeed(0.0f)
    , m_ScrollPhase(0.0f)
    , m_ElapsedTime(0.0f)
{
}

FruitFactLitePage::~FruitFactLitePage() {
}

// Binary @ 0x0017a220
void FruitFactLitePage::Update(float dt) {
    FruitFactPage::Update(dt);

    // +0xa4: total elapsed-time accumulator.
    m_ElapsedTime += dt;

    // +0xa0: scroll/cycle phase, advances at dt/3 and wraps at 5.0.
    // DAT_0017b11c = 0.0f (field init in ctor @ 0x0017ad5c).
    m_ScrollPhase += dt / 3.0f;
    if (m_ScrollPhase >= 5.0f) {
        m_ScrollPhase -= 5.0f;
    }

    BaseScreen::UpdateButtons(dt);
}
