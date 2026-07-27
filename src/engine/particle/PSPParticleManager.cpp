#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include "math/Random.h"
#include "render/Renderer.h"
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

// Parse "r g b a" ints (0..31) into RGBA bytes, scaling by 255/31.
// Matches binary's 255.0f/31.0f multiplier at DAT_001166c4.
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

// GL blend enum from string.
static uint16_t ParseBlendEnum(const char* s) {
    if (!s) return 0;
    if (!strcmp(s, "SourceAlpha") || !strcmp(s, "SrcAlpha")) return 0x302;
    if (!strcmp(s, "InverseSourceAlpha") || !strcmp(s, "InvSrcAlpha")) return 0x303;
    if (!strcmp(s, "One")) return 0x01;
    return 0;
}

// v1.6.1 PSPParticleManager::PSPParticleManager @0x0013bf40 — manager ctor.
// Sets m_GlobalPullRadius=0.0 (+0x00), m_GlobalTimeScale=1.0 (+0x04); NULLs all owned pointers.
// ASM-spec v1.6.1 PSPParticleManager @0x00013bf40 (non-polymorphic; +0x00 = float
//   m_GlobalPullRadius, not a vptr): the binary ctor writes this->__vptr = 0, i.e. it zeroes
//   +0x00 as a data field (the vortex pull radius), not a vtable pointer.
PSPParticleManager::PSPParticleManager()
    : m_GlobalPullRadius(0.0f)
    , m_GlobalTimeScale(1.0f)
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

// Binary @ 0x001148dc
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
    e->m_SizeBias = 1.0f;
    e->m_SpinScale = 1.0f;
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
// Update helpers
// -----------------------------------------------------------------------------
static inline float Rand01() {
    return (float)rand() / (float)RAND_MAX;
}
static inline float RandRange(float lo, float hi) {
    return lo + (hi - lo) * Rand01();
}

// Quadrant-mirror sign (AddParticle @ 0x001157c0):
//   v > 0 -> -1, v < 0 -> +1, v == 0 -> 0.
static inline float QuadrantMirror(float v) {
    if (v > 0.0f) return -1.0f;
    if (v < 0.0f) return 1.0f;
    return 0.0f;
}

// v1.6.1 PSPParticleManager::AddParticle @0x13c554 — pop free slot, init particle,
// push onto template live-list (head at particle_template+0x04).
// Returns 0 on failure (no free slots).
static uint16_t AddParticle(PSPParticle* buf, uint16_t& freeHead,
                             PSPParticleTemplate* tmpl,
                             PSPParticleEmitter& emitter, const PSPParticleSet& set) {
    uint16_t idx = freeHead;
    if (idx == 0) return 0;
    freeHead = buf[idx].m_NextLink;

    PSPParticle& p = buf[idx];
    p.m_pOwnerEmitter = &emitter;
    p.m_Pos = emitter.m_Pos;

    // Set-level velocity: randomized per component, halved.
    // Binary AddParticle @0x115644: local_78.xyz *= 0.5f unconditionally.
    float vx = RandRange(set.m_VelocityMin[0], set.m_VelocityMax[0]) * 0.5f;
    float vy = RandRange(set.m_VelocityMin[1], set.m_VelocityMax[1]) * 0.5f;
    float vz = RandRange(set.m_VelocityMin[2], set.m_VelocityMax[2]) * 0.5f;

    // 2D rotation by emitter's (DirCos, DirSin) pair.
    const float cosA = emitter.m_DirCos;
    const float sinA = emitter.m_DirSin;
    float rvx = vx * cosA + vy * sinA;
    float rvy = vy * cosA - sinA * vx;
    float rvz = vz;

    if (tmpl) {
        p.m_Gravity.x = RandRange(tmpl->m_GravityMin[0], tmpl->m_GravityMax[0]);
        p.m_Gravity.y = RandRange(tmpl->m_GravityMin[1], tmpl->m_GravityMax[1]);
        p.m_Gravity.z = RandRange(tmpl->m_GravityMin[2], tmpl->m_GravityMax[2]);

        p.m_Life = tmpl->m_Life;
        p.m_Age  = 0.0f;

        p.m_SizeStart = RandRange((float)tmpl->m_SizeStartMin, (float)tmpl->m_SizeStartMax);
        p.m_SizeMid   = RandRange((float)tmpl->m_SizeMidMin,   (float)tmpl->m_SizeMidMax);
        p.m_SizeEnd   = RandRange((float)tmpl->m_SizeEndMin,   (float)tmpl->m_SizeEndMax);

        // Spin rate: int16 * (182/65536) * 2pi * 60 -> rad/sec.
        static const float SPIN_INT16_TO_RAD_PER_SEC =
            (182.0f / 65536.0f) * 6.2831853f * 60.0f;
        p.m_SpinStart = RandRange((float)tmpl->m_SpinStartMin,
                                  (float)tmpl->m_SpinStartMax) * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_SpinEnd   = RandRange((float)tmpl->m_SpinEndMin,
                                  (float)tmpl->m_SpinEndMax)   * SPIN_INT16_TO_RAD_PER_SEC;
        p.m_Rotation = RandRange(tmpl->m_AngleMin, tmpl->m_AngleMax);

        p.m_RotCycleRate  = 0.5f * (tmpl->m_FrictionSpeedStart + tmpl->m_FrictionSpeedEnd);
        p.m_RotCycleAmp   = tmpl->m_FrictionOffsetMin * (3.14159265f / 180.0f);
        p.m_RotCyclePhase = Rand01() * 6.2831853f;

        p.m_CycleXRate  = 0.5f * ((float)tmpl->m_CycleXStart + (float)tmpl->m_CycleXEnd);
        p.m_CycleYRate  = 0.5f * ((float)tmpl->m_CycleYStart + (float)tmpl->m_CycleYEnd);
        p.m_CycleXPhase = Rand01() * 6.2831853f;
        p.m_CycleYPhase = Rand01() * 6.2831853f;

        // Quadrant-mirror branch (AddParticle @ 0x001157ae), gated on m_bMirrorX.
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

        // Shape-type branching (AddParticle @0x115644):
        //   0=Point, 1=Vortex (step back half-vel), 2=Direction (face velocity)
        switch (tmpl->m_Shape) {
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

    // Push onto template live-list: head is at particle_template+0x04.
    // ASM-verified: v1.6.1 AddParticle @0x0013c554 — r6=tmpl, *(r6+0x4)=newHead, slot->m_NextLink=oldHead.
    p.m_NextLink = tmpl ? tmpl->m_LiveHead : 0;
    if (tmpl) {
        tmpl->m_LiveHead = idx;
    }

    return idx;
}

// v1.6.1 PSPEmitterTemplate::Ends @0x00114884
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
static void UpdateEmitter(PSPParticleEmitter& e, float dt,
                          PSPParticle* buf, uint16_t& freeHead,
                          uint8_t* pTemplatesBase) {
    const uint8_t* eBlob = e.m_pTemplate;
    if (!eBlob) return;

    const PSPEmitterBlob* hdr = reinterpret_cast<const PSPEmitterBlob*>(eBlob);
    const float currentTime = e.m_Timer;
    const float dtScaled = dt * e.m_TimeScale;
    const float newTime = currentTime + dtScaled * e.m_RateScale;

    for (int si = 0; si < (int)hdr->m_NumSets; ++si) {
        const PSPParticleSet* set = PSPParticleManager::EmitterSet(
            const_cast<uint8_t*>(eBlob), si);

        PSPParticleTemplate* tmpl = 0;
        if (set->m_TemplateOffset != 0xFFFFFFFFu && pTemplatesBase) {
            tmpl = reinterpret_cast<PSPParticleTemplate*>(
                pTemplatesBase + set->m_TemplateOffset);
        }

        const float startT = set->m_TimeStart;
        const float stopT  = set->m_TimeStop;

        if (startT <= currentTime && (stopT == 0.0f || currentTime <= stopT)) {
            const float rate = set->m_PerSec;
            if (rate > 0.0f) {
                int desired = (int)(rate * ((currentTime + dtScaled * e.m_RateScale) - startT))
                            - (int)(rate * (currentTime - startT));
                for (int i = 0; i < desired; ++i)
                    AddParticle(buf, freeHead, tmpl, e, *set);
            }
        }

        if (currentTime <= startT && startT < newTime) {
            for (int i = 0; i < (int)set->m_InitCount; ++i)
                AddParticle(buf, freeHead, tmpl, e, *set);
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
            UpdateEmitter(*node, dt, m_pParticles, m_FreeHead, m_pTemplates);
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
// Draw helpers
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
                               const PSPParticleTemplate* tmpl,
                               Mortar::SmartPtr<Mortar::Texture>* texRefs,
                               int numTexRefs) {
    if (!verts.empty() && tmpl) {
        Mortar::Texture* tex = 0;
        uint32_t tidx = tmpl->m_TextureIdx;
        if (tidx != 0xFFFFFFFFu && texRefs && (int)tidx < numTexRefs) {
            tex = texRefs[tidx].Get();
        }
        if (tex) {
            // DIFFERS: original = port varied glBlendFunc per-template from the
            // asset's <SourceBlend>/<DestinationBlend> tags (tmpl->m_BlendMode);
            // binary v1.6.1 ignores those tags. glBlendFunc @0x0010c088 is xref'd
            // exactly twice, both at init (DisplayManagerBada::Init @0x00256c3c,
            // GlClientStates::Reset @0x00258050), both = (GL_SRC_ALPHA,
            // GL_ONE_MINUS_SRC_ALPHA); Mesh::DrawTris only toggles GL_BLEND enable,
            // never the func, and no glBlendEquation symbol exists at all. So every
            // particle template -- including the additive "rimhit" contact-flash
            // template -- draws straight-alpha in the real binary. Per-template
            // additive blending here washed the flash out under the bright splash.
            // See tmp/asm-verify/blade-flash-re.md.
            if (Renderer* r = Renderer::GetInstance()) {
                r->SetBlendEnabled(true);
                r->BindTexture2D(tex->GetTexId());
                r->DrawTriList(verts.data(), (int)verts.size(), false);
            }
        }
    }
    verts.clear();
}

// v1.6.1 PSPParticleManager::Draw @0x0013eccc — fused integrate+render.
// Outer loop: per particle template (stride 0xB8, count m_NumParticleTemplates).
// NOTE: spec says "Draw iterates m_pTemplates with m_NumEmitterTemplates as the
// count per RE" — but this seems like a RE annotation artefact (the outer loop
// iterates particle templates, not emitter templates). We iterate m_NumParticleTemplates.
// TODO: v1.6.1 PSPParticleManager::Draw @0x0013eccc — confirm outer loop count
//   (m_NumParticleTemplates vs m_NumEmitterTemplates) against binary disassembly.
// ASM-spec v1.6.1 PSPParticleManager::Draw @0x0013eccc: callers pass
// paused = (game_work.bM_Mode != 0). A per-particle gate
// `if (!paused || p->m_pOwnerEmitter->m_bUpdateWhenPaused)` wraps the rotation,
// velocity, position and lifetime integration; vertex emission stays outside it,
// so frozen particles keep rendering at their last state. Every ScreenEffect
// emitter is created via ScreenEffect::Activate @0x00148f08 -> AddEmitter(hash,
// NULL, false), i.e. m_bUpdateWhenPaused = 0, so the frenzy overlay freezes.
void PSPParticleManager::Draw(float dt, bool paused, int layer) {
    if (!m_pParticles || !m_pTemplates) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    m_DrawnParticleCount = 0;

    // TODO: v1.6.1 PSPParticleManager::Draw @0x0013eccc -- pull free particles toward
    //   m_GlobalOrigin within m_GlobalPullRadius (vortex); read-side not yet ported.
    //   Write-side (m_GlobalPullRadius set by SuperFruitControl::UpdateExplosion) is wired.

    static std::vector<QUADCUSTOMVERTEX> s_verts;
    const PSPParticleTemplate* curTmpl = 0;

    // Fused integrate+render over per-template live-lists.
    // Live-list head at particle_template+0x04 (m_LiveHead), chain via particle+0x40 (m_NextLink).
    for (int ti = 0; ti < m_NumParticleTemplates; ++ti) {
        PSPParticleTemplate* tmpl = GetParticleTemplate(ti);
        if (!tmpl || tmpl->m_LiveHead == 0) continue;

        // Walk the live-list for this template.
        uint16_t* prevLink = &tmpl->m_LiveHead;
        while (*prevLink != 0) {
            uint16_t idx = *prevLink;
            PSPParticle& p = m_pParticles[idx];

            // Layer filter.
            if (tmpl->m_UseDepth != layer) {
                prevLink = &p.m_NextLink;
                continue;
            }

            // ASM-spec v1.6.1 PSPParticleManager::Draw @0x0013eccc (local_174):
            // per-particle dt is scaled by the owning emitter's m_TimeScale.
            const float pdt = p.m_pOwnerEmitter
                ? dt * p.m_pOwnerEmitter->m_TimeScale
                : dt;
            const bool integrate = !paused
                || (p.m_pOwnerEmitter && p.m_pOwnerEmitter->m_bUpdateWhenPaused);

            // Integrate / age.
            if (integrate) p.m_Age += pdt;
            if (p.m_Age >= p.m_Life) {
                // Dead: splice out of live-list, return to free-list.
                *prevLink = p.m_NextLink;
                p.m_NextLink = m_FreeHead;
                m_FreeHead = idx;
                continue;
            }

            const float life = (p.m_Life > 0.0f) ? p.m_Life : 1.0f;
            const float t = p.m_Age / life;

            if (integrate) {
                p.m_Vel += p.m_Gravity * pdt;

                // Velocity damping from particle template (per-component lerp over life).
                const float dampX = tmpl->m_VelocityMin[0]
                    + (tmpl->m_VelocityMax[0] - tmpl->m_VelocityMin[0]) * t;
                const float dampY = tmpl->m_VelocityMin[1]
                    + (tmpl->m_VelocityMax[1] - tmpl->m_VelocityMin[1]) * t;
                const float dampZ = tmpl->m_VelocityMin[2]
                    + (tmpl->m_VelocityMax[2] - tmpl->m_VelocityMin[2]) * t;
                p.m_Vel.x *= dampX;
                p.m_Vel.y *= dampY;
                p.m_Vel.z *= dampZ;

                p.m_Pos += p.m_Vel * pdt;

                const float spin = p.m_SpinStart + (p.m_SpinEnd - p.m_SpinStart) * t;
                p.m_Rotation += spin * pdt;

                p.m_RotCyclePhase += p.m_RotCycleRate * pdt * 6.2831853f;
                p.m_CycleXPhase   += p.m_CycleXRate   * pdt * 6.2831853f;
                p.m_CycleYPhase   += p.m_CycleYRate   * pdt * 6.2831853f;
            }

            // Batch flush on template change.
            if (tmpl != curTmpl) {
                FlushParticleVerts(s_verts, curTmpl, m_pTextureRefs, m_NumTextureRefs);
                curTmpl = tmpl;
            }

            // Render.
            uint8_t col[4];
            float size;
            if (t < 0.5f) {
                float u = t * 2.0f;
                LerpColour(tmpl->m_ColourStartMin, tmpl->m_ColourMidMin, u, col);
                size = p.m_SizeStart + (p.m_SizeMid - p.m_SizeStart) * u;
            } else {
                float u = (t - 0.5f) * 2.0f;
                LerpColour(tmpl->m_ColourMidMin, tmpl->m_ColourEndMin, u, col);
                size = p.m_SizeMid + (p.m_SizeEnd - p.m_SizeMid) * u;
            }
            uint32_t packed = PackBGRA(col);

            float aspect = tmpl->m_AspectRatio;
            if (aspect <= 0.0f) aspect = 1.0f;
            // ASM-spec v1.6.1 PSPParticleManager::Draw @0x0013eccc: the quad half-extent
            // IS the interpolated size (X *= m_AspectRatio) -- NO 0.5 factor. Corners are
            // center +/- basis*size, so the quad spans 2*size. The port's *0.5f made every
            // particle 2x too small (most visible on the tiny pixel_blade particle).
            float hx = size * aspect;
            float hy = size;

            if (p.m_CycleXRate != 0.0f) hx *= cosf(p.m_CycleXPhase);
            if (p.m_CycleYRate != 0.0f) hy *= cosf(p.m_CycleYPhase);

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
            const float gx = tmpl->m_GridLockStart;
            const float gy = tmpl->m_GridLockEnd;
            if (gx > 0.0f) px = floorf(px / gx + 0.5f) * gx;
            if (gy > 0.0f) py = floorf(py / gy + 0.5f) * gy;

            struct C { float x, y, u, v; };
            C corners[4];
            corners[0].x = px - dxX - dyX; corners[0].y = py - dxY - dyY; corners[0].u = 0.0f; corners[0].v = 0.0f;
            corners[1].x = px + dxX - dyX; corners[1].y = py + dxY - dyY; corners[1].u = 1.0f; corners[1].v = 0.0f;
            corners[2].x = px + dxX + dyX; corners[2].y = py + dxY + dyY; corners[2].u = 1.0f; corners[2].v = 1.0f;
            corners[3].x = px - dxX + dyX; corners[3].y = py - dxY + dyY; corners[3].u = 0.0f; corners[3].v = 1.0f;
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

            ++m_DrawnParticleCount;
            prevLink = &p.m_NextLink;
        }
    }
    FlushParticleVerts(s_verts, curTmpl, m_pTextureRefs, m_NumTextureRefs);
    // No blend-func restore needed: glBlendFunc is the init-time constant
    // (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) everywhere and is never changed
    // per draw (see the DIFFERS note in FlushParticleVerts).
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

        // <spin>
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

        // <cycleX>, <cycleY>
        {
            TiXmlElement e = pt.FirstChildElement("cycleX");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_CycleXStart = (int16_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_CycleXEnd   = (int16_t)v;
            }
        }
        {
            TiXmlElement e = pt.FirstChildElement("cycleY");
            if (e) {
                int v = 0;
                if (e.QueryIntAttribute("startMin", &v) == TIXML_SUCCESS) tmpl->m_CycleYStart = (int16_t)v;
                if (e.QueryIntAttribute("endMin",   &v) == TIXML_SUCCESS) tmpl->m_CycleYEnd   = (int16_t)v;
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

        // <rotateCycle>
        {
            TiXmlElement e = pt.FirstChildElement("rotateCycle");
            if (e) {
                float fv = 0.0f;
                if (e.QueryFloatAttribute("speedStart", &fv) == TIXML_SUCCESS) tmpl->m_FrictionSpeedStart = fv;
                if (e.QueryFloatAttribute("speedEnd",   &fv) == TIXML_SUCCESS) tmpl->m_FrictionSpeedEnd   = fv;
                if (e.QueryFloatAttribute("start",      &fv) == TIXML_SUCCESS) tmpl->m_FrictionOffsetMin  = fv;
                if (e.QueryFloatAttribute("end",        &fv) == TIXML_SUCCESS) tmpl->m_FrictionOffsetMax  = fv;
                else                                                             tmpl->m_FrictionOffsetMax  = tmpl->m_FrictionOffsetMin;
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
#if !defined(__bada__)
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
#endif
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

// v1.6.1 PSPParticleManager::ClearEmitters @0x0010e258 (thunk) — drain
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
