#ifndef FN_GAME_WAVE_MODIFIER_H
#define FN_GAME_WAVE_MODIFIER_H

//
// WaveModifier — GameModifier subclass controlling WaveManager spawn rates / dt-mod.
// Binary size 0x48. GetType() == 3.
// vtable @ 0x2cc8b0 (v1.6.1).
//
// v1.6.1 binary addresses:
//   ctor            0x001503c4
//   UpdateSpecific  0x00150378
//   ParseSpecific   0x00150768
//   ApplyModifier   0x0015068c
//   RemoveModifier  0x00150590
//   Clone           0x00150640

#include "GameModifier.h"
#include "WaveStructs.h"
#include <vector>

class WaveModifier : public GameModifier {
public:
    // +0x20: count of override entries (ctor=0)
    int m_OverrideCount;

    // +0x24: per-modifier blitz/power-up spawn override entries (std::vector<PROBABILITY_OVERIDE>, 0xc bytes)
    std::vector<PROBABILITY_OVERIDE> m_OverideEntries;

    // +0x30: XML bombMultiplyer="..." (default 1.0) -- multiplied into WaveManager::m_SpawnLevel (+0x68)
    float m_BombMult;

    // +0x34: XML bombScale="..." (default 1.0)
    float m_BombScale;

    // +0x38: XML fruitMultiplyer="..." (default 1.0)
    float m_FruitMult;

    // +0x3c: XML powerUpDtMod="..." (default 1.0) — multiplied into PowerUpManager.m_WaveDtModCur
    float m_DtMod;

    // +0x40: XML waveOveride="N" (binary attr with typo, default 10000)
    int m_OverideProbabilityPool;

    // +0x44: XML criticalChance="..." (default 1.0)
    float m_CritChanceMod;

    WaveModifier();
    ~WaveModifier();

    // @ 0x00150378
    int UpdateSpecific(float dt) override;

    // vtable slot 5. The binary has NO separate OnDeferComplete symbol: slot 5
    // stores WaveModifier::ApplyModifier @0x0015068c, the same single compiled
    // body slot 8 uses (same ICF merge as the base class -- see GameModifier.cpp).
    // Kept as a C++ method purely so slot-5 dispatch from GameModifier::Update
    // reaches that body; it just forwards to ApplyModifier.
    void OnDeferComplete(bool unused, float* pExtra) override;

    int GetType() override { return 3; }  // binary @ 0x00150d40 returns 3

    // @ 0x0012836c
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // @ 0x0015068c -- chain base ApplyModifier, then (unless purchased) rewind
    //   WaveManager to m_OverideProbabilityPool wave, SelectType() each override entry,
    //   and append m_OverideEntries into WaveManager's current override list, then clear.
    void ApplyModifier(bool isPurchased, float* extra) override;
    // @ 0x00150590 -- if WaveManager current wave < 0, >= m_OverideProbabilityPool,
    //   and not online MP, reset WaveManager current wave to (5, 0.25, 0). Then
    //   always erases the front m_OverrideCount entries from
    //   WaveManager::m_ProbabilityOverride[gameMode], undoing the prepend done by
    //   ApplyModifier.
    void RemoveModifier() override;
    // @ 0x00150374 -- empty override in binary (no specific reset work); no-op is faithful.
    void ResetSpecific() override;
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(WaveModifier) == 0x48, "WaveModifier must be 0x48 bytes");
static_assert(offsetof(WaveModifier, m_OverrideCount)        == 0x20, "m_OverrideCount");
static_assert(offsetof(WaveModifier, m_OverideEntries)       == 0x24, "m_OverideEntries");
static_assert(offsetof(WaveModifier, m_BombMult)             == 0x30, "m_BombMult");
static_assert(offsetof(WaveModifier, m_OverideProbabilityPool) == 0x40, "m_OverideProbabilityPool");
static_assert(offsetof(WaveModifier, m_CritChanceMod)        == 0x44, "m_CritChanceMod");
#endif

#endif // FN_GAME_WAVE_MODIFIER_H
