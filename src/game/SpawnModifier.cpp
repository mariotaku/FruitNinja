// SpawnModifier — v1.6.1 custom-spawner modifier.
// Binary ctor @ 0x0014b9e0, ParseSpecific @ 0x0014be94,
// UpdateSpecific @ 0x0014ba70, GetType @ 0x0014c380.

#include "SpawnModifier.h"
#include "WaveManager.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

SpawnModifier::SpawnModifier()
    : GameModifier()
    , m_TimeAccum(0.0f)
{}

SpawnModifier::~SpawnModifier() {
    for (std::vector<SPAWNER_INFO*>::iterator it = m_Spawners.begin();
         it != m_Spawners.end(); ++it) {
        delete *it;
    }
    m_Spawners.clear();
}

void SpawnModifier::ResetSpecific() {
    m_TimeAccum = 0.0f;
}

void SpawnModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
}

// @ 0x0014ba70
// Advances accumulator by dt; for each spawner where m_Delay (+0x48) > 0,
// when floor(newTime/period) > floor(oldTime/period), a tick has elapsed:
//   spawner->m_SpawnTimer = -0.21875f (0xbe600000)
//   type = m_pFruitTypeHashes[ GetRandom().Rand32(m_FruitTypeCount) ]
//   WaveManager::SpawnFruit(1, type, spawner, 0)
int SpawnModifier::UpdateSpecific(float dt) {
    float prevTime = m_TimeAccum;
    m_TimeAccum += dt;

    WaveManager* wm = WaveManager::GetInstance();
    for (std::vector<SPAWNER_INFO*>::iterator it = m_Spawners.begin();
         it != m_Spawners.end(); ++it) {
        SPAWNER_INFO* spawner = *it;
        if (!spawner) continue;

        float period = spawner->m_Delay;
        if (period <= 0.0f) continue;

        float prevTick = floorf(prevTime / period);
        float newTick  = floorf(m_TimeAccum / period);

        if (newTick > prevTick) {
            spawner->m_SpawnTimer = -0.21875f;
            if (spawner->m_pFruitTypeHashes && spawner->m_FruitTypeCount > 0) {
                long type = (long)spawner->m_pFruitTypeHashes[
                    (int)wm->GetRandom().Rand32((uint32_t)spawner->m_FruitTypeCount)];
                wm->SpawnFruit(1, type, spawner, 0);
            }
        }
    }
    return 0;
}

// @ 0x0014be94
// Iterate <Spawn> children (capital S -- matches poweruplist.xml waterfall/spawn_mod
// XML: <Spawn type="coconut" delay="0.5" .../>). Binary attr parse identical to
// WaveManager::Init's <Spawn> handler (type, min, max, mininc, maxinc, delay,
// delayinc, gravity, velscale, placement, mirror).
void SpawnModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;

    for (TiXmlElement sp = xml->FirstChildElement("Spawn");
         sp; sp = sp.NextSiblingElement("Spawn")) {

        SPAWNER_INFO* s = new SPAWNER_INFO();

        // type -> fruit type hash array (same logic as WaveManager::Init <Spawn> handler).
        {
            const char* types = sp.Attribute("type");
            if (types) {
                std::vector<std::string> typeNames;
                WaveManager::SplitWords(types, typeNames);
                s->m_FruitTypeCount = (int)typeNames.size();
                if (s->m_FruitTypeCount > 0) {
                    s->m_pFruitTypeHashes = new int[s->m_FruitTypeCount];
                    for (int ti = 0; ti < s->m_FruitTypeCount; ++ti) {
                        const std::string& tn = typeNames[ti];
                        if (tn == "random")
                            s->m_pFruitTypeHashes[ti] = -1;
                        else if (tn == "bomb")
                            s->m_pFruitTypeHashes[ti] = -2;
                        else
                            s->m_pFruitTypeHashes[ti] = Fruit::FruitType(tn.c_str(), false);
                    }
                }
            }
        }

        sp.QueryFloatAttribute("min",      &s->m_SpawnMin);
        sp.QueryFloatAttribute("max",      &s->m_SpawnMax);
        sp.QueryFloatAttribute("mininc",   &s->m_GrowthInc);
        sp.QueryFloatAttribute("maxinc",   &s->m_GrowthInc);
        sp.QueryFloatAttribute("delay",    &s->m_Delay);
        sp.QueryFloatAttribute("delayinc", &s->m_DelayInc);

        const char* grav = sp.Attribute("gravity");
        if (grav && *grav) {
            // @ 0x0014f5a0 ParseVector(const char*): starts from zero-default Vec3.
            float gx = 0.0f, gy = 0.0f, gz = 0.0f;
            sscanf(grav, "%f,%f,%f", &gx, &gy, &gz);
            s->m_Gravity_x = gx;
            s->m_Gravity_y = gy;
            s->m_Gravity_z = gz;
        }

        // velscale copies to both VelXScale and VelYScale; velXscale/velYscale override individually.
        sp.QueryFloatAttribute("velscale",  &s->m_VelXScale);
        s->m_VelYScale = s->m_VelXScale;
        sp.QueryFloatAttribute("velXscale", &s->m_VelXScale);
        sp.QueryFloatAttribute("velYscale", &s->m_VelYScale);
        sp.QueryFloatAttribute("horizmin",  &s->m_HorizMin);
        sp.QueryFloatAttribute("horizmax",  &s->m_HorizMax);
        s->m_bMirror = 0;
        if (const char* mir = sp.Attribute("mirror"))
            s->m_bMirror = (strcmp(mir, "false") != 0) ? 1 : 0;
        const char* placement = sp.Attribute("placement");
        if (placement) s->m_SpawnType = WaveManager::ParsePlacement(placement);

        m_Spawners.push_back(s);
    }
}

GameModifier* SpawnModifier::Clone() {
    SpawnModifier* c = new SpawnModifier();
    c->m_Duration   = m_Duration;
    c->field_0x08   = field_0x08;
    c->m_BonusAccum = m_BonusAccum;
    c->m_bDeferred  = m_bDeferred;
    c->m_DeferTime  = m_DeferTime;
    c->m_bApplied   = m_bApplied;
    c->m_pDeferInfo = m_pDeferInfo;
    c->m_TimeAccum          = m_TimeAccum;
    // Deep-copy spawners
    for (std::vector<SPAWNER_INFO*>::const_iterator it = m_Spawners.begin();
         it != m_Spawners.end(); ++it) {
        if (*it) {
            SPAWNER_INFO* sc = new SPAWNER_INFO();
            *sc = **it;
            c->m_Spawners.push_back(sc);
        }
    }
    return c;
}
