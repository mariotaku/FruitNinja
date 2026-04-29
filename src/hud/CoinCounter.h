#ifndef FN_HUD_COIN_COUNTER_H
#define FN_HUD_COIN_COUNTER_H

// Analysed: 2026-04-30T00:00
//
// CoinCounter : HUDControl3d (size = 0xD4)
// Struct size confirmed: operator_new(0xd4) in GameInit. (Note: docs/structs/ui-widgets.md
// estimated ~0x94 as a lower bound; binary allocation is ground truth: 0xD4.)
// Coin count display HUD. Stored at Game+0x178. Created in GameInit step 5.
// Update is a true no-op (immediate return @ 0x00135580); all visual logic in Draw @ 0x0013569c.
//
// Binary addresses:
//   ctor (real)    0x00135600
//   ctor (alias)   0x00135644
//   ctor thunk     0x000f43d4
//   dtor (regular) 0x0013558c
//   dtor (inplace) 0x001355b8
//   dtor (deleting)0x001355dc
//   Reset          0x00135548
//   Update         0x00135580  (no-op — empty body)
//   Draw           0x0013569c
//
// m_CoinCount at offset +0x7C (uint16_t). Written directly by external callers.
// Note: Coin::ClearCoins @ 0x001731b8 is a Coin:: entity static, NOT CoinCounter::

#include "HUDControl3d.h"
#include <cstdint>

class CoinCounter : public HUDControl3d {
public:
    // +0x7C: coin count displayed. Written by external callers directly.
    uint16_t m_CoinCount;

    // Remaining subclass fields: 0xD4 - 0x7C - 2 = 0x56 bytes (layout not yet fully RE'd).
    // Reset() touches field_0x8C (float, [0,1] clamp) and field_0x90 (float, =1.0f).
    uint8_t  m_fields_7e[0x56];

    CoinCounter();
    ~CoinCounter() override;

    void Init() override {}     // vtable[2]: coin texture load (called from GameInit via vtable[2])
    void Reset() override;      // 0x00135548: clamps field_0x8C to [0,1], sets field_0x90=1.0f
    void Update(float dt) override { (void)dt; }  // 0x00135580: no-op
    void Draw(const Vec3& hudScale, int layerMask) override { (void)hudScale; (void)layerMask; }

    int GetType() override { return 1; }
};

#endif // FN_HUD_COIN_COUNTER_H
