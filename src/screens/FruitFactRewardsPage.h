#ifndef FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
#define FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H

//
// FruitFactRewardsPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x0017e4d8
//
// Layout (FruitFactPage base = BaseScreen(0x94) + m_pController(4) = 0x98 bytes):
//   Own fields relative to object start (absolute offsets):
//     +0x9c : BakedStringBox* m_pTitleBox (owned; stored directly, not via GenericHUDControl)
//     +0xa0 : float m_animA
//     +0xa4 : float m_animB
//     +0xa8 : float m_animC
//     +0xac : float m_timerAc
//     +0xb0 : float m_timerB0
//     +0xb4 : int   m_intB4  (init -1)
//     +0xbc : uint8_t m_byteBC (init 0)
//     +0xc4 : float m_timerC4
//     +0xc8 : float m_floatC8 (init 1.0f)
//     +0xcc : short m_shortCC (init 32000)
//     +0xd0 : float m_timerD0
//     +0xd8 : float m_timerD8
//     +0xe0 : uint8_t m_byteE0 (init 0)
//     +0xe4 : float m_timerE4
//   NOTE: field_0x94=0 in binary spec (DAT_0017e6c0) appears to zero-init
//   an additional field at +0x94 (same slot as m_pController); the actual
//   binary may have a separate int field here or the decompiler merged stores.
//   Port preserves the base ctor's m_pController write and skips the re-zero.
//

#include "FruitFactPage.h"
#include <cstdint>

namespace Mortar { class BakedStringBox; }

class FruitFactRewardsPage : public FruitFactPage {
public:
    // Binary @ 0x0017e4d8
    explicit FruitFactRewardsPage(FruitFactPageControl* pCtrl);
    ~FruitFactRewardsPage() override;

    void Init() override;  // Binary @ 0x0017e4d8 -- state init + CreateSenseisHead + title box

private:
    // NOTE: FruitFactPage ends at +0x98. The spec's field_0x9c is the first own member.
    // If the binary has a 4-byte field at +0x98, it is unresolved; port leaves it implicit.

    // +0x9c: owned title BakedStringBox (not via GenericHUDControl::SetText)
    Mortar::BakedStringBox* m_pTitleBox;   // @+0x9c

    // +0xa0..+0xa8: zeroed animation floats (ctor: field_0xa0=field_0xa4=field_0xa8=0)
    float m_animA;     // @+0xa0
    float m_animB;     // @+0xa4
    float m_animC;     // @+0xa8

    // +0xac: animation timer (ctor=0)
    float m_timerAc;   // @+0xac

    // +0xb0: animation timer (ctor=0)
    float m_timerB0;   // @+0xb0

    // +0xb4: int (ctor=-1)
    int   m_intB4;     // @+0xb4

    // +0xb8: pad (not initialized by ctor)
    uint8_t _padB8[4];

    // +0xbc: byte field (ctor=0)
    uint8_t m_byteBC;  // @+0xbc
    uint8_t _padBD[3];

    // +0xc4: animation timer (ctor=0)
    float m_timerC4;   // @+0xc4

    // +0xc8: float (ctor=1.0f)
    float m_floatC8;   // @+0xc8

    // +0xcc: short (ctor=32000)
    short m_shortCC;   // @+0xcc
    uint8_t _padCE[2];

    // +0xd0: animation timer (ctor=0)
    float m_timerD0;   // @+0xd0

    // +0xd4: pad
    uint8_t _padD4[4];

    // +0xd8: animation timer (ctor=0)
    float m_timerD8;   // @+0xd8

    // +0xdc: pad
    uint8_t _padDC[4];

    // +0xe0: byte field (ctor=0)
    uint8_t m_byteE0;  // @+0xe0
    uint8_t _padE1[3];

    // +0xe4: animation timer (ctor=0)
    float m_timerE4;   // @+0xe4
};

#endif // FN_SCREENS_FRUIT_FACT_REWARDS_PAGE_H
