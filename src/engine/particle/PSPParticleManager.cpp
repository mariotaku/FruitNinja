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

// GL blend enum from string. Matches LoadFile: "SrcAlpha"→0x302,
// "InvSrcAlpha"→0x303, "One"→0x01.
static uint16_t ParseBlendEnum(const char* s) {
    if (!s) return 0;
    if (!strcmp(s, "SourceAlpha") || !strcmp(s, "SrcAlpha")) return 0x302;
    if (!strcmp(s, "InverseSourceAlpha") || !strcmp(s, "InvSrcAlpha")) return 0x303;
    if (!strcmp(s, "One")) return 0x01;
    return 0;
}

// Directory containing the XML, used to locate sibling .tex files.
static std::string DirOf(const char* path) {
    std::string p(path);
    size_t pos = p.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string(".") : p.substr(0, pos);
}

PSPParticleManager::PSPParticleManager() {
}

PSPParticleManager::~PSPParticleManager() {
    Destroy();
}

// Binary @ 0x001155d0 — release tex refs, ClearEmitters, free 3 owned blocks.
void PSPParticleManager::Destroy() {
    // 1. Release texture SmartPtr refs on all particle templates
    //    (binary: SmartPtr::SetNull on each template+0xAC).
    for (size_t i = 0; i < m_ParticleTemplates.size(); ++i) {
        m_ParticleTemplates[i].m_Texture.SetNull();
    }
    // 2. Drain active emitters (matches binary step 2: ClearEmitters).
    ClearEmitters();
    // 3. Free owned storage blocks (binary: free m_pTemplates, m_pParticleArray-8,
    //    m_EmitterPool). Port equivalent: clear the owning vectors.
    m_ParticleTemplates.clear();
    m_EmitterTemplates.clear();
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

// Binary @ 0x001149e0 — pop from pool, init defaults, prepend to m_ActiveList.
PSPParticleEmitter* PSPParticleManager::AddEmitter(uint32_t hash,
                                                   PSPParticleEmitter** ppRef,
                                                   bool updateWhenPaused) {
    // Binary pool capacity is 120 (Create(this, 0x78) at 0x00115fc0); admit
    // predicate is `used + 1 < cap` -> at most 119 live emitters. Pool-full
    // path returns nullptr WITHOUT zeroing *ppRef; binary only zeros *ppRef
    // on hash-miss inside the pool-OK branch.
    // DIFFERS-BY-DESIGN: binary uses MemoryPool<PSPParticleEmitter>; port
    // uses std::vector but mirrors the same admit/return semantics.
    static const size_t POOL_CAPACITY = 119;
    if (m_Emitters.size() >= POOL_CAPACITY) {
        return nullptr;
    }

    const PSPEmitterTemplate* tmpl = FindTemplate(hash);
    if (!tmpl) {
        if (ppRef) *ppRef = nullptr;
        return nullptr;
    }

    m_Emitters.push_back(new PSPParticleEmitter());
    m_ParticleLists.push_back(std::vector<PSPParticle>());
    PSPParticleEmitter& e = *m_Emitters.back();
    // All defaults match the binary's explicit init block:
    e.m_Timer = 0.0f;
    e.m_Pos = Vec3(0, 0, 0);
    e.m_Vel = Vec3(0, 0, 0);
    e.m_DirSin = 0.0f;
    e.m_TimeScale = 1.0f;
    e.m_field24 = 1.0f;
    e.m_ScaleX = 1.0f;
    e.m_DirCos = 1.0f;
    e.m_field34 = 1.0f;
    e.m_field38 = 0;
    e.m_ParticleHead = 1;
    e.m_bUpdateWhenPaused = updateWhenPaused;
    e.m_pTemplate = tmpl;
    e.m_pRefPtr = ppRef;
    if (ppRef) *ppRef = &e;
    return &e;
}

// Binary @ 0x00114934 — find by ptr, unlink, clear back-ref, return to pool.
void PSPParticleManager::ClearEmitter(PSPParticleEmitter* emitter) {
    if (!emitter) return;
    for (size_t i = 0; i < m_Emitters.size(); ++i) {
        if (m_Emitters[i] == emitter) {
            if (emitter->m_pRefPtr) *emitter->m_pRefPtr = nullptr;
            delete m_Emitters[i];
            m_Emitters.erase(m_Emitters.begin() + i);
            m_ParticleLists.erase(m_ParticleLists.begin() + i);
            return;
        }
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

// Matches AddParticle (0x115644) — spawn one particle from (emitter, set).
// setIdx is the index of the set in emitter.m_pTemplate->m_Sets; stored in
// p.m_field44 so the draw pass can look up the per-particle PSPParticleTemplate
// without a per-particle pointer field (which would exceed binary struct size).
static void SpawnParticle(PSPParticleEmitter& emitter, const PSPParticleSet& set,
                          std::vector<PSPParticle>& particles, int32_t setIdx) {
    const PSPParticleTemplate* tmpl = set.m_pTemplate;

    PSPParticle p;
    p.m_Pos = emitter.m_Pos;
    p.m_field44 = setIdx;

    // Velocity: set-level min/max (randomized per component), halved, then
    // added to emitter vel. The `* 0.5f` matches the binary AddParticle
    // @ 0x115644: after picking the random set-level velocity it does an
    // unconditional `local_78.xyz *= 0.5f` before storing onto the particle.
    // Applies to all shape modes (not just two-player as originally guessed).
    float vx = RandRange(set.m_VelocityMin[0], set.m_VelocityMax[0]) * 0.5f;
    float vy = RandRange(set.m_VelocityMin[1], set.m_VelocityMax[1]) * 0.5f;
    float vz = RandRange(set.m_VelocityMin[2], set.m_VelocityMax[2]) * 0.5f;

    // 2D rotation of the XY velocity by the emitter's (cos, sin) pair stored
    // in m_DirCos (+0x2c) and m_DirSin (+0x30). Matches the binary AddParticle
    // @ 0x00115644 -- used to rotate fruit-impact particles so chunks spray
    // along the blade direction. Identity when the caller leaves defaults (cos=1, sin=0).
    const float cosA = emitter.m_DirCos;
    const float sinA = emitter.m_DirSin;
    const float rvx = vx * cosA + vy * sinA;
    const float rvy = vy * cosA - sinA * vx;

    p.m_Vel.x = emitter.m_Vel.x + rvx;
    p.m_Vel.y = emitter.m_Vel.y + rvy;
    p.m_Vel.z = emitter.m_Vel.z + vz;

    if (tmpl) {
        // NOTE: template m_VelocityMin/Max are NOT an initial-velocity range;
        // they are a per-component per-frame velocity LERP (damping) factor
        // applied each tick during UpdateEmitter integration. See binary
        // Draw @ 0x114c64.

        p.m_Gravity.x = RandRange(tmpl->m_GravityMin[0], tmpl->m_GravityMax[0]);
        p.m_Gravity.y = RandRange(tmpl->m_GravityMin[1], tmpl->m_GravityMax[1]);
        p.m_Gravity.z = RandRange(tmpl->m_GravityMin[2], tmpl->m_GravityMax[2]);

        p.m_Life = tmpl->m_StartTime;  // template's "<life>/60" seconds

        // Two-segment size lerp — random per stop so each particle gets its
        // own variation on start/mid/end.
        p.m_SizeStart = RandRange((float)tmpl->m_SizeStartMin, (float)tmpl->m_SizeStartMax);
        p.m_SizeMid   = RandRange((float)tmpl->m_SizeMidMin,   (float)tmpl->m_SizeMidMax);
        p.m_SizeEnd   = RandRange((float)tmpl->m_SizeEndMin,   (float)tmpl->m_SizeEndMax);

        // Spin rate lerp: template has start/end min/max int16 ranges.
        // Binary AddParticle @ 0x115644 multiplies the LERP'd int16 by
        // DAT_00115b64 = 182.0f (degrees -> 16-bit angle-index: 65536/360 ~= 182)
        // and stores it as an int16 angle-table index that is added to
        // field_0x28 each 1/60s tick. Convert to rad/sec for the port's
        // float-radian integration:
        //   rad_per_sec = int16 * (182/65536) * 2pi * 60
        //              = int16 * 6.28318 * 60 / 360
        //              = int16 * 1.0472
        // Each particle gets its own random start/end rate.
        static const float SPIN_INT16_TO_RAD_PER_SEC =
            (182.0f / 65536.0f) * 6.2831853f * 60.0f;
        p.m_SpinStart = RandRange((float)tmpl->m_SpinStartMin,
                                  (float)tmpl->m_SpinStartMax) * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_SpinEnd   = RandRange((float)tmpl->m_SpinEndMin,
                                  (float)tmpl->m_SpinEndMax)   * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_Rotation = RandRange(tmpl->m_AngleMin, tmpl->m_AngleMax);

        // RotCycle -- oscillating rotation offset. speedStart/End from
        // rotateCycle XML (stored in m_FrictionSpeed*). Amplitude lerps
        // from start to end base.
        p.m_RotCycleRate  = 0.5f * (tmpl->m_FrictionSpeedStart +
                                    tmpl->m_FrictionSpeedEnd);
        p.m_RotCycleAmp   = tmpl->m_FrictionOffsetMin * (3.14159265f / 180.0f);
        p.m_RotCyclePhase = Rand01() * 6.2831853f;

        // CycleX / CycleY — size modulation rates. m_CycleXStart/End are
        // int16 "cycles/sec" values parsed from <cycleX startMin endMin>.
        p.m_CycleXRate  = 0.5f * ((float)tmpl->m_CycleXStart + (float)tmpl->m_CycleXEnd);
        p.m_CycleYRate  = 0.5f * ((float)tmpl->m_CycleYStart + (float)tmpl->m_CycleYEnd);
        p.m_CycleXPhase = Rand01() * 6.2831853f;
        p.m_CycleYPhase = Rand01() * 6.2831853f;

        // Shape-type branching (matches AddParticle 0x115644):
        //   0 = Point     -- no extra init (pos = emitter.pos, vel = rotated set vel)
        //   1 = Vertex    -- start half a velocity step behind the emitter
        //   2 = Direction -- rotate particle to face its own velocity
        //   3 = Angular   -- swap pos.x/y, mirror gravity+vel by emitter quadrant
        switch (tmpl->m_Shape) {
            case 1: // Vertex
                p.m_Pos.x -= p.m_Vel.x;
                p.m_Pos.y -= p.m_Vel.y;
                p.m_Pos.z -= p.m_Vel.z;
                break;
            case 2: // Direction
                p.m_Rotation += atan2f(p.m_Vel.y, p.m_Vel.x);
                break;
            case 3: { // Angular -- binary @ 0x00115644 shape==3 branch
                // TODO: 0x00115644 shape==3 branch -- quadrant-mirror logic needs follow-up RE.
                // RE doc summary: swap pos.x/pos.y, mirror gravity by emitter quadrant
                // (sign of emitter.m_field38), mirror vel.x by particle pos.x sign.
                // No XML in particles_fast/slow.xml uses Angular; no-op fallback is safe.
                break;
            }
            default:
                break;
        }
    } else {
        p.m_Life = 1.0f;
        p.m_SizeStart = p.m_SizeMid = p.m_SizeEnd = 8.0f;
    }

    particles.push_back(p);
}

// Matches PSPParticleEmitter::Update (0x115d9c).
static void UpdateEmitter(PSPParticleEmitter& e, float dt, std::vector<PSPParticle>& particles) {
    const PSPEmitterTemplate* et = e.m_pTemplate;
    if (!et) return;

    const float currentTime = e.m_Timer;
    const float newTime = currentTime + dt * e.m_TimeScale;

    // Spawn pass -- for each set, check window and integrate rate
    for (int32_t si = 0; si < (int32_t)et->m_Sets.size(); ++si) {
        const PSPParticleSet& set = et->m_Sets[(size_t)si];
        const float startT = set.m_TimeStart;
        const float stopT  = set.m_TimeStop;

        // Continuous rate: only within [startT, stopT] (stopT==0 -> no limit).
        if (startT <= currentTime && (stopT == 0.0f || currentTime <= stopT)) {
            const float rate = set.m_PerSec;
            if (rate > 0.0f) {
                int desired = (int)(rate * ((currentTime + dt * e.m_TimeScale) - startT))
                            - (int)(rate * (currentTime - startT));
                for (int i = 0; i < desired; ++i) SpawnParticle(e, set, particles, si);
            }
        }

        // Burst on first frame crossing startT
        if (currentTime <= startT && startT < newTime) {
            for (int i = 0; i < (int)set.m_InitCount; ++i) SpawnParticle(e, set, particles, si);
            if (e.m_TimeScale == 0.0f) e.m_Timer += dt;
        }
    }

    // Physics pass -- age + integrate all particles
    for (size_t i = 0; i < particles.size(); ) {
        PSPParticle& p = particles[i];
        p.m_Age += dt;
        if (p.m_Age >= p.m_Life) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        // Binary Draw @ 0x114c64 integration:
        //   vel = (vel + gravity*dt) * lerp(tmpl.velMin, tmpl.velMax, t)
        //   pos = pos + vel*dt
        // The template's velMin/Max fields are a per-component per-frame
        // LERP (damping) factor -- NOT an initial velocity range.
        const float t = (p.m_Life > 0.0f) ? (p.m_Age / p.m_Life) : 0.0f;
        p.m_Vel += p.m_Gravity * dt;
        // Look up particle template from set index stored in m_field44.
        if (p.m_field44 >= 0 && (size_t)p.m_field44 < et->m_Sets.size()) {
            const PSPParticleTemplate* pt = et->m_Sets[(size_t)p.m_field44].m_pTemplate;
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
        // Spin rate lerp start->end over life, then integrate.
        const float spin = p.m_SpinStart + (p.m_SpinEnd - p.m_SpinStart) * t;
        p.m_Rotation += spin * dt;
        // Cycle accumulators (RotCycle + CycleX/Y). Rates are in cycles/s so
        // convert to radians by *2pi.
        p.m_RotCyclePhase += p.m_RotCycleRate * dt * 6.2831853f;
        p.m_CycleXPhase   += p.m_CycleXRate   * dt * 6.2831853f;
        p.m_CycleYPhase   += p.m_CycleYRate   * dt * 6.2831853f;
        ++i;
    }

    // Advance emitter state (matches binary)
    e.m_Timer = newTime;
    e.m_Pos += e.m_Vel;
}

// Matches PSPEmitterTemplate::Ends (0x00114884). Returns true if the
// template is "naturally terminating" — i.e. every set either has a positive
// TimeStop (finite window) OR zero continuous spawn rate (burst-only). Used
// by Manager::Update to reap infinite-lifetime emitters whose sets have all
// wound down. NOTE: this is a *static* property of the template, not runtime
// state — it does not look at timer or particle counts.
static bool EmitterTemplateEnds(const PSPEmitterTemplate* t) {
    if (!t) return true;
    for (std::vector<PSPParticleSet>::const_iterator cit = t->m_Sets.begin(); cit != t->m_Sets.end(); ++cit) {
        if (cit->m_TimeStop <= 0.0f && cit->m_PerSec > 0.0f) return false;
    }
    return true;
}

// Binary @ 0x00115ed8 — update all active emitters; skip when paused &&
// !emitter->m_bUpdateWhenPaused.
// TODO: wire paused from PauseScreen when that's ported (callers pass false for now).
void PSPParticleManager::Update(float dt, bool paused) {
    for (size_t i = 0; i < m_Emitters.size(); ) {
        PSPParticleEmitter& e = *m_Emitters[i];
        const PSPEmitterTemplate* et = e.m_pTemplate;
        std::vector<PSPParticle>& particles = m_ParticleLists[i];

        // Tick active emitters
        if (e.m_ParticleHead != 0 && e.m_TimeScale != 0.0f &&
            (!paused || e.m_bUpdateWhenPaused)) {
            UpdateEmitter(e, dt, particles);
        }

        // Keep-alive rule from binary Manager::Update:
        //   keep if timer < maxLifetime
        //        OR (maxLifetime <= 0 AND !Ends(template))
        // Meaning: finite-lifetime emitters die at maxLifetime; infinite
        // emitters (maxLifetime <= 0) only die when the template itself
        // signals termination (i.e. no set spawns continuously forever).
        // Bomb_smoke has an infinite set (TimeStop=0, PerSec=50), so
        // Ends()==false and it stays alive until explicit ClearEmitter.
        // Port addition: also keep alive while live particles remain, so
        // dying emitters finish playing out their last spawn.
        bool keep = true;
        if (et) {
            const bool naturallyInfinite = !EmitterTemplateEnds(et);
            if (et->m_MaxLifetime > 0.0f) {
                keep = (e.m_Timer < et->m_MaxLifetime) || !particles.empty();
            } else {
                keep = naturallyInfinite || !particles.empty();
            }
        }
        if (!keep) {
            // Binary @ 0x00115ed8 reap path: null the caller back-pointer so callers
            // that passed &member (Coin, ScreenEffect, etc.) see nullptr on next access.
            // Fruit passes nullptr for ppRef and relies on naturally-infinite templates
            // (see Note comments in Fruit.cpp CollisionResponse / SetTrailParticles).
            if (e.m_pRefPtr) *e.m_pRefPtr = nullptr;
            delete m_Emitters[i];
            m_Emitters.erase(m_Emitters.begin() + i);
            m_ParticleLists.erase(m_ParticleLists.begin() + i);
            continue;
        }
        ++i;
    }
}

// -----------------------------------------------------------------------------
// Draw — matches PSPParticleManager::Draw (0x00114c64, ~382 lines).
// Simplified port: for each emitter with particles, build a textured quad per
// particle into a scratch vertex buffer, then DrawTriList. Blend mode comes
// from template->m_BlendMode (destination factor; source is GL_SRC_ALPHA).
// Per-particle state uses the simpler struct from task #8 (not the full
// 0xA4-byte binary layout — see docs/engine/particles.md).
// -----------------------------------------------------------------------------
static inline uint32_t PackBGRA(const uint8_t c[4]) {
    // QUADCUSTOMVERTEX.colour is read as 4 × GL_UNSIGNED_BYTE in the shader
    // (normalized). Memory order matches the vertex attrib — so we pack the
    // bytes in the same BGRA order as the template's colour fields.
    return (uint32_t)c[0]
         | ((uint32_t)c[1] << 8)
         | ((uint32_t)c[2] << 16)
         | ((uint32_t)c[3] << 24);
}

// Lerp each component of an 8-bit BGRA tuple. `t` in [0,1], 0=start.
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

// ASM-verified: 2026-05-06T16:00 binary @ 0x00114c64 (asm-inspector)
// Binary @ 0x00114c64 — fused integrate+render. Port splits into Update/Draw;
// dt and paused are unused in the Draw body (integration happens in Update).
// DIFFERS: binary fuses per-particle integrate+render into one pass; port
// separates them so Update/Draw can be called independently.
// No glBlendFunc state restore at function exit -- binary leaves blend
// state at whatever the last template configured.
void PSPParticleManager::Draw(float dt, bool paused, int layer) {
    (void)dt;
    (void)paused;
    if (m_Emitters.empty()) return;

    // Reset world matrix + upload MVP so DrawTriList uses the current ortho.
    // Matches binary Draw 0x114c64: "MatrixStack::Reset + UploadCurrentMatrices".
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    static std::vector<QUADCUSTOMVERTEX> s_verts;
    const PSPParticleTemplate* curTmpl = nullptr;

    for (size_t ei = 0; ei < m_Emitters.size(); ++ei) {
        PSPParticleEmitter& e = *m_Emitters[ei];
        std::vector<PSPParticle>& particles = m_ParticleLists[ei];
        if (particles.empty()) continue;
        const PSPEmitterTemplate* et = e.m_pTemplate;

        for (std::vector<PSPParticle>::iterator pit = particles.begin(); pit != particles.end(); ++pit) {
            PSPParticle& p = *pit;

            // Resolve per-particle template via set index stored in m_field44.
            const PSPParticleTemplate* pTmpl = 0;
            if (et && p.m_field44 >= 0 && (size_t)p.m_field44 < et->m_Sets.size()) {
                pTmpl = et->m_Sets[(size_t)p.m_field44].m_pTemplate;
            }

            // Layer filter: binary draws only particles whose template's
            // m_UseDepth matches the requested layer.
            if (pTmpl && pTmpl->m_UseDepth != layer) continue;

            // Group flush on template change (batches DrawTriList per texture)
            if (pTmpl != curTmpl) {
                FlushParticleVerts(s_verts, curTmpl);
                curTmpl = pTmpl;
            }

            const float life = p.m_Life > 0.0f ? p.m_Life : 1.0f;
            float t = p.m_Age / life;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

            // Two-segment colour + size lerp: start->mid for t in [0,0.5),
            // mid->end for t in [0.5,1]. Matches binary Draw piecewise linear.
            // Colour looked up from template (ColourStartMin/MidMin/EndMin).
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

            // CycleX / CycleY size modulation — cos wave per axis
            if (p.m_CycleXRate != 0.0f) hx *= cosf(p.m_CycleXPhase);
            if (p.m_CycleYRate != 0.0f) hy *= cosf(p.m_CycleYPhase);

            // RotCycle oscillation — sin wave adds to base rotation
            float effectiveRot = p.m_Rotation;
            if (p.m_RotCycleAmp != 0.0f)
                effectiveRot += p.m_RotCycleAmp * sinf(p.m_RotCyclePhase);

            const float ca = cosf(effectiveRot);
            const float sa = sinf(effectiveRot);
            const float dxX =  ca * hx, dxY = sa * hx;
            const float dyX = -sa * hy, dyY = ca * hy;

            // Positions are in the binary-centred ortho space.
            float px = p.m_Pos.x;
            float py = p.m_Pos.y;
            const float pz = p.m_Pos.z;

            // Grid-lock: snap pos to cell centres when the template declares
            // non-zero cell sizes. Used by rim_spark (menu rim flash).
            // Matches binary Draw 0x114c64 gridLock block.
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
        }
    }
    FlushParticleVerts(s_verts, curTmpl);
    // Binary leaves blend state at whatever the last template set; subsequent
    // draws are responsible for configuring their own. Don't restore here.
}

// Binary @ 0x00115f60 — load particle templates from XML.
// texCategory is prepended to texture filenames: snprintf("%s/%s.tex", texCategory, name).
// outNames (optional): caller-allocated array receiving each <particleTemplate name="...">;
// strings are strcpy'd in parse order.
// Returns true on success.
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

    m_ParticleTemplates.clear();
    m_EmitterTemplates.clear();

    // texCategory is prepended to texture filenames per binary snprintf pattern.
    // DIFFERS: binary uses texCategory/"texName".tex; old port used DirOf(xmlPath).
    const std::string texCatStr(texCategory ? texCategory : "");

    // --- First loop: <particleTemplate> --------------------------------------
    std::unordered_map<uint32_t, size_t> nameToIndex;

    for (tinyxml2::XMLElement* pt = body->FirstChildElement("particleTemplate");
         pt != nullptr;
         pt = pt->NextSiblingElement("particleTemplate")) {

        PSPParticleTemplate tmpl = {};
        // m_VelocityMin/Max on the TEMPLATE are not initial-velocity — they
        // are a per-component per-frame velocity LERP (damping/amplification)
        // factor used by UpdateEmitter integration. Default to identity (1.0)
        // so templates that omit <velocity> get no damping. See binary Draw
        // @ 0x114c64 integration.
        tmpl.m_VelocityMin[0] = 1.0f; tmpl.m_VelocityMin[1] = 1.0f; tmpl.m_VelocityMin[2] = 1.0f;
        tmpl.m_VelocityMax[0] = 1.0f; tmpl.m_VelocityMax[1] = 1.0f; tmpl.m_VelocityMax[2] = 1.0f;

        const char* name = pt->Attribute("name");
        uint32_t hash = name ? StringHash(name) : 0;
        if (name) nameToIndex[hash] = m_ParticleTemplates.size();

        pt->QueryIntAttribute("useDepth", &tmpl.m_UseDepth);

        // <life> — stored as seconds after divide by 60
        if (auto* e = pt->FirstChildElement("life")) {
            const char* t = e->GetText();
            tmpl.m_StartTime = t ? (float)(atof(t) / 60.0) : 0.0f;
        }

        // <type> — 0=Point, 1=Vertex, 2=Direction, 3=Angular
        if (auto* e = pt->FirstChildElement("type")) {
            const char* t = e->GetText();
            if (t) {
                if      (!strcmp(t, "Point"))     tmpl.m_Shape = 0;
                else if (!strcmp(t, "Vertex"))    tmpl.m_Shape = 1;
                else if (!strcmp(t, "Direction")) tmpl.m_Shape = 2;
                else if (!strcmp(t, "Angular"))   tmpl.m_Shape = 3;
            }
        }
        // <system> — 0=Local, 1=Global
        if (auto* e = pt->FirstChildElement("system")) {
            const char* t = e->GetText();
            if (t && !strcmp(t, "Global")) tmpl.m_CoordSystem = 1;
        }

        // <gravity> — "x y z" (default min, max falls back to min)
        if (auto* e = pt->FirstChildElement("gravity")) {
            ParseVec3(e->GetText(), tmpl.m_GravityMin);
            memcpy(tmpl.m_GravityMax, tmpl.m_GravityMin, sizeof(tmpl.m_GravityMin));
        }
        if (auto* e = pt->FirstChildElement("gravity_max")) {
            ParseVec3(e->GetText(), tmpl.m_GravityMax);
        }

        // <velocity min="..." max="..."/>
        if (auto* e = pt->FirstChildElement("velocity")) {
            ParseVec3(e->Attribute("min"), tmpl.m_VelocityMin);
            ParseVec3(e->Attribute("max"), tmpl.m_VelocityMax);
        }

        // <color startMin="R G B A" startMax=".." endMin=".." endMax=".."/>
        if (auto* e = pt->FirstChildElement("color")) {
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
        if (auto* e = pt->FirstChildElement("size")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeStartMin = (uint8_t)v;
            if (e->QueryIntAttribute("startMax", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeStartMax = (uint8_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeEndMin   = (uint8_t)v;
            if (e->QueryIntAttribute("endMax",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SizeEndMax   = (uint8_t)v;
            tmpl.m_SizeMidMin = (uint8_t)(((int)tmpl.m_SizeStartMin + (int)tmpl.m_SizeEndMin) >> 1);
            tmpl.m_SizeMidMax = (uint8_t)(((int)tmpl.m_SizeStartMax + (int)tmpl.m_SizeEndMax) >> 1);
        }

        // <spin startMin=".." startMax=".." endMin=".." endMax=".."/>
        if (auto* e = pt->FirstChildElement("spin")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinStartMin = (int16_t)v;
            if (e->QueryIntAttribute("startMax", &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinStartMax = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinEndMin   = (int16_t)v;
            if (e->QueryIntAttribute("endMax",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_SpinEndMax   = (int16_t)v;
        }

        // <cycleX startMin="a" startMax="b" endMin="c" endMax="d"/>
        // Rate range: start rate lerp [startMin, startMax], end rate lerp
        // [endMin, endMax]. Modulates size_x via cos(phase) in Draw.
        if (auto* e = pt->FirstChildElement("cycleX")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleXStart = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleXEnd   = (int16_t)v;
        }
        if (auto* e = pt->FirstChildElement("cycleY")) {
            int v = 0;
            if (e->QueryIntAttribute("startMin", &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleYStart = (int16_t)v;
            if (e->QueryIntAttribute("endMin",   &v) == tinyxml2::XML_SUCCESS) tmpl.m_CycleYEnd   = (int16_t)v;
        }

        // <gridLock x="16" y="16"/> -- snap-to-grid lock per axis, applied in Draw.
        // Binary @ 0x00115f60 reads element attrs into template +0x64 / +0x68.
        // Used by pixel_blade and rim_spark templates.
        // ASM-verified: 2026-05-09 binary @ 0x00115f60 (re-analyst)
        {
            tinyxml2::XMLElement* e = pt->FirstChildElement("gridLock");
            if (e) {
                e->QueryFloatAttribute("x", &tmpl.m_GridLockStart);
                e->QueryFloatAttribute("y", &tmpl.m_GridLockEnd);
            }
        }

        // <friction start="x y z" end="x y z"/> -- per-component per-frame velocity
        // LERP factor (damping). Binary stores into template +0x08..+0x1C
        // (m_VelocityMin/Max) -- the same slots the port's UpdateEmitter integration
        // already reads. Templates that omit this keep the identity defaults set above.
        // pixel_1/2/3 use start="1 1 0" end="0 0 0" -> fast decel to stop.
        {
            tinyxml2::XMLElement* e = pt->FirstChildElement("friction");
            if (e) {
                ParseVec3(e->Attribute("start"), tmpl.m_VelocityMin);
                ParseVec3(e->Attribute("end"),   tmpl.m_VelocityMax);
            }
        }

        // <rotateCycle start="base" end="endBase" speedStart="rate1" speedEnd="rate2"/>
        // Quadratic rotation accumulator modulating m_Rotation with sin.
        // Port stores the four parameters in the m_Friction* float slots so
        // we don't need to add new template fields.
        if (auto* e = pt->FirstChildElement("rotateCycle")) {
            float fv = 0.0f;
            if (e->QueryFloatAttribute("speedStart", &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionSpeedStart = fv;
            if (e->QueryFloatAttribute("speedEnd",   &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionSpeedEnd   = fv;
            if (e->QueryFloatAttribute("start",      &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionOffsetMin  = fv;   // amplitude base
            if (e->QueryFloatAttribute("end",        &fv) == tinyxml2::XML_SUCCESS)
                tmpl.m_FrictionOffsetMax  = fv;
            else
                tmpl.m_FrictionOffsetMax  = tmpl.m_FrictionOffsetMin;
        }

        // <SourceBlend>, <DestinationBlend>
        if (auto* e = pt->FirstChildElement("SourceBlend"))
            tmpl.m_BlendMode = ParseBlendEnum(e->GetText());
        if (auto* e = pt->FirstChildElement("DestinationBlend"))
            tmpl.m_BlendMode = ParseBlendEnum(e->GetText());

        // <texture name="..."/> — load via TextureManager
        if (auto* e = pt->FirstChildElement("texture")) {
            const char* texName = e->Attribute("name");
            if (texName && *texName) {
                char buf[256];
                // Binary @ 0x115f60: snprintf("%s/%s.tex", texCategory, texName)
                snprintf(buf, sizeof(buf), "%s/%s.tex", texCatStr.c_str(), texName);
                tmpl.m_Texture = Mortar::TextureManager::GetInstance().Load(buf);
                if (tmpl.m_Texture.IsValid()) {
                    const float tw = (float)tmpl.m_Texture->m_Width;
                    const float th = (float)tmpl.m_Texture->m_Height;
                    if (th > 0.0f) tmpl.m_AspectRatio = tw / th;
                }
            }
        }

        // outNames: if provided, strcpy the template name into outNames[i].
        if (outNames && name) {
            strcpy(outNames[m_ParticleTemplates.size()], name);
        }
        m_ParticleTemplates.push_back(tmpl);
    }

    // --- Second loop: <emitter> ---------------------------------------------
    for (tinyxml2::XMLElement* em = body->FirstChildElement("emitter");
         em != nullptr;
         em = em->NextSiblingElement("emitter")) {

        PSPEmitterTemplate tmpl = {};
        const char* name = em->Attribute("name");
        if (name) {
            strncpy(tmpl.m_Name, name, sizeof(tmpl.m_Name) - 1);
            tmpl.m_Hash = StringHash(name);
        }

        if (tinyxml2::XMLElement* life = em->FirstChildElement("life")) {
            const char* t = life->GetText();
            tmpl.m_MaxLifetime = t ? (float)(atof(t) / 60.0) : 0.0f;
        }

        for (tinyxml2::XMLElement* ps = em->FirstChildElement("particleSet");
             ps != nullptr;
             ps = ps->NextSiblingElement("particleSet")) {

            PSPParticleSet set = {};
            set.m_pTemplate = nullptr;

            // Store template index encoded as a pointer — patched below after
            // all emitter templates are built (mirrors binary post-load patch).
            if (const char* psName = ps->Attribute("name")) {
                auto it = nameToIndex.find(StringHash(psName));
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
    // into the now-stable m_ParticleTemplates vector. Matches binary flow:
    // "*word0 = m_pTemplates + index * 0xB8".
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
    for (size_t i = 0; i < m_Emitters.size(); ++i) delete m_Emitters[i];
    m_Emitters.clear();
    m_ParticleLists.clear();
}

// Binary @ 0x00114974 — drain active list + reset particle free-list + zero
// per-template live-list heads. Port collapses 2-3 since particles live
// in the manager's parallel list. // DIFFERS: binary uses 3 separate lists; port uses vector.
void PSPParticleManager::ClearEmitters() {
    for (size_t i = 0; i < m_Emitters.size(); ++i) delete m_Emitters[i];
    m_Emitters.clear();
    m_ParticleLists.clear();
}
