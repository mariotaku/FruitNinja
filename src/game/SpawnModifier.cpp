// SpawnModifier — v1.6.1 custom-spawner modifier.
// Binary ctor @ 0x0014b9e0, ParseSpecific @ 0x0014be94,
// UpdateSpecific @ 0x0014ba70, GetType @ 0x0014c380.

#include "SpawnModifier.h"
#include "WaveManager.h"
#include "GameWork.h"
#include <tinyxml2.h>
#include <cmath>
#include <cstdio>

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
// Iterate <spawner> children; for each: new SPAWNER_INFO(0x64), parse, push_back.
// If parse fails, free and skip.
// Binary parses the same SPAWNER_INFO XML attrs as WaveManager::Init (type, min,
// max, mininc, maxinc, delay, delayinc, gravity, velscale, placement, mirror).
void SpawnModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;

    for (tinyxml2::XMLElement* sp = xml->FirstChildElement("spawner");
         sp; sp = sp->NextSiblingElement("spawner")) {

        SPAWNER_INFO* s = new SPAWNER_INFO();

        sp->QueryFloatAttribute("min",      &s->m_SpawnMin);
        sp->QueryFloatAttribute("max",      &s->m_SpawnMax);
        sp->QueryFloatAttribute("mininc",   &s->m_GrowthInc);
        sp->QueryFloatAttribute("maxinc",   &s->m_GrowthInc);
        sp->QueryFloatAttribute("delay",    &s->m_Delay);
        sp->QueryFloatAttribute("delayinc", &s->m_DelayInc);

        const char* grav = sp->Attribute("gravity");
        if (grav && *grav) {
            // @ 0x0014f5a0 ParseVector(const char*): starts from a zero-default
            // Vec3 (static .bss default @ 0x002d9288 == {0,0,0}), parses x with
            // _OS_atof, then y after the first comma (if non-empty), then z after
            // the second comma (if non-empty). Missing components stay 0.0f and
            // the whole Vec3 overwrites the ctor's (0,-1,0) gravity default.
            float gx = 0.0f, gy = 0.0f, gz = 0.0f;
            sscanf(grav, "%f,%f,%f", &gx, &gy, &gz);
            s->m_Gravity_x = gx;
            s->m_Gravity_y = gy;
            s->m_Gravity_z = gz;
        }

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
