#ifndef FN_GAME_TIME_MODIFIER_H
#define FN_GAME_TIME_MODIFIER_H

// Analysed: 2026-04-30T00:00
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

class TiXmlElement;

class TimeModifier : public GameModifier {
public:
    // +0x20: XML <scale amount="..."/> — target dtMod when active (default 1.0)
    float m_DtScale;

    // +0x24: XML <scale rate="..."/> — dt-per-second to ramp m_CurrentDtMod toward m_DtScale
    float m_TransitionRate;

    // +0x28: current applied dt-mod, lerped toward m_DtScale; reset to 0 in ResetSpecific
    float m_CurrentDtMod;

    // +0x2c: XML stop="true" — pause clock by adding m_Duration_remaining to m_field68
    bool m_bStopClock;
    uint8_t _pad2d[3];

    // +0x30: XML slow="..." (default 1.0) — multiplied into m_field6c each frame
    float m_TimeSlow;

    // +0x34: XML addTime="..." (default 0) — added to TimeControl after m_AddTimeDelay
    float m_AddTime;

    // +0x38: frames to delay AddTime; 1 if m_AddTime != 0; decrements to 0 then fires
    int m_AddTimeDelay;

    TimeModifier();

    // @ 0x0011ff4c
    void ResetSpecific() override;

    // @ 0x0011ffbc
    int UpdateSpecific(float dt) override;

    int GetType() override { return 0; }

    // @ 0x001200fc
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;
};

#endif // FN_GAME_TIME_MODIFIER_H
