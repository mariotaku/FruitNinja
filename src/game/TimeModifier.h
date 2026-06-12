#ifndef FN_GAME_TIME_MODIFIER_H
#define FN_GAME_TIME_MODIFIER_H

// Analysed: 2026-05-03T00:00
//
// TimeModifier — GameModifier subclass controlling dt-scale / stop-clock / slow-clock.
// Binary size 0x3c. GetType() == 0.
// vtable @ 0x001e8d80.
//
// Binary addresses:
//   ctor            0x0011a228
//   ResetSpecific   0x0011ff4c
//   UpdateSpecific  0x0011ffbc
//   GetType         0x001204ec (returns 0)
//   ParseSpecific   0x001200fc

#include "GameModifier.h"

class TimeModifier : public GameModifier {
public:
    // +0x20: XML <dt_speed dt="..."/> — target dtMod when active (default 1.0)
    float m_DtScale;

    // +0x24: XML <dt_speed transitionTime="..."/> — seconds to ramp m_CurrentDtMod toward m_DtScale
    float m_TransitionRate;

    // +0x28: current applied dt-mod, lerped toward m_DtScale; reset to 0 in ResetSpecific
    float m_CurrentDtMod;

    // +0x2c: XML stopClock="true" — pause clock by adding m_BonusAccum to StopClock each frame
    bool m_bStopClock;
    uint8_t _pad2d[3];

    // +0x30: XML slowClock="..." (default 1.0) — multiplied into clock each frame
    float m_TimeSlow;

    // +0x34: XML addClock="..." (default 0.0) — added to TimeControl after m_AddTimeDelay
    float m_AddTime;

    // +0x38: frames to delay AddTime; 1 if m_AddTime != 0; decrements to 0 then fires
    int m_AddTimeDelay;

    TimeModifier();

    // @ 0x0011ff4c
    void ResetSpecific() override;

    // @ 0x0011ffbc
    int UpdateSpecific(float dt) override;

    // TimeModifier::ApplyModifier — NOT overridden; inherits GameModifier::ApplyModifier (slot 8).
    // The v1.5.1 addr 0x001200f0 was UpdateSpecific, not ApplyModifier. Removed stale TODO.
    void ApplyModifier(bool isPurchased, float* extra) override {
        GameModifier::ApplyModifier(isPurchased, extra);
    }

    int GetType() override { return 0; }

    // @ 0x001200fc
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;
};

#endif // FN_GAME_TIME_MODIFIER_H
