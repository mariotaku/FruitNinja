#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include "math/Random.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

// Analysed: 2026-04-13T10:30

namespace Mortar {

// Parse "x y z" into three floats via sscanf, matching ParseInt3/ParseFloat3 use.
static bool ParseVec3(const char* s, float out[3]) {
    if (!s) return false;
    return sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

// Parse "r g b a" ints into a BGRA byte tuple, scaling 0-31 XML values to 0-255.
// Matches the binary's 255.0f/31.0f multiplier at DAT_001166c4.
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
    Clear();
}

const PSPEmitterTemplate* PSPParticleManager::FindTemplate(uint32_t hash) const {
    // Matches AddEmitter's linear search over m_pEmitterTemplates.
    for (size_t i = 0; i < m_EmitterTemplates.size(); ++i) {
        if (m_EmitterTemplates[i].m_Hash == hash)
            return &m_EmitterTemplates[i];
    }
    return nullptr;
}

// Matches AddEmitter (0x001149e0).
PSPParticleEmitter* PSPParticleManager::AddEmitter(uint32_t hash,
                                                   PSPParticleEmitter** ppRef,
                                                   bool /*persistent*/) {
    // Bada uses a MemoryPool with fixed capacity; we just grow the vector
    // (of unique_ptr, so emitter addresses remain stable).
    const PSPEmitterTemplate* tmpl = FindTemplate(hash);
    if (!tmpl) {
        if (ppRef) *ppRef = nullptr;
        return nullptr;
    }

    m_Emitters.emplace_back(new PSPParticleEmitter());
    PSPParticleEmitter& e = *m_Emitters.back();
    // All defaults match the binary's explicit init block:
    e.m_Timer = 0.0f;
    e.m_Pos = Vec3(0, 0, 0);
    e.m_Vel = Vec3(0, 0, 0);
    e.m_field30 = 0.0f;
    e.m_TimeScale = 1.0f;
    e.m_field24 = 1.0f;
    e.m_ScaleX = 1.0f;
    e.m_ScaleY = 1.0f;
    e.m_field34 = 1.0f;
    e.m_field38 = 0;
    e.m_ParticleHead = 1;
    e.m_bUpdateWhenPaused = false;
    e.m_pTemplate = tmpl;
    e.m_pRefPtr = ppRef;
    e.m_bActive = true;
    if (ppRef) *ppRef = &e;
    return &e;
}

// Matches ClearEmitter (0x00114934). Unlinks the emitter, clears the caller
// back-pointer, and removes it from the active list.
void PSPParticleManager::ClearEmitter(PSPParticleEmitter* emitter) {
    if (!emitter) return;
    for (size_t i = 0; i < m_Emitters.size(); ++i) {
        if (m_Emitters[i].get() == emitter) {
            if (emitter->m_pRefPtr) *emitter->m_pRefPtr = nullptr;
            m_Emitters.erase(m_Emitters.begin() + i);
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
// The port version pulls template defaults and applies set-level velocity.
static void SpawnParticle(PSPParticleEmitter& emitter, const PSPParticleSet& set) {
    const PSPParticleTemplate* tmpl = set.m_pTemplate;

    PSPParticle p;
    p.m_Pos = emitter.m_Pos;
    p.m_pTemplate = tmpl;

    // Velocity: set-level min/max (randomized per component), added to emitter vel.
    p.m_Vel.x = emitter.m_Vel.x + RandRange(set.m_VelocityMin[0], set.m_VelocityMax[0]);
    p.m_Vel.y = emitter.m_Vel.y + RandRange(set.m_VelocityMin[1], set.m_VelocityMax[1]);
    p.m_Vel.z = emitter.m_Vel.z + RandRange(set.m_VelocityMin[2], set.m_VelocityMax[2]);

    if (tmpl) {
        // Template-level velocity (also randomized, additive to set-level)
        p.m_Vel.x += RandRange(tmpl->m_VelocityMin[0], tmpl->m_VelocityMax[0]);
        p.m_Vel.y += RandRange(tmpl->m_VelocityMin[1], tmpl->m_VelocityMax[1]);
        p.m_Vel.z += RandRange(tmpl->m_VelocityMin[2], tmpl->m_VelocityMax[2]);

        p.m_Gravity.x = RandRange(tmpl->m_GravityMin[0], tmpl->m_GravityMax[0]);
        p.m_Gravity.y = RandRange(tmpl->m_GravityMin[1], tmpl->m_GravityMax[1]);
        p.m_Gravity.z = RandRange(tmpl->m_GravityMin[2], tmpl->m_GravityMax[2]);

        p.m_Life = tmpl->m_StartTime;  // template's "<life>/60" seconds

        // Two-segment size lerp — random per stop so each particle gets its
        // own variation on start/mid/end.
        p.m_SizeStart = RandRange((float)tmpl->m_SizeStartMin, (float)tmpl->m_SizeStartMax);
        p.m_SizeMid   = RandRange((float)tmpl->m_SizeMidMin,   (float)tmpl->m_SizeMidMax);
        p.m_SizeEnd   = RandRange((float)tmpl->m_SizeEndMin,   (float)tmpl->m_SizeEndMax);

        // Two-segment BGRA colour lerp (template already has mid = average of
        // start/end when the XML omits explicit mid attrs — see LoadFile).
        memcpy(p.m_ColourStart, tmpl->m_ColourStartMin, 4);
        memcpy(p.m_ColourMid,   tmpl->m_ColourMidMin,   4);
        memcpy(p.m_ColourEnd,   tmpl->m_ColourEndMin,   4);

        // Spin rate lerp: template has start/end min/max int16 ranges.
        // Binary stores these as 16-bit "degrees/frame" units — we convert
        // to radians/second via the same 0.01 scale the port has always
        // used. Each particle gets its own random start and end rate.
        p.m_SpinStart = RandRange((float)tmpl->m_SpinStartMin,
                                  (float)tmpl->m_SpinStartMax) * 0.01f;
        p.m_SpinEnd   = RandRange((float)tmpl->m_SpinEndMin,
                                  (float)tmpl->m_SpinEndMax)   * 0.01f;
        p.m_Rotation = RandRange(tmpl->m_AngleMin, tmpl->m_AngleMax);

        // RotCycle — oscillating rotation offset. speedStart/End from
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
        //   0 = Point     — no extra init (pos = emitter.pos, vel = rotated set vel)
        //   1 = Vertex    — start half a velocity step behind the emitter
        //   2 = Direction — rotate particle to face its own velocity
        //   3 = Angular   — skipped (requires emitter m_field38 state)
        switch (tmpl->m_Shape) {
            case 1: // Vertex
                p.m_Pos.x -= p.m_Vel.x;
                p.m_Pos.y -= p.m_Vel.y;
                p.m_Pos.z -= p.m_Vel.z;
                break;
            case 2: // Direction
                p.m_Rotation += atan2f(p.m_Vel.y, p.m_Vel.x);
                break;
            default:
                break;
        }
    } else {
        p.m_Life = 1.0f;
        p.m_SizeStart = p.m_SizeMid = p.m_SizeEnd = 8.0f;
    }

    emitter.m_Particles.push_back(p);
}

// Matches PSPParticleEmitter::Update (0x115d9c).
static void UpdateEmitter(PSPParticleEmitter& e, float dt) {
    const PSPEmitterTemplate* et = e.m_pTemplate;
    if (!et) return;

    const float currentTime = e.m_Timer;
    const float newTime = currentTime + dt * e.m_TimeScale;

    // Spawn pass — for each set, check window and integrate rate
    for (const PSPParticleSet& set : et->m_Sets) {
        const float startT = set.m_TimeStart;
        const float stopT  = set.m_TimeStop;

        // Continuous rate: only within [startT, stopT] (stopT==0 → no limit).
        if (startT <= currentTime && (stopT == 0.0f || currentTime <= stopT)) {
            const float rate = set.m_PerSec;
            if (rate > 0.0f) {
                int desired = (int)(rate * ((currentTime + dt * e.m_TimeScale) - startT))
                            - (int)(rate * (currentTime - startT));
                for (int i = 0; i < desired; ++i) SpawnParticle(e, set);
            }
        }

        // Burst on first frame crossing startT
        if (currentTime <= startT && startT < newTime) {
            for (int i = 0; i < (int)set.m_InitCount; ++i) SpawnParticle(e, set);
            if (e.m_TimeScale == 0.0f) e.m_Timer += dt;
        }
    }

    // Physics pass — age + integrate all particles
    for (size_t i = 0; i < e.m_Particles.size(); ) {
        PSPParticle& p = e.m_Particles[i];
        p.m_Age += dt;
        if (p.m_Age >= p.m_Life) {
            e.m_Particles[i] = e.m_Particles.back();
            e.m_Particles.pop_back();
            continue;
        }
        p.m_Vel   += p.m_Gravity * dt;
        p.m_Pos   += p.m_Vel * dt;
        // Spin rate lerp start→end over life, then integrate.
        const float t = (p.m_Life > 0.0f) ? (p.m_Age / p.m_Life) : 0.0f;
        const float spin = p.m_SpinStart + (p.m_SpinEnd - p.m_SpinStart) * t;
        p.m_Rotation += spin * dt;
        // Cycle accumulators (RotCycle + CycleX/Y). Rates are in cycles/s so
        // convert to radians by ×2π.
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
    for (const PSPParticleSet& s : t->m_Sets) {
        if (s.m_TimeStop <= 0.0f && s.m_PerSec > 0.0f) return false;
    }
    return true;
}

// Matches PSPParticleManager::Update (0x115ed8).
void PSPParticleManager::Update(float dt) {
    const bool paused = false; // port: game pause not routed here yet
    for (size_t i = 0; i < m_Emitters.size(); ) {
        PSPParticleEmitter& e = *m_Emitters[i];
        const PSPEmitterTemplate* et = e.m_pTemplate;

        // Tick active emitters
        if (e.m_ParticleHead != 0 && e.m_TimeScale != 0.0f &&
            (!paused || e.m_bUpdateWhenPaused)) {
            UpdateEmitter(e, dt);
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
                keep = (e.m_Timer < et->m_MaxLifetime) || !e.m_Particles.empty();
            } else {
                keep = naturallyInfinite || !e.m_Particles.empty();
            }
        }
        if (!keep) {
            if (e.m_pRefPtr) *e.m_pRefPtr = nullptr;
            m_Emitters.erase(m_Emitters.begin() + i);
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

void PSPParticleManager::Draw(int layer) {
    if (m_Emitters.empty()) return;

    // Reset world matrix + upload MVP so DrawTriList uses the current ortho.
    // Matches binary Draw 0x114c64: "MatrixStack::Reset + UploadCurrentMatrices".
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    static std::vector<QUADCUSTOMVERTEX> s_verts;
    const PSPParticleTemplate* curTmpl = nullptr;

    auto flush = [&]() {
        if (!s_verts.empty() && curTmpl && curTmpl->m_Texture.IsValid()) {
            // Blend mode: source = GL_SRC_ALPHA, dest from template (default
            // GL_ONE_MINUS_SRC_ALPHA if unset).
            GLenum dstFactor = curTmpl->m_BlendMode ? (GLenum)curTmpl->m_BlendMode
                                                    : GL_ONE_MINUS_SRC_ALPHA;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, dstFactor);
            glBindTexture(GL_TEXTURE_2D, curTmpl->m_Texture->m_TexId);
            if (Renderer* r = Renderer::GetInstance()) {
                r->DrawTriList(s_verts.data(), (int)s_verts.size());
            }
        }
        s_verts.clear();
    };

    for (auto& up : m_Emitters) {
        PSPParticleEmitter& e = *up;
        if (e.m_Particles.empty()) continue;

        for (PSPParticle& p : e.m_Particles) {
            // Layer filter: binary draws only particles whose template's
            // m_UseDepth matches the requested layer.
            if (p.m_pTemplate && p.m_pTemplate->m_UseDepth != layer) continue;

            // Group flush on template change (batches DrawTriList per texture)
            if (p.m_pTemplate != curTmpl) {
                flush();
                curTmpl = p.m_pTemplate;
            }

            const float life = p.m_Life > 0.0f ? p.m_Life : 1.0f;
            float t = p.m_Age / life;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

            // Two-segment colour + size lerp: start→mid for t∈[0,0.5),
            // mid→end for t∈[0.5,1]. Matches binary Draw piecewise linear.
            uint8_t col[4];
            float size;
            if (t < 0.5f) {
                float u = t * 2.0f;
                LerpColour(p.m_ColourStart, p.m_ColourMid, u, col);
                size = p.m_SizeStart + (p.m_SizeMid - p.m_SizeStart) * u;
            } else {
                float u = (t - 0.5f) * 2.0f;
                LerpColour(p.m_ColourMid, p.m_ColourEnd, u, col);
                size = p.m_SizeMid + (p.m_SizeEnd - p.m_SizeMid) * u;
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

            // HUD offset: entity positions are in centred coords, the
            // FruitCamera ortho is centred on (480, 320), so we shift by
            // that amount to map centred → screen space. Matches the offset
            // Fruit::Draw and Bomb::Draw add to their own draw positions.
            float px = p.m_Pos.x + 480.0f;
            float py = p.m_Pos.y + 320.0f;
            const float pz = p.m_Pos.z;

            // Grid-lock: binary snaps pos around the (480, 320) HUD origin
            // when the template declares non-zero cell sizes. Used by
            // rim_spark (menu rim flash) to keep particles aligned.
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
    flush();

    // Restore default blend state so we don't leak into subsequent draws.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Matches PSPParticleManager::LoadFile (0x00115f60).
// Layout: first loop parses `<particleTemplate>` elements into
// m_ParticleTemplates, second loop parses `<emitter>` elements and resolves
// each particleSet's template by name lookup.
// The `<life>` divisor is 60.0 (DAT_001161e8 / DAT_001170a0) — seconds at 60fps.
void PSPParticleManager::LoadFile(const char* path) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(path) != tinyxml2::XML_SUCCESS) {
        printf("[PSPParticleManager] LoadFile: failed to load %s\n", path);
        return;
    }
    tinyxml2::XMLElement* root = doc.FirstChildElement("particle_file");
    if (!root) return;
    tinyxml2::XMLElement* body = root->FirstChildElement("body");
    if (!body) return;

    m_ParticleTemplates.clear();
    m_EmitterTemplates.clear();

    const std::string texDir = DirOf(path);

    // --- First loop: <particleTemplate> --------------------------------------
    std::unordered_map<uint32_t, size_t> nameToIndex;
    nameToIndex.reserve(256);

    for (tinyxml2::XMLElement* pt = body->FirstChildElement("particleTemplate");
         pt != nullptr;
         pt = pt->NextSiblingElement("particleTemplate")) {

        PSPParticleTemplate tmpl = {};

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
                snprintf(buf, sizeof(buf), "%s/%s.tex", texDir.c_str(), texName);
                tmpl.m_Texture = TextureManager::GetInstance().Load(buf);
                if (tmpl.m_Texture.IsValid()) {
                    const float tw = (float)tmpl.m_Texture->m_Width;
                    const float th = (float)tmpl.m_Texture->m_Height;
                    if (th > 0.0f) tmpl.m_AspectRatio = tw / th;
                }
            }
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
    for (auto& emit : m_EmitterTemplates) {
        for (auto& set : emit.m_Sets) {
            uintptr_t encoded = reinterpret_cast<uintptr_t>(set.m_pTemplate);
            if (encoded == 0) continue;
            size_t idx = encoded - 1;
            set.m_pTemplate = (idx < m_ParticleTemplates.size())
                              ? &m_ParticleTemplates[idx]
                              : nullptr;
        }
    }

    printf("[PSPParticleManager] Loaded %zu particle templates, "
           "%zu emitter templates from %s\n",
           m_ParticleTemplates.size(), m_EmitterTemplates.size(), path);
}

void PSPParticleManager::Clear() {
    m_Emitters.clear();
}

} // namespace Mortar
