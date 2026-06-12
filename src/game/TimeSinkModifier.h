#ifndef FN_GAME_TIME_SINK_MODIFIER_H
#define FN_GAME_TIME_SINK_MODIFIER_H

//
// TimeSinkModifier : GameModifier — v1.6.1 time-from-score modifier.
// Binary size ~0x28 (40 bytes). GetType() == 4.
// On slice/score events, computes time delta = score * multiplier and
// calls TimeControl::AddTime.
//
// Binary addresses:
//   ctor            0x0014d9e8
//   ParseSpecific   0x0014dbf4
//   ApplyModifier   0x0014dc88
//   FruitWasSliced  (delegate target, near ApplyModifier)
//   ScoreNotification 0x0014dac4
//   GetType         0x0014e1b8

#include "GameModifier.h"

class Fruit;

class TimeSinkModifier : public GameModifier {
public:
    // +0x20: time-per-event multiplier (ctor default -4.0)
    float m_Multiplier;

    // +0x24: accumulator OR -1.0 sentinel.
    // Parsed: 'immediate' attr -> +0x24 = 1.0, else -1.0.
    // When >= 0: immediate AddTime mode.
    // When < 0: accumulate into this field until threshold.
    float m_Accumulator;

    TimeSinkModifier();
    ~TimeSinkModifier() override;

    void ResetSpecific() override;
    int  UpdateSpecific(float dt) override;

    // @ 0x0014dc88 — when not deferred and +0x24>=0, registers
    // ScoreNotification as Delegate2<void,int,int> on the score signal.
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 4; }

    // @ 0x0014dbf4 — reads 'value' -> m_Multiplier, 'immediate' flag -> m_Accumulator
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // Delegate target: called when score changes (int points, int extra)
    // @ 0x0014dac4 — accumulate or immediately call TimeControl::AddTime
    // TODO: 0x0014dac4 — wire to score notification signal
    void ScoreNotification(int points, int extra);

    // Delegate target: called when fruit is sliced (Fruit*, int score)
    // TODO: 0x0014da7c — wire to FruitManager's FruitWasSliced signal
    void FruitWasSlicedSink(Fruit* fruit, int score);
};

#endif // FN_GAME_TIME_SINK_MODIFIER_H
