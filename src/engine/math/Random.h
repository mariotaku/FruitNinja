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

// Engine-wide RNG instance (v1.6.1 Math::g_random @ 0x00351db0, .bss; reached
// from PIC code via GOT slot 0x002D8670). Populated with the boot-time clock
// seed by SystemManager::Init, re-seeded by SeedGlobalRng() at PauseScreen
// retry/continue/pause boundaries.
//
// This is the ONE shared stream for all gameplay randomness -- fruit spawns,
// splats, jiblets, coins, SFX variant picks. Draw count and order are
// therefore globally observable: adding, removing or reordering a draw in any
// consumer shifts every later draw in the frame. Callers must reproduce the
// binary's draw sequence exactly, including draws whose result is discarded
// and short-circuits that skip a draw.
extern Random g_Random;

// Re-seed g_Random with `seed` and reset all per-multiplier constants.
// v1.6.1: no exported SeedGlobalRng symbol; the binary emits file-local T.1054
// @0x001a566c = Math::Random::Seed(&Math::g_random, seed) (out-of-line
// Seed @0x0012c998), a compiler-OUTLINED helper shared by the PauseScreen TU's
// callers. Called by PauseScreen Retry/Continue/Pause callbacks with
// seed = Game::m_FrameTimer to make replays reproducible from frame-counter state.
// Gating differs per caller: RetryGameCallback (@0x001a5800) seeds
// UNCONDITIONALLY, while PauseGameCallback / ContinueGameCallback gate on
// game_work.m_bResumeSnapshotPresent (+0x89) != 0.
void SeedGlobalRng(uint32_t seed);

} // namespace Math

// ASM-spec v1.6.1 GetRandBetween<float> @0x001e2f00: global-scope template
// (NOT under namespace Math), 3 params (lo, hi, signFlipProb), hard-float
// s0/s1/s2 -- no instruction reads s3. Reads Math::g_Random internally (no
// Random& param).
//   vcmp.f32 s0,s1; beq skip          // exact IEEE equality, no epsilon
//     res = lo + (hi-lo) * RandF(g_Random, 1.0f)   // DRAW #1, skipped when lo==hi
//   skip:
//   if (signFlipProb > 0.0f) {
//     if (RandF(g_Random, 1.0f) <= signFlipProb) res = -res;  // DRAW #2, gated independently
//   }
//   return res;                        // lo==hi: res is lo unmodified, draw #2 still fires
template<typename T> T GetRandBetween(T lo, T hi, T signFlipProb);

#endif
