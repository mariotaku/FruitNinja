#ifndef FN_SPLAT_ENTITY_H
#define FN_SPLAT_ENTITY_H

//
// SplatEntity -- juice-splat pool for fruit slicing.
// Binary layout: 0x78 bytes (120), no base class.
// Vtable: 0x001ea5e8, 8 slots (D2, D0, Init, Release, DrawSplat, Update, Draw, DrawUpdate).
//
// Binary refs:
//   SplatEntity ctor            0x0017ed58
//   SplatEntity::MakeSplat      0x0017f2f0
//   SplatEntity::Update         0x0017f774
//   SplatEntity::DrawActiveSplats 0x00180344
//   UV atlas table              0x001bd014 (6 entries x 4 floats)
//   Vtable                      0x001ea5e8
//
// Analysed: 2026-05-04T00:00
//
// NOTE: SplatEntity is NOT an Entity subclass (confirmed from ctor at
// 0x0017ed58: does NOT call Entity::Entity). Managed by s_Pool, NOT ActorManager.
//

#include "math/Vec3.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <cstdint>
#include <cstddef>

class SplatEntity {
public:
    // +0x00: vtable pointer (set by ctor to vtable+8 @ 0x001ea5f0)

    // +0x04: colour-lerp phase. Starts at 0 (fruit-typed) or 0.75 (default path).
    //        Ticks toward 0 in Update.
    float    m_ColourPhase;      // +0x04

    // +0x08: BGRA colour bytes (Colour::Colour() default at ctor, overwritten by MakeSplat)
    uint8_t  m_ColB;             // +0x08
    uint8_t  m_ColG;             // +0x09
    uint8_t  m_ColR;             // +0x0A
    uint8_t  m_ColA;             // +0x0B

    float    m_AlphaBase;        // +0x0C: float copy of m_ColA at spawn; alpha decay scaler

    float    m_Angle;            // +0x10: rotation angle in degrees [0, 360)
    int      m_FruitType;        // +0x14: index into FRUIT_INFO array
    uint8_t  m_bParam3;          // +0x18: special-spawn flag (bomb-juice / critical variant)
    uint8_t  m_bSpecial;         // +0x19: copy of FRUIT_INFO[m_FruitType].m_bSpecial
    uint8_t  pad1A[2];           // +0x1A: padding

    Vec3     m_AxisA;            // +0x1C: right-axis at m_Angle * 0.5
    Vec3     m_AxisB;            // +0x28: up-axis at m_Angle+90 * 0.5

    uint8_t  m_bFlipV;           // +0x34: random V-flip flag
    uint8_t  pad35[3];           // +0x35: padding

    Vec3     m_Pos;              // +0x38: position (spawn z forced to 0; z < -50 -> land)
    Vec3     m_Scale;            // +0x44: (sc, -sc, sc) where sc = Rand[10,20); m_Scale.y mutates in slide-decay
    Vec3     m_ScaleSpawn;       // +0x50: copy of m_Scale at spawn (MakeSplat snapshot; never mutated)
    Vec3     m_Vel;              // +0x5C: velocity (transformed at spawn)

    float    m_Life;             // +0x68: life timer (set on LANDING, not on spawn)
    float    m_DecayRate;        // +0x6C: decay rate (set on LANDING, not on spawn)
    int      m_SplatType;        // +0x70: -1 = airborne; 0..5 once landed
    uint8_t  m_bSSMPHorizGravity; // +0x74: 1 if SSMP and game->field_0xc == 0 (horizontal-grav)
    uint8_t  m_bAlive;           // +0x75: live/dead flag
    uint8_t  pad76[2];           // +0x76: tail padding to 0x78

    // --- Vtable slot 2: Init (binary @ 0x0017edc0) ---
    // Sets m_SplatType=-1; m_bAlive=1. Called via vtable in MakeSplat.
    virtual void Init();

    // --- Vtable slot 3: Release (binary @ 0x0017edd0) ---
    // bx lr (no-op)
    virtual void Release();

    // --- Vtable slot 4: DrawSplat (binary @ 0x0017f008) ---
    // Writes 6 QUADCUSTOMVERTEX entries for this splat into caller's buffer.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    virtual void DrawSplat(QUADCUSTOMVERTEX* outVerts, const float tintRGB[3]);

    // --- Vtable slot 5: Update (binary @ 0x0017f774) ---
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

    // Matches SplatEntity::MakeSplat (0x0017f2f0).
    void MakeSplat(const Vec3& pos, const Vec3& vel, bool param3, int fruitType);

    // Binary: PlaySplat @ 0x0017f5ec -- plays one of 6 splat impact SFX.
    static void PlaySplat(int splatSize);

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

    // Pool walker -- calls fn(splat) for each pool slot.
    typedef void (*PoolVisitor)(SplatEntity* splat, void* user);
    static void ForEachInPool(PoolVisitor fn, void* user);

    // Default ctor (binary @ 0x0017ed58). Called by MemoryPool<SplatEntity>::Create
    // via new[] -- pool manages lifetime, direct construction is not part of normal flow.
    SplatEntity();
};

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
#endif

#endif
