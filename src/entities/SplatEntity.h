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
// Analysed: 2026-04-15T15:00
//

#include "Entity.h"
#include "math/Vec3.h"
#include <cstdint>

class SplatEntity : public Entity {
public:
    // +0x04: colour-phase timer. Starts at 0 for fruit-typed splats
    // (FruitTypeColour path) or 0.75 for the default-colour path.
    // Ticks toward 0; drives the base→fruit colour lerp in Draw.
    float  m_ColourPhase;

    // +0x08..+0x0b: BGRA colour captured at spawn (either
    // FruitTypeColour(fruitType) or the default).
    uint8_t m_ColB;
    uint8_t m_ColG;
    uint8_t m_ColR;
    uint8_t m_ColA;

    // +0x0c: base alpha (float copy of m_ColA at spawn). Draw uses
    // min(base_alpha, base_alpha * life_frac) for the fade-out.
    float  m_AlphaBase;

    // +0x10: rotation angle in degrees (random 0..359 at spawn).
    // Drives the axis vectors below.
    float  m_Angle;

    // +0x18: "param3" passed to MakeSplat — marks the special spawn
    // path (critical slice). Biases the landing-type RNG.
    uint8_t m_bParam3;

    // +0x19: copied from FruitInfo.m_bSpecial at spawn. Selects the
    // right half of the atlas table for types 0..3 (U += 0.5).
    uint8_t m_bSpecial;

    // +0x1c: right-axis vector. Computed at spawn from m_Angle:
    //   axis_a = (CosIdx(182*angle), SinIdx(182*angle), 0) * 0.5
    Vec3   m_AxisA;

    // +0x28: up-axis vector. Computed at spawn from m_Angle + 180°:
    //   axis_b = (CosIdx(182*(angle+180)), SinIdx(182*(angle+180)), 0) * 0.5
    Vec3   m_AxisB;

    // +0x34: random vertical flip flag (RandUint(2) != 0).
    uint8_t m_bFlipV;

    // +0x44..+0x4c: per-axis scale triple. Random sc = [10, 20],
    // stored as (sc, -sc, sc). Binary stores a mirror copy at
    // +0x50..+0x58; port collapses to one.
    Vec3   m_Scale;

    // +0x68: life timer. Init: Rand(2.5) + 3.75 → [3.75, 6.25].
    float  m_Life;

    // +0x6c: decay rate. Init: Rand(0.25) + 0.375 → [0.375, 0.625].
    float  m_DecayRate;

    // +0x70: fruit type index (for colour lookup / base_colour lerp).
    int    m_FruitType;

    // +0x74: splat variant. -1 while airborne; assigned to 0..5 once
    // pos.z drops below the landing threshold (-50).
    int8_t m_SplatType;

    // +0x75: alive flag — iterated by UpdateActive / DrawActive.
    uint8_t m_bAlive;

    SplatEntity();
    ~SplatEntity();

    // Matches SplatEntity::MakeSplat (0x0017f2f0).
    //   pos        — slice world position (Z is zeroed internally)
    //   vel        — blade-based velocity (transformed by Y*1.5, Z
    //                bias, final *6.0 multiplier per binary)
    //   param3     — true for the special-spawn path (sets m_bParam3)
    //   fruitType  — index into FRUIT_INFO for colour + bSpecial flag
    void MakeSplat(const Vec3& pos, const Vec3& vel, bool param3, int fruitType);

    // Per-splat tick: integrate physics, handle airborne-to-landing
    // transition (pos.z < -50), run the slide + decay pass once landed.
    void UpdateSplat(float dt);

    // --- Pool API ---
    static void CreatePool(int capacity);
    static void DestroyPool();
    static SplatEntity* GetFree();
    static void UpdateActive(float dt);
    static void DrawActive();
    static void RemoveAll();
    static void LoadContent();
    static void ReleaseContent();
};

#endif
