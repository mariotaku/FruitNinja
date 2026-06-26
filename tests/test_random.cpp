// Math::Random unit test -- determinism, range bounds, distribution sanity.
//
// RNG: 64-bit LCG (Knuth MMIX multiplier).
//   state' = state * 0x5D588B656C078965 + 0x269EC3
//   output = upper 32 bits of state'
//   Rand32(max): multiply-high range reduction, gate max in [1, 0xFFFFFFFE]
//   RandF(range): Rand32(0x7FFFF) / 524287.0f * range, result in [0, range)
//
// GUARANTEES UNDER TEST:
//   1. Determinism: same seed -> identical sequence.
//   2. Different seeds -> different first outputs.
//   3. Rand32(max) in [0, max) for various max; Rand32(1) always 0.
//   4. RandF(1.0f) in [0, 1.0f) -- no sample reaches the upper bound.
//   5. Distribution: Rand32(N) sampled 10000 times, every bin hit at least
//      once (loose; deterministic fixed seed, never flaky).
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "math/Random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// 1. Determinism: same seed -> identical sequence (core guarantee).
// ---------------------------------------------------------------------------
static void test_determinism()
{
    const uint64_t SEED = 0xCAFEBABEDEADBEEFULL;
    const int N = 64;
    uint32_t first[N];

    Math::Random rng(SEED);
    for (int i = 0; i < N; ++i) {
        first[i] = rng.Rand32(0x80000000U);
    }

    // Re-seed and regenerate -- must be byte-identical.
    Math::Random rng2(SEED);
    for (int i = 0; i < N; ++i) {
        uint32_t v = rng2.Rand32(0x80000000U);
        CHECK(v == first[i]);
    }

    // Seed() on existing instance also resets to deterministic sequence.
    Math::Random rng3;
    rng3.Seed(SEED);
    for (int i = 0; i < N; ++i) {
        uint32_t v = rng3.Rand32(0x80000000U);
        CHECK(v == first[i]);
    }
}

// ---------------------------------------------------------------------------
// 2. Different seeds -> different sequences.
// ---------------------------------------------------------------------------
static void test_different_seeds_differ()
{
    Math::Random a(0x1234567890ABCDEFULL);
    Math::Random b(0xFEDCBA9876543210ULL);

    // Check first 8 values -- any one differing is sufficient.
    int differs = 0;
    for (int i = 0; i < 8; ++i) {
        uint32_t va = a.Rand32(0xFFFFFFFFU);
        uint32_t vb = b.Rand32(0xFFFFFFFFU);
        if (va != vb) {
            ++differs;
        }
    }
    // At minimum the first value should differ for distinct seeds (LCG guarantee).
    CHECK(differs > 0);
}

// ---------------------------------------------------------------------------
// 3a. Rand32(max) always in [0, max).
// ---------------------------------------------------------------------------
static void test_rand32_range()
{
    Math::Random rng(0xABCDEF0123456789ULL);
    const int SAMPLES = 10000;

    // Test a variety of max values.
    uint32_t maxvals[] = { 2U, 3U, 7U, 10U, 100U, 256U, 1000U, 65535U,
                           1000000U, 0x7FFFFFFFU, 0xFFFFFFFEU };
    const int NMAXVALS = (int)(sizeof(maxvals) / sizeof(maxvals[0]));

    for (int m = 0; m < NMAXVALS; ++m) {
        uint32_t mx = maxvals[m];
        for (int i = 0; i < SAMPLES; ++i) {
            uint32_t v = rng.Rand32(mx);
            CHECK(v < mx);
        }
    }
}

// ---------------------------------------------------------------------------
// 3b. Rand32(1) always returns 0 (only element in [0, 1)).
// ---------------------------------------------------------------------------
static void test_rand32_one_always_zero()
{
    Math::Random rng(0x1111111111111111ULL);
    for (int i = 0; i < 1000; ++i) {
        CHECK(rng.Rand32(1) == 0);
    }
}

// ---------------------------------------------------------------------------
// 4. RandF(1.0f) in [0, 1.0f) -- no sample reaches the upper bound.
// ---------------------------------------------------------------------------
static void test_randf_bounds()
{
    Math::Random rng(0x9999999999999999ULL);
    const int SAMPLES = 100000;
    for (int i = 0; i < SAMPLES; ++i) {
        float v = rng.RandF(1.0f);
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }

    // Also verify an arbitrary range.
    Math::Random rng2(0x2222222222222222ULL);
    for (int i = 0; i < SAMPLES; ++i) {
        float v = rng2.RandF(100.0f);
        CHECK(v >= 0.0f);
        CHECK(v < 100.0f);
    }
}

// ---------------------------------------------------------------------------
// 5. Distribution: Rand32(N) over 10000 samples -- every bin hit at least once
//    and no bin wildly over-represented.
//    Fixed seed -> deterministic, never flaky.
// ---------------------------------------------------------------------------
static void test_distribution()
{
    const uint32_t BINS = 16;
    const int SAMPLES = 10000;
    // Expected mean per bin: 10000/16 = 625.
    // Loose bounds: each bin must be in [50, 3000] -- catches a broken mapping
    // (all-zero, all-one, or stark clustering) without being sensitive to the
    // exact LCG sequence.
    const int BIN_MIN = 50;
    const int BIN_MAX = 3000;

    int counts[16];
    std::memset(counts, 0, sizeof(counts));

    Math::Random rng(0xDEADBEEFCAFEBABEULL);
    for (int i = 0; i < SAMPLES; ++i) {
        uint32_t v = rng.Rand32(BINS);
        CHECK(v < BINS);
        counts[v]++;
    }

    for (uint32_t b = 0; b < BINS; ++b) {
        CHECK(counts[b] >= BIN_MIN);
        CHECK(counts[b] <= BIN_MAX);
    }
}

// ---------------------------------------------------------------------------
// 6. LCG step -- verify single-step output matches hand-computed values.
//    Uses the known binary constants:
//      mult = 0x5D588B656C078965
//      inc  = 0x269EC3
//      state' = state * mult + inc  (mod 2^64)
//      output = upper 32 bits of state'
// ---------------------------------------------------------------------------
static void test_lcg_known_output()
{
    // Compute by hand with seed=1:
    //   state0 = 1
    //   state1 = 1 * 0x5D588B656C078965 + 0x269EC3
    //          = 0x5D588B656C2A1428
    //   output1 = 0x5D588B65
    const uint64_t MULT = 0x5D588B656C078965ULL;
    const uint64_t INC  = 0x0000000000269EC3ULL;
    const uint64_t STATE0 = 1ULL;
    uint64_t state1 = STATE0 * MULT + INC;
    uint32_t expected_output1 = (uint32_t)(state1 >> 32);

    Math::Random rng(STATE0);
    // Call Rand32 with max=0xFFFFFFFFU -- that triggers the multiply-high path,
    // NOT the identity path. To get the raw output we need max=0 or to call
    // with a max that passes through the gate unchanged. The gate fires when
    // max in [1, 0xFFFFFFFE]. For max=0xFFFFFFFFU (not in the gate range),
    // the output is returned as-is without multiply-high scaling.
    // Gate condition: max >= 1 && max <= 0xFFFFFFFE. 0xFFFFFFFFU fails the
    // upper bound, so output is returned raw (unscaled).
    uint32_t got = rng.Rand32(0xFFFFFFFFU);
    CHECK(got == expected_output1);

    // Second step: verify determinism of two consecutive steps from seed=1.
    uint64_t state2 = state1 * MULT + INC;
    uint32_t expected_output2 = (uint32_t)(state2 >> 32);
    uint32_t got2 = rng.Rand32(0xFFFFFFFFU);
    CHECK(got2 == expected_output2);
}

int main()
{
    std::printf("test_random: start\n");

    test_determinism();
    std::printf("  determinism (same seed -> identical sequence): OK\n");

    test_different_seeds_differ();
    std::printf("  different seeds produce different sequences: OK\n");

    test_rand32_range();
    std::printf("  Rand32(max) always in [0, max): OK\n");

    test_rand32_one_always_zero();
    std::printf("  Rand32(1) always returns 0: OK\n");

    test_randf_bounds();
    std::printf("  RandF(range) always in [0, range): OK\n");

    test_distribution();
    std::printf("  distribution: every bin hit (10000 samples, 16 bins): OK\n");

    test_lcg_known_output();
    std::printf("  LCG step: hand-computed known output verified: OK\n");

    std::printf("test_random: PASS\n");
    return 0;
}
