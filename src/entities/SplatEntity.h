#ifndef FN_SPLAT_ENTITY_H
#define FN_SPLAT_ENTITY_H

//
// SplatEntity -- juice-splat pool for fruit slicing.
// Binary layout: 0x78 bytes (120), no base class.
// Vtable: 0x001ea5e8, 8 slots (D2, D0, Init, Release, DrawSplat, Update, Draw, DrawUpdate).
//
// Binary refs:
//   SplatEntity ctor            0x0017ed58
//   SplatEntity::MakeSplat      v1.6.1 @0x001eb910
//   SplatEntity::Update         v1.6.1 @0x001ebee0
//   SplatEntity::DrawSplat      0x001eb5d8
//   SplatEntity::DrawActiveSplats 0x001ece34
//   UV atlas table              0x001bd014 (6 entries x 4 floats)
//   Vtable                      0x001ea5e8
//
// Analysed: 2026-05-04T00:00
//
// NOTE: SplatEntity is NOT an Mortar::Entity subclass (confirmed from ctor at
// 0x0017ed58: does NOT call Mortar::Entity::Entity). Managed by a flat
// round-robin pool (s_PoolBase/s_PoolCount/s_CurrentFree), NOT Mortar::ActorManager
// and NOT Mortar::MemoryPool<T> -- see SplatEntity::GetFree @0x001eb318.
//

#include "math/_Vector3.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <cstdint>
#include <cstddef>

class SplatEntity {
public:
    // +0x00: vtable pointer (set by ctor to vtable+8 @ 0x001ea5f0)

    // +0x04: colour-lerp phase. Starts at 0 (fruit-typed) or 1.5 (default/invalid
    //        fruitType path). Ticks toward 0 in Update.
    float    m_ColourPhase;      // +0x04

    // +0x08: BGRA colour bytes (Colour::Colour() default at ctor, overwritten by MakeSplat)
    uint8_t  m_ColB;             // +0x08
    uint8_t  m_ColG;             // +0x09
    uint8_t  m_ColR;             // +0x0A
    uint8_t  m_ColA;             // +0x0B

    float    m_AlphaBase;        // +0x0C: float copy of m_ColA at spawn; alpha decay scaler

    float    m_Angle;            // +0x10: rotation angle in degrees [0, 360)
    int      m_FruitType;        // +0x14: index into FRUIT_INFO array
    uint8_t  m_bParam3;          // +0x18: streak-eligible flag; true only from the
                                 //        slash-trail spawner (SlashEntity::Update
                                 //        @0x001e982c). On landing: 1-in-2 chance to
                                 //        become directional streak type 4/5.
    uint8_t  m_bSpecial;         // +0x19: copy of FRUIT_INFO[m_FruitType].m_bSpecial
    uint8_t  pad1A[2];           // +0x1A: padding

    _Vector3<float> m_AxisA;            // +0x1C: right-axis at m_Angle * 0.5
    _Vector3<float> m_AxisB;            // +0x28: up-axis at m_Angle+90 * 0.5
                                        //        (streak types 4/5 rebuild both on
                                        //        landing from the velocity angle;
                                        //        axisB * 0.25 -> elongated quad)

    uint8_t  m_bFlipV;           // +0x34: random V-flip flag
    uint8_t  pad35[3];           // +0x35: padding

    _Vector3<float> m_Pos;              // +0x38: position (spawn z forced to 0; z < -50 -> land)
    _Vector3<float> m_Scale;            // +0x44: (sc, -sc, sc) where sc = Rand[10,20); m_Scale.y mutates in slide-decay
    _Vector3<float> m_ScaleSpawn;       // +0x50: copy of m_Scale at spawn (MakeSplat snapshot; never mutated)
    _Vector3<float> m_Vel;              // +0x5C: velocity (transformed at spawn)

    float    m_Life;             // +0x68: life timer (set on LANDING, not on spawn)
    float    m_DecayRate;        // +0x6C: decay rate (set on LANDING, not on spawn)
    int      m_SplatType;        // +0x70: -1 = airborne; 0..5 once landed
    uint8_t  m_bSSMPHorizGravity; // +0x74: 1 if SSMP and game->field_0xc == 0 (horizontal-grav)
    uint8_t  m_bAlive;           // +0x75: live/dead flag
    uint8_t  m_bMuteSfx;         // +0x76: 1 = suppress landing PlaySplat SFX (super-fruit splats land silent)
    uint8_t  pad77[1];           // +0x77: tail padding to 0x78

    // --- Vtable slot 2: Init (binary @ 0x001eb264) ---
    // Binary signature: (SplatEntity*, void*, long, _Vector3*). All args ignored.
    // Body: m_SplatType=-1; m_bAlive=1.
    virtual void Init(void* param1 = 0, long param2 = 0, _Vector3<float>* param3 = 0);

    // --- Vtable slot 3: Release (binary @ 0x0017edd0) ---
    // bx lr (no-op)
    virtual void Release();

    // --- Vtable slot 4: DrawSplat (binary @ 0x001eb5d8) ---
    // Pure thiscall -- pulls vertex cursor from s_NumActiveSplats and writes
    // into s_pSplatVertexBuffer. Tint read from s_CurrentTintRGB (set by
    // DrawActiveSplats before dispatch).
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017f008 (re-analyst) [addr updated: 0x001eb5d8]
    virtual void DrawSplat();

    // --- Vtable slot 5: Update (v1.6.1 SplatEntity::Update @0x001ebee0) ---
    // Per-splat physics, landing transition, slide/colour decay.
    virtual void UpdateSplat(float dt);

    // --- Vtable slot 6: Draw (binary @ 0x0017ee30) ---
    // bx lr (no-op -- splats are batched, never per-instance Draw())
    virtual void Draw();

    // --- Vtable slot 7: DrawUpdate (binary @ 0x0017ee2c) ---
    // bx lr (no-op)
    virtual void DrawUpdate(float dt);

    // Destructor pair (vtable slots 0 and 1)
    virtual ~SplatEntity();

    // ASM-verified: 2026-07-15T00:00Z v1.6.1 SplatEntity::MakeSplat @ 0x001eb910 (asm-inspector)
    //   Signature: (Vec3 pos, Vec3 vel, bool param3, bool mute, long fruitType).
    //   Always spawns AIRBORNE (m_SplatType=-1) with the real launch velocity;
    //   there is no immediate-landing path in the binary. Splats land via
    //   normal Update physics (m_Pos.z < -50 threshold). param3 = streak
    //   eligibility: on landing, 1-in-2 chance to become directional streak
    //   type 4/5, oriented along the landing velocity (applied in Update, not
    //   here). Only the slash-trail spawner passes true.
    //   mute -> m_bMuteSfx (suppress landing SFX). Slash-trail passes the
    //   caller's (FruitInfo::m_bIsSuperFruit @+0x330 != 0); Jiblet::Update and
    //   ExplodeSuperFruit pass a constant 1.
    //
    //   CONTRACT -- MakeSplat ALWAYS initialises the slot and ALWAYS consumes
    //   RNG. It never early-returns. A splat can still be suppressed (25%
    //   roll, transparent fruit, on-side roll), but suppression only clears
    //   m_bAlive at the end; pos/vel/angle/scale/axes/m_SplatType and the
    //   slot-2 Init() call have already run. Callers must therefore check
    //   m_bAlive after the call rather than assuming a spawn succeeded.
    //
    //   Draws off the shared Math::g_Random stream: 5 per call minimum
    //   (flipV, velZ, angle, scale, suppression roll 1), 6 when the fruit is
    //   in range and on-side (suppression roll 2). This count is globally
    //   observable -- see the draw-order table in SplatEntity.cpp.
    void MakeSplat(_Vector3<float> pos, _Vector3<float> vel, bool param3, bool mute, long fruitType);

    // --- Pool API ---
    // Binary: SplatEntity::CreatePool @ 0x001eb490 -- flat round-robin pool
    // (s_PoolBase/s_PoolCount/s_CurrentFree), NOT Mortar::MemoryPool<T>.
    static void CreatePool(int capacity);
    static void DestroyPool();
    // Binary: SplatEntity::GetFree @ 0x001eb318. Round-robin scan for a dead
    // slot starting at s_CurrentFree; NEVER returns null once a pool exists --
    // when every slot is alive it steals (overwrites) the slot at the cursor.
    static SplatEntity* GetFree();
    // Binary: SplatEntity::NumActiveSplats @ 0x0017ee34
    static int NumActiveSplats();
    // Binary: SplatEntity::UpdateActiveSplats v1.6.1 @0x001ec5d8
    static void UpdateActiveSplats(float dt);
    // Binary: SplatEntity::DrawActiveSplats @ 0x001ece34
    static void DrawActiveSplats();
    // Binary: SplatEntity::RemoveAllSplats @ 0x0017eea4
    static void RemoveAllSplats();
    static void LoadContent();
    // Binary: SplatEntity::CleanUp @ 0x001eb404 (v1.6.1; stale 0x0017eee0 was v1.5.x).
    // Destroys the flat pool (dtors + free). Does NOT touch textures.
    static void CleanUp();

    // Pool walker -- calls fn(splat) for each pool slot.
    typedef void (*PoolVisitor)(SplatEntity* splat, void* user);
    static void ForEachInPool(PoolVisitor fn, void* user);

    // Default ctor (binary @ 0x0017ed58). Called by MemoryPool<SplatEntity>::Create
    // via new[] -- pool manages lifetime, direct construction is not part of normal flow.
    SplatEntity();

    // Test seam: s_RandKillEnabled defaults true (binary-faithful suppression rolls).
    // Tests set false to force deterministic splat spawn. Production never modifies it;
    // default behaviour is byte-identical. Setting it false also SKIPS the two
    // suppression draws on Math::g_Random, so the stream diverges from the
    // binary's for everything that draws afterwards.
    static bool s_RandKillEnabled;
};

// Free fn (binary: _Z9PlaySplati @ 0x0017f5ec) -- plays one of 6 splat impact SFX.
void PlaySplat(int splatSize);

// v1.6.1 CleanUpSplat @ 0x001ec88c (capital U -- DISTINCT from CleanupSplat).
// Full SplatEntity teardown: destroys pool (SplatEntity::CleanUp), clears load flag,
// nulls splat texture(s). Called from GameDestroy in order after CleanupFruit.
void CleanUpSplat();

// v1.6.1 CleanupSplat @ 0x001ed0ec (lowercase u -- DISTINCT from CleanUpSplat).
// Dead code in v1.6.1: present in export table but has no callers.
// Stub preserved per "defunct symbols -- stub, never skip" policy.
void CleanupSplat();

// Layout asserts -- only valid on 32-bit ARM cross-compile where pointers are 4 bytes.
// Run under #ifdef __bada__ so the 64-bit port build skips vtable-affected offsets.
#ifdef __bada__
static_assert(sizeof(SplatEntity) == 0x78, "SplatEntity size mismatch");
static_assert(__builtin_offsetof(SplatEntity, m_ColourPhase)       == 0x04, "");
static_assert(__builtin_offsetof(SplatEntity, m_ColB)              == 0x08, "");
static_assert(__builtin_offsetof(SplatEntity, m_ColG)              == 0x09, "");
static_assert(__builtin_offsetof(SplatEntity, m_ColR)              == 0x0A, "");
static_assert(__builtin_offsetof(SplatEntity, m_ColA)              == 0x0B, "");
static_assert(__builtin_offsetof(SplatEntity, m_AlphaBase)         == 0x0C, "");
static_assert(__builtin_offsetof(SplatEntity, m_Angle)             == 0x10, "");
static_assert(__builtin_offsetof(SplatEntity, m_FruitType)         == 0x14, "");
static_assert(__builtin_offsetof(SplatEntity, m_bParam3)           == 0x18, "");
static_assert(__builtin_offsetof(SplatEntity, m_bSpecial)          == 0x19, "");
static_assert(__builtin_offsetof(SplatEntity, m_AxisA)             == 0x1C, "");
static_assert(__builtin_offsetof(SplatEntity, m_AxisB)             == 0x28, "");
static_assert(__builtin_offsetof(SplatEntity, m_bFlipV)            == 0x34, "");
static_assert(__builtin_offsetof(SplatEntity, m_Pos)               == 0x38, "");
static_assert(__builtin_offsetof(SplatEntity, m_Scale)             == 0x44, "");
static_assert(__builtin_offsetof(SplatEntity, m_ScaleSpawn)        == 0x50, "");
static_assert(__builtin_offsetof(SplatEntity, m_Vel)               == 0x5C, "");
static_assert(__builtin_offsetof(SplatEntity, m_Life)              == 0x68, "");
static_assert(__builtin_offsetof(SplatEntity, m_DecayRate)         == 0x6C, "");
static_assert(__builtin_offsetof(SplatEntity, m_SplatType)         == 0x70, "");
static_assert(__builtin_offsetof(SplatEntity, m_bSSMPHorizGravity) == 0x74, "");
static_assert(__builtin_offsetof(SplatEntity, m_bAlive)            == 0x75, "");
static_assert(__builtin_offsetof(SplatEntity, m_bMuteSfx)          == 0x76, "");
#endif

#endif
