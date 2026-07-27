#include "Random.h"

namespace Math {

// Constants from original binary
static const uint64_t kDefaultSeed  = 0x00000000DEADBEEFULL;
static const uint64_t kMultiplier   = 0x5D588B656C078965ULL; // Knuth MMIX LCG
static const uint64_t kIncrement    = 0x0000000000269EC3ULL;

// ASM-verified: 2026-05-06T13:42 v1.6.1 binary @ 0x00195278 (asm-inspector)
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

// ASM-spec v1.6.1 Random::Seed @0x0012c998: (uint32_t) seed; high=0; resets m_Mult/m_Inc.
void Random::Seed(uint32_t seed) {
    m_State = (uint64_t)seed;  // low=seed, high=0 (zero-extends)
    m_Mult  = kMultiplier;
    m_Inc   = kIncrement;
}

// ASM-verified: 2026-06-19T00:00Z v1.6.1 Math::Random::Rand32 @ 0x00121c2c..0x00121c74 (asm-inspector)
// gate is max in [1,0xFFFFFFFE]; Rand32(1) returns 0.
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

// ASM-spec v1.6.1: the seed sequence is inlined into its callers (e.g.
// PauseScreen::RetryGameCallback @0x001a5800); no standalone symbol was resolved.
// TODO: v1.6.1 Math::SeedGlobalRng -- own address UNVERIFIED (no standalone symbol
//   resolved; the sequence is inlined into its callers).
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

// ASM-spec v1.6.1 GetRandBetween<float> @0x001e2f00..0x001e2f74 (global scope,
// mangles to _Z14GetRandBetweenIfET_S0_S0_f). Explicit specialization (not a
// bare template definition) so the out-of-line symbol is emitted and pairs
// with the binary in symbol-diff.
// ASM-verified: 2026-07-15T00:00Z v1.6.1 GetRandBetween<float> @ 0x001e2f00..0x001e2f74 (asm-inspector)
template<> float GetRandBetween<float>(float lo, float hi, float signFlipProb, float /*unused*/) {
    float res = lo + Math::g_Random.RandF(hi - lo);
    if (signFlipProb > 0.0f) {
        if (Math::g_Random.RandF(1.0f) <= signFlipProb) res = -res;
    }
    return res;
}
