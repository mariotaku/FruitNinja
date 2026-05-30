#ifndef MORTAR_PSP_PARTICLE_MANAGER_H
#define MORTAR_PSP_PARTICLE_MANAGER_H

// Analysed: 2026-04-13T10:30

#include "math/Vec3.h"
#include "math/Vec2.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "core/Singleton.h"
#include <cstdint>
#include <vector>
// GCC 4.4 / -fno-rtti: <memory> pulls in shared_ptr internals that use
// typeid, which fails under -fno-rtti. Use raw-pointer ownership instead.

// ----------------------------------------------------------------------------
// PSPParticleTemplate — loaded from first loop of LoadFile.
// Populated from `<particleTemplate>` XML elements. Not present in current
// FruitNinja XML (particles_fast/slow.xml contain only `<emitter>`s), but the
// struct still exists because `PSPParticleSet::m_pTemplate` points into a flat
// `m_pTemplates` array.
// Field offsets in comments are ARM32 layout from the binary (0xB8 bytes total).
// On 64-bit hosts sizeof() will differ because of 8-byte pointer alignment —
// that's fine, the port is a reimplementation, not a memory-compatible ABI copy.
// See docs/engine/particles.md for per-field XML source.
// ----------------------------------------------------------------------------
struct PSPParticleTemplate {
    float    m_StartTime;          // +0x00  <life>/60
    uint16_t _pad04;               // +0x04
    uint16_t _pad06;               // +0x06
    float    m_VelocityMin[3];     // +0x08..+0x10
    float    m_VelocityMax[3];     // +0x14..+0x1C
    float    m_GravityMin[3];      // +0x20..+0x28
    float    m_GravityMax[3];      // +0x2C..+0x34
    uint8_t  m_Shape;              // +0x38  0=Point,1=Vertex,2=Direction,3=Angular
    uint8_t  m_CoordSystem;        // +0x39  0=Local,1=Global
    uint8_t  m_SizeStartMin;       // +0x3A
    uint8_t  m_SizeStartMax;       // +0x3B
    uint8_t  m_SizeMidMin;         // +0x3C
    uint8_t  m_SizeMidMax;         // +0x3D
    uint8_t  m_SizeEndMin;         // +0x3E
    uint8_t  m_SizeEndMax;         // +0x3F
    int16_t  m_CycleXStart;        // +0x40
    int16_t  m_CycleXEnd;          // +0x42
    int16_t  m_CycleYStart;        // +0x44
    int16_t  m_CycleYEnd;          // +0x46
    int16_t  m_RotCycleStart;      // +0x48
    int16_t  m_RotCycleEndMin;     // +0x4A
    int16_t  m_RotCycleStart2;     // +0x4C
    int16_t  m_RotCycleEndMax;     // +0x4E
    int16_t  m_SpinStartMin;       // +0x50
    int16_t  m_SpinEndMin;         // +0x52
    int16_t  m_SpinStartMax;       // +0x54
    int16_t  m_SpinEndMax;         // +0x56
    uint16_t m_BlendMode;          // +0x58  GL blend func (0x302/0x303/0x01)
    uint16_t _pad5a;               // +0x5A
    float    m_AngleMin;           // +0x5C
    float    m_AngleMax;           // +0x60
    float    m_GridLockStart;      // +0x64
    float    m_GridLockEnd;        // +0x68
    float    m_FrictionSpeedStart;    // +0x6C
    float    m_FrictionSpeedStartMin; // +0x70
    float    m_FrictionSpeedStartMax; // +0x74
    float    m_FrictionSpeedEnd;      // +0x78
    float    m_FrictionSpeedEndMin;   // +0x7C
    float    m_FrictionSpeedEndMax;   // +0x80
    float    m_FrictionOffsetMin;     // +0x84
    float    m_FrictionOffsetMax;     // +0x88
    float    m_FrictionAngle;         // +0x8C
    float    m_FrictionSpin;          // +0x90
    uint8_t  m_ColourStartMax[4];  // +0x94  BGRA
    uint8_t  m_ColourStartMin[4];  // +0x98
    uint8_t  m_ColourMidMin[4];    // +0x9C
    uint8_t  m_ColourMidMax[4];    // +0xA0
    uint8_t  m_ColourEndMin[4];    // +0xA4
    uint8_t  m_ColourEndMax[4];    // +0xA8
    Mortar::SmartPtr<Mortar::Texture> m_Texture;   // +0xAC  4 bytes (pointer)
    float    m_AspectRatio;        // +0xB0
    int32_t  m_UseDepth;           // +0xB4
};
// NOTE: sizeof() on host (x86_64) is larger than the ARM32 0xB8 due to
// pointer/alignment padding. Port uses logical fields, not exact byte layout.

// ----------------------------------------------------------------------------
// PSPParticleSet (0x30 bytes) — inline after PSPEmitterTemplate header.
// One entry per `<particleSet>` XML child of `<emitter>`.
// ----------------------------------------------------------------------------
struct PSPParticleSet {
    // +0x00: pre-load this is a float-encoded template index (0-based),
    // post-load it becomes a PSPParticleTemplate* into m_pTemplates.
    PSPParticleTemplate* m_pTemplate; // +0x00
    float    m_TimeStart;          // +0x04  <time start="...">
    float    m_TimeStop;           // +0x08  <time stop="...">
    uint8_t  m_InitCount;          // +0x0C  <particleNumber init="...">
    uint8_t  _pad0d[3];            // +0x0D
    float    m_PerSec;             // +0x10  <particleNumber perSec="...">
    float    _unused14;            // +0x14
    float    m_VelocityMin[3];     // +0x18..+0x20
    float    m_VelocityMax[3];     // +0x24..+0x2C
};
// NOTE: sizeof() is larger than ARM32 0x30 on 64-bit hosts (pointer pad).

// ----------------------------------------------------------------------------
// PSPEmitterTemplate — loaded from second loop of LoadFile. One per `<emitter>`.
// Original ARM32 layout is a 0x4C-byte header followed inline by
// `m_NumSets × 0x30` bytes of PSPParticleSet entries (variable-length).
// Port uses a std::vector<PSPParticleSet> for the sets list, so the sets are
// heap-allocated rather than inlined — the binary logic (lookup by hash,
// iterate sets) is preserved.
// ----------------------------------------------------------------------------
struct PSPEmitterTemplate {
    char     m_Name[0x40];         // +0x00  name attr strcpy'd (64 bytes)
    uint32_t m_Hash;               // +0x40  StringHash(name) — stored as float in bin but used as u32
    float    m_MaxLifetime;        // +0x44  <life>/60
    uint8_t  m_NumSets;             // +0x4B  number of particleSets
    std::vector<PSPParticleSet> m_Sets;
};

// ----------------------------------------------------------------------------
// Per-particle runtime state. Binary: 0xA4 (164) bytes, non-polymorphic.
// Layout reconstructed from copy-ctor field-write extent (highest write:
// str r2,[r4,#0xa0] -> 0xA0+4 = 0xA4). Member types from copy-ctor VFP vs
// integer move usage; uint16 fields confirmed via ldrh/strh at offsets
// 0x44,0x50,0x5C,0x7C,0x7E,0x80. Vec2 sub-objects at 0x84,0x8C from real
// _Vector2<float> copy-ctor calls (blx 0x000fd554). The sole field the
// default ctor zeroes is the trailing int32 at 0xA0 (named m_field44 in
// Ghidra; all others are left uninitialised by the default ctor).
// Ctors: default 0x00117650, copy 0x00117710.
//
// Fields at 0x24..0x34 and 0x40 use integer moves in copy-ctor (ldr/ldm)
// but may be float in practice -- ARM ldr works for both. Declared as float
// here (same 4-byte, 4-byte-aligned layout) to match port simulation usage.
//
// Port repurposes m_field44 (+0xA0, binary zeroes in default ctor) as the
// particle-set index within the owning emitter's template m_Sets array.
// This is the only per-particle routing field we need and it fits in int32.
// All colour data is looked up at draw time via the set's PSPParticleTemplate.
// ----------------------------------------------------------------------------
struct PSPParticle {
    Vec3     m_Pos;             // +0x00  _Vector3<float> member-ctor
    Vec3     m_Vel;             // +0x0C  _Vector3<float> member-ctor
    Vec3     m_Gravity;         // +0x18  _Vector3<float> member-ctor
    float    m_Age;             // +0x24  integer move in copy-ctor (may be float)
    float    m_Life;            // +0x28  integer move in copy-ctor (may be float)
    float    m_SizeStart;       // +0x2C  integer move in copy-ctor (may be float)
    float    m_SizeMid;         // +0x30  integer move in copy-ctor (may be float)
    float    m_SizeEnd;         // +0x34  integer move in copy-ctor (may be float)
    float    m_Rotation;        // +0x38  vldr in copy-ctor
    float    m_SpinStart;       // +0x3C  vldr in copy-ctor
    float    m_SpinEnd;         // +0x40  integer move in copy-ctor (may be float)
    uint16_t m_field0x44;       // +0x44  ldrh/strh in copy-ctor
    uint16_t m_pad46;           // +0x46  alignment pad (uint16 -> next 4-byte slot)
    float    m_RotCycleRate;    // +0x48  vldr in copy-ctor
    float    m_RotCyclePhase;   // +0x4C  vldr in copy-ctor
    uint16_t m_field0x50;       // +0x50  ldrh/strh in copy-ctor
    uint16_t m_pad52;           // +0x52  alignment pad
    float    m_RotCycleAmp;     // +0x54  vldr in copy-ctor
    float    m_field0x58;       // +0x58  vldr in copy-ctor
    uint16_t m_field0x5c;       // +0x5C  ldrh/strh in copy-ctor
    uint16_t m_pad5e;           // +0x5E  alignment pad
    float    m_CycleXRate;      // +0x60  vldr in copy-ctor
    float    m_CycleXPhase;     // +0x64  vldr in copy-ctor
    float    m_CycleYRate;      // +0x68  vldr in copy-ctor
    float    m_CycleYPhase;     // +0x6C  vldr in copy-ctor
    float    m_field0x70;       // +0x70  vldr in copy-ctor
    float    m_field0x74;       // +0x74  vldr in copy-ctor
    float    m_field0x78;       // +0x78  vldr in copy-ctor
    uint16_t m_field0x7c;       // +0x7C  ldrh/strh in copy-ctor
    uint16_t m_field0x7e;       // +0x7E  ldrh/strh in copy-ctor
    uint16_t m_field0x80;       // +0x80  ldrh/strh in copy-ctor
    uint16_t m_pad82;           // +0x82  alignment pad to reach 0x84
    Vec2     m_field0x84;       // +0x84  _Vector2<float> member-ctor (blx 0x000fd554)
    Vec2     m_field0x8c;       // +0x8C  _Vector2<float> member-ctor (blx 0x000fd554)
    float    m_field0x94;       // +0x94  vldr in copy-ctor
    float    m_field0x98;       // +0x98  vldr in copy-ctor
    uint8_t  m_field0x9c;       // +0x9C  ldrb/strb in copy-ctor
    uint8_t  m_pad9d[3];        // +0x9D  alignment pad
    int32_t  m_field44;         // +0xA0  Ghidra name; default ctor zeroes this only

    PSPParticle()
        : m_Pos(0,0,0), m_Vel(0,0,0), m_Gravity(0,0,0)
        , m_Age(0), m_Life(0)
        , m_SizeStart(0), m_SizeMid(0), m_SizeEnd(0)
        , m_Rotation(0), m_SpinStart(0), m_SpinEnd(0)
        , m_field0x44(0), m_pad46(0)
        , m_RotCycleRate(0), m_RotCyclePhase(0)
        , m_field0x50(0), m_pad52(0)
        , m_RotCycleAmp(0), m_field0x58(0)
        , m_field0x5c(0), m_pad5e(0)
        , m_CycleXRate(0), m_CycleXPhase(0)
        , m_CycleYRate(0), m_CycleYPhase(0)
        , m_field0x70(0), m_field0x74(0), m_field0x78(0)
        , m_field0x7c(0), m_field0x7e(0), m_field0x80(0), m_pad82(0)
        , m_field0x84(0,0), m_field0x8c(0,0)
        , m_field0x94(0), m_field0x98(0)
        , m_field0x9c(0), m_field44(0)
    {
        m_pad9d[0] = m_pad9d[1] = m_pad9d[2] = 0;
    }
};
#ifdef __bada__
static_assert(sizeof(PSPParticle) == 164, "PSPParticle size mismatch");
static_assert(__builtin_offsetof(PSPParticle, m_Pos)          == 0x00, "");
static_assert(__builtin_offsetof(PSPParticle, m_Vel)          == 0x0C, "");
static_assert(__builtin_offsetof(PSPParticle, m_Gravity)      == 0x18, "");
static_assert(__builtin_offsetof(PSPParticle, m_Age)          == 0x24, "");
static_assert(__builtin_offsetof(PSPParticle, m_Life)         == 0x28, "");
static_assert(__builtin_offsetof(PSPParticle, m_Rotation)     == 0x38, "");
static_assert(__builtin_offsetof(PSPParticle, m_SpinStart)    == 0x3C, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x44)    == 0x44, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotCycleRate) == 0x48, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x50)    == 0x50, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotCycleAmp)  == 0x54, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x5c)    == 0x5C, "");
static_assert(__builtin_offsetof(PSPParticle, m_CycleXRate)   == 0x60, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x7c)    == 0x7C, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x84)    == 0x84, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x8c)    == 0x8C, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x94)    == 0x94, "");
static_assert(__builtin_offsetof(PSPParticle, m_field0x9c)    == 0x9C, "");
static_assert(__builtin_offsetof(PSPParticle, m_field44)      == 0xA0, "");
#endif

// ----------------------------------------------------------------------------
// Runtime emitter instance. Binary: 76 (0x4C) bytes, non-polymorphic.
// No base class. Single default ctor at 0x00117640 (4 stores + bx lr).
// Ctor only initialises last 4 fields (+60..+72); earlier fields set by
// the Init/Spawn path. m_Next forms a free-list link; emitters are
// pool/array-allocated by the owning system (no per-emitter operator new).
// Binary uses a global particle pool indexed via m_ParticleHead. The port
// mirrors binary size exactly; per-emitter particle lists live in the manager.
// ----------------------------------------------------------------------------
struct PSPParticleEmitter {
    float    m_Timer;                           // +0x00
    uint16_t m_ParticleHead;                    // +0x04  first particle index
    uint16_t m_pad06;                           // +0x06  alignment pad (Vec3 needs 4-byte alignment)
    Vec3     m_Pos;                             // +0x08  (12 bytes)
    Vec3     m_Vel;                             // +0x14  (12 bytes)
    float    m_TimeScale;                       // +0x20  speed multiplier; default 1.0
    float    m_field24;                         // +0x24  default 1.0
    float    m_ScaleX;                          // +0x28  default 1.0
    float    m_DirCos;                          // +0x2C  cos(trail orientation), default 1.0
    float    m_DirSin;                          // +0x30  sin(trail orientation), default 0.0
    float    m_field34;                         // +0x34  default 1.0
    uint8_t  m_field38;                         // +0x38  default 0
    uint8_t  m_pad39[3];                        // +0x39  alignment pad
    const PSPEmitterTemplate*   m_pTemplate;    // +0x3C  ctor: 0
    PSPParticleEmitter*         m_Next;         // +0x40  ctor: 0  (free-list link)
    PSPParticleEmitter**        m_pRefPtr;      // +0x44  ctor: 0  (caller back-pointer)
    uint8_t  m_bUpdateWhenPaused;               // +0x48  ctor: 0
    uint8_t  m_pad49[3];                        // +0x49  tail pad to binary size 76

    PSPParticleEmitter()
        : m_Timer(0), m_ParticleHead(1), m_pad06(0)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_TimeScale(1.0f), m_field24(1.0f)
        , m_ScaleX(1.0f), m_DirCos(1.0f)
        , m_DirSin(0.0f), m_field34(1.0f), m_field38(0)
        , m_pTemplate(0), m_Next(0), m_pRefPtr(0)
        , m_bUpdateWhenPaused(0)
    {
        m_pad39[0] = m_pad39[1] = m_pad39[2] = 0;
        m_pad49[0] = m_pad49[1] = m_pad49[2] = 0;
    }
};
#ifdef __bada__
static_assert(sizeof(PSPParticleEmitter) == 76, "PSPParticleEmitter size mismatch");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Timer)             == 0x00, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_ParticleHead)      == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Pos)               == 0x08, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Vel)               == 0x14, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_TimeScale)         == 0x20, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_DirCos)            == 0x2C, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_DirSin)            == 0x30, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_field38)           == 0x38, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_pTemplate)         == 0x3C, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Next)              == 0x40, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_pRefPtr)           == 0x44, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_bUpdateWhenPaused) == 0x48, "");
#endif

// ----------------------------------------------------------------------------
// Singleton manager
// ----------------------------------------------------------------------------
class PSPParticleManager : public Mortar::Singleton<PSPParticleManager> {
    friend class Mortar::Singleton<PSPParticleManager>;

public:
    // Add emitter by template hash. Matches AddEmitter (0x1149e0).
    // ppRef (optional) is filled with the returned pointer for caller cleanup;
    // it is cleared to nullptr if template lookup fails.
    // updateWhenPaused maps directly to e.m_bUpdateWhenPaused (binary third arg).
    // Binary @ 0x001149e0 — pop from pool, init defaults, prepend to m_ActiveList
    PSPParticleEmitter* AddEmitter(uint32_t hash,
                                   PSPParticleEmitter** ppRef = nullptr,
                                   bool updateWhenPaused = false);

    // Explicitly release an emitter (matches ClearEmitter 0x114934). Clears
    // the caller back-pointer and marks the emitter for removal on next tick.
    // Binary @ 0x00114934 — find by ptr, unlink, clear back-ref, return to pool
    void ClearEmitter(PSPParticleEmitter* emitter);

    // Binary @ 0x00115ed8 — update all active emitters; skip when paused &&
    // !emitter->m_bUpdateWhenPaused.
    void Update(float dt, bool paused = false);

    // Binary @ 0x00114c64 — fused integrate+render. Port splits into Update/Draw;
    // ABI signature kept as (dt, paused, layer). dt and paused are unused in Draw.
    void Draw(float dt, bool paused, int layer = 0);

    // Binary @ 0x00115f60 — load particle templates from XML. texCategory is
    // prepended to texture filenames: snprintf("%s/%s.tex", texCategory, name).
    // outNames (optional) receives a copy of each <particleTemplate name="...">
    // string (caller-allocated array). Returns true on success.
    bool LoadFile(const char* texCategory, const char* xmlPath, char** outNames = nullptr);

    // Binary @ 0x001155d0 — release tex refs, ClearEmitters, free 3 owned blocks.
    void Destroy();

    void Clear();

    // Binary @ 0x0016cf74 area — deactivate all live emitters (called from GameExit).
    // Binary @ 0x00114974 — drain active list + reset particle free-list + zero
    // per-template live-list heads. Port collapses 2-3 since particles live
    // in the manager's parallel list. // DIFFERS: binary uses 3 separate lists; port uses vector
    void ClearEmitters();

    // Binary @ 0x001148dc — linear hash lookup over emitter templates; bool result.
    bool EmitterExists(uint32_t hash);

    // Binary @ 0x0011490c — index lookup into m_EmitterTemplates[idx].
    // Returns nullptr if idx is out of range.
    PSPEmitterTemplate* GetEmitterTemplate(int idx);

    // Template lookup — used by AddEmitter and unit tests.
    const PSPEmitterTemplate* FindTemplate(uint32_t hash) const;

private:
    PSPParticleManager();
    ~PSPParticleManager();

    std::vector<PSPParticleTemplate> m_ParticleTemplates;
    std::vector<PSPEmitterTemplate>  m_EmitterTemplates;

    // Raw-pointer vector: each entry is owned (new'd in AddEmitter, deleted in
    // Clear/ClearEmitter/dtor). Addresses are stable across appends because
    // emitters are heap-allocated, not inline — same stability guarantee as
    // unique_ptr but without <memory> / typeid dependency (GCC 4.4 / -fno-rtti).
    std::vector<PSPParticleEmitter*>          m_Emitters;
    // Parallel per-emitter particle lists. Index i owns the particles for m_Emitters[i].
    // DIFFERS: binary uses a global flat particle pool indexed via emitter.m_ParticleHead;
    // port uses per-emitter std::vector kept in the manager to preserve binary struct sizes.
    std::vector<std::vector<PSPParticle> >    m_ParticleLists;
};

#endif
