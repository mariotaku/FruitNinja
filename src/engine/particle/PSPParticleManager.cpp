#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include "math/Random.h"
#include "math/MathUtil.h"
#include "math/Colour.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include "xml/TiXml.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// File-scope hash->index map entry (local struct not allowed as template arg in GCC 4.4.1).
struct LoadFileHashIdx { uint32_t hash; int idx; };

// Parse "x y z" into three floats via sscanf.
static bool ParseVec3(const char* s, float out[3]) {
    if (!s) return false;
    return sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

// Parse "r g b a" ints (0..31) into BGRA bytes, scaling by 255/31.
// Matches binary's 255.0f/31.0f multiplier at DAT_001166c4.
//
// Byte order is load-bearing: v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554
// copies these template bytes into the particle LANE BY LANE with no swizzle, and
// Draw @0x0013eccc then feeds lane 2 to red, lane 1 to green, lane 0 to blue and
// lane 3 to alpha. So lane 0 must hold blue -- same order as struct Colour.
static void ParseColourBGRA(const char* s, uint8_t out[4]) {
    if (!s) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    int r = 0, g = 0, b = 0, a = 0;
    sscanf(s, "%d %d %d %d", &r, &g, &b, &a);
    const float scale = 255.0f / 31.0f;
    out[0] = (uint8_t)(b * scale); // B
    out[1] = (uint8_t)(g * scale); // G
    out[2] = (uint8_t)(r * scale); // R
    out[3] = (uint8_t)(a * scale); // A
}

// GL blend enum from string.
static uint16_t ParseBlendEnum(const char* s) {
    if (!s) return 0;
    if (!strcmp(s, "SourceAlpha") || !strcmp(s, "SrcAlpha")) return 0x302;
    if (!strcmp(s, "InverseSourceAlpha") || !strcmp(s, "InvSrcAlpha")) return 0x303;
    if (!strcmp(s, "One")) return 0x01;
    return 0;
}

// v1.6.1 PSPParticleManager::PSPParticleManager @0x0013bf40 — manager ctor.
// Sets m_GlobalPullRadius=0.0 (+0x00), m_GlobalPullStrength=1.0 (+0x04); NULLs all owned pointers.
// ASM-spec v1.6.1 PSPParticleManager @0x00013bf40 (non-polymorphic; +0x00 = float
//   m_GlobalPullRadius, not a vptr): the binary ctor writes this->__vptr = 0, i.e. it zeroes
//   +0x00 as a data field (the vortex pull radius), not a vtable pointer.
PSPParticleManager::PSPParticleManager()
    : m_GlobalPullRadius(0.0f)
    , m_GlobalPullStrength(1.0f)
    , m_GlobalOrigin(0.0f, 0.0f, 0.0f)
    , m_pParticles(0)
    , m_FreeHead(0)
    , _pad1a(0)
    , m_DrawnParticleCount(0)
    , m_pActiveEmitters(0)
    , m_NumParticleTemplates(0)
    , m_pTemplates(0)
    , m_NumEmitterTemplates(0)
    , m_pEmitterTemplates(0)
    , m_pEmitterPool(0)
    , m_pTextureRefs(0)
    , m_NumTextureRefs(0)
{
}

PSPParticleManager::~PSPParticleManager() {
    Destroy();
}

// v1.6.1 PSPParticleManager::Destroy @0x0013cfb8 — release tex refs, ClearEmitters,
// free owned blocks.
void PSPParticleManager::Destroy() {
    // 1. Release texture SmartPtr refs.
    if (m_pTextureRefs) {
        for (int i = 0; i < m_NumTextureRefs; ++i)
            m_pTextureRefs[i].SetNull();
        delete[] m_pTextureRefs;
        m_pTextureRefs = 0;
        m_NumTextureRefs = 0;
    }
    // 2. Drain active emitters.
    ClearEmitters();
    // 3. Free the 1024-slot particle buffer.
    // DIFFERS: binary alloc = operator new[](0x29008) with 8-byte cookie prefix;
    // port uses new PSPParticle[1024] + delete[]. v1.6.1 PSPParticleManager::Destroy @0x0013cfb8.
    if (m_pParticles) {
        delete[] m_pParticles;
        m_pParticles = 0;
    }
    // 4. Free the template blob (single allocation covering both particle and emitter templates).
    if (m_pTemplates) {
        delete[] m_pTemplates;
        m_pTemplates = 0;
        m_pEmitterTemplates = 0;
    }
    m_NumParticleTemplates = 0;
    m_NumEmitterTemplates = 0;
    // 5. Delete emitter MemoryPool.
    if (m_pEmitterPool) {
        delete m_pEmitterPool;
        m_pEmitterPool = 0;
    }
}

// v1.6.1 PSPParticleManager::GetEmitterTemplate @0x0013c044 — variable-stride walk.
// Returns pointer to emitter blob record at index idx, or null if out of range.
uint8_t* PSPParticleManager::GetEmitterTemplate(int idx) {
    if (!m_pEmitterTemplates || idx < 0 || idx >= m_NumEmitterTemplates) return 0;
    uint8_t* p = m_pEmitterTemplates;
    for (int i = 0; ; ++i) {
        if (i >= m_NumEmitterTemplates) return 0;
        if (i == idx) return p;
        PSPEmitterBlob* hdr = reinterpret_cast<PSPEmitterBlob*>(p);
        p += 0x4C + hdr->m_NumSets * 0x30;
    }
}

// FindTemplate: linear hash scan over emitter blob (variable stride).
const uint8_t* PSPParticleManager::FindTemplate(uint32_t hash) const {
    if (!m_pEmitterTemplates) return 0;
    uint8_t* p = m_pEmitterTemplates;
    for (int i = 0; i < m_NumEmitterTemplates; ++i) {
        PSPEmitterBlob* hdr = reinterpret_cast<PSPEmitterBlob*>(p);
        if (hdr->m_Hash == hash) return p;
        p += 0x4C + hdr->m_NumSets * 0x30;
    }
    return 0;
}

// TODO: PSPParticleManager::EmitterExists -- address unresolved (0x001148dc's PLT thunk
// resolves to the unrelated Mortar::InitPlacementArrayCopy<_Vector3<float>>, not this
// function's body).
bool PSPParticleManager::EmitterExists(uint32_t hash) {
    return FindTemplate(hash) != 0;
}

// v1.6.1 PSPParticleManager::AddEmitter @0x0013c1b8
PSPParticleEmitter* PSPParticleManager::AddEmitter(uint32_t hash,
                                                   PSPParticleEmitter** ppRef,
                                                   bool updateWhenPaused) {
    if (!m_pEmitterPool) return 0;
    if (m_pEmitterPool->InUseCount() + 1 >= 120) {
        return 0;
    }

    const uint8_t* tmplBlob = FindTemplate(hash);
    if (!tmplBlob) {
        if (ppRef) *ppRef = 0;
        return 0;
    }

    PSPParticleEmitter* e = m_pEmitterPool->Pop();
    if (!e) return 0;

    e->m_Timer = 0.0f;
    e->m_bStarted = 1;
    e->m_Pos = _Vector3<float>(0, 0, 0);
    e->m_Vel = _Vector3<float>(0, 0, 0);
    e->m_RateScale = 1.0f;
    e->m_LifeBias = 1.0f;
    e->m_SizeScale = 1.0f;
    e->m_TimeScale = 1.0f;
    e->m_DirCos = 1.0f;
    e->m_DirSin = 0.0f;
    e->m_VelScale = 1.0f;
    e->m_bMirrorX = 0;
    e->m_bUpdateWhenPaused = updateWhenPaused ? 1 : 0;
    e->m_bTrailStarted = 0;
    e->m_pTemplate = tmplBlob;
    e->m_pRefPtr = ppRef;

    e->m_Next = m_pActiveEmitters;
    m_pActiveEmitters = e;

    if (ppRef) *ppRef = e;
    return e;
}

// v1.6.1 PSPParticleManager::ClearEmitter @0x0013c088
void PSPParticleManager::ClearEmitter(PSPParticleEmitter* emitter) {
    if (!emitter) return;
    PSPParticleEmitter** cur = &m_pActiveEmitters;
    while (*cur) {
        if (*cur == emitter) {
            *cur = emitter->m_Next;
            if (emitter->m_pRefPtr) *emitter->m_pRefPtr = 0;
            if (m_pEmitterPool) m_pEmitterPool->Push(emitter);
            return;
        }
        cur = &(*cur)->m_Next;
    }
}

// -----------------------------------------------------------------------------
// Spawn / draw primitives shared by AddParticle and Draw
// -----------------------------------------------------------------------------

// v1.6.1 T.971 @0x0013c514 -- Rand32(&Math::g_random, 524287) / 524287.0f, i.e. a
// uniform float in [0,1). Every random value AddParticle bakes comes from here, off
// the single shared gameplay stream, so the draw COUNT and ORDER are observable.
static float T_971() {
    return Math::g_Random.RandF(1.0f);
}

// v1.6.1 floatLERP @0x0013bed8 -- s0 + (s1 - s0) * s2.
static inline float floatLERP(float a, float b, float t) {
    return a + (b - a) * t;
}

// v1.6.1 LERP @0x0013bec0 -- integer lerp with a 12-bit fraction (frac12 in [0,4095]).
static int LERP(int a, int b, int frac12) {
    return (a * 0x1000 + ((frac12 * (b * 0x1000 - a * 0x1000)) >> 12)) >> 12;
}

// ARM VCVT.U32.F32: negative inputs clamp to 0, positives truncate toward zero.
// The binary NEVER clamps the high end, so colour/size channels above 255 wrap.
static inline uint32_t VcvtU32(float v) {
    return v > 0.0f ? (uint32_t)(int32_t)v : 0u;
}

// Quadrant-mirror sign: v > 0 -> -1, v < 0 -> +1, v == 0 -> 0.
// (vcmpe.f32 + vmovgt/vmovmi/vldrpl in AddParticle @0x0013c704.)
static inline float QuadrantMirror(float v) {
    if (v > 0.0f) return -1.0f;
    if (v < 0.0f) return 1.0f;
    return 0.0f;
}

// v1.6.1 PSPParticleEmitter::AddParticle @0x0013c554
//
// Draw sequence, in order (do not reorder -- g_Random is the shared stream):
//   1  T_971  gravity/acceleration lerp t  (ONE t shared by all three components)
//   3  T_971  set velocity X, Y, Z         (independent t each, in that order)
//   1  T_971  spawn angle
//   3  libc rand() & 0xFFF                 (size start / mid / end, through LERP)
//  12  T_971  colour: byte lanes 0,8,16,24; per lane start, mid, end
//   6  T_971  tail: spin pair, cycleA pair, cycleB pair, wobble rate+accel,
//                   wobble amp pair, wobble phase base   (11 template lerps)
//             with a conditional Rand32(0) after the cycleA and cycleB draws
// = 23 x T_971 + 3 x libc rand() + 0/1/2 x Rand32(0).
//
// The three rand() calls really are libc rand(), not g_Random -- keep them libc.
void PSPParticleEmitter::AddParticle(PSPParticleSet* set, PSPParticleManager& mgr) {
    const uint16_t idx = mgr.m_FreeHead;
    if (idx == 0) return;

    // Port specific: the binary stores a resolved PSPParticleTemplate* at set+0x00 and
    // dereferences it unconditionally. The port stores a byte offset into the template
    // blob plus a 0xFFFFFFFF "unresolved name" sentinel that the binary has no analogue
    // for, so it has to be screened out here.
    if (set->m_TemplateOffset == 0xFFFFFFFFu || !mgr.m_pTemplates || !mgr.m_pParticles) return;
    PSPParticleTemplate* tmpl =
        reinterpret_cast<PSPParticleTemplate*>(mgr.m_pTemplates + set->m_TemplateOffset);

    PSPParticle& p = mgr.m_pParticles[idx];
    const float life = tmpl->m_Life;

    p.m_TimeRemaining = life;
    ++mgr.m_DrawnParticleCount;   // transient: Draw zeroes this every frame
    mgr.m_FreeHead = p.m_NextLink;
    p.m_NextLink = tmpl->m_LiveHead;
    tmpl->m_LiveHead = idx;
    p.m_DeathThreshold = life - life * m_LifeBias;

    p.m_Pos = (tmpl->m_CoordSystem == 0) ? m_Pos : _Vector3<float>::Zero();
    p.m_pOwner = this;

    // --- acceleration: lerp(gravityMin, gravityMax, t), ONE t for all components ---
    {
        const _Vector3<float> gMin(tmpl->m_GravityMin[0], tmpl->m_GravityMin[1], tmpl->m_GravityMin[2]);
        const _Vector3<float> gMax(tmpl->m_GravityMax[0], tmpl->m_GravityMax[1], tmpl->m_GravityMax[2]);
        const _Vector3<float> gDelta = gMax - gMin;
        const float t = T_971();
        p.m_Accel = gMin + gDelta * t;
    }

    // --- set velocity: independent t per component, drawn X then Y then Z ---
    const float tvx = T_971();
    const float tvy = T_971();
    const float tvz = T_971();
    float vx = set->m_VelocityMin[0] + (set->m_VelocityMax[0] - set->m_VelocityMin[0]) * tvx;
    float vy = set->m_VelocityMin[1] + (set->m_VelocityMax[1] - set->m_VelocityMin[1]) * tvy;
    float vz = set->m_VelocityMin[2] + (set->m_VelocityMax[2] - set->m_VelocityMin[2]) * tvz;

    // Emitter velocity scale is applied BEFORE the direction rotation.
    vx *= m_VelScale;
    vy *= m_VelScale;
    vz *= m_VelScale;

    // 2D rotation by the emitter's (m_DirCos, m_DirSin) pair.
    {
        const float rx = vy * m_DirSin + vx * m_DirCos;
        vy = vy * m_DirCos - m_DirSin * vx;
        vx = rx;
    }

    if (m_bMirrorX != 0) {
        // Acceleration mirrors around the PARTICLE's x, velocity around the EMITTER's.
        const float g = p.m_Accel.x;
        p.m_Accel.x = p.m_Accel.y;
        p.m_Accel.y = g;
        p.m_Accel.x *= QuadrantMirror(p.m_Pos.x);
        p.m_Accel *= m_VelScale;

        const float s = vx;
        vx = vy;
        vy = s;
        vx *= QuadrantMirror(m_Pos.x);
        vx *= m_VelScale;
        vy *= m_VelScale;
        vz *= m_VelScale;
    }

    // Stored RAW -- the emitter's own m_Vel is NOT added.
    p.m_Vel = _Vector3<float>(vx / 2.0f, vy / 2.0f, vz / 2.0f);

    // --- spawn angle: int32 template range lerped, then scaled into the index domain ---
    {
        const float t = T_971();
        const float angle = floatLERP((float)tmpl->m_AngleMin, (float)tmpl->m_AngleMax, t);
        p.m_MirrorX   = m_bMirrorX;
        p.m_NoAttract = m_bTrailStarted;
        int a = (int)(angle * 182.0f);
        p.m_RotAngleIdx = (uint16_t)a;
        if (m_bMirrorX != 0) {
            a += (p.m_Pos.x > 0.0f) ? 0xC000 : 0x4000;
            p.m_RotAngleIdx = (uint16_t)a;
        }
    }

    // Shape 1 ("Vortex"): rewind a whole template-life of travel so the particle
    // sweeps INTO the emitter instead of away from it.
    if (tmpl->m_Shape == 1) {
        p.m_Pos -= p.m_Vel * life;
    }

    // --- size curve: three libc rand() draws through the 12-bit integer LERP ---
    {
        const int rs = LERP(tmpl->m_SizeStartMin, tmpl->m_SizeStartMax, rand() & 0xFFF);
        const float sizeStart = (float)rs;
        const int rm = LERP(tmpl->m_SizeMidMin, tmpl->m_SizeMidMax, rand() & 0xFFF);
        const float sizeMid = (float)rm;
        const int re = LERP(tmpl->m_SizeEndMin, tmpl->m_SizeEndMax, rand() & 0xFFF);
        const float scale = m_SizeScale;

        p.m_SizeStart    = (uint16_t)VcvtU32(sizeStart * scale);
        const float sizeEnd = (float)re;
        p.m_SizeMidDelta = (int16_t)(int)((sizeMid - sizeStart) * scale);
        p.m_SizeEndDelta = (int16_t)(int)((sizeEnd - sizeMid) * scale);
    }

    // --- colour curve: one pass per BYTE LANE, three draws each ---
    // The binary loads each 4-byte template colour as a word and masks/shifts lane
    // `sh` = 0, 8, 16, 24 out of it; indexing the byte array is the same value. No
    // swizzle happens here, so the particle inherits the template's byte order.
    for (int lane = 0; lane < 4; ++lane) {
        const int c94 = tmpl->m_ColourStartMax[lane];
        const int c98 = tmpl->m_ColourStartMin[lane];
        const float tStart = T_971();
        const int startVal = (int)((float)c94 + (float)(c98 - c94) * tStart);

        const int c9c = tmpl->m_ColourMidMin[lane];
        const int ca0 = tmpl->m_ColourMidMax[lane];
        const float tMid = T_971();

        const int ca4 = tmpl->m_ColourEndMin[lane];
        const int ca8 = tmpl->m_ColourEndMax[lane];
        const float tEnd = T_971();

        p.m_ColourStart[lane] = (uint8_t)startVal;

        const int midVal = (int)(uint16_t)(int)((float)c9c + (float)(ca0 - c9c) * tMid);
        p.m_ColourMidDelta[lane] = (int16_t)(midVal - startVal);
        const int endVal = (int)((float)ca4 + (float)(ca8 - ca4) * tEnd);
        p.m_ColourEndDelta[lane] = (int16_t)(endVal - midVal);
    }

    // Shape 2 ("Direction"): face the velocity. Note the argument order -- the binary
    // passes (vel.x, vel.y) into Atan2Idx(y, x), so x and y really are swapped here.
    if (tmpl->m_Shape == 2) {
        p.m_RotAngleIdx = (uint16_t)(Math::Atan2Idx(p.m_Vel.x, p.m_Vel.y) + (int)p.m_RotAngleIdx);
    }

    // --- tail: 6 draws feeding 11 template lerps ---
    {
        const float t = T_971();
        p.m_SpinPair[0] = floatLERP((float)tmpl->m_SpinStartMin, (float)tmpl->m_SpinStartMax, t);
        p.m_SpinPair[1] = floatLERP((float)tmpl->m_SpinEndMin,   (float)tmpl->m_SpinEndMax,   t);
    }
    {
        const float t = T_971();
        p.m_CycleA[0] = floatLERP((float)tmpl->m_CycleXStartMin, (float)tmpl->m_CycleXStartMax, t);
        p.m_CycleA[1] = floatLERP((float)tmpl->m_CycleXEndMin,   (float)tmpl->m_CycleXEndMax,   t);
        if (p.m_CycleA[1] == 0.0f && p.m_CycleA[0] == 0.0f) {
            p.m_ScaleXIdx = 0;
        } else {
            // Rand32 only range-reduces for max in [2, 0xFFFFFFFE]; Rand32(0) hands
            // back the raw state high word, i.e. a full-range 16-bit start phase.
            p.m_ScaleXIdx = (uint16_t)Math::g_Random.Rand32(0);
        }
    }
    {
        const float t = T_971();
        p.m_CycleB[0] = floatLERP((float)tmpl->m_CycleYStartMin, (float)tmpl->m_CycleYStartMax, t);
        p.m_CycleB[1] = floatLERP((float)tmpl->m_CycleYEndMin,   (float)tmpl->m_CycleYEndMax,   t);
        if (p.m_CycleB[1] == 0.0f && p.m_CycleB[0] == 0.0f) {
            p.m_ScaleYIdx = 0;
        } else {
            p.m_ScaleYIdx = (uint16_t)Math::g_Random.Rand32(0);
        }
    }
    {
        const float t = T_971();
        const float rate = floatLERP(tmpl->m_WobbleRateStartMin, tmpl->m_WobbleRateStartMax, t);
        p.m_WobbleRate = rate;
        p.m_WobbleAccel =
            (floatLERP(tmpl->m_WobbleRateEndMin, tmpl->m_WobbleRateEndMax, t) - rate) / life;
    }
    {
        const float t = T_971();
        p.m_WobbleAmp[0] = floatLERP(tmpl->m_WobbleAmpStartMin, tmpl->m_WobbleAmpStartMax, t);
        p.m_WobbleAmp[1] = floatLERP(tmpl->m_WobbleAmpEndMin,   tmpl->m_WobbleAmpEndMax,   t);
    }
    {
        const float t = T_971();
        p.m_WobblePhaseBase = floatLERP(tmpl->m_WobblePhaseMin, tmpl->m_WobblePhaseMax, t);
    }

    // --- initial quad basis ---
    if (p.m_RotAngleIdx == 0) {
        // NOT an optimisation of the general case: the binary hardcodes these two.
        p.m_Basis2Sin = -1.0f;
        p.m_Basis2Cos = 1.0f;
        p.m_BasisX = _Vector2<float>(1.0f, 0.0f);
        p.m_BasisY = _Vector2<float>(0.0f, 1.0f);
    } else {
        const uint16_t a = p.m_RotAngleIdx;
        p.m_BasisX = _Vector2<float>(Math::SinIdx((uint16_t)(a + 0x4000)),
                                     Math::CosIdx((uint16_t)(a + 0x4000)));
        p.m_BasisY = _Vector2<float>(Math::SinIdx(a), Math::CosIdx(a));
        // Replicated literally: the modulus is 0xFFF0, not 0x10000, which leaves a
        // 16-index (~0.09 degree) slip in the original for small `a`. Do not "fix".
        const uint16_t rem = (uint16_t)(((int)a + 0xDFF2) % 0xFFF0);
        p.m_Basis2Sin = Math::SinIdx(rem) * 1.41f;
        p.m_Basis2Cos = Math::CosIdx(rem) * 1.41f;
    }
}

// v1.6.1 PSPEmitterTemplate::Ends @0x0013bee4
bool PSPParticleManager::EmitterEnds(const uint8_t* eBlob) {
    if (!eBlob) return true;
    const PSPEmitterBlob* hdr = reinterpret_cast<const PSPEmitterBlob*>(eBlob);
    for (int si = 0; si < (int)hdr->m_NumSets; ++si) {
        const PSPParticleSet* set = PSPParticleManager::EmitterSet(
            const_cast<uint8_t*>(eBlob), si);
        if (set->m_TimeStop <= 0.0f && set->m_PerSec > 0.0f) return false;
    }
    return true;
}

// UpdateEmitter — spawn pass + advance timer. Mirrors PSPParticleEmitter::Update @0x115d9c.
static void UpdateEmitter(PSPParticleEmitter& e, float dt, PSPParticleManager& mgr) {
    const uint8_t* eBlob = e.m_pTemplate;
    if (!eBlob) return;

    const PSPEmitterBlob* hdr = reinterpret_cast<const PSPEmitterBlob*>(eBlob);
    const float currentTime = e.m_Timer;
    const float dtScaled = dt * e.m_TimeScale;
    const float newTime = currentTime + dtScaled * e.m_RateScale;

    for (int si = 0; si < (int)hdr->m_NumSets; ++si) {
        PSPParticleSet* set = PSPParticleManager::EmitterSet(
            const_cast<uint8_t*>(eBlob), si);

        const float startT = set->m_TimeStart;
        const float stopT  = set->m_TimeStop;

        if (startT <= currentTime && (stopT == 0.0f || currentTime <= stopT)) {
            const float rate = set->m_PerSec;
            if (rate > 0.0f) {
                int desired = (int)(rate * ((currentTime + dtScaled * e.m_RateScale) - startT))
                            - (int)(rate * (currentTime - startT));
                for (int i = 0; i < desired; ++i)
                    e.AddParticle(set, mgr);
            }
        }

        if (currentTime <= startT && startT < newTime) {
            for (int i = 0; i < (int)set->m_InitCount; ++i)
                e.AddParticle(set, mgr);
            if (e.m_RateScale == 0.0f) e.m_Timer += dt;
        }
    }

    e.m_Timer = newTime;
    e.m_Pos += e.m_Vel;
}

// v1.6.1 PSPParticleManager::Update @0x0013cee8
// ASM-spec v1.6.1 PSPParticleManager::Update @0x0013cee8: callers pass
// paused = (game_work.bM_Mode != 0); the per-emitter gate below is
// m_bStarted && m_RateScale != 0 && (!paused || m_bUpdateWhenPaused).
void PSPParticleManager::Update(float dt, bool paused) {
    if (!m_pParticles || !m_pEmitterPool) return;

    PSPParticleEmitter** cur = &m_pActiveEmitters;
    while (*cur) {
        PSPParticleEmitter* node = *cur;
        const uint8_t* eBlob = node->m_pTemplate;

        if (node->m_bStarted != 0 && node->m_RateScale != 0.0f &&
            (!paused || node->m_bUpdateWhenPaused)) {
            UpdateEmitter(*node, dt, *this);
        }

        bool keep = true;
        if (eBlob) {
            const PSPEmitterBlob* hdr = reinterpret_cast<const PSPEmitterBlob*>(eBlob);
            const bool naturallyInfinite = !PSPParticleManager::EmitterEnds(eBlob);
            if (hdr->m_MaxLifetime > 0.0f) {
                keep = (node->m_Timer < hdr->m_MaxLifetime);
            } else {
                keep = naturallyInfinite;
            }
        }

        if (!keep) {
            *cur = node->m_Next;
            if (node->m_pRefPtr) *node->m_pRefPtr = 0;
            m_pEmitterPool->Push(node);
        } else {
            cur = &node->m_Next;
        }
    }
}

// -----------------------------------------------------------------------------
// Draw
// -----------------------------------------------------------------------------

Mortar::Texture* PSPParticleManager::GetTemplateTexture(const PSPParticleTemplate* tmpl) const {
    const uint32_t tidx = tmpl->m_TextureIdx;
    if (tidx == 0xFFFFFFFFu || !m_pTextureRefs || (int)tidx >= m_NumTextureRefs) return 0;
    return m_pTextureRefs[tidx].Get();
}

// v1.6.1 PSPParticleManager::Draw @0x0013eccc — fused integrate+render.
//
// Structure: reset the world matrix stack, upload modelview, zero
// m_DrawnParticleCount, then for each PARTICLE template (count is mgr+0x24 =
// m_NumParticleTemplates): skip it whole if its live list is empty or its layer
// (template+0xB4) does not match, otherwise walk the live list taking a 0xA4
// stack copy of each particle, reap the expired ones, emit 6 vertices per
// survivor into one function-static buffer, integrate, and finish with a single
// Mesh::DrawTriList for the template.
//
// Callers pass paused = (game_work.bM_Mode != 0). Every ScreenEffect emitter is
// created via ScreenEffect::Activate @0x00148f08 -> AddEmitter(hash, NULL, false),
// i.e. m_bUpdateWhenPaused = 0, so the frenzy overlay freezes while paused.
void PSPParticleManager::Draw(float dt, bool paused, int layer) {
    // The binary's vertex staging buffer is a function-static array with NO bounds
    // check. Sized here for the full 1024-slot particle buffer at 6 verts each plus
    // the deliberate one-vertex hole every template leaves behind.
    static QUADCUSTOMVERTEX vt[1024 * 6 + 256];

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    m_DrawnParticleCount = 0;

    int tmplOff    = 0;   // byte cursor into m_pTemplates (stride 0xB8)
    int vcount     = 0;   // write cursor into vt
    int batchStart = 0;   // first vt slot belonging to the current template

    for (int ti = 0; ti < m_NumParticleTemplates; ++ti, tmplOff += 0xB8) {
        PSPParticleTemplate* tmpl =
            reinterpret_cast<PSPParticleTemplate*>(m_pTemplates + tmplOff);

        uint32_t cur = tmpl->m_LiveHead;
        int batchEnd = batchStart;

        // ONE layer test for the whole template, not one per particle.
        if (cur != 0 && tmpl->m_UseDepth == layer) {
            uint32_t prev = 0;
            do {
                PSPParticle* p = &m_pParticles[cur];
                // Full 0xA4 copy: every curve below is evaluated against the
                // pre-integration state, so the copy is load-bearing, not a cache.
                const PSPParticle c = *p;

                if (!(c.m_TimeRemaining > c.m_DeathThreshold)) {
                    // Expired: unlink by index and push the slot onto the free list.
                    // `prev` deliberately does NOT advance.
                    if (prev == 0) tmpl->m_LiveHead = p->m_NextLink;
                    else           m_pParticles[prev].m_NextLink = p->m_NextLink;
                    const uint16_t oldFree = m_FreeHead;
                    m_FreeHead = (uint16_t)cur;
                    m_pParticles[cur].m_NextLink = oldFree;
                    cur = c.m_NextLink;
                    continue;
                }

                // Normalised age off the TEMPLATE's life, not a per-particle one.
                const float tl = tmpl->m_Life;
                const float t  = (tl - c.m_TimeRemaining) / tl;

                // Colour + size: two straight segments split at t = 0.5, each half
                // remapped to [0,1]. Saturation is VCVT.U32.F32 then `& 0xFF`, so
                // negatives clamp to 0 but anything over 255 WRAPS -- no hi clamp.
                uint32_t ch0, ch1, ch2, ch3;
                float size;
                if (t < 0.5f) {
                    const float u = t + t;
                    ch0 = VcvtU32((float)c.m_ColourStart[0] + (float)c.m_ColourMidDelta[0] * u);
                    ch1 = VcvtU32((float)c.m_ColourStart[1] + (float)c.m_ColourMidDelta[1] * u);
                    ch2 = VcvtU32((float)c.m_ColourStart[2] + (float)c.m_ColourMidDelta[2] * u);
                    ch3 = VcvtU32((float)c.m_ColourStart[3] + (float)c.m_ColourMidDelta[3] * u);
                    size = (float)c.m_SizeStart + (float)c.m_SizeMidDelta * u;
                } else {
                    const float u = (t - 0.5f) + (t - 0.5f);
                    ch0 = VcvtU32((float)(c.m_ColourMidDelta[0] + c.m_ColourStart[0])
                                  + (float)c.m_ColourEndDelta[0] * u);
                    ch1 = VcvtU32((float)(c.m_ColourMidDelta[1] + c.m_ColourStart[1])
                                  + (float)c.m_ColourEndDelta[1] * u);
                    ch2 = VcvtU32((float)(c.m_ColourMidDelta[2] + c.m_ColourStart[2])
                                  + (float)c.m_ColourEndDelta[2] * u);
                    ch3 = VcvtU32((float)(c.m_ColourMidDelta[3] + c.m_ColourStart[3])
                                  + (float)c.m_ColourEndDelta[3] * u);
                    size = (float)c.m_SizeStart + (float)c.m_SizeMidDelta
                         + (float)c.m_SizeEndDelta * u;
                }
                ch0 &= 0xFF; ch1 &= 0xFF; ch2 &= 0xFF; ch3 &= 0xFF;

                float sizeX = size * tmpl->m_AspectRatio;

                const bool advance =
                    (!paused) || (c.m_pOwner != 0 && c.m_pOwner->m_bUpdateWhenPaused != 0);

                if (advance) {
                    // Rotation + wobble. The wobble phase is QUADRATIC in age.
                    const float spin = floatLERP(c.m_SpinPair[0], c.m_SpinPair[1], t);
                    const float age  = tl - c.m_TimeRemaining;
                    const float phase =
                        c.m_WobblePhaseBase + (c.m_WobbleRate + c.m_WobbleAccel * 0.5f * age) * age;

                    uint32_t wobble = 0;
                    if (phase != 0.0f) {
                        const float s = Math::SinIdx((uint16_t)((int)(phase * 65536.0f) & 0xFFFF));
                        const float amp = floatLERP(c.m_WobbleAmp[0], c.m_WobbleAmp[1], t);
                        wobble = (uint32_t)(int)(s * amp * 182.0f) & 0xFFFF;
                    }

                    // The basis is only rebuilt when the quad actually turns.
                    if (wobble != 0 || spin != 0.0f) {
                        const uint32_t rot =
                            (uint32_t)(int)(spin * 360.0f * dt * 182.0f) + p->m_RotAngleIdx;
                        const uint32_t a16 = rot & 0xFFFF;
                        p->m_RotAngleIdx = (uint16_t)rot;
                        if (a16 == 0) {
                            p->m_Basis2Sin = -1.0f;
                            p->m_Basis2Cos = 1.0f;
                            p->m_BasisX = _Vector2<float>(1.0f, 0.0f);
                            p->m_BasisY = _Vector2<float>(0.0f, 1.0f);
                        } else {
                            const uint16_t a = (uint16_t)((wobble + a16) & 0xFFFF);
                            p->m_BasisX = _Vector2<float>(Math::SinIdx((uint16_t)(a + 0x4000)),
                                                          Math::CosIdx((uint16_t)(a + 0x4000)));
                            p->m_BasisY = _Vector2<float>(Math::SinIdx(a), Math::CosIdx(a));
                            // Replicated literally: the modulus is 0xFFF0, not 0x10000.
                            const uint16_t rem = (uint16_t)(((int)a + 0xDFF2) % 0xFFF0);
                            p->m_Basis2Sin = Math::SinIdx(rem) * 1.41f;
                            p->m_Basis2Cos = Math::CosIdx(rem) * 1.41f;
                        }
                    }

                    // Scale cycles: 16-bit phase indices advanced by rate*182*360*dt,
                    // then CosIdx of the phase multiplies the quad extent.
                    const float rateX = floatLERP(c.m_CycleA[0], c.m_CycleA[1], t);
                    if (rateX != 0.0f) {
                        p->m_ScaleXIdx = (uint16_t)VcvtU32(
                            (float)p->m_ScaleXIdx + rateX * 182.0f * 360.0f * dt);
                    }
                    if (p->m_ScaleXIdx != 0) sizeX = sizeX * Math::CosIdx(p->m_ScaleXIdx);

                    const float rateY = floatLERP(c.m_CycleB[0], c.m_CycleB[1], t);
                    if (rateY != 0.0f) {
                        p->m_ScaleYIdx = (uint16_t)VcvtU32(
                            (float)p->m_ScaleYIdx + rateY * 182.0f * 360.0f * dt);
                    }
                    if (p->m_ScaleYIdx != 0) size = size * Math::CosIdx(p->m_ScaleYIdx);
                }

                // Channel mapping: lane 2 -> red, lane 1 -> green, lane 0 -> blue,
                // lane 3 -> alpha, i.e. the baked bytes are already [B,G,R,A].
                const Colour col((uint8_t)ch2, (uint8_t)ch1, (uint8_t)ch0, (uint8_t)ch3);

                const _Vector2<float> ax = p->m_BasisX * sizeX;   // quad width axis
                const _Vector2<float> ay = p->m_BasisY * size;    // quad height axis

                float px = c.m_Pos.x;
                float py = c.m_Pos.y;
                float pz = c.m_Pos.z;
                // Global-space templates are stored relative to their emitter.
                if (tmpl->m_CoordSystem == 1) {
                    px += c.m_pOwner->m_Pos.x;
                    py += c.m_pOwner->m_Pos.y;
                    pz += c.m_pOwner->m_Pos.z;
                }

                // Grid lock. The +480 / +320 bias puts the snap grid in screen-corner
                // space rather than around the centred origin.
                const float gx = tmpl->m_GridLockStart;
                if (gx > 0.0f) px = (float)(int)((px + 480.0f) / gx + 0.5f) * gx - 480.0f;
                const float gy = tmpl->m_GridLockEnd;
                if (gy > 0.0f) py = (float)(int)((py + 320.0f) / gy + 0.5f) * gy - 320.0f;

                // Six verts, two triangles: V3 duplicates V2 and V4 duplicates V1.
                // Normals at +0x0C..+0x17 are never written (the static buffer keeps
                // whatever it was zero-initialised with), matching the binary.
                QUADCUSTOMVERTEX* v = &vt[vcount];
                v[0].x = px + ax.x + ay.x;      v[0].y = py + ax.y + ay.y;
                v[0].u = 1.0f;                  v[0].v = 0.0f;
                v[1].x = px + (ay.x - ax.x);    v[1].y = py + (ay.y - ax.y);
                v[1].u = 0.0f;                  v[1].v = 0.0f;
                v[2].x = px + (ax.x - ay.x);    v[2].y = py + (ax.y - ay.y);
                v[2].u = 1.0f;                  v[2].v = 1.0f;
                v[3] = v[2];
                v[4] = v[1];
                v[5].x = px + (-ax.x - ay.x);   v[5].y = py + (-ax.y - ay.y);
                v[5].u = 0.0f;                  v[5].v = 1.0f;
                for (int i = 0; i < 6; ++i) {
                    v[i].z = pz;
                    v[i].colour = col.PlatformColour();
                }
                vcount += 6;

                // Integration happens AFTER the vertices are emitted, so the frame
                // shows the pre-integration state.
                if (advance) {
                    const float ts = (c.m_pOwner == 0) ? 1.0f : c.m_pOwner->m_TimeScale;
                    const float dts = dt * ts;
                    p->m_TimeRemaining = c.m_TimeRemaining - dts;   // full dts, unhalved

                    // Big steps integrate at half rate. (The binary runs the velocity
                    // and position pass twice when it halves; both passes compute the
                    // same values, so the first one's stores are dead.)
                    const float h = (dts > 0.025f) ? dts * 0.5f : dts;

                    p->m_Vel.x = (c.m_Vel.x + h * c.m_Accel.x)
                               * floatLERP(tmpl->m_VelocityMin[0], tmpl->m_VelocityMax[0], t);
                    p->m_Vel.y = (c.m_Vel.y + h * c.m_Accel.y)
                               * floatLERP(tmpl->m_VelocityMin[1], tmpl->m_VelocityMax[1], t);
                    p->m_Vel.z = (c.m_Vel.z + h * c.m_Accel.z)
                               * floatLERP(tmpl->m_VelocityMin[2], tmpl->m_VelocityMax[2], t);

                    // Super-fruit explosion shockwave: pushes AWAY from m_GlobalOrigin.
                    if (m_GlobalPullRadius > 0.0f && tmpl->m_CoordSystem != 1) {
                        if (c.m_Pos != m_GlobalOrigin && c.m_NoAttract == 0) {
                            _Vector3<float> dir = c.m_Pos - m_GlobalOrigin;
                            const float len = dir.Normalise();
                            if (len < m_GlobalPullRadius) {
                                p->m_Vel += dir * (m_GlobalPullRadius - len) * h * 10.0f
                                          * m_GlobalPullStrength;
                            }
                        }
                    }

                    p->m_Pos.x = c.m_Pos.x + h * p->m_Vel.x;
                    p->m_Pos.y = c.m_Pos.y + h * p->m_Vel.y;
                    p->m_Pos.z = c.m_Pos.z + h * p->m_Vel.z;
                }

                prev = cur;
                cur = c.m_NextLink;
            } while (cur != 0);

            const int verts = vcount - batchStart;
            // Replicated literally: the binary burns one vertex slot per drawn
            // template, leaving a permanent hole between batches.
            ++vcount;
            batchEnd = vcount;

            if (verts != 0) {
                m_DrawnParticleCount += (vcount - batchStart) / 6;
                // Port specific: the binary keeps a SmartPtr<Texture> inline at
                // template+0xAC and calls its vtable slots +0x0C / +0x10 without a
                // null check; the port can legitimately have no texture when an asset
                // fails to load, so the bind/unbind pair is guarded while the draw
                // itself still happens.
                Mortar::Texture* tex = GetTemplateTexture(tmpl);
                if (tex) tex->Set();
                Mortar::Mesh::DrawTriList(vt + batchStart, verts, false, 0, 0);
                if (tex) tex->UnSet(true);
            }
        }

        batchStart = batchEnd;
    }
}

// v1.6.1 PSPParticleManager::LoadFile @0x0013d09c
//
// Parse flow (matches binary):
//   1. First-call init: alloc 1024-slot PSPParticle buffer + MemoryPool.
//   2. Scratch buffer: 0xa0a0 bytes.
//   3. Pass 1 <particleTemplate>: zero 0xB8, parse fields, record hash in local map, cursor+=0xB8.
//   4. Pass 2 <emitter>: zero 0x4C header, parse sets (zero 0x30 each), cursor+=0x30 per set.
//   5. Final: n = cursor-scratch; m_pTemplates = new[n+1]; memcpy; m_pEmitterTemplates = m_pTemplates+nPart*0xB8.
//   6. Patch: walk emitter blob variable-stride; per set: set+0x00 = blob-offset = storedIndex*0xB8.
//   7. delete[] scratch.
//
// BUG FIX: life divisor is /60.0 (was /78.0). v1.6.1 PSPParticleManager::LoadFile @0x0013d09c.
// BUG FIX: shape 1 string is "Vortex" (was "Vertex"). v1.6.1 PSPParticleManager::LoadFile @0x0013d09c.
bool PSPParticleManager::LoadFile(const char* texCategory, const char* xmlPath, char** outNames) {
    TiXmlDocument doc;
    if (!doc.LoadFile(xmlPath)) {
        return false;
    }
    TiXmlElement root = doc.FirstChildElement("particle_file");
    if (!root) return false;
    TiXmlElement body = root.FirstChildElement("body");
    if (!body) return false;

    // (a) Alloc 1024-slot particle buffer on first call.
    // DIFFERS: binary uses cookie-prefixed operator new[] block; port uses new PSPParticle[1024].
    // v1.6.1 PSPParticleManager::LoadFile @0x0013d09c.
    if (!m_pParticles) {
        m_pParticles = new PSPParticle[1024];
        // Thread free-list: slots 1..1023; sentinel at slot 0.
        for (int i = 1; i <= 1022; ++i) {
            m_pParticles[i].m_NextLink = (uint16_t)(i + 1);
        }
        m_pParticles[1023].m_NextLink = 0;
        m_FreeHead = 1;
    }

    // (b) Create emitter MemoryPool(120) on first call.
    if (!m_pEmitterPool) {
        m_pEmitterPool = new Mortar::MemoryPool<PSPParticleEmitter>();
        m_pEmitterPool->Create(120);
    }

    // Free old template blob and texture refs on reload.
    if (m_pTextureRefs) {
        for (int i = 0; i < m_NumTextureRefs; ++i)
            m_pTextureRefs[i].SetNull();
        delete[] m_pTextureRefs;
        m_pTextureRefs = 0;
        m_NumTextureRefs = 0;
    }
    if (m_pTemplates) {
        delete[] m_pTemplates;
        m_pTemplates = 0;
        m_pEmitterTemplates = 0;
    }
    m_NumParticleTemplates = 0;
    m_NumEmitterTemplates  = 0;

    const std::string texCatStr(texCategory ? texCategory : "");

    // (c) Scratch buffer: 0xa0a0 bytes (matches binary LoadFile @0x0013d09c).
    const int SCRATCH_SIZE = 0xa0a0;
    uint8_t* scratch = new uint8_t[SCRATCH_SIZE];
    uint8_t* cursor  = scratch;

    // Local index map: hash -> particle-template index (0-based).
    // Binary uses int[1024] on stack. Port uses a vector of file-scope LoadFileHashIdx
    // (local types cannot be used as std::vector template args in GCC 4.4.1).
    std::vector<LoadFileHashIdx> nameToIndex;

    // --- Pass 1: <particleTemplate> ----------------------------------------
    for (TiXmlElement pt = body.FirstChildElement("particleTemplate");
         pt;
         pt = pt.NextSiblingElement("particleTemplate")) {

        // Zero the 0xB8 record at cursor position.
        memset(cursor, 0, 0xB8);
        PSPParticleTemplate* tmpl = reinterpret_cast<PSPParticleTemplate*>(cursor);

        // Default damping = identity (1.0) so templates without <velocity> get no damping.
        tmpl->m_VelocityMin[0] = 1.0f; tmpl->m_VelocityMin[1] = 1.0f; tmpl->m_VelocityMin[2] = 1.0f;
        tmpl->m_VelocityMax[0] = 1.0f; tmpl->m_VelocityMax[1] = 1.0f; tmpl->m_VelocityMax[2] = 1.0f;
        // Texture index: none by default.
        tmpl->m_TextureIdx = 0xFFFFFFFFu;

        const char* name = pt.Attribute("name");
        uint32_t hash = name ? StringHash(name) : 0;
        if (name) {
            LoadFileHashIdx hi; hi.hash = hash; hi.idx = m_NumParticleTemplates;
            nameToIndex.push_back(hi);
        }

        { int _v = 0; pt.QueryIntAttribute("useDepth", &_v); tmpl->m_UseDepth = (int32_t)_v; }

        // <life> — BUG FIX: divisor is 60.0, not 78.0. v1.6.1 @0x0013d09c.
        {
            TiXmlElement e = pt.FirstChildElement("life");
            if (e) {
                const char* t = e.GetText();
                tmpl->m_Life = t ? (float)(atof(t) / 60.0f) : 0.0f;
            }
        }

        // <type> — BUG FIX: shape 1 is "Vortex", not "Vertex". v1.6.1 @0x0013d09c.
        {
            TiXmlElement e = pt.FirstChildElement("type");
            if (e) {
                const char* t = e.GetText();
                if (t) {
                    if      (!strcmp(t, "Point"))     tmpl->m_Shape = 0;
                    else if (!strcmp(t, "Vortex"))    tmpl->m_Shape = 1;
                    else if (!strcmp(t, "Direction")) tmpl->m_Shape = 2;
                    else if (!strcmp(t, "Angular"))   tmpl->m_Shape = 3;
                }
            }
        }

        // <system>
        {
            TiXmlElement e = pt.FirstChildElement("system");
            if (e) {
                const char* t = e.GetText();
                if (t && !strcmp(t, "Global")) tmpl->m_CoordSystem = 1;
            }
        }

        // <gravity>
        {
            TiXmlElement e = pt.FirstChildElement("gravity");
            if (e) {
                ParseVec3(e.GetText(), tmpl->m_GravityMin);
                memcpy(tmpl->m_GravityMax, tmpl->m_GravityMin, sizeof(tmpl->m_GravityMin));
            }
        }
        {
            TiXmlElement e = pt.FirstChildElement("gravity_max");
            if (e) ParseVec3(e.GetText(), tmpl->m_GravityMax);
        }

        // <velocity min="..." max="..."/>
        {
            TiXmlElement e = pt.FirstChildElement("velocity");
            if (e) {
                ParseVec3(e.Attribute("min"), tmpl->m_VelocityMin);
                ParseVec3(e.Attribute("max"), tmpl->m_VelocityMax);
            }
        }

        // <color>
        {
            TiXmlElement e = pt.FirstChildElement("color");
            if (e) {
                ParseColourBGRA(e.Attribute("startMin"), tmpl->m_ColourStartMin);
                ParseColourBGRA(e.Attribute("startMax"), tmpl->m_ColourStartMax);
                ParseColourBGRA(e.Attribute("endMin"),   tmpl->m_ColourEndMin);
                ParseColourBGRA(e.Attribute("endMax"),   tmpl->m_ColourEndMax);
                for (int i = 0; i < 4; ++i) {
                    tmpl->m_ColourMidMin[i] = (uint8_t)(((int)tmpl->m_ColourStartMin[i] +
                                                         (int)tmpl->m_ColourEndMin[i]) >> 1);
                    tmpl->m_ColourMidMax[i] = (uint8_t)(((int)tmpl->m_ColourStartMax[i] +
                                                         (int)tmpl->m_ColourEndMax[i]) >> 1);
                }
            }
        }

        // <size>
        {
            TiXmlElement e = pt.FirstChildElement("size");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_SizeStartMin = (uint8_t)v;
                if (e.QueryIntAttribute("startMax", &v) == TIXML_SUCCESS) tmpl->m_SizeStartMax = (uint8_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_SizeEndMin   = (uint8_t)v;
                if (e.QueryIntAttribute("endMax",   &v) == TIXML_SUCCESS) tmpl->m_SizeEndMax   = (uint8_t)v;
                tmpl->m_SizeMidMin = (uint8_t)(((int)tmpl->m_SizeStartMin + (int)tmpl->m_SizeEndMin) >> 1);
                tmpl->m_SizeMidMax = (uint8_t)(((int)tmpl->m_SizeStartMax + (int)tmpl->m_SizeEndMax) >> 1);
            }
        }

        // <spin startMin startMax endMin endMax> -> tmpl+0x50..0x56.
        // Slot order proven by AddParticle @0x0013c554: it lerps (0x50,0x52) into the
        // particle's spin-at-t=0 and (0x54,0x56) into spin-at-t=1 with one shared t.
        {
            TiXmlElement e = pt.FirstChildElement("spin");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_SpinStartMin = (int16_t)v;
                if (e.QueryIntAttribute("startMax", &v) == TIXML_SUCCESS) tmpl->m_SpinStartMax = (int16_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_SpinEndMin   = (int16_t)v;
                if (e.QueryIntAttribute("endMax",   &v) == TIXML_SUCCESS) tmpl->m_SpinEndMax   = (int16_t)v;
            }
        }

        // <cycleX> -> tmpl+0x40..0x46, <cycleY> -> tmpl+0x48..0x4E; same four-slot
        // (startMin, startMax, endMin, endMax) shape as <spin>.
        {
            TiXmlElement e = pt.FirstChildElement("cycleX");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_CycleXStartMin = (int16_t)v;
                if (e.QueryIntAttribute("startMax", &v) == TIXML_SUCCESS) tmpl->m_CycleXStartMax = (int16_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_CycleXEndMin   = (int16_t)v;
                if (e.QueryIntAttribute("endMax",   &v) == TIXML_SUCCESS) tmpl->m_CycleXEndMax   = (int16_t)v;
            }
        }
        {
            TiXmlElement e = pt.FirstChildElement("cycleY");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_CycleYStartMin = (int16_t)v;
                if (e.QueryIntAttribute("startMax", &v) == TIXML_SUCCESS) tmpl->m_CycleYStartMax = (int16_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_CycleYEndMin   = (int16_t)v;
                if (e.QueryIntAttribute("endMax",   &v) == TIXML_SUCCESS) tmpl->m_CycleYEndMax   = (int16_t)v;
            }
        }

        // <gridLock>
        {
            TiXmlElement e = pt.FirstChildElement("gridLock");
            if (e) {
                e.QueryFloatAttribute("x", &tmpl->m_GridLockStart);
                e.QueryFloatAttribute("y", &tmpl->m_GridLockEnd);
            }
        }

        // <friction>
        {
            TiXmlElement e = pt.FirstChildElement("friction");
            if (e) {
                ParseVec3(e.Attribute("start"), tmpl->m_VelocityMin);
                ParseVec3(e.Attribute("end"),   tmpl->m_VelocityMax);
            }
        }

        // <rotateCycle start end speedStart speedEnd>
        // TODO: v1.6.1 0x0013d09c (PSPParticleManager::LoadFile) — the attribute ->
        //   slot mapping for this block is UNVERIFIED. AddParticle @0x0013c554 proves
        //   the slots are five (min,max) pairs: amp-at-t0 (0x6C,0x70), amp-at-t1
        //   (0x74,0x78), rate-at-t0 (0x7C,0x80), rate-at-t1 (0x84,0x88) and phase
        //   (0x8C,0x90). The XML only supplies four scalars, so each presumably fills
        //   BOTH halves of one pair, but which scalar goes to which pair has not been
        //   read out of LoadFile. The assignments below are the pre-existing port
        //   mapping, kept byte-for-byte so this change introduces no new guess --
        //   they land in the amp/rate slots but almost certainly in the wrong order.
        {
            TiXmlElement e = pt.FirstChildElement("rotateCycle");
            if (e) {
                float fv = 0.0f;
                if (e.QueryFloatAttribute("speedStart", &fv) == TIXML_SUCCESS) tmpl->m_WobbleAmpStartMin = fv;
                if (e.QueryFloatAttribute("speedEnd",   &fv) == TIXML_SUCCESS) tmpl->m_WobbleAmpEndMax   = fv;
                if (e.QueryFloatAttribute("start",      &fv) == TIXML_SUCCESS) tmpl->m_WobbleRateEndMin  = fv;
                if (e.QueryFloatAttribute("end",        &fv) == TIXML_SUCCESS) tmpl->m_WobbleRateEndMax  = fv;
                else                                                          tmpl->m_WobbleRateEndMax  = tmpl->m_WobbleRateEndMin;
            }
        }

        // <SourceBlend>, <DestinationBlend>
        {
            TiXmlElement e = pt.FirstChildElement("SourceBlend");
            if (e) tmpl->m_BlendMode = ParseBlendEnum(e.GetText());
        }
        {
            TiXmlElement e = pt.FirstChildElement("DestinationBlend");
            if (e) tmpl->m_BlendMode = ParseBlendEnum(e.GetText());
        }

        // <texture> — store texture via side array (see m_pTextureRefs DIFFERS note).
        {
            TiXmlElement e = pt.FirstChildElement("texture");
            if (e) {
                const char* texName = e.Attribute("name");
                if (texName && *texName) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s/%s.tex", texCatStr.c_str(), texName);
                    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::GetInstance().Load(buf);
                    if (tex.IsValid()) {
                        // Assign texture index = current count; will be stored into blob after final alloc.
                        // For now store as local index in m_TextureIdx.
                        tmpl->m_TextureIdx = (uint32_t)m_NumTextureRefs;
                        ++m_NumTextureRefs;
                        // Temporarily stash the SmartPtr in a growing side vector.
                        // We will allocate the real array after Pass 2.
                        // Use the tex object count as temporary storage index.
                        // Since we haven't allocated m_pTextureRefs yet, store temporarily.
                        // We'll rebuild after final blob copy using a local vector.
                        // (tex goes out of scope here — need to keep it alive)
                    }
                }
            }
        }

        if (outNames && name) {
            strcpy(outNames[m_NumParticleTemplates], name);
        }
        cursor += 0xB8;
        ++m_NumParticleTemplates;
    }

    // --- Pass 2: <emitter> --------------------------------------------------
    for (TiXmlElement em = body.FirstChildElement("emitter");
         em;
         em = em.NextSiblingElement("emitter")) {

        // Zero 0x4C header.
        memset(cursor, 0, 0x4C);
        PSPEmitterBlob* hdr = reinterpret_cast<PSPEmitterBlob*>(cursor);

        const char* name = em.Attribute("name");
        if (name) {
            strncpy(hdr->m_Name, name, sizeof(hdr->m_Name) - 1);
            hdr->m_Hash = StringHash(name);
        }

        {
            TiXmlElement life = em.FirstChildElement("life");
            if (life) {
                const char* t = life.GetText();
                // BUG FIX: divisor is 60.0. v1.6.1 PSPParticleManager::LoadFile @0x0013d09c.
                hdr->m_MaxLifetime = t ? (float)(atof(t) / 60.0f) : 0.0f;
            }
        }

        uint8_t numSets = 0;
        uint8_t* setCursor = cursor + 0x4C;

        for (TiXmlElement ps = em.FirstChildElement("particleSet");
             ps;
             ps = ps.NextSiblingElement("particleSet")) {

            // Zero 0x30 set record.
            memset(setCursor, 0, 0x30);
            PSPParticleSet* set = reinterpret_cast<PSPParticleSet*>(setCursor);
            set->m_TemplateOffset = 0xFFFFFFFFu;  // "none" sentinel

            // Look up particle-template index by name hash; store raw index as float encoding.
            // Post-load patch will convert to blob offset.
            {
                const char* psName = ps.Attribute("name");
                if (psName) {
                    uint32_t psHash = StringHash(psName);
                    for (int ii = 0; ii < (int)nameToIndex.size(); ++ii) {
                        if (nameToIndex[(size_t)ii].hash == psHash) {
                            // Store index+1 (so 0 remains the "none" sentinel) as uint32.
                            set->m_TemplateOffset = (uint32_t)(nameToIndex[(size_t)ii].idx + 1);
                            break;
                        }
                    }
                }
            }

            {
                TiXmlElement time = ps.FirstChildElement("time");
                if (time) {
                    time.QueryFloatAttribute("start", &set->m_TimeStart);
                    time.QueryFloatAttribute("stop",  &set->m_TimeStop);
                }
            }

            {
                TiXmlElement num = ps.FirstChildElement("particleNumber");
                if (num) {
                    int init = 0;
                    num.QueryIntAttribute("init", &init);
                    set->m_InitCount = (uint8_t)init;
                    num.QueryFloatAttribute("perSec", &set->m_PerSec);
                }
            }

            {
                TiXmlElement vel = ps.FirstChildElement("velocity");
                if (vel) {
                    ParseVec3(vel.Attribute("min"), set->m_VelocityMin);
                    ParseVec3(vel.Attribute("max"), set->m_VelocityMax);
                }
            }

            setCursor += 0x30;
            ++numSets;
        }

        hdr->m_NumSets = numSets;
        cursor = setCursor;
        ++m_NumEmitterTemplates;
    }

    // (e) Allocate final blob and copy scratch.
    int n = (int)(cursor - scratch);
    uint8_t* finalBlob = new uint8_t[(size_t)(n + 1)];
    memcpy(finalBlob, scratch, (size_t)n);
    m_pTemplates = finalBlob;
    m_pEmitterTemplates = m_pTemplates + (size_t)m_NumParticleTemplates * 0xB8;
    delete[] scratch;

    // (f) Post-load patch: convert set m_TemplateOffset from (idx+1) to actual byte offset.
    // Walk emitter blob variable-stride.
    {
        uint8_t* ep = m_pEmitterTemplates;
        for (int ei = 0; ei < m_NumEmitterTemplates; ++ei) {
            PSPEmitterBlob* hdr2 = reinterpret_cast<PSPEmitterBlob*>(ep);
            for (int si = 0; si < (int)hdr2->m_NumSets; ++si) {
                PSPParticleSet* set = EmitterSet(ep, si);
                uint32_t encoded = set->m_TemplateOffset;
                if (encoded == 0xFFFFFFFFu || encoded == 0) {
                    set->m_TemplateOffset = 0xFFFFFFFFu;
                } else {
                    uint32_t tmplIdx = encoded - 1;
                    if ((int)tmplIdx < m_NumParticleTemplates) {
                        // Blob offset in bytes from m_pTemplates base.
                        set->m_TemplateOffset = (uint32_t)(tmplIdx * 0xB8);
                    } else {
                        set->m_TemplateOffset = 0xFFFFFFFFu;
                    }
                }
            }
            ep += 0x4C + hdr2->m_NumSets * 0x30;
        }
    }

    // (g) Allocate texture ref side-array and populate by re-scanning <particleTemplate>.
    // We need to reload textures now that the blob is stable (m_NumTextureRefs was counted above).
    // Reset m_NumTextureRefs and reallocate; re-scan particle templates in blob order.
    if (m_NumTextureRefs > 0) {
        m_pTextureRefs = new Mortar::SmartPtr<Mortar::Texture>[m_NumTextureRefs];
    }
    // Re-scan to populate texture refs in the same order as pass 1.
    {
        int texSlot = 0;
        for (TiXmlElement pt = body.FirstChildElement("particleTemplate");
             pt;
             pt = pt.NextSiblingElement("particleTemplate")) {

            TiXmlElement e = pt.FirstChildElement("texture");
            if (!e) continue;
            const char* texName = e.Attribute("name");
            if (!texName || !*texName) continue;

            char buf[256];
            snprintf(buf, sizeof(buf), "%s/%s.tex", texCatStr.c_str(), texName);
            Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::GetInstance().Load(buf);
            if (tex.IsValid()) {
                if (texSlot < m_NumTextureRefs) {
                    m_pTextureRefs[texSlot] = tex;
                    // Set aspect ratio in the blob (we already computed m_TextureIdx = texSlot in Pass 1).
                    // Find the particle template blob record that corresponds.
                    // m_TextureIdx was set to texSlot during Pass 1 iteration order.
                    // We need to find the right blob record.
                    // Since Pass 1 and this re-scan iterate in the same order,
                    // we can count non-texture templates to find the right record.
                    // Simpler: iterate blob records to find m_TextureIdx == texSlot.
                    for (int ti2 = 0; ti2 < m_NumParticleTemplates; ++ti2) {
                        PSPParticleTemplate* tmpl2 = GetParticleTemplate(ti2);
                        if (tmpl2 && tmpl2->m_TextureIdx == (uint32_t)texSlot) {
                            const float tw = (float)tex->GetWidth();
                            const float th = (float)tex->GetHeight();
                            if (th > 0.0f) tmpl2->m_AspectRatio = tw / th;
                            break;
                        }
                    }
                }
                ++texSlot;
            }
        }
    }

    LOG_DEBUG("PSPParticleManager", "Loaded %d particle templates, %d emitter templates from %s",
              m_NumParticleTemplates, m_NumEmitterTemplates, xmlPath);
    return true;
}

void PSPParticleManager::Clear() {
    ClearEmitters();
}

// v1.6.1 PSPParticleManager::ClearEmitters @0x0013c100 (thunk) — drain
// m_pActiveEmitters to pool; rebuild free-list; zero every template's live-list head.
// ASM-verified: ClearEmitters zeros per-template head at blob+0x04.
void PSPParticleManager::ClearEmitters() {
    // Drain active emitter list to pool.
    while (m_pActiveEmitters) {
        PSPParticleEmitter* node = m_pActiveEmitters;
        m_pActiveEmitters = node->m_Next;
        if (node->m_pRefPtr) *node->m_pRefPtr = 0;
        if (m_pEmitterPool) m_pEmitterPool->Push(node);
    }

    // Re-thread free-list (slots 1..1023; sentinel at slot 0).
    if (m_pParticles) {
        for (int i = 1; i <= 1022; ++i) {
            m_pParticles[i].m_NextLink = (uint16_t)(i + 1);
        }
        m_pParticles[1023].m_NextLink = 0;
        m_FreeHead = 1;
    }

    // Zero every particle-template live-list head (blob+0x04).
    // Binary ClearEmitters @0x0013c100: for i in m_NumParticleTemplates: *(uint16*)(m_pTemplates+i*0xB8+0x4)=0.
    // NOTE: spec mentions m_NumEmitterTemplates as the count but the intent is particle templates
    // (each has its own live-list head at +0x04). Using m_NumParticleTemplates.
    for (int i = 0; i < m_NumParticleTemplates; ++i) {
        PSPParticleTemplate* tmpl = GetParticleTemplate(i);
        if (tmpl) tmpl->m_LiveHead = 0;
    }
}
