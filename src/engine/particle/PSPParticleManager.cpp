#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include "math/Random.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

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
    // Bada uses a MemoryPool with fixed capacity; we just grow the vector.
    const PSPEmitterTemplate* tmpl = FindTemplate(hash);
    if (!tmpl) {
        if (ppRef) *ppRef = nullptr;
        return nullptr;
    }

    m_Emitters.emplace_back();
    PSPParticleEmitter& e = m_Emitters.back();
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
        p.m_SizeStart = RandRange((float)tmpl->m_SizeStartMin, (float)tmpl->m_SizeStartMax);
        p.m_SizeEnd   = RandRange((float)tmpl->m_SizeEndMin,   (float)tmpl->m_SizeEndMax);

        memcpy(p.m_ColourStart, tmpl->m_ColourStartMin, 4);
        memcpy(p.m_ColourEnd,   tmpl->m_ColourEndMin,   4);

        p.m_Spin = RandRange((float)tmpl->m_SpinStartMin, (float)tmpl->m_SpinStartMax) * 0.01f;
        p.m_Rotation = RandRange(tmpl->m_AngleMin, tmpl->m_AngleMax);
    } else {
        p.m_Life = 1.0f;
        p.m_SizeStart = p.m_SizeEnd = 8.0f;
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
        p.m_Rotation += p.m_Spin * dt;
        ++i;
    }

    // Advance emitter state (matches binary)
    e.m_Timer = newTime;
    e.m_Pos += e.m_Vel;
}

// Matches PSPParticleManager::Update (0x115ed8).
void PSPParticleManager::Update(float dt) {
    const bool paused = false; // port: game pause not routed here yet
    for (size_t i = 0; i < m_Emitters.size(); ) {
        PSPParticleEmitter& e = m_Emitters[i];
        const PSPEmitterTemplate* et = e.m_pTemplate;

        // Tick active emitters
        if (e.m_ParticleHead != 0 && e.m_TimeScale != 0.0f &&
            (!paused || e.m_bUpdateWhenPaused)) {
            UpdateEmitter(e, dt);
        }

        // Removal: timer >= maxLifetime AND NOT (infinite AND still spawning)
        bool keep = true;
        if (et) {
            if (et->m_MaxLifetime > 0.0f) {
                keep = (e.m_Timer < et->m_MaxLifetime) || !e.m_Particles.empty();
            } else {
                // Infinite emitter — only removed by explicit ClearEmitter.
                keep = true;
            }
        }
        if (!keep) {
            if (e.m_pRefPtr) *e.m_pRefPtr = nullptr;
            m_Emitters[i] = m_Emitters.back();
            m_Emitters.pop_back();
            continue;
        }
        ++i;
    }
}

void PSPParticleManager::Draw() {
    // Stub — particle rendering not yet implemented (task #9).
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
