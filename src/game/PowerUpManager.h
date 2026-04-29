#ifndef FN_GAME_POWER_UP_MANAGER_H
#define FN_GAME_POWER_UP_MANAGER_H

// Analysed: 2026-04-30T00:00
//
// PowerUpManager — tracks active power-ups: hash->PowerUp* maps, active list,
// screen-effect map, dt multiplier (slow-time), score gain/loss multipliers.
// Drives blitz/chrono/double-points/etc. modifiers.
// Size: ~0x90 (144 bytes). Ctor inits m_field70/m_field74 at 0x70-0x74;
//       last accessed offset 0x84. Per docs/structs/game-managers.md.
//
// IMPORTANT: A no-op stub will compile but silently disables blitz mode,
// freeze, double points, etc. Mark all stubs with TODO.
// See docs/systems/power-ups.md for full method index.
//
// Binary addresses:
//   ctor (real)             0x00117d20
//   ctor (alias)            0x00117d60
//   ctor thunk              0x00104004
//   dtor (regular)          0x001187fc
//   dtor (deleting)         0x00118880
//   GetInstance             0x00118134
//   Update                  0x001189b4  (wrapper: 0x000f3ccc)
//   Reset                   0x00119b08
//   ClearTimedPowers        0x00118904
//   ActivatePower           0x001197c4
//   Load                    0x00119cb0
//   ApplyDtMod              0x001204dc
//   SlowClock               0x001204cc
//   ClearScreenEffects      0x00117ed8
//   GetScoreGainMultiplier  0x0010ad34
//   GetScoreLossMultiplier  0x0010ad40

#include <cstdint>

class PowerUpManager {
public:
    static PowerUpManager* GetInstance() {
        static PowerUpManager s_instance;
        return &s_instance;
    }

    // @ 0x001189b4 — tick all active powers, handle expiry
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void Update(float dt) { (void)dt; }

    // @ 0x00119b08 — clears all active powers + state
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void Reset(bool fullReset) { (void)fullReset; }

    // @ 0x00118904 — remove timed-only powers (called on bomb hit)
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void ClearTimedPowers() {}

    // @ 0x001197c4 — clone PowerUp by hash and activate
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void ActivatePower(uint32_t hash) { (void)hash; }

    // @ 0x00119cb0 — load poweruplist.xml into hash maps
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void Load() {}

    // @ 0x001204dc — m_DtMod *= param (slow-time hook)
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void ApplyDtMod(float f) { (void)f; }

    // @ 0x001204cc — slow-time activation
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void SlowClock() {}

    // @ 0x00117ed8 — clear all screen-effect entries
    // TODO: real impl pending -- see docs/systems/power-ups.md
    void ClearScreenEffects() {}

    // @ 0x0010ad34 — returns m_ScoreGainMult * m_ScoreGainFactor
    // TODO: real impl pending -- see docs/systems/power-ups.md
    float GetScoreGainMultiplier() const { return 1.0f; }

    // @ 0x0010ad40 — returns +0x80 * +0x84
    // TODO: real impl pending -- see docs/systems/power-ups.md
    float GetScoreLossMultiplier() const { return 1.0f; }

    // Fields read by WaveManager/TimeControl:
    float m_DtMod;      // dt multiplier (slow-time). Init = 1.0f.
    float m_field68;    // @ +0x68
    float m_field6c;    // @ +0x6c

private:
    // ctor @ 0x00117d20: 3 std::map ctors, 2 std::list ctors, init m_DtMod/m_field70 = 1.0f
    PowerUpManager()
        : m_DtMod(1.0f), m_field68(1.0f), m_field6c(1.0f) {}
    ~PowerUpManager() {}
};

#endif // FN_GAME_POWER_UP_MANAGER_H
