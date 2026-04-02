# Math::Random — RNG System

## Overview

The game uses a **64-bit Linear Congruential Generator (LCG)** with Knuth's MMIX multiplier. A single global instance lives at `Math::g_random` (0x0026c8b0), and `WaveManager` embeds its own instance at offset +0x00.

## Struct Layout (24 bytes)

```
+0x00: uint32  state_lo     // state low word
+0x04: uint32  state_hi     // state high word (used as output)
+0x08: uint32  mult_lo      // multiplier low word
+0x0C: uint32  mult_hi      // multiplier high word
+0x10: uint32  inc_lo       // increment low word
+0x14: uint32  inc_hi       // increment high word
```

## Constants

| Field | Value (hex) | Value (decimal) | Notes |
|-------|-------------|-----------------|-------|
| Initial seed | `0x00000000_DEADBEEF` | 3,735,928,559 | Classic magic number |
| Multiplier | `0x5D588B65_6C078965` | 6,364,136,223,846,793,317 | **Knuth MMIX LCG** multiplier |
| Increment | `0x00000000_00269EC3` | 2,531,011 | Standard LCG increment |

## Algorithm

### Rand32(max) — Integer random in [0, max)

```c
uint32_t Rand32(Random* rng, uint32_t max) {
    // 64-bit LCG step: state = state * multiplier + increment
    uint64_t state = ((uint64_t)rng->state_hi << 32) | rng->state_lo;
    uint64_t mult  = ((uint64_t)rng->mult_hi  << 32) | rng->mult_lo;
    uint64_t inc   = ((uint64_t)rng->inc_hi   << 32) | rng->inc_lo;
    
    state = state * mult + inc;
    
    rng->state_lo = (uint32_t)state;
    rng->state_hi = (uint32_t)(state >> 32);
    
    // Range reduction: output = upper 32 bits, scaled to [0, max)
    uint32_t output = rng->state_hi;
    if (max >= 2 && max <= 0xFFFFFFFE) {
        output = (uint32_t)(((uint64_t)max * (uint64_t)output) >> 32);
    }
    return output;
}
```

The range reduction `(max * output) >> 32` is a **multiply-high** technique (Lemire's method) — avoids expensive modulo while giving near-uniform distribution.

### RandF(max_float) — Float random in [0.0, max_float)

```c
float RandF(Random* rng, float max_float) {
    uint32_t r = Rand32(rng, 0x7FFFF);   // [0, 524287)
    return ((float)r / 524287.0f) * max_float;
}
```

Produces ~19 bits of precision (2^19 - 1 = 524287). Output range: `[0.0, max_float)`.

### Constructor / InitRandom_Engine

```c
void InitRandom_Engine(Random* rng) {
    rng->inc_lo   = 0x00269EC3;  // increment
    rng->inc_hi   = 0x00000000;
    rng->state_lo = 0xDEADBEEF;  // initial seed
    rng->state_hi = 0x00000000;
    rng->mult_lo  = 0x6C078965;  // multiplier (Knuth MMIX)
    rng->mult_hi  = 0x5D588B65;
}
```

**Important for fidelity**: The seed is fixed (`0xDEADBEEF`), meaning if no external seeding occurs, every game session produces the same RNG sequence. Check whether `WaveManager::Init` or `Game::Init` re-seeds the RNG (e.g., from a timer).

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| Math::Random::Rand32 | 0x00117588 | 17 | Core 64-bit LCG + range reduction |
| Math::Random::RandF | 0x001262c4 | 8 | Float wrapper: Rand32(0x7FFFF) / 524287.0 |
| Math::Random::Random (ctor) | 0x001952b0 | 3 | Calls InitRandom_Engine |
| InitRandom_Engine | (inlined near ctor) | 10 | Loads 6 constants into struct |
| Math::g_random (global) | 0x0026c8b0 | — | Global RNG instance (24 bytes in .bss) |

## Usage Pattern

```
WaveManager::GetNextWave   → Rand32(waveCount)        // pick wave index
Fruit::RandomFruit         → Rand32(totalWeight)       // weighted fruit selection
Fruit::Slice               → Rand32(0x5550)            // random rotation angles
SplatEntity::Update        → Rand32(...)               // splat variant selection
```

All RNG calls use the `WaveManager`'s embedded Random instance (`WaveManager+0x00`), accessed via `WaveManager::GetInstance()`.

## For Porting

```c
// Drop-in replacement for port
struct Random {
    uint32_t state_lo, state_hi;
    uint32_t mult_lo, mult_hi;
    uint32_t inc_lo, inc_hi;
};

void Random_Init(Random* r) {
    r->state_lo = 0xDEADBEEF; r->state_hi = 0;
    r->mult_lo = 0x6C078965;  r->mult_hi = 0x5D588B65;
    r->inc_lo = 0x00269EC3;   r->inc_hi = 0;
}

uint32_t Random_Rand32(Random* r, uint32_t max) {
    uint64_t s = ((uint64_t)r->state_hi << 32) | r->state_lo;
    uint64_t m = ((uint64_t)r->mult_hi << 32)  | r->mult_lo;
    uint64_t c = ((uint64_t)r->inc_hi << 32)   | r->inc_lo;
    s = s * m + c;
    r->state_lo = (uint32_t)s;
    r->state_hi = (uint32_t)(s >> 32);
    uint32_t out = r->state_hi;
    if (max >= 2 && max <= 0xFFFFFFFE)
        out = (uint32_t)(((uint64_t)max * (uint64_t)out) >> 32);
    return out;
}

float Random_RandF(Random* r, float max_f) {
    uint32_t v = Random_Rand32(r, 0x7FFFF);
    return ((float)v / 524287.0f) * max_f;
}
```

---

## See Also

- [Wave system](wave-system.md) — WaveManager embeds Random at +0x00
- [Physics](physics.md) — random rotation on fruit slice
- [Fruit functions](../functions/fruit.md) — RandomFruit weighted selection
