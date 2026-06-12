// SpawnModifier — v1.6.1 custom-spawner modifier.
// Binary ctor @ 0x0014b9e0, ParseSpecific @ 0x0014be94,
// UpdateSpecific @ 0x0014ba70, GetType @ 0x0014c380.

#include "SpawnModifier.h"
#include "WaveManager.h"
#include "GameWork.h"
#include <tinyxml2.h>
#include <cmath>

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
// For each spawner where spawner->m_field58 (spawner[0x12] in binary, maps to
// SPAWNER_INFO::m_field58 at +0x58) > 0, treat m_field58 as the period counter.
// When floor(newTime/period) > floor(oldTime/period), a tick has elapsed:
//   reset spawner->m_SpawnTimer = -0.21875f (0xbe600000)
//   call WaveManager::SpawnFruit(1, spawner->SelectTypes()[Rand], spawner, 0)
// The period is stored as m_field58 (int) cast to float for division.
// TODO: 0x0014ba70 — wire spawner period field to correct SPAWNER_INFO member once
// RE confirms which byte maps to spawner[0x12] (currently using m_field58 as proxy)
int SpawnModifier::UpdateSpecific(float dt) {
    float prevTime = m_TimeAccum;
    m_TimeAccum += dt;

    WaveManager* wm = WaveManager::GetInstance();
    for (std::vector<SPAWNER_INFO*>::iterator it = m_Spawners.begin();
         it != m_Spawners.end(); ++it) {
        SPAWNER_INFO* spawner = *it;
        if (!spawner) continue;

        // m_field58 used as period proxy — binary spawner[0x12] field
        if (spawner->m_field58 <= 0) continue;

        float period = (float)spawner->m_field58;
        float prevTick = floorf(prevTime / period);
        float newTick  = floorf(m_TimeAccum / period);

        if (newTick > prevTick) {
            // Reset spawn timer to binary constant 0xbe600000 = -0.21875f
            spawner->m_SpawnTimer = -0.21875f;
            // TODO: 0x0014ba70 — call wm->SpawnFruit(1, type, spawner, 0)
            // type = spawner->SelectTypes()[Rand32(spawner->m_FruitTypeCount)]
            // Deferred: SelectTypes returns void, needs index selection path
            (void)wm;
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
        if (grav) {
            float gx = 0.0f, gy = 0.0f, gz = 0.0f;
            tinyxml2::XMLUtil::ToFloat(grav, &gx);
            (void)gx; (void)gy; (void)gz;
            // TODO: 0x0014be94 — parse gravity as "x,y,z" CSV into s->m_Gravity_{x,y,z}
        }

        // period stored as int in m_field58 (proxy for binary spawner[0x12])
        // TODO: 0x0014be94 — confirm which SPAWNER_INFO field maps to spawner[0x12]
        sp->QueryIntAttribute("period", &s->m_field58);

        m_Spawners.push_back(s);
    }
}

GameModifier* SpawnModifier::Clone() {
    SpawnModifier* c = new SpawnModifier();
    c->m_Duration           = m_Duration;
    c->field_0x08           = field_0x08;
    c->m_Duration_remaining = m_Duration_remaining;
    c->m_bDeferred          = m_bDeferred;
    c->m_DeferStart         = m_DeferStart;
    c->m_bApplied           = m_bApplied;
    c->m_pOwner             = m_pOwner;
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
