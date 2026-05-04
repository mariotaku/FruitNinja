#ifndef FN_SPLAT_ENTITY_H
#define FN_SPLAT_ENTITY_H

//
// SplatEntity — juice-splat pool for fruit slicing.
// 1:1 port of the binary's 0x78-byte splat with accurate velocity
// transform, axis vectors, z-based landing, and colour lerp.
//
// Binary refs:
//   SplatEntity::MakeSplat          0x0017f2f0 (131 lines)
//   SplatEntity::Update             0x0017f774 (267 lines)
//   SplatEntity::DrawActiveSplats   0x00180344 (45 lines)
//   UV atlas table                  0x001bd014 (6 entries × 4 floats)
//
// Analysed: 2026-05-04T00:00
//
// NOTE: SplatEntity is NOT an Entity subclass in the binary (confirmed from
// ctor at 0x0017ed58: does NOT call Entity::Entity, no m_Col/m_RecycleFlag).
// Port previously had `: public Entity`; this has been removed (2026-05-04).
// SplatEntity is managed by its own pool (s_Pool), NOT ActorManager.
// TODO: 0x0017ed58 — SplatEntity full layout RE pending; current field
//   layout is provisional based on MakeSplat / Update decompile.
//

#include "math/Vec3.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <cstdint>

class SplatEntity {
public:
    // pos/vel: motion state — used by MakeSplat and UpdateSplat.
    // Binary: own fields in SplatEntity (not inherited from Entity).
    Vec3 pos;
    Vec3 vel;

    // +0x04 (relative to start of own data): colour-phase timer.
    // Starts at 0 for fruit-typed splats (FruitTypeColour path) or
    // 0.75 for the default-colour path. Ticks toward 0.
    float  m_ColourPhase;

    // BGRA colour captured at spawn.
    uint8_t m_ColB;
    uint8_t m_ColG;
    uint8_t m_ColR;
    uint8_t m_ColA;

    // base alpha (float copy of m_ColA at spawn).
    float  m_AlphaBase;

    // rotation angle in degrees (random 0..359 at spawn).
    float  m_Angle;

    // "param3" passed to MakeSplat — marks the special spawn path.
    uint8_t m_bParam3;

    // copied from FruitInfo.m_bSpecial at spawn.
    uint8_t m_bSpecial;

    // right-axis vector from m_Angle.
    Vec3   m_AxisA;

    // up-axis vector from m_Angle + 180.
    Vec3   m_AxisB;

    // random vertical flip flag.
    uint8_t m_bFlipV;

    // per-axis scale triple (sc, -sc, sc).
    Vec3   m_Scale;

    // life timer. Init: Rand(2.5) + 3.75.
    float  m_Life;

    // decay rate. Init: Rand(0.25) + 0.375.
    float  m_DecayRate;

    // fruit type index.
    int    m_FruitType;

    // splat variant. -1 while airborne; 0..5 once landed.
    int8_t m_SplatType;

    // alive flag — iterated by UpdateActiveSplats / DrawActiveSplats.
    uint8_t m_bAlive;

    SplatEntity();
    virtual ~SplatEntity();

    // Matches SplatEntity::MakeSplat (0x0017f2f0).
    void MakeSplat(const Vec3& pos, const Vec3& vel, bool param3, int fruitType);

    // Per-splat tick.
    void UpdateSplat(float dt);

    // Binary: PlaySplat @ 0x0017f5ec — plays one of 6 splat impact SFX.
    static void PlaySplat(int splatSize);

    // Virtual per-instance render: writes 6 QUADCUSTOMVERTEX entries.
    // Binary: SplatEntity::DrawSplat @ 0x0017f008 (virtual vtable slot).
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    virtual void DrawSplat(QUADCUSTOMVERTEX* outVerts, const float tintRGB[3]);

    // Binary: SplatEntity::DrawUpdate @ 0x0017ee2c (single bx lr) — no-op.
    virtual void DrawUpdate(float dt);

    // --- Pool API ---
    static void CreatePool(int capacity);
    static void DestroyPool();
    static SplatEntity* GetFree();
    // Binary: SplatEntity::NumActiveSplats @ 0x0017ee34
    static int NumActiveSplats();
    // Binary: SplatEntity::UpdateActiveSplats @ 0x0017fd68
    static void UpdateActiveSplats(float dt);
    // Binary: SplatEntity::DrawActiveSplats @ 0x00180344
    static void DrawActiveSplats();
    // Binary: SplatEntity::RemoveAllSplats @ 0x0017eea4
    static void RemoveAllSplats();
    static void LoadContent();
    // Binary: SplatEntity::CleanUp @ 0x0017eee0
    static void CleanUp();

    // Pool walker — calls fn(splat) for each pool slot.
    typedef void (*PoolVisitor)(SplatEntity* splat, void* user);
    static void ForEachInPool(PoolVisitor fn, void* user);
};

#endif
