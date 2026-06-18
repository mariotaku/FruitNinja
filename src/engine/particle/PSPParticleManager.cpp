#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "util/PathCI.h"
#include "asset/TextureManager.h"
#include "math/Random.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include <tinyxml2.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

// Analysed: 2026-04-13T10:30

// Parse "x y z" into three floats via sscanf, matching ParseInt3/ParseFloat3 use.
static bool ParseVec3(const char* s, float out[3]) {
    if (!s) return false;
    return sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

// Parse "r g b a" ints into a BGRA byte tuple, scaling 0-31 XML values to 0-255.
// Matches the binary's 255.0f/31.0f multiplier at DAT_001166c4.
// XML colour attributes are written in R G B A order with 5-bit values
// (0..31). Memory order must match what glColorPointer(4, GL_UNSIGNED_BYTE)
// expects downstream — i.e. byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A.
// The function was previously named "BGRA" and stored in BGRA order, which
// made R and B swap in the shader — visible as bombs emitting cyan/blue
// smoke instead of white fading to warm yellow. Kept the name for the
// smaller diff; the layout is now RGBA.
static void ParseColourBGRA(const char* s, uint8_t out[4]) {
    if (!s) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    int r = 0, g = 0, b = 0, a = 0;
    sscanf(s, "%d %d %d %d", &r, &g, &b, &a);
    const float scale = 255.0f / 31.0f;
    out[0] = (uint8_t)(r * scale); // R
    out[1] = (uint8_t)(g * scale); // G
    out[2] = (uint8_t)(b * scale); // B
    out[3] = (uint8_t)(a * scale); // A
}

// GL blend enum from string. Matches LoadFile: "SrcAlpha"->0x302,
// "InvSrcAlpha"->0x303, "One"->0x01.
static uint16_t ParseBlendEnum(const char* s) {
    if (!s) return 0;
    if (!strcmp(s, "SourceAlpha") || !strcmp(s, "SrcAlpha")) return 0x302;
    if (!strcmp(s, "InverseSourceAlpha") || !strcmp(s, "InvSrcAlpha")) return 0x303;
    if (!strcmp(s, "One")) return 0x01;
    return 0;
}

// v1.6.1 PSPParticleManager::PSPParticleManager @0x0013bf40 — manager ctor.
// Sets m_GlobalTimeScale=1.0; NULLs the owned pointers (buffer + pool).
PSPParticleManager::PSPParticleManager()
    : m_GlobalTimeMod(0.0f)
    , m_GlobalTimeScale(1.0f)
    , m_GlobalOrigin(0.0f, 0.0f, 0.0f)
    , m_pParticles(0)
    , m_FreeHead(0)
    , m_DrawnParticleCount(0)
    , m_pActiveEmitters(0)
    , m_pEmitterPool(0)
{
}

PSPParticleManager::~PSPParticleManager() {
    Destroy();
}

// v1.6.1 PSPParticleManager::Destroy @0x0013cfb8 — release tex refs, ClearEmitters,
// free the particle buffer, delete the emitter pool.
void PSPParticleManager::Destroy() {
    // 1. Release texture SmartPtr refs on all particle templates
    //    (binary: SmartPtr::SetNull on each template+0xAC).
    for (size_t i = 0; i < m_ParticleTemplates.size(); ++i) {
        m_ParticleTemplates[i].m_Texture.SetNull();
    }
    // 2. Drain active emitters (matches binary step 2: ClearEmitters).
    ClearEmitters();
    // 3. Free the 1024-slot particle buffer. Binary: free(m_pParticles - 8)
    //    because binary uses an 8-byte cookie prefix. Port allocates with
    //    new PSPParticle[1024] so free with delete[].
    //    // DIFFERS: binary alloc = operator new[](0x29008) with 8-byte cookie
    //    prefix; port uses new PSPParticle[1024] + delete[] for simplicity.
    if (m_pParticles) {
        delete[] m_pParticles;
        m_pParticles = 0;
    }
    // 4. Delete emitter MemoryPool (binary: delete m_pEmitterPool).
    if (m_pEmitterPool) {
        delete m_pEmitterPool;
        m_pEmitterPool = 0;
    }
    // 5. In the binary, template arrays are flat operator_new[] blocks whose
    //    memory was already freed in step 3/4. The port's std::vector members
    //    will be destructed by ~PSPParticleManager and LoadFile calls clear()
    //    explicitly before re-loading — no explicit clear needed here.
}

const PSPEmitterTemplate* PSPParticleManager::FindTemplate(uint32_t hash) const {
    // Matches AddEmitter's linear search over m_pEmitterTemplates.
    for (size_t i = 0; i < m_EmitterTemplates.size(); ++i) {
        if (m_EmitterTemplates[i].m_Hash == hash)
            return &m_EmitterTemplates[i];
    }
    return nullptr;
}

// Binary @ 0x001148dc — linear hash lookup over emitter templates; bool result.
bool PSPParticleManager::EmitterExists(uint32_t hash) {
    return FindTemplate(hash) != nullptr;
}

// Binary @ 0x0011490c — index lookup; returns &m_EmitterTemplates[idx] or nullptr.
PSPEmitterTemplate* PSPParticleManager::GetEmitterTemplate(int idx) {
    if (idx < 0 || (size_t)idx >= m_EmitterTemplates.size()) return nullptr;
    return &m_EmitterTemplates[(size_t)idx];
}

// v1.6.1 PSPParticleManager::AddEmitter @0x0013c1b8 — pop from pool, init defaults,
// prepend to m_pActiveEmitters intrusive list.
// Admit predicate: InUseCount()+1 < 120 (at most 119 live emitters).
// Pool-full path returns nullptr WITHOUT zeroing *ppRef; binary only zeros
// *ppRef on hash-miss inside the pool-OK branch.
PSPParticleEmitter* PSPParticleManager::AddEmitter(uint32_t hash,
                                                   PSPParticleEmitter** ppRef,
                                                   bool updateWhenPaused) {
    if (!m_pEmitterPool) return nullptr;
    if (m_pEmitterPool->InUseCount() + 1 >= 120) {
        return nullptr;
    }

    const PSPEmitterTemplate* tmpl = FindTemplate(hash);
    if (!tmpl) {
        if (ppRef) *ppRef = nullptr;
        return nullptr;
    }

    PSPParticleEmitter* e = m_pEmitterPool->Pop();
    if (!e) return nullptr;

    // Reset emitter state — all defaults match the binary's explicit init block
    // (AddEmitter @ 0x13c1b8):
    //   +0x00(Timer)=0, +0x08..+0x1C(Pos,Vel)=0, +0x04(bStarted u16)=1
    //   +0x20(RateScale)=1.0, +0x24(SizeBias)=1.0, +0x28(SpinScale)=1.0,
    //   +0x2C(TimeScale)=1.0, +0x30(DirCos)=1.0, +0x34(DirSin)=0,
    //   +0x38(VelScale)=1.0, +0x3C(bMirrorX)=0, +0x4D(bTrailStarted)=0.
    e->m_Timer = 0.0f;
    e->m_bStarted = 1;
    e->m_Pos = Vec3(0, 0, 0);
    e->m_Vel = Vec3(0, 0, 0);
    e->m_RateScale = 1.0f;
    e->m_SizeBias = 1.0f;
    e->m_SpinScale = 1.0f;
    e->m_TimeScale = 1.0f;
    e->m_DirCos = 1.0f;
    e->m_DirSin = 0.0f;
    e->m_VelScale = 1.0f;
    e->m_bMirrorX = 0;
    e->m_bUpdateWhenPaused = updateWhenPaused ? 1 : 0;
    e->m_bTrailStarted = 0;
    e->m_pTemplate = tmpl;
    e->m_pRefPtr = ppRef;

    // Prepend to active intrusive list (binary: e->m_Next = m_pActiveEmitters; m_pActiveEmitters = e).
    e->m_Next = m_pActiveEmitters;
    m_pActiveEmitters = e;

    if (ppRef) *ppRef = e;
    return e;
}

// v1.6.1 PSPParticleManager::ClearEmitter @0x0013c088 — unlink the node from
// m_pActiveEmitters (head or prev->m_Next fixup), null *m_pRefPtr, pool->Push.
void PSPParticleManager::ClearEmitter(PSPParticleEmitter* emitter) {
    if (!emitter) return;
    PSPParticleEmitter** cur = &m_pActiveEmitters;
    while (*cur) {
        if (*cur == emitter) {
            *cur = emitter->m_Next;
            if (emitter->m_pRefPtr) *emitter->m_pRefPtr = nullptr;
            if (m_pEmitterPool) m_pEmitterPool->Push(emitter);
            return;
        }
        cur = &(*cur)->m_Next;
    }
}

// -----------------------------------------------------------------------------
// Update helpers
// -----------------------------------------------------------------------------
// rand() in [0,1)
static inline float Rand01() {
    return (float)rand() / (float)RAND_MAX;
}
// rand() in [lo, hi]
static inline float RandRange(float lo, float hi) {
    return lo + (hi - lo) * Rand01();
}

// Quadrant-mirror sign used by AddParticle's m_bMirrorX branch (0x115644).
// Binary @ 0x001157c0/0x115800: vcmpe vs 0 then ble/ite produces
//   v > 0  -> -1.0
//   v < 0  -> +1.0
//   v == 0 ->  0.0 (DAT_00115b60 = 0.0f)
// i.e. -sign(v) with sign(0)==0. Used to mirror gravity/velocity by the
// owner's screen-half so a split-touch two-player layout sprays particles
// symmetrically about the centre line.
static inline float QuadrantMirror(float v) {
    if (v > 0.0f) return -1.0f;
    if (v < 0.0f) return 1.0f;
    return 0.0f;
}

// v1.6.1 PSPParticleManager::AddParticle @0x13c554 — pop a free slot from
// m_FreeHead, init the particle, link into the template's live-list.
// Returns 0 on failure (no free slots).
static uint16_t AddParticle(PSPParticle* buf, uint16_t& freeHead,
                             PSPEmitterTemplate* tmpl,
                             PSPParticleEmitter& emitter, const PSPParticleSet& set,
                             int32_t setIdx) {
    // Pop from free list.
    uint16_t idx = freeHead;
    if (idx == 0) return 0; // no free slots
    freeHead = buf[idx].m_NextLink;

    PSPParticle& p = buf[idx];
    p.m_field44 = setIdx;
    p.m_Pos = emitter.m_Pos;

    // Velocity: set-level min/max (randomized per component), halved, then
    // added to emitter vel. The `* 0.5f` matches the binary AddParticle
    // @ 0x115644: after picking the random set-level velocity it does an
    // unconditional `local_78.xyz *= 0.5f` before storing onto the particle.
    float vx = RandRange(set.m_VelocityMin[0], set.m_VelocityMax[0]) * 0.5f;
    float vy = RandRange(set.m_VelocityMin[1], set.m_VelocityMax[1]) * 0.5f;
    float vz = RandRange(set.m_VelocityMin[2], set.m_VelocityMax[2]) * 0.5f;

    // 2D rotation of the XY velocity by the emitter's (cos, sin) pair stored
    // in m_DirCos (+0x30) and m_DirSin (+0x34). Matches the binary AddParticle
    // @ 0x00115644 -- used to rotate fruit-impact particles so chunks spray
    // along the blade direction. Identity when the caller leaves defaults (cos=1, sin=0).
    const float cosA = emitter.m_DirCos;
    const float sinA = emitter.m_DirSin;
    float rvx = vx * cosA + vy * sinA;
    float rvy = vy * cosA - sinA * vx;
    float rvz = vz;

    const PSPParticleTemplate* ptmpl = set.m_pTemplate;

    if (ptmpl) {
        // NOTE: template m_VelocityMin/Max are NOT an initial-velocity range;
        // they are a per-component per-frame velocity LERP (damping) factor
        // applied each tick during UpdateEmitter integration.

        p.m_Gravity.x = RandRange(ptmpl->m_GravityMin[0], ptmpl->m_GravityMax[0]);
        p.m_Gravity.y = RandRange(ptmpl->m_GravityMin[1], ptmpl->m_GravityMax[1]);
        p.m_Gravity.z = RandRange(ptmpl->m_GravityMin[2], ptmpl->m_GravityMax[2]);

        p.m_Life = ptmpl->m_StartTime;
        p.m_Age  = 0.0f;

        // Two-segment size lerp — random per stop so each particle gets its own.
        p.m_SizeStart = RandRange((float)ptmpl->m_SizeStartMin, (float)ptmpl->m_SizeStartMax);
        p.m_SizeMid   = RandRange((float)ptmpl->m_SizeMidMin,   (float)ptmpl->m_SizeMidMax);
        p.m_SizeEnd   = RandRange((float)ptmpl->m_SizeEndMin,   (float)ptmpl->m_SizeEndMax);

        // Spin rate lerp: template has start/end min/max int16 ranges.
        // Binary AddParticle @ 0x115644 multiplies the LERP'd int16 by
        // DAT_00115b64 = 182.0f (degrees -> 16-bit angle-index: 65536/360 ~= 182)
        // and stores it as an int16 angle-table index added to field_0x28 each 1/60s tick.
        // Convert to rad/sec for the port's float-radian integration:
        //   rad_per_sec = int16 * (182/65536) * 2pi * 60
        //              = int16 * 6.28318 * 60 / 360
        //              = int16 * 1.0472
        static const float SPIN_INT16_TO_RAD_PER_SEC =
            (182.0f / 65536.0f) * 6.2831853f * 60.0f;
        p.m_SpinStart = RandRange((float)ptmpl->m_SpinStartMin,
                                  (float)ptmpl->m_SpinStartMax) * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_SpinEnd   = RandRange((float)ptmpl->m_SpinEndMin,
                                  (float)ptmpl->m_SpinEndMax)   * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_Rotation = RandRange(ptmpl->m_AngleMin, ptmpl->m_AngleMax);

        // RotCycle -- oscillating rotation offset.
        p.m_RotCycleRate  = 0.5f * (ptmpl->m_FrictionSpeedStart +
                                    ptmpl->m_FrictionSpeedEnd);
        p.m_RotCycleAmp   = ptmpl->m_FrictionOffsetMin * (3.14159265f / 180.0f);
        p.m_RotCyclePhase = Rand01() * 6.2831853f;

        // CycleX / CycleY — size modulation rates.
        p.m_CycleXRate  = 0.5f * ((float)ptmpl->m_CycleXStart + (float)ptmpl->m_CycleXEnd);
        p.m_CycleYRate  = 0.5f * ((float)ptmpl->m_CycleYStart + (float)ptmpl->m_CycleYEnd);
        p.m_CycleXPhase = Rand01() * 6.2831853f;
        p.m_CycleYPhase = Rand01() * 6.2831853f;

        // Quadrant-mirror branch (AddParticle @ 0x001157ae). Gated on the
        // emitter's m_bMirrorX, NOT on the template shape.
        if (emitter.m_bMirrorX != 0) {
            float gtmp = p.m_Gravity.x;
            p.m_Gravity.x = p.m_Gravity.y;
            p.m_Gravity.y = gtmp;
            p.m_Gravity.x *= QuadrantMirror(p.m_Pos.x);
            p.m_Gravity.x *= emitter.m_TimeScale;
            p.m_Gravity.y *= emitter.m_TimeScale;
            p.m_Gravity.z *= emitter.m_TimeScale;

            float vtmp = rvx;
            rvx = rvy;
            rvy = vtmp;
            rvx *= QuadrantMirror(emitter.m_Pos.x);
            rvx *= emitter.m_VelScale;
            rvy *= emitter.m_VelScale;
            rvz *= emitter.m_VelScale;
        }

        p.m_Vel.x = emitter.m_Vel.x + rvx;
        p.m_Vel.y = emitter.m_Vel.y + rvy;
        p.m_Vel.z = emitter.m_Vel.z + rvz;

        // Shape-type branching (matches AddParticle 0x115644):
        //   0 = Point     -- no extra init
        //   1 = Vertex    -- start half a velocity step behind the emitter
        //   2 = Direction -- rotate particle to face its own velocity
        switch (ptmpl->m_Shape) {
            case 1:
                p.m_Pos.x -= p.m_Vel.x;
                p.m_Pos.y -= p.m_Vel.y;
                p.m_Pos.z -= p.m_Vel.z;
                break;
            case 2:
                p.m_Rotation += atan2f(p.m_Vel.y, p.m_Vel.x);
                break;
            default:
                break;
        }
    } else {
        p.m_Age  = 0.0f;
        p.m_Vel.x = emitter.m_Vel.x + rvx;
        p.m_Vel.y = emitter.m_Vel.y + rvy;
        p.m_Vel.z = emitter.m_Vel.z + rvz;
        p.m_Life = 1.0f;
        p.m_SizeStart = p.m_SizeMid = p.m_SizeEnd = 8.0f;
    }

    // Prepend to template live-list:
    //   p.m_NextLink = template->m_ParticleHead (old head);
    //   template->m_ParticleHead = idx;
    //   ++template->m_ParticleCount.
    p.m_NextLink = tmpl->m_ParticleHead;
    tmpl->m_ParticleHead = idx;
    ++tmpl->m_ParticleCount;

    return idx;
}

// Matches PSPEmitterTemplate::Ends (0x00114884). Returns true if the
// template is "naturally terminating" — i.e. every set either has a positive
// TimeStop (finite window) OR zero continuous spawn rate (burst-only).
static bool EmitterTemplateEnds(const PSPEmitterTemplate* t) {
    if (!t) return true;
    for (std::vector<PSPParticleSet>::const_iterator cit = t->m_Sets.begin(); cit != t->m_Sets.end(); ++cit) {
        if (cit->m_TimeStop <= 0.0f && cit->m_PerSec > 0.0f) return false;
    }
    return true;
}

// Matches PSPParticleEmitter::Update (0x115d9c) — spawn pass + advance emitter timer.
// Particle physics pass (aging, death, integration) is done in the template
// live-list walk during Draw/Update, NOT per-emitter, because orphan particles
// from reaped emitters must continue to drain naturally.
static void UpdateEmitter(PSPParticleEmitter& e, float dt,
                          PSPParticle* buf, uint16_t& freeHead) {
    const PSPEmitterTemplate* et = e.m_pTemplate;
    if (!et) return;

    // Non-const pointer needed to update template live-list head/count.
    PSPEmitterTemplate* etMut = const_cast<PSPEmitterTemplate*>(et);

    const float currentTime = e.m_Timer;
    // Binary Update @0x13cd70: dtScaled = dt * m_TimeScale[+0x2c];
    // newTimer = m_Timer + dtScaled * m_RateScale[+0x20]
    const float dtScaled = dt * e.m_TimeScale;
    const float newTime = currentTime + dtScaled * e.m_RateScale;

    // Spawn pass -- for each set, check window and integrate rate
    for (int32_t si = 0; si < (int32_t)et->m_Sets.size(); ++si) {
        const PSPParticleSet& set = et->m_Sets[(size_t)si];
        const float startT = set.m_TimeStart;
        const float stopT  = set.m_TimeStop;

        // Continuous rate: only within [startT, stopT] (stopT==0 -> no limit).
        if (startT <= currentTime && (stopT == 0.0f || currentTime <= stopT)) {
            const float rate = set.m_PerSec;
            if (rate > 0.0f) {
                int desired = (int)(rate * ((currentTime + dtScaled * e.m_RateScale) - startT))
                            - (int)(rate * (currentTime - startT));
                for (int i = 0; i < desired; ++i)
                    AddParticle(buf, freeHead, etMut, e, set, si);
            }
        }

        // Burst on first frame crossing startT
        if (currentTime <= startT && startT < newTime) {
            for (int i = 0; i < (int)set.m_InitCount; ++i)
                AddParticle(buf, freeHead, etMut, e, set, si);
            if (e.m_RateScale == 0.0f) e.m_Timer += dt;
        }
    }

    // Advance emitter timer and position.
    e.m_Timer = newTime;
    e.m_Pos += e.m_Vel;
}

// v1.6.1 PSPParticleManager::Update @0x0013cee8 — update all active emitters;
// reap on timer vs lifetime; orphan particles drain via template live-list.
// Uses address-of-link cursor for safe in-place removal from intrusive list.
void PSPParticleManager::Update(float dt, bool paused) {
    if (!m_pParticles || !m_pEmitterPool) return;

    PSPParticleEmitter** cur = &m_pActiveEmitters;
    while (*cur) {
        PSPParticleEmitter* node = *cur;
        const PSPEmitterTemplate* et = node->m_pTemplate;

        // Tick: binary gates on m_bStarted && m_RateScale != 0.
        if (node->m_bStarted != 0 && node->m_RateScale != 0.0f &&
            (!paused || node->m_bUpdateWhenPaused)) {
            UpdateEmitter(*node, dt, m_pParticles, m_FreeHead);
        }

        // Keep-alive rule (binary Manager::Update @0x0013cee8):
        //   keep if timer < maxLifetime
        //   OR  (maxLifetime <= 0 AND !Ends(template))
        // Binary reaps on lifetime only, never on particle count.
        bool keep = true;
        if (et) {
            const bool naturallyInfinite = !EmitterTemplateEnds(et);
            if (et->m_MaxLifetime > 0.0f) {
                keep = (node->m_Timer < et->m_MaxLifetime);
            } else {
                keep = naturallyInfinite;
            }
        }

        if (!keep) {
            // Reap: unlink, null caller back-pointer, return to pool.
            // Do NOT touch particles — orphan particles drain naturally via
            // the template live-list walk in Draw/Update particle-aging pass.
            *cur = node->m_Next;
            if (node->m_pRefPtr) *node->m_pRefPtr = nullptr;
            m_pEmitterPool->Push(node);
        } else {
            cur = &node->m_Next;
        }
    }

    // Particle aging + death: walk every emitter template's live-list.
    // Removes dead particles, pushes their slots back to the free-list.
    for (size_t ti = 0; ti < m_EmitterTemplates.size(); ++ti) {
        PSPEmitterTemplate& tmpl = m_EmitterTemplates[ti];
        if (tmpl.m_ParticleHead == 0) continue;

        // Cursor: pointer to the link field of the predecessor (head or prev->m_NextLink).
        // We iterate the chain and unlink dead particles in-place.
        uint16_t* prevLink = &tmpl.m_ParticleHead;
        while (*prevLink != 0) {
            uint16_t idx = *prevLink;
            PSPParticle& p = m_pParticles[idx];
            p.m_Age += dt;

            if (p.m_Age >= p.m_Life) {
                // Unlink from live-list and push to free-list.
                *prevLink = p.m_NextLink;
                p.m_NextLink = m_FreeHead;
                m_FreeHead = idx;
                --tmpl.m_ParticleCount;
                continue;
            }

            // Physics integration (matches binary Draw @ 0x0013eccc).
            const float life = (p.m_Life > 0.0f) ? p.m_Life : 1.0f;
            const float t = p.m_Age / life;

            p.m_Vel += p.m_Gravity * dt;

            // Per-particle template velocity damping (from set index in m_field44).
            if (p.m_field44 >= 0 && (size_t)p.m_field44 < tmpl.m_Sets.size()) {
                const PSPParticleTemplate* pt = tmpl.m_Sets[(size_t)p.m_field44].m_pTemplate;
                if (pt) {
                    const float dampX = pt->m_VelocityMin[0] + (pt->m_VelocityMax[0] - pt->m_VelocityMin[0]) * t;
                    const float dampY = pt->m_VelocityMin[1] + (pt->m_VelocityMax[1] - pt->m_VelocityMin[1]) * t;
                    const float dampZ = pt->m_VelocityMin[2] + (pt->m_VelocityMax[2] - pt->m_VelocityMin[2]) * t;
                    p.m_Vel.x *= dampX;
                    p.m_Vel.y *= dampY;
                    p.m_Vel.z *= dampZ;
                }
            }

            p.m_Pos += p.m_Vel * dt;

            // Spin: lerp start->end over life, then integrate.
            const float spin = p.m_SpinStart + (p.m_SpinEnd - p.m_SpinStart) * t;
            p.m_Rotation += spin * dt;

            // Cycle accumulators (RotCycle + CycleX/Y). Rates in cycles/s -> radians.
            p.m_RotCyclePhase += p.m_RotCycleRate * dt * 6.2831853f;
            p.m_CycleXPhase   += p.m_CycleXRate   * dt * 6.2831853f;
            p.m_CycleYPhase   += p.m_CycleYRate   * dt * 6.2831853f;

            prevLink = &p.m_NextLink;
        }
    }
}

// -----------------------------------------------------------------------------
// Draw — v1.6.1 PSPParticleManager::Draw @0x0013eccc — fused integrate+render.
// Port separates integrate (Update) from render (Draw); dt and paused are
// unused in this body.
// DIFFERS: original @ 0x0013eccc = fused integrate+render over intrusive
// pool (m_NumEmitterTemplates templates, stride 0xB8, per-template index list
// at template+0x04, PSPParticle 0xA4 bytes, Math::SinIdx/CosIdx 16-bit
// angle tables, Mesh::DrawTriList+TextureAtlasPage); port separates
// integrate(Update)/render(Draw) over template live-lists with reduced
// PSPParticle, replaces angle-index trig with cosf/sinf radians, replaces
// Mesh::DrawTriList+TextureAtlasPage with Renderer::DrawTriList+GL.
// No glBlendFunc state restore at function exit -- binary leaves blend
// state at whatever the last template configured.
// -----------------------------------------------------------------------------
static inline uint32_t PackBGRA(const uint8_t c[4]) {
    return (uint32_t)c[0]
         | ((uint32_t)c[1] << 8)
         | ((uint32_t)c[2] << 16)
         | ((uint32_t)c[3] << 24);
}

static inline void LerpColour(const uint8_t a[4], const uint8_t b[4],
                              float t, uint8_t out[4]) {
    for (int i = 0; i < 4; ++i) {
        int v = (int)(a[i] + (b[i] - a[i]) * t);
        if (v < 0) v = 0; if (v > 255) v = 255;
        out[i] = (uint8_t)v;
    }
}

static void FlushParticleVerts(std::vector<QUADCUSTOMVERTEX>& verts,
                               const PSPParticleTemplate* tmpl) {
    if (!verts.empty() && tmpl && tmpl->m_Texture.IsValid()) {
        GLenum dstFactor = tmpl->m_BlendMode ? (GLenum)tmpl->m_BlendMode
                                             : GL_ONE_MINUS_SRC_ALPHA;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, dstFactor);
        glBindTexture(GL_TEXTURE_2D, tmpl->m_Texture->m_TexId);
        if (Renderer* r = Renderer::GetInstance()) {
            r->DrawTriList(verts.data(), (int)verts.size());
        }
    }
    verts.clear();
}

void PSPParticleManager::Draw(float dt, bool paused, int layer) {
    (void)dt;
    (void)paused;
    if (!m_pParticles) return;

    // Reset world matrix + upload MVP so DrawTriList uses the current ortho.
    // Matches binary Draw 0x114c64: "MatrixStack::Reset + UploadCurrentMatrices".
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    static std::vector<QUADCUSTOMVERTEX> s_verts;
    const PSPParticleTemplate* curTmpl = nullptr;

    // Iterate per-template live-lists (replaces binary's per-emitter-list loop).
    for (size_t ti = 0; ti < m_EmitterTemplates.size(); ++ti) {
        const PSPEmitterTemplate& tmpl = m_EmitterTemplates[ti];
        uint16_t idx = tmpl.m_ParticleHead;
        while (idx != 0) {
            PSPParticle& p = m_pParticles[idx];
            uint16_t nextIdx = p.m_NextLink;

            // Resolve per-particle template via set index stored in m_field44.
            const PSPParticleTemplate* pTmpl = 0;
            if (p.m_field44 >= 0 && (size_t)p.m_field44 < tmpl.m_Sets.size()) {
                pTmpl = tmpl.m_Sets[(size_t)p.m_field44].m_pTemplate;
            }

            // Layer filter: binary draws only particles whose template's
            // m_UseDepth matches the requested layer.
            if (pTmpl && pTmpl->m_UseDepth != layer) {
                idx = nextIdx;
                continue;
            }

            // Group flush on template change (batches DrawTriList per texture).
            if (pTmpl != curTmpl) {
                FlushParticleVerts(s_verts, curTmpl);
                curTmpl = pTmpl;
            }

            const float life = p.m_Life > 0.0f ? p.m_Life : 1.0f;
            float t = p.m_Age / life;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

            // Two-segment colour + size lerp: start->mid for t in [0,0.5),
            // mid->end for t in [0.5,1].
            uint8_t col[4];
            float size;
            if (pTmpl) {
                if (t < 0.5f) {
                    float u = t * 2.0f;
                    LerpColour(pTmpl->m_ColourStartMin, pTmpl->m_ColourMidMin, u, col);
                    size = p.m_SizeStart + (p.m_SizeMid - p.m_SizeStart) * u;
                } else {
                    float u = (t - 0.5f) * 2.0f;
                    LerpColour(pTmpl->m_ColourMidMin, pTmpl->m_ColourEndMin, u, col);
                    size = p.m_SizeMid + (p.m_SizeEnd - p.m_SizeMid) * u;
                }
            } else {
                col[0] = col[1] = col[2] = col[3] = 255;
                size = p.m_SizeStart;
            }
            uint32_t packed = PackBGRA(col);

            float aspect = curTmpl ? curTmpl->m_AspectRatio : 1.0f;
            if (aspect <= 0.0f) aspect = 1.0f;
            float hx = size * 0.5f * aspect;
            float hy = size * 0.5f;

            // CycleX / CycleY size modulation.
            if (p.m_CycleXRate != 0.0f) hx *= cosf(p.m_CycleXPhase);
            if (p.m_CycleYRate != 0.0f) hy *= cosf(p.m_CycleYPhase);

            // RotCycle oscillation.
            float effectiveRot = p.m_Rotation;
            if (p.m_RotCycleAmp != 0.0f)
                effectiveRot += p.m_RotCycleAmp * sinf(p.m_RotCyclePhase);

            const float ca = cosf(effectiveRot);
            const float sa = sinf(effectiveRot);
            const float dxX =  ca * hx, dxY = sa * hx;
            const float dyX = -sa * hy, dyY = ca * hy;

            float px = p.m_Pos.x;
            float py = p.m_Pos.y;
            const float pz = p.m_Pos.z;

            // Grid-lock: snap pos to cell centres.
            if (curTmpl) {
                const float gx = curTmpl->m_GridLockStart;
                const float gy = curTmpl->m_GridLockEnd;
                if (gx > 0.0f) px = floorf(px / gx + 0.5f) * gx;
                if (gy > 0.0f) py = floorf(py / gy + 0.5f) * gy;
            }

            const struct C { float x, y, u, v; } corners[4] = {
                { px - dxX - dyX, py - dxY - dyY, 0.0f, 0.0f }, // TL
                { px + dxX - dyX, py + dxY - dyY, 1.0f, 0.0f }, // TR
                { px + dxX + dyX, py + dxY + dyY, 1.0f, 1.0f }, // BR
                { px - dxX + dyX, py - dxY + dyY, 0.0f, 1.0f }, // BL
            };
            const int tri[6] = { 0, 1, 2, 0, 2, 3 };
            for (int i = 0; i < 6; ++i) {
                const C& c = corners[tri[i]];
                QUADCUSTOMVERTEX v;
                v.x = c.x; v.y = c.y; v.z = pz;
                v.nx = 0; v.ny = 0; v.nz = 1.0f;
                v.colour = packed;
                v.u = c.u; v.v = c.v;
                s_verts.push_back(v);
            }

            idx = nextIdx;
        }
    }
    FlushParticleVerts(s_verts, curTmpl);
}

// v1.6.1 PSPParticleManager::LoadFile @0x0013d09c — load particle templates from XML.
// texCategory is prepended to texture filenames: snprintf("%s/%s.tex", texCategory, name).
// outNames (optional): caller-allocated array receiving each <particleTemplate name="...">;
// strings are strcpy'd in parse order.
// Initialises the 1024-slot particle buffer and 120-slot emitter MemoryPool on first call
// (guarded on null -- ctor only NULLs the pointers).
// Returns true on success.
// ASM-spec v1.6.1 PSPParticleManager::LoadFile @0x0013d09c: 1024-slot particle buffer +
// free-list (m_NextLink, 1-based) + MemoryPool::Create(120).
bool PSPParticleManager::LoadFile(const char* texCategory, const char* xmlPath, char** outNames) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError xerr = doc.LoadFile(xmlPath);
    if (xerr != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(xmlPath);
        if (!ci.empty()) xerr = doc.LoadFile(ci.c_str());
    }
    if (xerr != tinyxml2::XML_SUCCESS) {
        LOG_WARN("PSPParticleManager", "LoadFile: failed to load %s", xmlPath);
        return false;
    }
    tinyxml2::XMLElement* root = doc.FirstChildElement("particle_file");
    if (!root) return false;
    tinyxml2::XMLElement* body = root->FirstChildElement("body");
    if (!body) return false;

    // (a) Allocate 1024-slot particle buffer on first call (guarded on null).
    // Binary: operator new[](0x29008) = 8-byte cookie + 1024*0xa4; m_pParticles=blk+8.
    // Port uses new PSPParticle[1024] (delete[] in Destroy) for simplicity.
    // // DIFFERS: binary alloc = cookie-prefixed block (stride/count in cookie[0/1]);
    //    port uses plain new[]. Semantics (1024 slots, free-list, Destroy frees) match.
    if (!m_pParticles) {
        m_pParticles = new PSPParticle[1024];
        // (b) Thread the free-list: slot 0 is sentinel (never allocated).
        // Slots 1..1023: p[i].m_NextLink = i+1; p[1023].m_NextLink = 0 (terminator).
        for (int i = 1; i <= 1022; ++i) {
            m_pParticles[i].m_NextLink = (uint16_t)(i + 1);
        }
        m_pParticles[1023].m_NextLink = 0;
        m_FreeHead = 1;
    }

    // (c) Create emitter MemoryPool(120) on first call (guarded on null).
    if (!m_pEmitterPool) {
        m_pEmitterPool = new Mortar::MemoryPool<PSPParticleEmitter>();
        m_pEmitterPool->Create(120);
    }

    m_ParticleTemplates.clear();
    m_EmitterTemplates.clear();

    // texCategory is prepended to texture filenames per binary snprintf pattern.
    const std::string texCatStr(texCategory ? texCategory : "");

    // --- First loop: <particleTemplate> --------------------------------------
    std::unordered_map<uint32_t, size_t> nameToIndex;

    for (tinyxml2::XMLElement* pt = body->FirstChildElement("particleTemplate");
         pt != nullptr;
         pt = pt->NextSiblingElement("particleTemplate")) {

        PSPParticleTemplate tmpl = {};
        // m_VelocityMin/Max on the TEMPLATE are a per-component per-frame velocity
        // LERP (damping) factor. Default to identity (1.0) so templates that omit
        // <velocity> get no damping.
        tmpl.m_VelocityMin[0] = 1.0f; tmpl.m_VelocityMin[1] = 1.0f; tmpl.m_VelocityMin[2] = 1.0f;
        tmpl.m_VelocityMax[0] = 1.0f; tmpl.m_VelocityMax[1] = 1.0f; tmpl.m_VelocityMax[2] = 1.0f;

        const char* name = pt->Attribute("name");
        uint32_t hash = name ? StringHash(name) : 0;
        if (name) nameToIndex[hash] = m_ParticleTemplates.size();

        { int _v = 0; pt->QueryIntAttribute("useDepth", &_v); tmpl.m_UseDepth = (int32_t)_v; }

        // <life> — stored as seconds after divide by 78.0
        // Binary @ 0x0013d558 uses divisor 78.0 (DAT 0x404e000000000000).
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("life")) {
            const char* t = e->GetText();
            tmpl.m_StartTime = t ? (float)(atof(t) / 78.0f) : 0.0f;
        }

        // <type> — 0=Point, 1=Vertex, 2=Direction, 3=Angular
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("type")) {
            const char* t = e->GetText();
            if (t) {
                if      (!strcmp(t, "Point"))     tmpl.m_Shape = 0;
                else if (!strcmp(t, "Vertex"))    tmpl.m_Shape = 1;
                else if (!strcmp(t, "Direction")) tmpl.m_Shape = 2;
                else if (!strcmp(t, "Angular"))   tmpl.m_Shape = 3;
            }
        }
        // <system> — 0=Local, 1=Global
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("system")) {
            const char* t = e->GetText();
            if (t && !strcmp(t, "Global")) tmpl.m_CoordSystem = 1;
        }

        // <gravity> — "x y z" (default min, max falls back to min)
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("gravity")) {
            ParseVec3(e->GetText(), tmpl.m_GravityMin);
            memcpy(tmpl.m_GravityMax, tmpl.m_GravityMin, sizeof(tmpl.m_GravityMin));
        }
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("gravity_max")) {
            ParseVec3(e->GetText(), tmpl.m_GravityMax);
        }

        // <velocity min="..." max="..."/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("velocity")) {
            ParseVec3(e->Attribute("min"), tmpl.m_VelocityMin);
            ParseVec3(e->Attribute("max"), tmpl.m_VelocityMax);
        }

        // <color startMin="R G B A" startMax=".." endMin=".." endMax=".."/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("color")) {
            ParseColourBGRA(e->Attribute("startMin"), tmpl.m_ColourStartMin);
            ParseColourBGRA(e->Attribute("startMax"), tmpl.m_ColourStartMax);
            ParseColourBGRA(e->Attribute("endMin"),   tmpl.m_ColourEndMin);
            ParseColourBGRA(e->Attribute("endMax"),   tmpl.m_ColourEndMax);
            // mid = average of start/end (matches binary fallback)
            for (int i = 0; i < 4; ++i) {
                tmpl.m_ColourMidMin[i] = (uint8_t)(((int)tmpl.m_ColourStartMin[i] +
                                                    (int)tmpl.m_ColourEndMin[i]) >> 1);
                tmpl.m_ColourMidMax[i] = (uint8_t)(((int)tmpl.m_ColourStartMax[i] +
                                                    (int)tmpl.m_ColourEndMax[i]) >> 1);
            }
        }

        // <size startMin=".." startMax=".." endMin=".." endMax=".."/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("size")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeStartMin = (uint8_t)v;
            if (e->QueryIntAttribute("startMax", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeStartMax = (uint8_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeEndMin   = (uint8_t)v;
            if (e->QueryIntAttribute("endMax",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeEndMax   = (uint8_t)v;
            tmpl.m_SizeMidMin = (uint8_t)(((int)tmpl.m_SizeStartMin + (int)tmpl.m_SizeEndMin) >> 1);
            tmpl.m_SizeMidMax = (uint8_t)(((int)tmpl.m_SizeStartMax + (int)tmpl.m_SizeEndMax) >> 1);
        }

        // <spin startMin=".." startMax=".." endMin=".." endMax=".."/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("spin")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinStartMin = (int16_t)v;
            if (e->QueryIntAttribute("startMax", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinStartMax = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinEndMin   = (int16_t)v;
            if (e->QueryIntAttribute("endMax",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinEndMax   = (int16_t)v;
        }

        // <cycleX startMin="a" startMax="b" endMin="c" endMax="d"/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("cycleX")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleXStart = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleXEnd   = (int16_t)v;
        }
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("cycleY")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleYStart = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleYEnd   = (int16_t)v;
        }

        // <gridLock x="16" y="16"/> -- snap-to-grid lock per axis.
        // ASM-verified: 2026-05-09 binary @ 0x00115f60 (re-analyst)
        {
            tinyxml2::XMLElement* e = pt->FirstChildElement("gridLock");
            if (e) {
                e->QueryFloatAttribute("x", &tmpl.m_GridLockStart);
                e->QueryFloatAttribute("y", &tmpl.m_GridLockEnd);
            }
        }

        // <friction start="x y z" end="x y z"/> -- velocity LERP damping factor.
        {
            tinyxml2::XMLElement* e = pt->FirstChildElement("friction");
            if (e) {
                ParseVec3(e->Attribute("start"), tmpl.m_VelocityMin);
                ParseVec3(e->Attribute("end"),   tmpl.m_VelocityMax);
            }
        }

        // <rotateCycle start="base" end="endBase" speedStart="rate1" speedEnd="rate2"/>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("rotateCycle")) {
            float fv = 0.0f;
            if (e->QueryFloatAttribute("speedStart", &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionSpeedStart = fv;
            if (e->QueryFloatAttribute("speedEnd",   &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionSpeedEnd   = fv;
            if (e->QueryFloatAttribute("start",      &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionOffsetMin  = fv;
            if (e->QueryFloatAttribute("end",        &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionOffsetMax  = fv;
            else
                tmpl.m_FrictionOffsetMax  = tmpl.m_FrictionOffsetMin;
        }

        // <SourceBlend>, <DestinationBlend>
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("SourceBlend"))
            tmpl.m_BlendMode = ParseBlendEnum(e->GetText());
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("DestinationBlend"))
            tmpl.m_BlendMode = ParseBlendEnum(e->GetText());

        // <texture name="..."/> — load via TextureManager
        if (tinyxml2::XMLElement* e = pt->FirstChildElement("texture")) {
            const char* texName = e->Attribute("name");
            if (texName && *texName) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s/%s.tex", texCatStr.c_str(), texName);
                tmpl.m_Texture = Mortar::TextureManager::GetInstance().Load(buf);
                if (tmpl.m_Texture.IsValid()) {
                    const float tw = (float)tmpl.m_Texture->m_Width;
                    const float th = (float)tmpl.m_Texture->m_Height;
                    if (th > 0.0f) tmpl.m_AspectRatio = tw / th;
                }
            }
        }

        if (outNames && name) {
            strcpy(outNames[m_ParticleTemplates.size()], name);
        }
        m_ParticleTemplates.push_back(tmpl);
    }

    // --- Second loop: <emitter> ---------------------------------------------
    for (tinyxml2::XMLElement* em = body->FirstChildElement("emitter");
         em != nullptr;
         em = em->NextSiblingElement("emitter")) {

        PSPEmitterTemplate tmpl;
        const char* name = em->Attribute("name");
        if (name) {
            strncpy(tmpl.m_Name, name, sizeof(tmpl.m_Name) - 1);
            tmpl.m_Hash = StringHash(name);
        }

        if (tinyxml2::XMLElement* life = em->FirstChildElement("life")) {
            const char* t = life->GetText();
            // Binary @ 0x0013d558 uses divisor 78.0 (DAT 0x404e000000000000).
            tmpl.m_MaxLifetime = t ? (float)(atof(t) / 78.0f) : 0.0f;
        }

        for (tinyxml2::XMLElement* ps = em->FirstChildElement("particleSet");
             ps != nullptr;
             ps = ps->NextSiblingElement("particleSet")) {

            PSPParticleSet set = {};
            set.m_pTemplate = nullptr;

            // Store template index encoded as a pointer -- patched below after
            // all emitter templates are built (mirrors binary post-load patch).
            if (const char* psName = ps->Attribute("name")) {
                std::unordered_map<uint32_t, size_t>::iterator it = nameToIndex.find(StringHash(psName));
                if (it != nameToIndex.end()) {
                    set.m_pTemplate = reinterpret_cast<PSPParticleTemplate*>(
                        static_cast<uintptr_t>(it->second + 1)); // +1 so 0 == "none"
                }
            }

            if (tinyxml2::XMLElement* time = ps->FirstChildElement("time")) {
                time->QueryFloatAttribute("start", &set.m_TimeStart);
                time->QueryFloatAttribute("stop",  &set.m_TimeStop);
            }

            if (tinyxml2::XMLElement* num = ps->FirstChildElement("particleNumber")) {
                int init = 0;
                num->QueryIntAttribute("init", &init);
                set.m_InitCount = (uint8_t)init;
                num->QueryFloatAttribute("perSec", &set.m_PerSec);
            }

            if (tinyxml2::XMLElement* vel = ps->FirstChildElement("velocity")) {
                ParseVec3(vel->Attribute("min"), set.m_VelocityMin);
                ParseVec3(vel->Attribute("max"), set.m_VelocityMax);
            }

            tmpl.m_Sets.push_back(set);
        }
        tmpl.m_NumSets = (uint8_t)tmpl.m_Sets.size();

        m_EmitterTemplates.push_back(tmpl);
    }

    // Post-load patch: replace encoded index (as pointer) with real pointers
    // into the now-stable m_ParticleTemplates vector.
    for (std::vector<PSPEmitterTemplate>::iterator eit = m_EmitterTemplates.begin(); eit != m_EmitterTemplates.end(); ++eit) {
        PSPEmitterTemplate& emit = *eit;
        for (std::vector<PSPParticleSet>::iterator sit = emit.m_Sets.begin(); sit != emit.m_Sets.end(); ++sit) {
            PSPParticleSet& set = *sit;
            uintptr_t encoded = reinterpret_cast<uintptr_t>(set.m_pTemplate);
            if (encoded == 0) continue;
            size_t idx = encoded - 1;
            set.m_pTemplate = (idx < m_ParticleTemplates.size())
                              ? &m_ParticleTemplates[idx]
                              : nullptr;
        }
    }

    LOG_DEBUG("PSPParticleManager", "Loaded %zu particle templates, %zu emitter templates from %s",
              m_ParticleTemplates.size(), m_EmitterTemplates.size(), xmlPath);
    return true;
}

void PSPParticleManager::Clear() {
    ClearEmitters();
}

// v1.6.1 PSPParticleManager::ClearEmitters @0x0010e258 (thunk) — drain
// m_pActiveEmitters to pool; reset m_FreeHead=1 + re-thread free-list;
// zero every template's m_ParticleHead/m_ParticleCount.
void PSPParticleManager::ClearEmitters() {
    // Drain the active emitter list back to pool, null all caller back-pointers.
    while (m_pActiveEmitters) {
        PSPParticleEmitter* node = m_pActiveEmitters;
        m_pActiveEmitters = node->m_Next;
        if (node->m_pRefPtr) *node->m_pRefPtr = nullptr;
        if (m_pEmitterPool) m_pEmitterPool->Push(node);
    }

    // Re-thread the particle free-list (slots 1..1023; sentinel at 0).
    if (m_pParticles) {
        for (int i = 1; i <= 1022; ++i) {
            m_pParticles[i].m_NextLink = (uint16_t)(i + 1);
        }
        m_pParticles[1023].m_NextLink = 0;
        m_FreeHead = 1;
    }

    // Zero per-template live-list heads and counts.
    for (size_t i = 0; i < m_EmitterTemplates.size(); ++i) {
        m_EmitterTemplates[i].m_ParticleHead  = 0;
        m_EmitterTemplates[i].m_ParticleCount = 0;
    }
}
