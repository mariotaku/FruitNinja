// v1.6.1 CoinCounter @0x0016765c

#include "CoinCounter.h"
#include <cstring>

// ctor @ v1.6.1 0x0016765c
CoinCounter::CoinCounter()
    : m_Flags(0)
    , _pad7E{0, 0}
    , m_Field80(0.0f)
    , m_CoinCount(0)
    , m_Field88(0.0f)
    , m_AnimScale(0.0f)
    , m_ScaleReset(0.0f)
{
    std::memset(m_CountText, 0, sizeof(m_CountText));
}

// dtor @ v1.6.1 0x001675f4
CoinCounter::~CoinCounter() {}

// Init @ v1.6.1 0x00167568 — binary body is an empty no-op (immediate return)
void CoinCounter::Init() {}

// Update @ v1.6.1 0x001675e8: no-op
void CoinCounter::Update(float dt) { (void)dt; }

// Reset @ v1.6.1 0x00167574
// ASM-spec v1.6.1 CoinCounter::Reset @ 0x00167574:
//   clamp m_AnimScale(+0x8C) to [0,1]; m_ScaleReset(+0x90) = 1.0f.
//   Disasm: vldr s15,[r0,#0x8c]; clamp path; vstr s14(=1.0),[r0,#0x90].
void CoinCounter::Reset() {
    if (m_AnimScale < 0.0f) m_AnimScale = 0.0f;
    if (m_AnimScale > 1.0f) m_AnimScale = 1.0f;
    m_ScaleReset = 1.0f;
}

// Draw @ v1.6.1 0x00167730
// TODO: v1.6.1 0x00167730 (CoinCounter::Draw) — full draw body not yet RE'd;
//   uses m_CoinCount (+0x84) and m_CountText (+0x94) for Font::DrawString render.
void CoinCounter::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
}

// Release @ v1.6.1 0x0016756c — binary body is an empty no-op (immediate return)
void CoinCounter::Release() {}

// PreDraw @ v1.6.1 0x001675e4 — binary body is an empty no-op (immediate return)
void CoinCounter::PreDraw(const Vec3& hudScale) { (void)hudScale; }

// Skip @ v1.6.1 0x001675e8 — binary body is an empty no-op (immediate return)
void CoinCounter::Skip() {}
