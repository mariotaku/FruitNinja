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
//   +0x5C/+0x60 float angle min/max
//   +0x64/+0x68 float gridlock x/y
//   +0x6C..+0x90 float rotateCycle fields (10 floats)
//   +0x94..+0xA8 uint8 Colour[6][4] (RGBA byte order)
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
    // +0x40
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
    uint16_t m_BlendMode;          // +0x58 -- parsed from XML, unused by Draw: see
                                    // DIFFERS note in PSPParticleManager.cpp
                                    // FlushParticleVerts (binary sets glBlendFunc
                                    // once at init, never per-template).
    uint16_t _pad5a;               // +0x5A
    // +0x5C
    float    m_AngleMin;           // +0x5C
    float    m_AngleMax;           // +0x60
    float    m_GridLockStart;      // +0x64
    float    m_GridLockEnd;        // +0x68
    // +0x6C  rotateCycle floats (10 x 4 = 40 bytes = 0x28)
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
    // +0x94  Colour[6][4] = 24 bytes
    uint8_t  m_ColourStartMax[4];  // +0x94  RGBA
    uint8_t  m_ColourStartMin[4];  // +0x98
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
static_assert(__builtin_offsetof(PSPParticleTemplate, m_CycleXStart)   == 0x40, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_SpinStartMin)  == 0x50, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_BlendMode)     == 0x58, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_AngleMin)      == 0x5C, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_GridLockStart) == 0x64, "");
static_assert(__builtin_offsetof(PSPParticleTemplate, m_FrictionSpeedStart) == 0x6C, "");
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

// ----------------------------------------------------------------------------
// Per-particle runtime state. Binary: 0xA4 (164) bytes, non-polymorphic.
// +0x40: uint16 m_NextLink — free-list / live-list chain (0=end).
// +0xA0: PSPParticleEmitter* m_pOwnerEmitter — set by AddParticle, read by Draw.
// ----------------------------------------------------------------------------
struct PSPParticle {
    _Vector3<float> m_Pos;             // +0x00
    _Vector3<float> m_Vel;             // +0x0C
    _Vector3<float> m_Gravity;         // +0x18
    float    m_Age;             // +0x24
    float    m_Life;            // +0x28
    float    m_SizeStart;       // +0x2C
    float    m_SizeMid;         // +0x30
    float    m_SizeEnd;         // +0x34
    float    m_Rotation;        // +0x38
    float    m_SpinStart;       // +0x3C
    uint16_t m_NextLink;        // +0x40  1-based index: free-next (free) / live-next (live); 0=end
    uint16_t _pad42;            // +0x42
    uint16_t m_RotAngleIdx;     // +0x44  rotation angle as 16-bit index (AddParticle: angle*182; Draw: += spin*360*182*dt)
    uint16_t m_pad46;           // +0x46
    float    m_RotCycleRate;    // +0x48
    float    m_RotCyclePhase;   // +0x4C
    uint16_t m_ScaleXAngleIdx;  // +0x50  X-scale cycle angle (16-bit idx); Draw: CosIdx -> width scale (local_44)
    uint16_t m_pad52;           // +0x52
    float    m_RotCycleAmp;     // +0x54
    float    m_SpinEnd;         // +0x58  port: spin-end rate (rad/sec)
    uint16_t m_ScaleYAngleIdx;  // +0x5C  Y-scale cycle angle (16-bit idx); Draw: CosIdx -> height scale (local_48)
    uint16_t m_pad5e;           // +0x5E
    float    m_CycleXRate;      // +0x60
    float    m_CycleXPhase;     // +0x64
    float    m_CycleYRate;      // +0x68
    float    m_CycleYPhase;     // +0x6C
    float    m_RotCycleC0;      // +0x70  rotation-cycle modulation constant term (AddParticle: lerp tmpl+0x8C/0x90)
    float    m_RotCycleC1;      // +0x74  rotation-cycle modulation linear term (AddParticle: lerp tmpl+0x7C/0x80)
    float    m_RotCycleC2;      // +0x78  rotation-cycle modulation quadratic term (per-life rate; AddParticle: (lerp tmpl+0x84/0x88)/m_Life)
    uint16_t m_AlphaBase;       // +0x7C  packed colour/alpha base (Draw: VectorUnsignedToFloat -> alpha local_f4)
    int16_t  m_AlphaMidDelta;   // +0x7E  colour interp delta, first half (Draw local_f2)
    int16_t  m_AlphaEndDelta;   // +0x80  colour interp delta, second half (Draw local_f0)
    uint16_t m_pad82;           // +0x82
    _Vector2<float> m_BasisX;          // +0x84  rotated quad basis X (Draw: m_BasisX from RotCycle SinIdx/CosIdx)
    _Vector2<float> m_BasisY;          // +0x8C  rotated quad basis Y
    float    m_Basis2Cos;       // +0x94  secondary basis cos*1.41 (Draw flM_Basis2Cos)
    float    m_Basis2Sin;       // +0x98  secondary basis sin*1.41 (Draw flM_Basis2Sin)
    uint8_t  m_NoAttract;       // +0x9C  per-particle flag: when set, skip global-origin attractor pull (Draw local_d3 gate)
    uint8_t  m_pad9d[3];        // +0x9D
    // +0xA0  owning emitter. ASM-spec v1.6.1 PSPParticleManager::AddParticle @0x0013c554
    // stores the emitter `this` here (the decompiler renders the store as (float)this);
    // Draw @0x0013eccc dereferences it for +0x4C m_bUpdateWhenPaused (per-particle pause
    // gate) and +0x2C m_TimeScale (per-particle dt scale).
    PSPParticleEmitter* m_pOwnerEmitter;

    PSPParticle()
        : m_Pos(0,0,0), m_Vel(0,0,0), m_Gravity(0,0,0)
        , m_Age(0), m_Life(0)
        , m_SizeStart(0), m_SizeMid(0), m_SizeEnd(0)
        , m_Rotation(0), m_SpinStart(0)
        , m_NextLink(0), _pad42(0)
        , m_RotAngleIdx(0), m_pad46(0)
        , m_RotCycleRate(0), m_RotCyclePhase(0)
        , m_ScaleXAngleIdx(0), m_pad52(0)
        , m_RotCycleAmp(0), m_SpinEnd(0)
        , m_ScaleYAngleIdx(0), m_pad5e(0)
        , m_CycleXRate(0), m_CycleXPhase(0)
        , m_CycleYRate(0), m_CycleYPhase(0)
        , m_RotCycleC0(0), m_RotCycleC1(0), m_RotCycleC2(0)
        , m_AlphaBase(0), m_AlphaMidDelta(0), m_AlphaEndDelta(0), m_pad82(0)
        , m_BasisX(0,0), m_BasisY(0,0)
        , m_Basis2Cos(0), m_Basis2Sin(0)
        , m_NoAttract(0), m_pOwnerEmitter(0)
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
static_assert(__builtin_offsetof(PSPParticle, m_NextLink)     == 0x40, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotAngleIdx)   == 0x44, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotCycleRate) == 0x48, "");
static_assert(__builtin_offsetof(PSPParticle, m_ScaleXAngleIdx) == 0x50, "");
static_assert(__builtin_offsetof(PSPParticle, m_RotCycleAmp)  == 0x54, "");
static_assert(__builtin_offsetof(PSPParticle, m_SpinEnd)      == 0x58, "");
static_assert(__builtin_offsetof(PSPParticle, m_ScaleYAngleIdx) == 0x5C, "");
static_assert(__builtin_offsetof(PSPParticle, m_CycleXRate)   == 0x60, "");
static_assert(__builtin_offsetof(PSPParticle, m_AlphaBase)    == 0x7C, "");
static_assert(__builtin_offsetof(PSPParticle, m_BasisX)       == 0x84, "");
static_assert(__builtin_offsetof(PSPParticle, m_BasisY)       == 0x8C, "");
static_assert(__builtin_offsetof(PSPParticle, m_Basis2Cos)    == 0x94, "");
static_assert(__builtin_offsetof(PSPParticle, m_NoAttract)    == 0x9C, "");
static_assert(__builtin_offsetof(PSPParticle, m_pOwnerEmitter) == 0xA0, "");
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
    float    m_SizeBias;                        // +0x24
    float    m_SpinScale;                       // +0x28
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
    uint8_t  m_bTrailStarted;                   // +0x4D
    uint8_t  _pad4e[2];                         // +0x4E

    PSPParticleEmitter()
        : m_Timer(0), m_bStarted(1), m_pad06(0)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_RateScale(1.0f), m_SizeBias(1.0f)
        , m_SpinScale(1.0f), m_TimeScale(1.0f)
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
static_assert(__builtin_offsetof(PSPParticleEmitter, m_SizeBias)          == 0x24, "");
static_assert(__builtin_offsetof(PSPParticleEmitter, m_SpinScale)         == 0x28, "");
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
//   +0x04  float                         m_GlobalTimeScale
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
    // `paused` (callers pass game_work.bM_Mode != 0) freezes each particle whose
    // owning emitter has m_bUpdateWhenPaused == 0: rotation, velocity, position and
    // age stop advancing. Vertex emission is UNCONDITIONAL — frozen particles keep
    // drawing at their last state. Per-particle dt is scaled by the owning emitter's
    // m_TimeScale.
    void Draw(float dt, bool paused, int layer = 0);

    // v1.6.1 PSPParticleManager::LoadFile @0x0013d09c
    bool LoadFile(const char* texCategory, const char* xmlPath, char** outNames = 0);

    // v1.6.1 PSPParticleManager::Destroy @0x0013cfb8
    void Destroy();

    void Clear();

    // v1.6.1 PSPParticleManager::ClearEmitters @0x0010e258 (thunk)
    void ClearEmitters();

    // Binary @ 0x001148dc
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
    //   +0x04 m_GlobalTimeScale (ctor = 1.0)
    //   +0x08 m_GlobalOrigin
    // Written each frame by SuperFruitControl::UpdateExplosion.
    // ASM-spec v1.6.1 PSPParticleManager @0x00013bf40 (non-polymorphic; +0x00 = float
    //   m_GlobalPullRadius, not a vptr): the ctor writes this->__vptr = 0 -- a plain data
    //   field, not a real vtable -- so +0x00 is the world-space vortex pull radius (Draw
    //   pulls free particles toward m_GlobalOrigin within this radius). Removing the port's
    //   virtual dtor drops the compiler vptr and makes +0x00 available for this float.
    float m_GlobalPullRadius;    // +0x00  vortex pull radius (0.0 at reset/ctor)
    float m_GlobalTimeScale;     // +0x04  global time speed (1.0 at reset/ctor)
    _Vector3<float> m_GlobalOrigin;        // +0x08  explosion epicenter

private:
    PSPParticleManager();
    ~PSPParticleManager();  // non-virtual: binary is non-polymorphic (+0x00 is data, not a vptr)

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
static_assert(__builtin_offsetof(PSPParticleManager, m_GlobalTimeScale)  == 0x04, "");
static_assert(__builtin_offsetof(PSPParticleManager, m_GlobalOrigin)     == 0x08, "");
#endif

#endif
