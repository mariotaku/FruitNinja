#ifndef MORTAR_RANDOM_H
#define MORTAR_RANDOM_H

#include <cstdint>

namespace Math {

// 64-bit LCG with Knuth MMIX multiplier, matching original Math::Random (24 bytes)
class Random {
    uint64_t m_State;
    uint64_t m_Mult;
    uint64_t m_Inc;

public:
    Random();
    explicit Random(uint64_t seed);

    // ASM-spec v1.6.1 Random::Seed @0x0012c998: (uint32_t) seed; high=0; resets m_Mult/m_Inc.
    void Seed(uint32_t seed);

    // Integer random in [0, max)
    // Uses multiply-high range reduction (Lemire's method)
    uint32_t Rand32(uint32_t max);

    // Float random in [0, range)
    // ~19 bits of precision (2^19 - 1 = 524287)
    float RandF(float range);
};

// Engine-wide RNG instance (binary @ 0x0026C8B0). Populated with the
// boot-time clock seed by SystemManager::Init, re-seeded by
// SeedGlobalRng() at PauseScreen retry/continue/pause boundaries.
extern Random g_Random;

// Re-seed g_Random with `seed` and reset all per-multiplier constants
// (binary @ 0x00153f20). Called by PauseScreen Retry/Continue/Pause
// callbacks with seed = Game::m_FrameTimer to make replays reproducible
// from frame-counter state.
void SeedGlobalRng(uint32_t seed);

// ASM-spec v1.6.1 GetRandBetween<float> @0x001e2f00: uniform draw in
// [lo, hi) via rng.RandF(hi-lo); if halfChance > 0, a SECOND rng.RandF(1.0f)
// draw decides sign (<=halfChance flips negative). halfChance == 0 skips the
// second draw entirely (draw count matters for RNG-sequence fidelity).
float GetRandBetween(Random& rng, float lo, float hi, float halfChance);

} // namespace Math

#endif
