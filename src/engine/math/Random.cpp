#include "Random.h"

namespace Math {

// Constants from original binary
static const uint64_t kDefaultSeed  = 0x00000000DEADBEEFULL;
static const uint64_t kMultiplier   = 0x5D588B656C078965ULL; // Knuth MMIX LCG
static const uint64_t kIncrement    = 0x0000000000269EC3ULL;

// ASM-verified: 2026-05-06T13:42 v1.6.1 Math::Random::Random() @ 0x00242074 (asm-inspector)
// DEADBEEF seed / 0x5D588B656C078965 multiplier / 0x269EC3 increment all confirmed.
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

// v1.6.1: no exported SeedGlobalRng symbol; the binary emits file-local T.1054
// @0x001a566c = Math::Random::Seed(&Math::g_random, seed) (out-of-line
// Seed @0x0012c998). It is a compiler-OUTLINED helper shared by the PauseScreen
// TU's callers -- not inlined into each one.
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

// ASM-spec v1.6.1 GetRandBetween<float> @0x001e2f00: global scope (not under
// Math::), 3 params only -- s0=lo, s1=hi, s2=signFlipProb; no instruction
// reads s3. Explicit specialization (not a bare template definition) so the
// out-of-line symbol is emitted and pairs with the binary in symbol-diff.
// Draw #1 (the lo+RandF(hi-lo) term) is gated behind an exact `lo != hi`
// check -- the binary's `vcmp.f32 s0,s1; beq skip` takes NO draw when
// lo==hi. Draw #2 (sign flip) is gated independently on signFlipProb > 0
// and still fires even when lo==hi. Keeping RandF(hi-lo) instead of the
// binary's literal lo + (hi-lo)*RandF(1.0f) is fine: RandF(scale) is
// (r/524287)*scale, so multiplication commutativity makes the two forms
// IEEE-identical and both take exactly one draw.
// ASM-verified: 2026-07-15T00:00Z v1.6.1 GetRandBetween<float> @ 0x001e2f00..0x001e2f74 (asm-inspector)
template<> float GetRandBetween<float>(float lo, float hi, float signFlipProb) {
    float res = lo;
    if (lo != hi) {
        res = lo + Math::g_Random.RandF(hi - lo);
    }
    if (signFlipProb > 0.0f) {
        if (Math::g_Random.RandF(1.0f) <= signFlipProb) res = -res;
    }
    return res;
}
