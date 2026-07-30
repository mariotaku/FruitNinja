#ifndef MORTAR_PSP_PARTICLE_MANAGER_H
#define MORTAR_PSP_PARTICLE_MANAGER_H

#include "math/_Vector3.h"
#include "math/_Vector2.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include "asset/Texture.h"
#include "core/Singleton.h"
#include <cstdint>

// ============================================================================
// Byte-faithful blob structs (0xB8 / 0x4C+N*0x30 / 0x30)
//
// x64 pitfall: SmartPtr<Texture> is 8 bytes on x64 (pointer-sized) but 4 bytes
// in the ARM32 binary. To preserve 0xB8 stride on all hosts, the blob stores
// a 4-byte texture index (into a side-array of SmartPtr<Texture>). Similarly,
// PSPParticleSet::m_pTemplate is a 4-byte blob-offset post-load. All struct
// strides must equal the ARM32 literals 0xB8 / 0x30 / 0x4C header — enforced
// via static_asserts under __bada__ (cross-build) and guarded sizeof checks.
// ============================================================================

// ----------------------------------------------------------------------------
// PSPParticleTemplate blob record — 0xB8 bytes, stride in m_pTemplates.
// Offset map from binary RE:
//   +0x00 float m_Life;
//   +0x04 uint16 live-list HEAD (1-based slot idx into m_pParticles; 0=empty)
//   +0x06 uint16 _pad06
//   +0x08..+0x1C float m_VelocityMin/Max[3] (damping lerp start/end)
//   +0x20..+0x34 float m_GravityMin/Max[3]
//   +0x38 uint8 m_Shape
//   +0x39 uint8 m_CoordSystem
//   +0x3A..+0x3F uint8 size bytes
//   +0x40..+0x56 int16 cycle/spin fields
//   +0x58 uint16 m_BlendMode; +0x5A uint16 pad
//   +0x5C/+0x60 int32 angle min/max
//   +0x64/+0x68 float gridlock x/y
//   +0x6C..+0x90 float rotateCycle fields (10 floats)
//   +0x94..+0xA8 uint8 Colour[6][4] (BGRA byte order)
//   +0xAC uint32 m_TextureIdx (blob: index into m_pTextureRefs; 0xFFFFFFFF=none)
//   +0xB0 float m_AspectRatio
//   +0xB4 int32 m_UseDepth
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct PSPParticleTemplate {
    float    m_Life;               // +0x00  <life>/60.0
    uint16_t m_LiveHead;           // +0x04  live-list HEAD (1-based; 0=empty)
    uint16_t _pad06;               // +0x06
    float    m_VelocityMin[3];     // +0x08  friction/damping lerp start
    float    m_VelocityMax[3];     // +0x14  friction/damping lerp end
    float    m_GravityMin[3];      // +0x20
    float    m_GravityMax[3];      // +0x2C
    // +0x38
    uint8_t  m_Shape;              // +0x38  0=Point, 1=Vortex, 2=Direction, 3=Angular
    uint8_t  m_CoordSystem;        // +0x39  0=Local, 1=Global
    uint8_t  m_SizeStartMin;       // +0x3A
    uint8_t  m_SizeStartMax;       // +0x3B
    uint8_t  m_SizeMidMin;         // +0x3C
    uint8_t  m_SizeMidMax;         // +0x3D
    uint8_t  m_SizeEndMin;         // +0x3E
    uint8_t  m_SizeEndMax;         // +0x3F
    // +0x40..+0x56: three (startMin, startMax, endMin, endMax) int16 quads. Slot order
    // is pinned by v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554, which lerps the
    // two START slots and the two END slots with the SAME random t so a particle that
    // draws "fast" is fast at both ends of its life.
    int16_t  m_CycleXStartMin;     // +0x40  -> particle m_CycleA[0] (X scale cycle rate at t=0)
    int16_t  m_CycleXStartMax;     // +0x42
    int16_t  m_CycleXEndMin;       // +0x44  -> particle m_CycleA[1] (X scale cycle rate at t=1)
    int16_t  m_CycleXEndMax;       // +0x46
    int16_t  m_CycleYStartMin;     // +0x48  -> particle m_CycleB[0]
    int16_t  m_CycleYStartMax;     // +0x4A
    int16_t  m_CycleYEndMin;       // +0x4C  -> particle m_CycleB[1]
    int16_t  m_CycleYEndMax;       // +0x4E
    int16_t  m_SpinStartMin;       // +0x50  -> particle m_SpinPair[0]
    int16_t  m_SpinStartMax;       // +0x52
    int16_t  m_SpinEndMin;         // +0x54  -> particle m_SpinPair[1]
    int16_t  m_SpinEndMax;         // +0x56
    // +0x58: parsed from XML but never consumed. The binary sets glBlendFunc exactly
    // twice, both at init (DisplayManagerBada::Init @0x00256c3c, GlClientStates::Reset
    // @0x00258050), both to (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), and Mesh::DrawTris
    // only toggles the GL_BLEND enable -- so every particle template, including the
    // additive-looking "rimhit" flash, actually draws straight-alpha.
    uint16_t m_BlendMode;          // +0x58
    uint16_t _pad5a;               // +0x5A
    // +0x5C  spawn-angle range. INT32 in the binary (AddParticle @0x0013c554 loads them
    //         with vldr+vcvt.f32.s32, not as floats) -- the lerped result is scaled by
    //         182.0 into the 16-bit angle-index domain.
    int32_t  m_AngleMin;           // +0x5C
    int32_t  m_AngleMax;           // +0x60
    float    m_GridLockStart;      // +0x64
    float    m_GridLockEnd;        // +0x68
    // +0x6C  <rotateCycle> block: five (min, max) float pairs, each lerped with one
    // random t by v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554. The names below
    // record which particle field each pair feeds -- the offsets are proven, the XML
    // attribute that fills each slot is NOT (see the TODO in LoadFile).
    float    m_WobbleAmpStartMin;     // +0x6C  -> particle m_WobbleAmp[0]
    float    m_WobbleAmpStartMax;     // +0x70
    float    m_WobbleAmpEndMin;       // +0x74  -> particle m_WobbleAmp[1]
    float    m_WobbleAmpEndMax;       // +0x78
    float    m_WobbleRateStartMin;    // +0x7C  -> particle m_WobbleRate
    float    m_WobbleRateStartMax;    // +0x80
    float    m_WobbleRateEndMin;      // +0x84  -> particle m_WobbleAccel, via
    float    m_WobbleRateEndMax;      // +0x88     (lerp(EndMin,EndMax) - rate) / life
    float    m_WobblePhaseMin;        // +0x8C  -> particle m_WobblePhaseBase
    float    m_WobblePhaseMax;        // +0x90
    // +0x94  Colour[6][4] = 24 bytes, each in Colour's own [B,G,R,A] byte order.
    // v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554 bakes these into the particle
    // with a straight byte-lane copy (lane i of +0x94 -> particle m_ColourStart[i]),
    // so the byte order here IS the byte order Draw hands to Colour(r,g,b,a).
    // Each pair is lerped low-offset -> high-offset with one random t per lane.
    uint8_t  m_ColourStartMax[4];  // +0x94  BGRA -- lerp source A
    uint8_t  m_ColourStartMin[4];  // +0x98  BGRA -- lerp source B
    uint8_t  m_ColourMidMin[4];    // +0x9C
    uint8_t  m_ColourMidMax[4];    // +0xA0
    uint8_t  m_ColourEndMin[4];    // +0xA4
    uint8_t  m_ColourEndMax[4];    // +0xA8
    // +0xAC
    uint32_t m_TextureIdx;         // +0xAC  index into m_pTextureRefs (0xFFFFFFFF = none)
    float    m_AspectRatio;        // +0xB0
    int32_t  m_UseDepth;           // +0xB4
    // total: 0xB8
};
#pragma pack(pop)

static_assert(sizeof(PSPParticleTemplate) == 0xB8, "PSPParticleTemplate must be 0xB8 bytes");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_Life)          == 0x00, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_LiveHead)      == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_VelocityMin)   == 0x08, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_VelocityMax)   == 0x14, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_GravityMin)    == 0x20, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_GravityMax)    == 0x2C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_Shape)         == 0x38, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_CoordSystem)   == 0x39, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_CycleXStartMin) == 0x40, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_CycleYStartMin) == 0x48, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_SpinStartMin)  == 0x50, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_BlendMode)     == 0x58, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_AngleMin)      == 0x5C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_GridLockStart) == 0x64, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_WobbleAmpStartMin) == 0x6C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_WobbleRateStartMin) == 0x7C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_WobblePhaseMin)    == 0x8C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_ColourStartMax) == 0x94, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_TextureIdx)    == 0xAC, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_AspectRatio)   == 0xB0, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_UseDepth)      == 0xB4, "");

// ----------------------------------------------------------------------------
// PSPParticleSet blob record — 0x30 bytes, inline after emitter header.
// +0x00 uint32 m_TemplateOffset: parse-time = float-encoded index;
//               post-load = byte offset from m_pTemplates base (or 0xFFFFFFFF=none)
// +0x04 float m_TimeStart; +0x08 float m_TimeStop; +0x0C uint8 m_InitCount
// +0x10 float m_PerSec; +0x18 float[3] m_VelocityMin; +0x24 float[3] m_VelocityMax
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct PSPParticleSet {
    uint32_t m_TemplateOffset;    // +0x00  post-load: byte offset into m_pTemplates
    float    m_TimeStart;         // +0x04
    float    m_TimeStop;          // +0x08
    uint8_t  m_InitCount;         // +0x0C
    uint8_t  _pad0d[3];           // +0x0D
    float    m_PerSec;            // +0x10
    float    _unused14;           // +0x14
    float    m_VelocityMin[3];    // +0x18
    float    m_VelocityMax[3];    // +0x24
    // total: 0x30
};
#pragma pack(pop)

static_assert(sizeof(PSPParticleSet) == 0x30, "PSPParticleSet must be 0x30 bytes");
static_assert(__builtin_offsetof(PSPParticleSet, m_TemplateOffset) == 0x00, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_TimeStart)      == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_TimeStop)       == 0x08, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_InitCount)      == 0x0C, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_PerSec)         == 0x10, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_VelocityMin)    == 0x18, "");
static_assert(__builtin_offsetof(PSPParticleSet, m_VelocityMax)    == 0x24, "");

// ----------------------------------------------------------------------------
// PSPEmitterTemplate blob record — variable size: 0x4C header + m_NumSets*0x30.
// Access via PSPParticleManager::GetEmitterTemplate(idx) (variable-stride walk).
// Direct field access via reinterpret_cast<PSPEmitterBlob*>(ptr) or byte offsets.
// Header layout:
//   +0x00 char m_Name[0x40]
//   +0x40 uint32 m_Hash
//   +0x44 float m_MaxLifetime
//   +0x48 uint8[3] _pad48
//   +0x4B uint8 m_NumSets
//   +0x4C: first PSPParticleSet (0x30 bytes each, m_NumSets total)
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct PSPEmitterBlob {
    char     m_Name[0x40];   // +0x00
    uint32_t m_Hash;         // +0x40
    float    m_MaxLifetime;  // +0x44
    uint8_t  _pad48[3];      // +0x48
    uint8_t  m_NumSets;      // +0x4B
    // PSPParticleSet sets[m_NumSets] follow at +0x4C
};
#pragma pack(pop)

static_assert(sizeof(PSPEmitterBlob) == 0x4C, "PSPEmitterBlob header must be 0x4C bytes");
static_assert(__builtin_offsetof(PSPEmitterBlob, m_Name)        == 0x00, "");
static_assert(__builtin_offsetof(PSPEmitterBlob, m_Hash)        == 0x40, "");
static_assert(__builtin_offsetof(PSPEmitterBlob, m_MaxLifetime) == 0x44, "");
static_assert(__builtin_offsetof(PSPEmitterBlob, m_NumSets)     == 0x4B, "");

struct PSPParticleEmitter;
class PSPParticleManager;

// ----------------------------------------------------------------------------
// Per-particle runtime state. Binary: 0xA4 (164) bytes, non-polymorphic.
//
// Everything the renderer needs is BAKED HERE AT SPAWN by
// v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554 -- Draw never re-reads the
// template's colour/size/spin ranges, it only evaluates the baked curves. The
// two consequences that matter for anyone touching this struct:
//
//   * Colour and size are stored as a start value plus two SIGNED deltas
//     (start -> mid over t in [0,0.5], mid -> end over t in [0.5,1]).
//     Draw's saturation is ARM VCVT.U32.F32 + `& 0xFF`: negatives clamp to 0,
//     but there is NO upper clamp -- values above 255 WRAP. That is the
//     binary's behaviour and is deliberate here.
//   * m_TimeRemaining COUNTS DOWN and the particle is alive while
//     m_TimeRemaining > m_DeathThreshold. m_DeathThreshold is not zero: at
//     spawn it is `life - life * emitter->m_LifeBias`, so the visible duration
//     is `templateLife * emitter->m_LifeBias`. Normalised age used by every
//     curve is `t = (templateLife - m_TimeRemaining) / templateLife`, i.e. it
//     is keyed off the TEMPLATE's life, not a per-particle one.
//
// Angles are 16-bit indices (65536 == full turn, so degrees * 182), not radians.
// ----------------------------------------------------------------------------
struct PSPParticle {
    _Vector3<float> m_Pos;          // +0x00
    _Vector3<float> m_Vel;          // +0x0C
    _Vector3<float> m_Accel;        // +0x18  constant acceleration (gravity), baked per particle
    uint8_t  m_ColourStart[4];      // +0x24  [B,G,R,A] -- same byte order as struct Colour
    int16_t  m_ColourMidDelta[4];   // +0x28  colour at t=0.5 minus m_ColourStart, per lane
    int16_t  m_ColourEndDelta[4];   // +0x30  colour at t=1 minus colour at t=0.5, per lane
    float    m_TimeRemaining;       // +0x38  COUNTDOWN; Draw decrements by dt*emitter->m_TimeScale AFTER emitting verts
    float    m_DeathThreshold;      // +0x3C  alive iff m_TimeRemaining > this
    uint16_t m_NextLink;            // +0x40  1-based slot index: live-list next / free-list next; 0=end
    uint16_t _pad42;                // +0x42  (binary writes +0x40 as a 32-bit store)
    uint16_t m_RotAngleIdx;         // +0x44  persistent quad rotation, 16-bit angle index
    uint16_t _pad46;                // +0x46
    float    m_SpinPair[2];         // +0x48  spin rate (turns/sec) at t=0 and t=1; Draw lerps by t
    uint16_t m_ScaleXIdx;           // +0x50  X scale-cycle phase, 16-bit angle index (CosIdx -> width multiplier)
    uint16_t _pad52;                // +0x52
    float    m_CycleA[2];           // +0x54  X scale-cycle rate at t=0 / t=1
    uint16_t m_ScaleYIdx;           // +0x5C  Y scale-cycle phase, 16-bit angle index (CosIdx -> height multiplier)
    uint16_t _pad5e;                // +0x5E
    float    m_CycleB[2];           // +0x60  Y scale-cycle rate at t=0 / t=1
    float    m_WobbleAmp[2];        // +0x68  rotation-wobble amplitude at t=0 / t=1
    float    m_WobblePhaseBase;     // +0x70  wobble phase constant term
    float    m_WobbleRate;          // +0x74  wobble phase linear term
    float    m_WobbleAccel;         // +0x78  wobble phase quadratic term (already divided by template life)
    uint16_t m_SizeStart;           // +0x7C  quad half-height at t=0 (unsigned)
    int16_t  m_SizeMidDelta;        // +0x7E  half-height at t=0.5 minus m_SizeStart
    int16_t  m_SizeEndDelta;        // +0x80  half-height at t=1 minus half-height at t=0.5
    uint16_t _pad82;                // +0x82
    _Vector2<float> m_BasisX;       // +0x84  quad width axis (unit), recomputed by Draw when spinning/wobbling
    _Vector2<float> m_BasisY;       // +0x8C  quad height axis (unit)
    // +0x94/+0x98: a second, 1.41-scaled basis pair derived from
    // `(angle + 0xDFF2) % 0xFFF0`. Written by both AddParticle and Draw and
    // never read by either -- kept because it is part of the 0xA4 layout.
    float    m_Basis2Sin;           // +0x94
    float    m_Basis2Cos;           // +0x98
    uint8_t  m_MirrorX;             // +0x9C  copy of emitter->m_bMirrorX at spawn
    uint8_t  m_NoAttract;           // +0x9D  copy of emitter->m_bTrailStarted; when set, skip the global pull
    uint8_t  _pad9e[2];             // +0x9E
    // +0xA0  owning emitter. Draw dereferences it for +0x08 m_Pos (global-space
    // templates), +0x2C m_TimeScale and +0x4C m_bUpdateWhenPaused.
    PSPParticleEmitter* m_pOwner;

    PSPParticle()
        : m_Pos(0,0,0), m_Vel(0,0,0), m_Accel(0,0,0)
        , m_TimeRemaining(0), m_DeathThreshold(0)
        , m_NextLink(0), _pad42(0)
        , m_RotAngleIdx(0), _pad46(0)
        , m_ScaleXIdx(0), _pad52(0)
        , m_ScaleYIdx(0), _pad5e(0)
        , m_WobblePhaseBase(0), m_WobbleRate(0), m_WobbleAccel(0)
        , m_SizeStart(0), m_SizeMidDelta(0), m_SizeEndDelta(0), _pad82(0)
        , m_BasisX(0,0), m_BasisY(0,0)
        , m_Basis2Sin(0), m_Basis2Cos(0)
        , m_MirrorX(0), m_NoAttract(0)
        , m_pOwner(0)
    {
        for (int i = 0; i < 4; ++i) {
            m_ColourStart[i] = 0;
            m_ColourMidDelta[i] = 0;
            m_ColourEndDelta[i] = 0;
        }
        m_SpinPair[0] = m_SpinPair[1] = 0.0f;
        m_CycleA[0] = m_CycleA[1] = 0.0f;
        m_CycleB[0] = m_CycleB[1] = 0.0f;
        m_WobbleAmp[0] = m_WobbleAmp[1] = 0.0f;
        _pad9e[0] = _pad9e[1] = 0;
    }
};
#ifdef __bada__
static_assert(sizeof(PSPParticle) == 164, "PSPParticle size mismatch");
static_assert(__builtin_offsetof(PSPParticle, m_Pos)             == 0x00, "");
static_assert(__builtin_offsetof(PSPParticle, m_Vel)             == 0x0C, "");
static_assert(__builtin_offsetof(PSPParticle, m_Accel)           == 0x18, "");
static_assert(__builtin_offsetof(PSPParticle, m_ColourStart)     == 0x24, "");
static_assert(__builtin_offsetof(PSPParticle, m_ColourMidDelta)  == 0x28, "");
static_assert(__builtin_offsetof(PSPParticle, m_ColourEndDelta)  == 0x30, "");
static_assert(__builtin_offsetof(PSPParticle, m_TimeRemaining)   == 0x38, "");
static_assert(__builtin_offsetof(PSPParticle, m_DeathThreshold)  == 0x3C, "");
static_assert(__builtin_offsetof(PSPParticle, m_NextLink)        == 0x40, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotAngleIdx)     == 0x44, "");
static_assert(__builtin_offsetof(PSPParticle, m_SpinPair)        == 0x48, "");
static_assert(__builtin_offsetof(PSPParticle, m_ScaleXIdx)       == 0x50, "");
static_assert(__builtin_offsetof(PSPParticle, m_CycleA)          == 0x54, "");
static_assert(__builtin_offsetof(PSPParticle, m_ScaleYIdx)       == 0x5C, "");
static_assert(__builtin_offsetof(PSPParticle, m_CycleB)          == 0x60, "");
static_assert(__builtin_offsetof(PSPParticle, m_WobbleAmp)       == 0x68, "");
static_assert(__builtin_offsetof(PSPParticle, m_WobblePhaseBase) == 0x70, "");
static_assert(__builtin_offsetof(PSPParticle, m_WobbleRate)      == 0x74, "");
static_assert(__builtin_offsetof(PSPParticle, m_WobbleAccel)     == 0x78, "");
static_assert(__builtin_offsetof(PSPParticle, m_SizeStart)       == 0x7C, "");
static_assert(__builtin_offsetof(PSPParticle, m_SizeMidDelta)    == 0x7E, "");
static_assert(__builtin_offsetof(PSPParticle, m_SizeEndDelta)    == 0x80, "");
static_assert(__builtin_offsetof(PSPParticle, m_BasisX)          == 0x84, "");
static_assert(__builtin_offsetof(PSPParticle, m_BasisY)          == 0x8C, "");
static_assert(__builtin_offsetof(PSPParticle, m_Basis2Sin)       == 0x94, "");
static_assert(__builtin_offsetof(PSPParticle, m_Basis2Cos)       == 0x98, "");
static_assert(__builtin_offsetof(PSPParticle, m_MirrorX)         == 0x9C, "");
static_assert(__builtin_offsetof(PSPParticle, m_NoAttract)       == 0x9D, "");
static_assert(__builtin_offsetof(PSPParticle, m_pOwner)          == 0xA0, "");
#endif

// ----------------------------------------------------------------------------
// Runtime emitter instance. Binary: 80 (0x50) bytes, non-polymorphic.
// ----------------------------------------------------------------------------
struct PSPParticleEmitter {
    float    m_Timer;                           // +0x00
    uint16_t m_bStarted;                        // +0x04
    uint16_t m_pad06;                           // +0x06
    _Vector3<float> m_Pos;                             // +0x08
    _Vector3<float> m_Vel;                             // +0x14
    float    m_RateScale;                       // +0x20
    // +0x24: fraction of the template life a particle actually gets. AddParticle sets
    // the death threshold to `life - life * m_LifeBias`, so the visible duration is
    // `templateLife * m_LifeBias` while the curves still run against the full template
    // life (a bias below 1 therefore truncates the tail of the colour/size ramp).
    float    m_LifeBias;                        // +0x24
    float    m_SizeScale;                       // +0x28  multiplies the baked start/mid/end sizes
    float    m_TimeScale;                       // +0x2C
    float    m_DirCos;                          // +0x30
    float    m_DirSin;                          // +0x34
    float    m_VelScale;                        // +0x38
    uint8_t  m_bMirrorX;                        // +0x3C
    uint8_t  _pad3d[3];                         // +0x3D
    const uint8_t*         m_pTemplate;         // +0x40  points into m_pEmitterTemplates blob
    PSPParticleEmitter*    m_Next;              // +0x44
    PSPParticleEmitter**   m_pRefPtr;           // +0x48
    uint8_t  m_bUpdateWhenPaused;               // +0x4C
    uint8_t  m_bTrailStarted;                   // +0x4D  copied into the particle's m_NoAttract
    uint8_t  _pad4e[2];                         // +0x4E

    // v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554 -- pop a free slot from
    // `mgr`, bake every per-particle curve from `set` + its particle template, and
    // push the slot onto that template's live list (head at template+0x04).
    //
    // Draws EXACTLY 23 values from Math::g_Random via RandF(1.0f), plus 3 libc
    // rand() calls and 0/1/2 conditional Rand32(0) draws. g_Random is the one
    // shared gameplay stream, so the count and order are globally observable --
    // do not add, remove or reorder a draw. Silently does nothing when the free
    // list is empty (1024 slots).
    void AddParticle(PSPParticleSet* set, PSPParticleManager& mgr);

    PSPParticleEmitter()
        : m_Timer(0), m_bStarted(1), m_pad06(0)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_RateScale(1.0f), m_LifeBias(1.0f)
        , m_SizeScale(1.0f), m_TimeScale(1.0f)
        , m_DirCos(1.0f), m_DirSin(0.0f), m_VelScale(1.0f)
        , m_bMirrorX(0)
        , m_pTemplate(0), m_Next(0), m_pRefPtr(0)
        , m_bUpdateWhenPaused(0), m_bTrailStarted(0)
    {
        _pad3d[0] = _pad3d[1] = _pad3d[2] = 0;
        _pad4e[0] = _pad4e[1] = 0;
    }
};
#ifdef __bada__
static_assert(sizeof(PSPParticleEmitter) == 0x50, "PSPParticleEmitter size mismatch");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Timer)             == 0x00, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_bStarted)          == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Pos)               == 0x08, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Vel)               == 0x14, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_RateScale)         == 0x20, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_LifeBias)          == 0x24, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_SizeScale)         == 0x28, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_TimeScale)         == 0x2C, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_DirCos)            == 0x30, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_DirSin)            == 0x34, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_VelScale)          == 0x38, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_bMirrorX)          == 0x3C, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_pTemplate)         == 0x40, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_Next)              == 0x44, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_pRefPtr)           == 0x48, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_bUpdateWhenPaused) == 0x4C, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_bTrailStarted)     == 0x4D, "");
#endif

// ----------------------------------------------------------------------------
// PSPParticleManager — singleton manager.
// Binary struct layout (0x38 bytes, v1.6.1 @0x0013bf40):
//   +0x00  float                         m_GlobalPullRadius  (non-polymorphic; NOT a vptr)
//   +0x04  float                         m_GlobalPullStrength
//   +0x08  Vec3                          m_GlobalOrigin
//   +0x14  PSPParticle*                  m_pParticles
//   +0x18  uint16_t                      m_FreeHead
//   +0x1C  int                           m_DrawnParticleCount
//   +0x20  PSPParticleEmitter*           m_pActiveEmitters
//   +0x24  int                           m_NumParticleTemplates
//   +0x28  uint8_t*                      m_pTemplates (blob base, stride 0xB8)
//   +0x2C  int                           m_NumEmitterTemplates
//   +0x30  uint8_t*                      m_pEmitterTemplates (= m_pTemplates + m_NumParticleTemplates*0xB8)
//   +0x34  MemoryPool<PSPParticleEmitter>* m_pEmitterPool
// ----------------------------------------------------------------------------
class PSPParticleManager : public Mortar::Singleton<PSPParticleManager> {
    friend class Mortar::Singleton<PSPParticleManager>;
    // AddParticle is a PSPParticleEmitter method in the binary and pops directly
    // from the manager's free list / particle buffer.
    friend struct PSPParticleEmitter;

public:
    // v1.6.1 PSPParticleManager::AddEmitter @0x0013c1b8
    PSPParticleEmitter* AddEmitter(uint32_t hash,
                                   PSPParticleEmitter** ppRef = 0,
                                   bool updateWhenPaused = false);

    // v1.6.1 PSPParticleManager::ClearEmitter @0x0013c088
    void ClearEmitter(PSPParticleEmitter* emitter);

    // v1.6.1 PSPParticleManager::Update @0x0013cee8 — emitter spawn + reap pass.
    // `paused` (callers pass game_work.bM_Mode != 0): per-emitter gate is
    // m_bStarted && m_RateScale != 0 && (!paused || m_bUpdateWhenPaused), so a
    // paused emitter neither spawns nor advances its timer.
    void Update(float dt, bool paused = false);

    // v1.6.1 PSPParticleManager::Draw @0x0013eccc — fused integrate+render.
    //
    // Iterates PARTICLE templates (m_NumParticleTemplates). `layer` is tested ONCE
    // per template against template+0xB4, so a template belongs wholly to one layer;
    // GameDraw calls this three times per frame with layer = -1, 0, 1.
    //
    // Per template it walks the live list, taking a full 0xA4 stack copy of each
    // particle first (every curve is evaluated against the pre-integration state),
    // reaps expired slots back onto the free list, appends 6 vertices per survivor
    // into one function-static buffer and issues a single Mesh::DrawTriList for the
    // whole template with template+0xAC's texture bound around it. Integration runs
    // AFTER vertex emission, so what you see is last frame's state.
    //
    // `paused` (callers pass game_work.bM_Mode != 0) freezes each particle whose
    // owning emitter has m_bUpdateWhenPaused == 0: rotation, scale cycles, velocity,
    // position and lifetime all stop advancing. Vertex emission stays UNCONDITIONAL,
    // so frozen particles keep drawing at their last state. Per-particle dt is scaled
    // by the owning emitter's m_TimeScale.
    //
    // Also resets m_DrawnParticleCount at entry and accumulates verts/6 per template.
    void Draw(float dt, bool paused, int layer = 0);

    // v1.6.1 PSPParticleManager::LoadFile @0x0013d09c
    bool LoadFile(const char* texCategory, const char* xmlPath, char** outNames = 0);

    // v1.6.1 PSPParticleManager::Destroy @0x0013cfb8
    void Destroy();

    void Clear();

    // v1.6.1 PSPParticleManager::ClearEmitters @0x0013c100 (thunk)
    void ClearEmitters();

    // TODO: PSPParticleManager::EmitterExists -- address unresolved (0x001148dc's PLT thunk
    // resolves to the unrelated Mortar::InitPlacementArrayCopy<_Vector3<float>>, not this
    // function's body).
    bool EmitterExists(uint32_t hash);

    // v1.6.1 PSPEmitterTemplate::Ends @0x00114884 -- true if every set has a
    // finite time window (stopT > 0) or zero continuous rate (i.e. the emitter
    // is not indefinitely self-sustaining). Takes the raw emitter-template blob
    // pointer (PSPParticleEmitter::m_pTemplate).
    static bool EmitterEnds(const uint8_t* eBlob);

    // v1.6.1 PSPParticleManager::GetEmitterTemplate @0x0013c044 — variable-stride walk.
    // Returns pointer into m_pEmitterTemplates blob (or null if idx out of range).
    uint8_t* GetEmitterTemplate(int idx);

    // Port specific: test-only introspection -- number of live emitters on the
    // m_pActiveEmitters list (unit tests assert emitter spawn counts, e.g. the
    // achievement-banner confetti burst). Inline on purpose: no emitted symbol,
    // so symbol-diff never pairs it against the binary (which has no counterpart).
    int CountActiveEmitters() const {
        int n = 0;
        for (const PSPParticleEmitter* e = m_pActiveEmitters; e; e = e->m_Next) ++n;
        return n;
    }

    // Port specific: test-only introspection -- number of particles the LAST
    // Draw(dt, paused, layer) call emitted vertices for. Reset at the top of
    // every Draw, so it is per-LAYER: a caller mirroring GameDraw's
    // Draw(-1)/Draw(0)/Draw(1) triple must read and sum it after each call.
    // Lets a scene test assert "the burst actually emitted" directly instead of
    // inferring it from a framebuffer pixel count. Inline on purpose: no emitted
    // symbol, so symbol-diff never pairs it against the binary.
    int GetDrawnParticleCount() const { return m_DrawnParticleCount; }

    // Accessor: get particle template blob record i (stride 0xB8).
    PSPParticleTemplate* GetParticleTemplate(int i) {
        if (!m_pTemplates || i < 0 || i >= m_NumParticleTemplates) return 0;
        return reinterpret_cast<PSPParticleTemplate*>(m_pTemplates + (size_t)i * 0xB8);
    }

    // Accessor: get set i within emitter blob e.
    static PSPParticleSet* EmitterSet(uint8_t* e, int i) {
        return reinterpret_cast<PSPParticleSet*>(e + 0x4C + (size_t)i * 0x30);
    }

    // Template hash lookup — used by AddEmitter.
    const uint8_t* FindTemplate(uint32_t hash) const;

    // Binary manager global fields (v1.6.1 @0x0013bf40):
    //   +0x00 m_GlobalPullRadius (ctor = 0.0)
    //   +0x04 m_GlobalPullStrength (ctor = 1.0)
    //   +0x08 m_GlobalOrigin
    // Written each frame by SuperFruitControl::UpdateExplosion.
    // ASM-spec v1.6.1 PSPParticleManager @0x00013bf40 (non-polymorphic; +0x00 = float
    //   m_GlobalPullRadius, not a vptr): the ctor writes this->__vptr = 0 -- a plain data
    //   field, not a real vtable -- so +0x00 is the world-space vortex pull radius (Draw
    //   pulls free particles toward m_GlobalOrigin within this radius). Removing the port's
    //   virtual dtor drops the compiler vptr and makes +0x00 available for this float.
    // Draw applies these to every particle whose template is LOCAL-space
    // (template+0x39 != 1) and whose m_NoAttract flag is clear:
    //   len = |pos - m_GlobalOrigin|; if (len < m_GlobalPullRadius)
    //     vel += normalise(pos - m_GlobalOrigin) * (m_GlobalPullRadius - len)
    //            * h * 10.0f * m_GlobalPullStrength;
    // The direction points AWAY from the origin, so this is the super-fruit
    // explosion shockwave, not an attractor. Disabled while m_GlobalPullRadius <= 0.
    float m_GlobalPullRadius;    // +0x00  shockwave radius (0.0 at reset/ctor)
    float m_GlobalPullStrength;  // +0x04  shockwave strength multiplier (1.0 at reset/ctor)
    _Vector3<float> m_GlobalOrigin;        // +0x08  explosion epicenter

private:
    PSPParticleManager();
    ~PSPParticleManager();  // non-virtual: binary is non-polymorphic (+0x00 is data, not a vptr)

    // Resolve template+0xAC to a Texture. The binary keeps a SmartPtr<Texture> inline
    // in the blob; the port keeps a 4-byte index into m_pTextureRefs so the blob stride
    // stays 0xB8 on 64-bit hosts. Returns null when the template has no texture or the
    // asset failed to load (a port-only outcome -- the binary ships all of them).
    Mortar::Texture* GetTemplateTexture(const PSPParticleTemplate* tmpl) const;

    // Binary-faithful layout fields (+0x14 onwards, following vptr+GlobalTimeScale+GlobalOrigin):
    PSPParticle*                              m_pParticles;           // +0x14  1024-slot flat buffer
    uint16_t                                  m_FreeHead;             // +0x18  1-based free-list head
    uint16_t                                  _pad1a;                 // +0x1A  (int alignment pad for +0x1C)
    int                                       m_DrawnParticleCount;   // +0x1C
    PSPParticleEmitter*                       m_pActiveEmitters;      // +0x20
    int                                       m_NumParticleTemplates; // +0x24
    uint8_t*                                  m_pTemplates;           // +0x28  blob base (stride 0xB8)
    int                                       m_NumEmitterTemplates;  // +0x2C
    uint8_t*                                  m_pEmitterTemplates;    // +0x30  = m_pTemplates + m_NumParticleTemplates*0xB8
    Mortar::MemoryPool<PSPParticleEmitter>*   m_pEmitterPool;         // +0x34

    // Port-only side array: SmartPtr<Texture> refs indexed by PSPParticleTemplate::m_TextureIdx.
    // Not in binary (binary stores SmartPtr inline at +0xAC via 4-byte pointer on ARM32).
    // DIFFERS: binary embeds SmartPtr<Texture> at blob+0xAC (4 bytes on ARM32); port stores
    // a uint32_t index in the blob and keeps SmartPtrs in this separate array so the blob
    // stride stays 0xB8 on all hosts. v1.6.1 PSPParticleManager @0x0013bf40.
    Mortar::SmartPtr<Mortar::Texture>*        m_pTextureRefs;         // port-only, not in binary
    int                                       m_NumTextureRefs;       // port-only
};

#ifdef __bada__
// Manager layout assertions: only public members can be checked with __builtin_offsetof
// under GCC 4.4 (private member access is an error at compile time even in constexpr context).
// The private fields at +0x14..+0x34 follow sequentially after m_GlobalOrigin (+0x08, Vec3=12 bytes)
// with no holes; their correctness is enforced by the sizeof(PSPParticleManager) check (when port-only
// tail fields are absent), not by individual offset asserts.
static_assert(__builtin_offsetof(PSPParticleManager, m_GlobalPullRadius) == 0x00, "");
static_assert(__builtin_offsetof(PSPParticleManager, m_GlobalPullStrength) == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleManager, m_GlobalOrigin)     == 0x08, "");
#endif

#endif
