#ifndef FN_GAME_SPAWN_MODIFIER_H
#define FN_GAME_SPAWN_MODIFIER_H

//
// SpawnModifier : GameModifier — v1.6.1 custom-spawner modifier.
// Binary size 0x30 (48 bytes): base 0x20 + vector (0x0c) + float (0x04). GetType() == 5.
// Holds a list of SPAWNER_INFO* entries parsed from <spawner> XML children.
// UpdateSpecific ticks an accumulator and spawns fruit when timer periods elapse.
//
// Binary addresses:
//   ctor            0x0014b9e0
//   ParseSpecific   0x0014be94
//   UpdateSpecific  0x0014ba70
//   GetType         0x0014c380

#include "GameModifier.h"
#include "WaveStructs.h"
#include <vector>

class SpawnModifier : public GameModifier {
public:
    // +0x20: owned list of parsed spawner configs (std::vector<SPAWNER_INFO*>)
    std::vector<SPAWNER_INFO*> m_Spawners;

    // +0x2c: time accumulator (advanced by dt each UpdateSpecific call)
    float m_TimeAccum;

    SpawnModifier();
    ~SpawnModifier() override;

    void ResetSpecific() override;

    // @ 0x0014ba70 — advance accumulator; for each spawner with period>0,
    // when floor(now/period) ticks over, call WaveManager::SpawnFruit.
    int UpdateSpecific(float dt) override;

    // ApplyModifier — base passthrough per binary
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 5; }

    // @ 0x0014be94 — iterate <spawner> children, new SPAWNER_INFO(0x64),
    // parse & push_back (free on parse-fail).
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;
};

#ifdef __bada__
static_assert(sizeof(SpawnModifier) == 0x30,
    "SpawnModifier must be 0x30 bytes");
#endif

#endif // FN_GAME_SPAWN_MODIFIER_H
