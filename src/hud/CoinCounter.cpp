// Analysed: 2026-04-30T00:00

#include "CoinCounter.h"
#include <cstring>

// ctor @ 0x00135600
CoinCounter::CoinCounter()
    : m_CoinCount(0) {
    std::memset(m_fields_7e, 0, sizeof(m_fields_7e));
}

// dtor @ 0x0013558c / 0x001355b8 / 0x001355dc
CoinCounter::~CoinCounter() {}

// Init @ 0x00135544 -- binary body is an empty no-op (immediate return); faithful.
void CoinCounter::Init() {}

// Update @ 0x00135580: no-op
void CoinCounter::Update(float dt) { (void)dt; }

// Reset @ 0x00135548
// Binary: clamps field_0x8C (float at this+0x8C) to [0,1.0]; sets field_0x90 (float at this+0x90) = 1.0f.
// Offsets relative to CoinCounter base (0x7C from HUDControl3d start):
//   field_0x8C is at local offset 0x8C - 0x7C = 0x10 within m_fields_7e
//   field_0x90 is at local offset 0x90 - 0x7C = 0x14 within m_fields_7e
void CoinCounter::Reset() {
    float* f8c = reinterpret_cast<float*>(m_fields_7e + 0x10);
    float* f90 = reinterpret_cast<float*>(m_fields_7e + 0x14);
    if (*f8c < 0.0f) *f8c = 0.0f;
    if (*f8c > 1.0f) *f8c = 1.0f;
    *f90 = 1.0f;
}

// Release @ 0x0013557c -- binary body is an empty no-op (immediate return); faithful.
void CoinCounter::Release() {}

// PreDraw @ 0x00135584 -- binary body is an empty no-op (immediate return); faithful.
void CoinCounter::PreDraw(const Vec3& hudScale) { (void)hudScale; }

// Skip @ 0x00135588 -- binary body is an empty no-op (immediate return); faithful.
void CoinCounter::Skip() {}
