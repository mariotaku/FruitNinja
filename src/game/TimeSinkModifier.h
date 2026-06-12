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

namespace Mortar { class Entity; }
class Fruit;

class TimeSinkModifier : public GameModifier {
public:
    // +0x20: time-per-event multiplier (ctor default 0.0; set via ParseSpecific 'value' attr)
    float m_Multiplier;

    // +0x24: threshold gate. ctor default -1.0f (binary ctor @ 0x0014da30).
    // Parsed: 'immediate' attr present -> 0.0f (immediate AddTime mode);
    //         'immediate' attr absent  -> -1.0f (accumulate mode).
    // ApplyModifier: fires only when m_BonusAccum(+0x0c)<=0 && m_Accumulator>=0.
    // When >= 0: immediate AddTime mode.
    // When < 0: accumulate into this field.
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

    // Delegate target: called when fruit is sliced (Fruit*, int score, Entity* slasher).
    // Subscribed in ApplyModifier to g_FruitWasSliced (Fruit.cpp, GOT 0x332a34).
    // TODO: 0x0014da7c — verify binary arg3 (Entity* slasher) is unused in sink body.
    void FruitWasSlicedSink(Fruit* fruit, int score, Mortar::Entity* slasher);
};

#ifdef __bada__
static_assert(sizeof(TimeSinkModifier) == 0x28,
    "TimeSinkModifier must be 0x28 bytes");
#endif

#endif // FN_GAME_TIME_SINK_MODIFIER_H
