#ifndef FN_GAME_SCORE_MODIFIER_H
#define FN_GAME_SCORE_MODIFIER_H

// Analysed: 2026-04-30T00:00
//
// ScoreModifier — GameModifier subclass that multiplies/adds to score gain/loss.
// Binary size 0x3c. GetType() == 2.
// vtable @ 0x001e8d00.
//
// Binary addresses:
//   ctor            0x0011ca8c
//   ResetSpecific   0x0011cb44
//   UpdateSpecific  0x0011cb70
//   ApplyModifier   0x0011cbe8
//   RemoveModifier  0x0011cd44
//   GetType         0x0011d134  (returns 2)
//   ParseSpecific   0x0011ccb0
//   Clone           0x0011cc6c

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

    // +0x30: number of times Apply was called (combo counter for stacking)
    int m_ApplyCount;

    // +0x34: XML deferPoints="true" — defers AddToCurrentScore via delegate
    bool m_bDeferPoints;
    uint8_t _pad35[3];

    // +0x38: pad
    uint32_t field_0x38;

    ScoreModifier();

    // @ 0x0011cb44
    void ResetSpecific() override;

    // @ 0x0011cb70
    int UpdateSpecific(float dt) override;

    // @ 0x0011cbe8
    void ApplyModifier(bool isPurchased, float* extra) override;

    // @ 0x0011cd44
    void RemoveModifier() override;

    int GetType() override { return 2; }

    // @ 0x0011ccb0
    void ParseSpecific(TiXmlElement* xml) override;

    // @ 0x0011cc6c
    GameModifier* Clone() override;

    // Callee<ScoreModifier> callable — invoked by score delegate trampoline
    // when m_bDeferPoints is active.
    // TODO: implement operator() body (binary addr TBD — RE needed)
    int operator()(int n);

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ScoreModifier::DeferPoints -- auto stub from binary missing-symbol set
    void DeferPoints(int);
    // ---- end AUTO-STUB MERGE ----
};

#endif // FN_GAME_SCORE_MODIFIER_H
