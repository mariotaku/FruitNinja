#ifndef FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
#define FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H

//
// FruitFactRewardsPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x0017e4d8
//
// DEAD in v1.6.1 -- linked-but-unreferenced relic, NOT in-game UI.
// Evidence: the ctor @0x0017e4d8 has NO .plt thunk (.plt = 0x00102964-0x00116d13),
// its only xref is Ghidra's synthetic Entry Point [EXTERNAL]. GameOverScreen::Update
// @0x00187220, the sole FruitFactControl::RegisterPage call site, registers exactly
// three pages -- FruitFactZenPage, FruitFactBonusFactPage, FruitFactClassicFactPage.
// This class never appears. (Contrast the live sibling FruitFactBonusFactPage: ctor
// 0x00173d90, .plt thunk 0x0010c77c, called from 0x00187340.)
// Do NOT instantiate to force visuals -- that would ADD divergence, per the game-over
// nav-arrows precedent. Zero instantiation sites is CORRECT.
//
// Layout (FruitFactPage base = BaseScreen(0x94) + m_pController(4) = 0x98 bytes):
//   Derived own-fields (absolute from object start, confirmed from disassembly):
//     +0x98 : int (zero-init first own slot)
//     +0x9c : (pad/unused by ctor)
//     +0xa0 : BakedStringBox* m_pTitleBox   (str [r4,#0xa0], ldr [r4,#0xa0] x4)
//     +0xa4 : float m_animA   (=0)
//     +0xa8 : float m_animB   (=0)
//     +0xac : float m_animC   (=0)
//     +0xb0 : float m_timerB0 (=0.0f)
//     +0xb4 : float m_timerB4 (=0.0f)
//     +0xb8 : int   m_intB8   (=−1)
//     +0xbc : (pad)
//     +0xc0 : uint8 m_byteC0  (=0)
//     +0xc4 : (pad)
//     +0xc8 : float m_floatC8 (=0.0f)
//     +0xcc : float m_floatCC (=1.0f)
//     +0xd0 : short m_shortD0 (=32000)
//     +0xd4 : float m_floatD4 (=1.0f)
//     +0xd8 : (pad)
//     +0xdc : float m_floatDC (=0.0f)
//     +0xe0 : (pad)
//     +0xe4 : uint8 m_byteE4  (=0)
//     +0xe8 : float m_floatE8 (=1.0f)
//   Object size: 0xec (236 bytes).
//

#include "FruitFactPage.h"
#include <cstdint>

namespace Mortar { class BakedStringBox; }

class FruitFactRewardsPage : public FruitFactPage {
public:
    // Binary @ 0x0017e4d8
    explicit FruitFactRewardsPage(FruitFactControl* pCtrl);
    ~FruitFactRewardsPage() override;

private:
    // +0x98: lazily-created "Progress" reward button (Ghidra BSButton* pM_pButton).
    // ctor @0x0017e4d8 inits 0 (str r3=0,[r4,#0x98]); Update @0x0017d44c creates it
    // (operator_new(0xe8), Progress_Button.tex) when 0; Release @0x0017d0a4 reads it
    // (ldr r1,[r0,#0x98]), HUD::RemoveControl + dtor, then nulls. Held as int to preserve
    // 4-byte layout; real type is BSButton*.
    int   m_pRewardButton;   // @+0x98

    // +0x9c: pad (not written by ctor)
    uint8_t _pad9C[4];

    // +0xa0: owned title BakedStringBox (str/ldr [r4,#0xa0])
    Mortar::BakedStringBox* m_pTitleBox;   // @+0xa0

    // +0xa4..+0xac: zeroed animation floats
    float m_animA;     // @+0xa4
    float m_animB;     // @+0xa8
    float m_animC;     // @+0xac

    // +0xb0: animation timer (=0.0f)
    float m_timerB0;   // @+0xb0

    // +0xb4: animation timer (=0.0f)
    float m_timerB4;   // @+0xb4

    // +0xb8: int (=-1)
    int   m_intB8;     // @+0xb8

    // +0xbc: pad
    uint8_t _padBC[4];

    // +0xc0: byte field (=0)
    uint8_t m_byteC0;  // @+0xc0
    uint8_t _padC1[3];

    // +0xc4: pad (not written)
    uint8_t _padC4[4];

    // +0xc8: float (=0.0f)
    float m_floatC8;   // @+0xc8

    // +0xcc: float (=1.0f)
    float m_floatCC;   // @+0xcc

    // +0xd0: short (=32000)
    short m_shortD0;   // @+0xd0
    uint8_t _padD2[2];

    // +0xd4: float (=1.0f)
    float m_floatD4;   // @+0xd4

    // +0xd8: pad
    uint8_t _padD8[4];

    // +0xdc: float (=0.0f)
    float m_floatDC;   // @+0xdc

    // +0xe0: pad
    uint8_t _padE0[4];

    // +0xe4: byte field (=0)
    uint8_t m_byteE4;  // @+0xe4
    uint8_t _padE5[3];

    // +0xe8: float (=1.0f)
    float m_floatE8;   // @+0xe8

#ifdef __bada__
    friend struct FruitFactRewardsPageLayoutAssert;
#endif
};

#ifdef __bada__
#include <cstddef>
struct FruitFactRewardsPageLayoutAssert {
    static_assert(offsetof(FruitFactRewardsPage, m_pTitleBox) == 0xa0,
        "FruitFactRewardsPage::m_pTitleBox must be at +0xa0");
    static_assert(sizeof(FruitFactRewardsPage) == 0xec,
        "FruitFactRewardsPage size must be 0xec (236)");
};
#endif

#endif // FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
