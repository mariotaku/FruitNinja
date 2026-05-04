#ifndef MORTAR_RANDOM_H
#define MORTAR_RANDOM_H

#include <cstdint>

namespace Math {

// 64-bit LCG with Knuth MMIX multiplier, matching original Math::Random (24 bytes)
// See docs/engine/rng.md for full analysis
class Random {
    uint64_t m_State;
    uint64_t m_Mult;
    uint64_t m_Inc;

public:
    Random();
    explicit Random(uint64_t seed);

    void Seed(uint64_t seed);

    // Integer random in [0, max)
    // Uses multiply-high range reduction (Lemire's method)
    uint32_t Rand32(uint32_t max);

    // Float random in [0, range)
    // ~19 bits of precision (2^19 - 1 = 524287)
    float RandF(float range);
};

} // namespace Math

#endif
