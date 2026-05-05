#include "Random.h"

namespace Math {

// Constants from original binary (see docs/engine/rng.md)
static const uint64_t kDefaultSeed  = 0x00000000DEADBEEFULL;
static const uint64_t kMultiplier   = 0x5D588B656C078965ULL; // Knuth MMIX LCG
static const uint64_t kIncrement    = 0x0000000000269EC3ULL;

// ASM-verified: 2026-05-06T13:42 binary @ 0x00195278 (asm-inspector)
Random::Random()
    : m_State(kDefaultSeed)
    , m_Mult(kMultiplier)
    , m_Inc(kIncrement)
{
}

Random::Random(uint64_t seed)
    : m_State(seed)
    , m_Mult(kMultiplier)
    , m_Inc(kIncrement)
{
}

void Random::Seed(uint64_t seed) {
    m_State = seed;
}

// ASM-verified: 2026-05-06T13:42 binary @ 0x00117588 (asm-inspector)
uint32_t Random::Rand32(uint32_t max) {
    // 64-bit LCG step
    m_State = m_State * m_Mult + m_Inc;

    // Output = upper 32 bits
    uint32_t output = (uint32_t)(m_State >> 32);

    // Range reduction via multiply-high
    if (max >= 2 && max <= 0xFFFFFFFE) {
        output = (uint32_t)(((uint64_t)max * (uint64_t)output) >> 32);
    }
    return output;
}

float Random::RandF(float range) {
    uint32_t v = Rand32(0x7FFFF); // [0, 524287)
    return ((float)v / 524287.0f) * range;
}

} // namespace Math
