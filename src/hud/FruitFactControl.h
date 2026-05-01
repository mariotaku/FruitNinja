#ifndef FN_FRUIT_FACT_CONTROL_H
#define FN_FRUIT_FACT_CONTROL_H

// FruitFactControl : HUDControl3d (size 0x204)
// "Best fruit you sliced" callout panel shown on game-over screen.
// Binary: ctor 0x0013cb60, LoadContent 0x135010, UnLoadContent 0x13508c
//
// Fields used by GameOverScreen:
//   +0xD0: combo hash (ulong) — read for combo-star achievement
//   +0xE0: (int8) combo type — in [0, 0x18] gates the achievement
//   +0xE5: m_PomCount  (uint8)
//   +0xE9: m_StarCount (uint8)
//
// TODO: port full FruitFactControl as a separate task (see docs/engine/gameoverscreen-deep-re.md §6)

#include "hud/HUDControl3d.h"
#include <cstdint>

class FruitFactControl : public HUDControl3d {
public:
    // Fields accessed by GameOverScreen (at their binary offsets relative to FruitFactControl)
    // Port uses plain members; binary offsets are documentation only.

    // +0xD0: combo hash (ulong) for combo-star achievement
    unsigned long m_ComboHash;   // +0xD0

    // +0xE0: combo type (-1 = none, 0..0x18 = valid combo)
    int8_t  m_ComboType;         // +0xE0

    // +0xE5: pom-pom count set by GameOverScreen state 6
    uint8_t m_PomCount;          // +0xE5

    // +0xE9: star count set by GameOverScreen state 6
    uint8_t m_StarCount;         // +0xE9

    FruitFactControl()
        : m_ComboHash(0),
          m_ComboType(-1),
          m_PomCount(0),
          m_StarCount(0) {}

    void Update(float /*dt*/) override {}
    void Draw(const Vec3& /*hudScale*/, int /*layerMask*/) override {}
    int  GetType() override { return 6; }

    static void LoadContent() {}
    static void UnLoadContent() {}
};

#endif // FN_FRUIT_FACT_CONTROL_H
