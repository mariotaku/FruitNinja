#ifndef FN_GAME_SCORE_MODIFIER_H
#define FN_GAME_SCORE_MODIFIER_H

// Analysed: 2026-04-30T00:00
//
// ScoreModifier — GameModifier subclass that multiplies/adds to score gain/loss.
// Binary size 0x3c. GetType() == 2.
// vtable @ 0x001e8d00.
//
// Binary addresses (v1.6.1):
//   ctor            0x001477d8
//   ~ScoreModifier  0x00147830  (D1 dtor)
//   ResetSpecific   0x00147898
//   UpdateSpecific  0x001478e0
//   DeferPoints     0x001478b8
//   ApplyModifier   0x0014798c
//   Clone           0x00147a58
//   ParseSpecific   0x00147abc
//   RemoveModifier  0x00147b8c
//   GetType         0x001480e0  (returns 2)

#include "GameModifier.h"

class ScoreModifier : public GameModifier {
public:
    // +0x20: XML gainAdd="N" — added to m_ScoreGainFactor per m_ApplyCount
    int m_GainAdd;

    // +0x24: XML gainMultiply="N" (default 1) — multiplied into m_ScoreGainMult
    int m_GainMultiply;

    // +0x28: XML lossAdd="N"
    int m_LossAdd;

    // +0x2c: XML lossMultiply="N" (default 1)
    int m_LossMultiply;

    // +0x30: apply/repeat counter — ApplyModifier @0x14798c increments; UpdateSpecific @0x1478e0 reads as loop bound
    int m_ApplyCount;

    // +0x34: XML deferPoints="true" — defers AddToCurrentScore via delegate
    bool m_bDeferPoints;
    uint8_t _pad35[3];

    // +0x38: deferred-points accumulator — DeferPoints @0x1478b8 does ldr/add/str [r4,#0x38]
    int m_DeferAccum;

    ScoreModifier();

    // @ 0x00147898
    void ResetSpecific() override;

    // @ 0x001478e0
    int UpdateSpecific(float dt) override;

    // @ 0x0014798c
    void ApplyModifier(bool isPurchased, float* extra) override;

    // @ 0x00147b8c
    void RemoveModifier() override;

    int GetType() override { return 2; }

    // @ 0x00147abc
    void ParseSpecific(TiXmlElement* xml) override;

    // @ 0x00147a58
    GameModifier* Clone() override;

    // @ 0x001478b8 — score delegate target when m_bDeferPoints is active
    int DeferPoints(int points);
};

#endif // FN_GAME_SCORE_MODIFIER_H
