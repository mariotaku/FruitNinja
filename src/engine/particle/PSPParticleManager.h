#ifndef MORTAR_PSP_PARTICLE_MANAGER_H
#define MORTAR_PSP_PARTICLE_MANAGER_H

// Analysed: 2026-04-13T10:30

#include "math/Vec3.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "core/Singleton.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Mortar {

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
    SmartPtr<Texture> m_Texture;   // +0xAC  4 bytes (pointer)
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
// Per-particle runtime state. Binary uses a 0xA4-byte flat array; the port
// uses std::vector per emitter for simplicity. Fields inferred from
// AddParticle (0x115644) and Draw (0x114c64).
// ----------------------------------------------------------------------------
struct PSPParticle {
    Vec3     m_Pos;
    Vec3     m_Vel;
    Vec3     m_Gravity;
    float    m_Age;
    float    m_Life;        // total lifetime in seconds
    // Two-segment size lerp: start → mid → end, split at age = life/2.
    float    m_SizeStart;
    float    m_SizeMid;
    float    m_SizeEnd;
    float    m_Rotation;    // current angle (radians)
    // Spin rate lerp over lifetime: binary uses int16 RotCycle pairs at
    // template +0x48..+0x4F. Port does a simple linear lerp start→end.
    float    m_SpinStart;   // rad/s at age=0
    float    m_SpinEnd;     // rad/s at age=life
    // Cycle modulation — per-particle phase accumulators + constant rates
    // picked at spawn from template range. Rates are in "cycles/second".
    // RotCycle adds an oscillating offset to m_Rotation (sine wave).
    // CycleX / CycleY scale the drawn half-extents via cosine.
    float    m_RotCycleRate;
    float    m_RotCyclePhase;
    float    m_RotCycleAmp;     // amplitude (radians) = lerp(start, end)
    float    m_CycleXRate;
    float    m_CycleXPhase;
    float    m_CycleYRate;
    float    m_CycleYPhase;
    // Two-segment BGRA colour lerp: start → mid → end, split at age = life/2.
    uint8_t  m_ColourStart[4];
    uint8_t  m_ColourMid[4];
    uint8_t  m_ColourEnd[4];
    const PSPParticleTemplate* m_pTemplate; // for texture + blend mode

    PSPParticle()
        : m_Pos(0,0,0), m_Vel(0,0,0), m_Gravity(0,0,0)
        , m_Age(0), m_Life(0)
        , m_SizeStart(0), m_SizeMid(0), m_SizeEnd(0)
        , m_Rotation(0), m_SpinStart(0), m_SpinEnd(0)
        , m_RotCycleRate(0), m_RotCyclePhase(0), m_RotCycleAmp(0)
        , m_CycleXRate(0), m_CycleXPhase(0)
        , m_CycleYRate(0), m_CycleYPhase(0)
        , m_pTemplate(nullptr)
    {
        for (int i = 0; i < 4; ++i)
            m_ColourStart[i] = m_ColourMid[i] = m_ColourEnd[i] = 255;
    }
};

// ----------------------------------------------------------------------------
// Runtime emitter instance (~0x4C bytes, created from a template by AddEmitter)
// ----------------------------------------------------------------------------
struct PSPParticleEmitter {
    float    m_Timer;           // +0x00
    uint16_t m_ParticleHead;    // +0x04  first particle index (1=uninit sentinel)
    std::vector<PSPParticle> m_Particles; // port: owns particle list per emitter
    Vec3     m_Pos;             // +0x08
    Vec3     m_Vel;             // +0x14
    float    m_TimeScale;       // +0x20  speed multiplier
    float    m_field24;         // +0x24  default 1.0
    float    m_ScaleX;          // +0x28  default 1.0
    float    m_ScaleY;          // +0x2C  default 1.0
    float    m_field30;         // +0x30  default 0.0
    float    m_field34;         // +0x34  default 1.0
    uint8_t  m_field38;         // +0x38  default 0
    const PSPEmitterTemplate* m_pTemplate;       // +0x3C
    PSPParticleEmitter*       m_pNext;           // +0x40  intrusive list
    PSPParticleEmitter**      m_pRefPtr;         // +0x44  caller back-pointer
    bool     m_bUpdateWhenPaused;                // +0x48
    bool     m_bActive;

    PSPParticleEmitter()
        : m_Timer(0), m_ParticleHead(1)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_TimeScale(1.0f), m_field24(1.0f)
        , m_ScaleX(1.0f), m_ScaleY(1.0f)
        , m_field30(0.0f), m_field34(1.0f), m_field38(0)
        , m_pTemplate(nullptr), m_pNext(nullptr), m_pRefPtr(nullptr)
        , m_bUpdateWhenPaused(false), m_bActive(false)
    {}
};

// ----------------------------------------------------------------------------
// Singleton manager
// ----------------------------------------------------------------------------
class PSPParticleManager : public Singleton<PSPParticleManager> {
    friend class Singleton<PSPParticleManager>;

public:
    // Add emitter by template hash. Matches AddEmitter (0x1149e0).
    // ppRef (optional) is filled with the returned pointer for caller cleanup;
    // it is cleared to nullptr if template lookup fails. `persistent` is
    // accepted for signature compatibility but currently unused (matches bin).
    PSPParticleEmitter* AddEmitter(uint32_t hash,
                                   PSPParticleEmitter** ppRef = nullptr,
                                   bool persistent = false);

    // Explicitly release an emitter (matches ClearEmitter 0x114934). Clears
    // the caller back-pointer and marks the emitter for removal on next tick.
    void ClearEmitter(PSPParticleEmitter* emitter);

    void Update(float dt);

    // Draw particles whose template `m_UseDepth` equals `layer`. Matches the
    // binary filter `(float)layer == template->m_UseDepth`. Typical usage:
    //   Draw(0)  — mid/default layer (before HUD overlays)
    //   Draw(1)  — foreground layer   (after HUD overlays)
    void Draw(int layer = 0);

    // Load particle templates from an XML file. Matches PSPParticleManager::LoadFile
    // (0x115f60). Parses `<emitter>` elements into m_EmitterTemplates backed by
    // m_TemplateData. `<particleTemplate>` elements are not present in the shipping
    // XML files, so the particle-template side of LoadFile is a no-op.
    void LoadFile(const char* path);

    void Clear();

    // @ 0x0016cf74 area — deactivate all live emitters (called from GameExit).
    void ClearEmitters();

    // Template lookup — used by AddEmitter and unit tests.
    const PSPEmitterTemplate* FindTemplate(uint32_t hash) const;

private:
    PSPParticleManager();
    ~PSPParticleManager();

    std::vector<PSPParticleTemplate> m_ParticleTemplates;
    std::vector<PSPEmitterTemplate>  m_EmitterTemplates;

    // unique_ptr keeps PSPParticleEmitter addresses stable across vector
    // growth so callers can hold raw pointers (matches MemoryPool semantics
    // of the binary).
    std::vector<std::unique_ptr<PSPParticleEmitter>> m_Emitters;
};

} // namespace Mortar

#endif
