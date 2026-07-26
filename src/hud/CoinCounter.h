#ifndef FN_HUD_COIN_COUNTER_H
#define FN_HUD_COIN_COUNTER_H

// Defunct: coin/starfruit counter HUD — DEAD in v1.6.1; v1.6.1 CoinCounter::Draw @0x00167730.
//   GameInit @0x001ce1c0 news + HUD-adds it but assigns no texture/pos/size;
//   game_work.pM_Fonts[3] is NULL (GameInitialise @0x0011d7b4); nothing writes
//   m_CoinCount (+0x84), m_AnimScale (+0x8c) or m_CountText (+0x94), so the
//   `m_AnimScale > 0` gate never fires. Real coin credit is
//   CoinArrived @0x001d7a88 -> AddCoins @0x00119f78 -> game_work.nM_CoinsBalance.
//   Draw is still ported (shape fidelity) behind the never-firing gate; do NOT
//   force it visible — that would ADD divergence.
//
// ASM-spec v1.6.1 CoinCounter @ 0x0016765c — size 0xD4:
//   +0x7C uint16_t m_Flags    (ctor=0)
//   +0x7E (2 bytes pad to align +0x80)
//   +0x80 float    m_Field80  (ctor=0.0f; semantics unknown)
//   +0x84 int      m_CoinCount (ctor=0; NO writer exists in v1.6.1 — dead field)
//   +0x88 float    m_Field88  (ctor=0.0f; semantics unknown)
//   +0x8C float    m_AnimScale (Reset clamps to [0,1]; vldr s15,[r0,#0x8c])
//   +0x90 float    m_ScaleReset (Reset sets 1.0f; vstr s14,[r0,#0x90])
//   +0x94 char[64] m_CountText (render buffer; Font::DrawString in Draw)
// Binary: ctor @0x0016765c, dtor @0x001675f4, Reset @0x00167574, Draw @0x00167730.
// (Previous marker addresses were stale v1.5.x; restamped to v1.6.1 here.)

#include "HUDControl3d.h"
#include <cstdint>

class CoinCounter : public HUDControl3d {
public:
    // +0x7C
    uint16_t m_Flags;       // ctor=0
    uint8_t  _pad7E[2];     // alignment pad to reach +0x80

    // +0x80
    float    m_Field80;     // ctor=0.0f (semantics unknown)

    // +0x84
    int      m_CoinCount;   // ctor=0; NO writer exists in v1.6.1 (dead field, see Defunct marker)

    // +0x88
    float    m_Field88;     // ctor=0.0f (semantics unknown)

    // +0x8C
    // ASM-spec v1.6.1 CoinCounter::Reset @ 0x00167574:
    //   clamp m_AnimScale(+0x8C) to [0,1]; m_ScaleReset(+0x90) = 1.0f.
    //   Disasm: vldr s15,[r0,#0x8c]; clamp; vstr s14(=1.0),[r0,#0x90].
    float    m_AnimScale;   // clamped [0,1] by Reset

    // +0x90
    float    m_ScaleReset;  // Reset sets 1.0f

    // +0x94
    char     m_CountText[64]; // render buffer; used by Draw via Font::DrawString

    CoinCounter();
    ~CoinCounter() override;

    void Init() override;       // v1.6.1 @0x00167568: binary body is empty no-op (immediate return)
    void Reset() override;      // v1.6.1 @0x00167574
    void Update(float dt) override;  // v1.6.1 @0x001675e8: no-op
    void Draw(float* hudScaleRaw) override; // v1.6.1 @0x00167730 — ported but unreachable (gate never fires)

    int GetType() override { return 3; }  // v1.6.1 @0x00167b5c

    // Faithful empty no-op overrides (binary bodies are immediate returns):
    void Release() override;      // v1.6.1 @0x0016756c
    void PreDraw(float* hudScale) override;  // v1.6.1 @0x001675e4
    void Skip() override;         // v1.6.1 @0x001675e8 (shares address range with Update nop)
};

#ifdef __bada__
static_assert(sizeof(CoinCounter) == 0xD4, "CoinCounter size must be 0xD4 (v1.6.1 CoinCounter @0x0016765c)");
#endif

#endif // FN_HUD_COIN_COUNTER_H
