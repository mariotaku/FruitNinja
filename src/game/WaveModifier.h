#ifndef FN_GAME_WAVE_MODIFIER_H
#define FN_GAME_WAVE_MODIFIER_H

// Analysed: 2026-04-30T00:00
//
// WaveModifier — GameModifier subclass controlling WaveManager spawn rates / dt-mod.
// Binary size 0x44. GetType() == 1.
// vtable @ 0x001e8e18.
//
// Binary addresses:
//   ctor            0x00128158
//   UpdateSpecific  0x001280e4
//   ParseSpecific   0x0012836c

#include "GameModifier.h"
#include <vector>

class TiXmlElement;

// PROBABILITY_OVERIDE — per-modifier blitz/power-up spawn override entry.
// See docs/engine/wavemanager-deep-re.md §5 for consumer side.
struct PROBABILITY_OVERIDE;

class WaveModifier : public GameModifier {
public:
    // +0x20: per-modifier blitz/power-up spawn override entries (std::vector<PROBABILITY_OVERIDE>)
    // TODO: PROBABILITY_OVERIDE struct layout from wavemanager-deep-re.md §5
    std::vector<void*> m_OverideEntries;   // placeholder until PROBABILITY_OVERIDE is ported

    // +0x2c: XML bombMultiplyer="..." (default 1.0) — multiplied into WaveManager spawnLevel
    float m_BombMult;

    // +0x30: XML bombScale="..." (default 1.0)
    float m_BombScale;

    // +0x34: XML fruitMultiplyer="..." (default 1.0)
    float m_FruitMult;

    // +0x38: XML dtMod="..." (default 1.0) — multiplied into PowerUpManager.m_field70
    float m_DtMod;

    // +0x3c: XML overrideProbabilityPool="N" (default 10000)
    int m_OverideProbabilityPool;

    // +0x40: XML criticalChance="..." (default 1.0)
    float m_CritChanceMod;

    WaveModifier();

    // @ 0x001280e4
    int UpdateSpecific(float dt) override;

    int GetType() override { return 1; }

    // @ 0x0012836c
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;
};

#endif // FN_GAME_WAVE_MODIFIER_H
