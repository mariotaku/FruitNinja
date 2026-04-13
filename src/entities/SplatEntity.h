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

    // Packed tint colour (from fruit palette). Binary lerps this
    // toward white over lifetime; port holds constant.
    uint8_t m_ColourR;
    uint8_t m_ColourG;
    uint8_t m_ColourB;
    uint8_t m_ColourA;

    // 16-bit angle (Atan2Idx of velocity at spawn). Drives the quad
    // orientation in DrawActive.
    uint16_t m_Angle;

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
