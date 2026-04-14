#ifndef FN_SPLAT_ENTITY_H
#define FN_SPLAT_ENTITY_H

//
// SplatEntity — juice-splat pool for fruit slicing.
// Simplified port of the binary's 0x78-byte splat pool.
//
// Binary refs (docs/entities/splat-entity.md):
//   SplatEntity::MakeSplat       0x0017f2f0 (131 lines)
//   SplatEntity::UpdateActiveSplats 0x0017fd68 (93 lines)
//   SplatEntity::DrawActiveSplats   0x00180344 (45 lines)
//   SplatEntity::DrawSplat          0x0017f008 (per-splat vertex build)
//   SplatEntity::GetFree            0x0017ee4c (round-robin pool scan)
//   SplatEntity::CreatePool         0x0017ef34 (heap alloc)
//
// Port diverges in two ways to keep the first pass small:
//   - Uses Mortar::MemoryPool<SplatEntity> (free-list stack) instead of
//     the binary's round-robin active-flag scan. Functionally equivalent.
//   - Renders a rotated textured quad in one batched DrawTriList pass,
//     without the 6-sprite atlas sub-region math. Uses slice_fruit.tex
//     at full UV 0..1 until the atlas layout is resolved.
//
// Analysed: 2026-04-14T01:00
//

#include "Entity.h"
#include "math/Vec3.h"
#include <cstdint>

class SplatEntity : public Entity {
public:
    // --- Runtime state (simplified from binary +0x00..+0x78 layout) ---

    // Size of the splat quad. Binary stores per-component scale at
    // +0x44/+0x48; port uses a single scalar and scales both axes.
    float  m_Scale;

    // Lifetime countdown (seconds). Binary init: `Rand(2.5) + 3.75`
    // via DAT_0017fad8+0x28.
    float  m_Life;

    // Per-frame life decay rate. Binary: `Rand(0.25) + 0.375`.
    float  m_DecayRate;

    // Splat variant index (0..5). Binary picks it in UpdateActiveSplats
    // after pos.z crosses a threshold; port assigns at spawn.
    int    m_SplatType;

    // Fruit palette colour (captured at spawn). Binary lerps the
    // displayed colour between the splat texture base (a pink/grey
    // canvas) and this fruit colour over the last 0.5s of the colour
    // phase timer — see m_ColourPhase below.
    uint8_t m_FruitR;
    uint8_t m_FruitG;
    uint8_t m_FruitB;
    uint8_t m_FruitA;

    // +0x04 equiv: colour-phase timer. Counts down from 1.5s (binary
    // DAT_0017f564). While >= 0.5, the splat shows the texture base
    // colour. Below 0.5, it lerps toward the fruit colour — matches
    // binary UpdateActiveSplats colour block.
    float  m_ColourPhase;

    // 16-bit angle (Atan2Idx of velocity at spawn). Drives the quad
    // orientation in DrawActive.
    uint16_t m_Angle;

    // +0x19 equiv: "special fruit" flag copied from FruitInfo.m_bSpecial.
    // Selects the right half of the atlas when set (binary offsets
    // fVar24/fVar16 by +0.5 in DrawSplat, using U 0.5..1.0 instead of
    // 0..0.5).
    uint8_t m_bSpecial;

    // +0x34 equiv: random horizontal-flip flag (~50% at spawn).
    // Swaps the V coords in DrawActive so the atlas row can render
    // mirrored.
    uint8_t m_bFlipV;

    SplatEntity();
    ~SplatEntity();

    // Matches SplatEntity::MakeSplat (0x17f2f0). Initialises all
    // fields from (pos, vel, fruitType) and a random rotation.
    void MakeSplat(const Vec3& pos, const Vec3& vel, int fruitType);

    // Per-splat tick: integrate pos/vel, apply gravity, decay life.
    // Marks inactive when life reaches zero.
    void UpdateSplat(float dt);

    // Pool API (wraps Mortar::MemoryPool<SplatEntity>). CreatePool
    // must be called once from GameInitialise before any MakeSplat.
    static void CreatePool(int capacity);
    static void DestroyPool();

    // Borrow a free slot. Returns NULL if the pool is exhausted.
    // Matches GetFree semantics; caller runs MakeSplat on the result.
    static SplatEntity* GetFree();

    // Tick every active splat in the pool. Slots whose life runs out
    // get pushed back into the pool automatically.
    // Matches UpdateActiveSplats (0x17fd68).
    static void UpdateActive(float dt);

    // Render every active splat as a single batched triangle list.
    // Matches DrawActiveSplats (0x180344).
    static void DrawActive();

    // Bulk-remove all active splats (called during game-over cleanup).
    // Matches RemoveAllSplats (0x17eea4).
    static void RemoveAll();

    // Load the shared splat texture (slice_fruit.tex). Idempotent.
    static void LoadContent();
    static void ReleaseContent();
};

#endif
