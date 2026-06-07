#ifndef FN_GAME_WAVE_MODIFIER_H
#define FN_GAME_WAVE_MODIFIER_H

// Analysed: 2026-05-03T00:00
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
#include "WaveStructs.h"
#include <vector>

class WaveModifier : public GameModifier {
public:
    // +0x20: per-modifier blitz/power-up spawn override entries (std::vector<PROBABILITY_OVERIDE>)
    std::vector<PROBABILITY_OVERIDE> m_OverideEntries;

    // +0x2c: XML bombMultiplyer="..." (default 1.0) — multiplied into WaveManager spawnLevel
    float m_BombMult;

    // +0x30: XML bombScale="..." (default 1.0)
    float m_BombScale;

    // +0x34: XML fruitMultiplyer="..." (default 1.0)
    float m_FruitMult;

    // +0x38: XML powerUpDtMod="..." (default 1.0) — multiplied into PowerUpManager.m_field70
    float m_DtMod;

    // +0x3c: XML waveOveride="N" (binary attr with typo, default 0)
    int m_OverideProbabilityPool;

    // +0x40: XML criticalChance="..." (default 0.0)
    float m_CritChanceMod;

    WaveModifier();
    ~WaveModifier();

    // @ 0x001280e4
    int UpdateSpecific(float dt) override;

    int GetType() override { return 1; }

    // @ 0x0012836c
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // TODO: 0x001282d4 -- chain base ApplyModifier, then (unless purchased) rewind
    //   WaveManager to m_OverideProbabilityPool wave, SelectType() each override entry,
    //   and append m_OverideEntries into WaveManager's current override list, then clear.
    void ApplyModifier(bool isPurchased, float* extra) override;
    // TODO: 0x00128128 -- if WaveManager current wave < 0 and >= m_OverideProbabilityPool,
    //   reset WaveManager current wave to (5, 0.25, 0).
    void RemoveModifier() override;
    // @ 0x001280e0 -- empty override in binary (no specific reset work); no-op is faithful.
    void ResetSpecific() override;
};

#endif // FN_GAME_WAVE_MODIFIER_H
