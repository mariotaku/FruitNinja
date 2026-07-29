// FruitFactRewardsPage -- v1.6.1 rewards fact page.
// Binary refs: ctor 0x0017e4d8.
//
// DEAD in v1.6.1 -- see the class comment in FruitFactRewardsPage.h. Only the
// ctor/dtor are ported (for compile-clean call-graph completeness); Init() was
// removed (task #134) since the class is never instantiated.

#include "FruitFactRewardsPage.h"
#include "engine/render/BakedStringBox.h"

// Binary @ 0x0017e4d8
FruitFactRewardsPage::FruitFactRewardsPage(FruitFactControl* pCtrl)
    : FruitFactPage(pCtrl)
    , m_pRewardButton(0)
    , m_pTitleBox(0)
    , m_animA(0.0f)
    , m_animB(0.0f)
    , m_animC(0.0f)
    , m_timerB0(0.0f)
    , m_timerB4(0.0f)
    , m_intB8(-1)
    , m_byteC0(0)
    , m_floatC8(0.0f)
    , m_floatCC(1.0f)
    , m_shortD0((short)32000)
    , m_floatD4(1.0f)
    , m_floatDC(0.0f)
    , m_byteE4(0)
    , m_floatE8(1.0f)
{
    _pad9C[0] = 0; _pad9C[1] = 0; _pad9C[2] = 0; _pad9C[3] = 0;
    _padBC[0] = 0; _padBC[1] = 0; _padBC[2] = 0; _padBC[3] = 0;
    _padC1[0] = 0; _padC1[1] = 0; _padC1[2] = 0;
    _padC4[0] = 0; _padC4[1] = 0; _padC4[2] = 0; _padC4[3] = 0;
    _padD2[0] = 0; _padD2[1] = 0;
    _padD8[0] = 0; _padD8[1] = 0; _padD8[2] = 0; _padD8[3] = 0;
    _padE0[0] = 0; _padE0[1] = 0; _padE0[2] = 0; _padE0[3] = 0;
    _padE5[0] = 0; _padE5[1] = 0; _padE5[2] = 0;
}

FruitFactRewardsPage::~FruitFactRewardsPage() {
    delete m_pTitleBox;
    m_pTitleBox = 0;
}
