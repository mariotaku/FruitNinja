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
// ASM-verified: 2026-05-18 binary @ 0x00117588 (re-analyst) — gate is max in [1,0xFFFFFFFE]; Rand32(1) returns 0.
// Binary: add r12,r1,#-1; cmn r12,#3; it ls; umull.ls r2,r3,r1,r3; mov r0,r3
uint32_t Random::Rand32(uint32_t max) {
    // 64-bit LCG step
    m_State = m_State * m_Mult + m_Inc;

    // Output = upper 32 bits
    uint32_t output = (uint32_t)(m_State >> 32);

    // Range reduction via multiply-high: binary gates on max-1 <=u 0xFFFFFFFD, i.e. max in [1,0xFFFFFFFE].
    if (max >= 1 && max <= 0xFFFFFFFE) {
        output = (uint32_t)(((uint64_t)max * (uint64_t)output) >> 32);
    }
    return output;
}

float Random::RandF(float range) {
    uint32_t v = Rand32(0x7FFFF); // [0, 524287)
    return ((float)v / 524287.0f) * range;
}

// Engine-wide RNG instance (binary @ 0x0026C8B0).
Random g_Random;

// ASM-spec for binary @ 0x00153f20 (re-analyst):
//   void Seed(uint32_t seed):
//     state[0]=seed; state[1]=0;
//     state[2]=0x6C078965; state[3]=0x5D588B65;     // -> m_Mult (LE 64-bit)
//     state[4]=0x00269EC3; state[5]=0x00000000;     // -> m_Inc  (LE 64-bit)
// The 6-dword reset is exactly Random ctor + Seed(seed): both m_Mult and
// m_Inc come back to their port-side kMultiplier / kIncrement defaults
// (since the constants are byte-identical and m_Mult / m_Inc are only
// otherwise written by the ctor).
void SeedGlobalRng(uint32_t seed) {
    g_Random = Random((uint64_t)seed);
}

} // namespace Math
