#ifndef FN_GAME_POWER_UP_MANAGER_H
#define FN_GAME_POWER_UP_MANAGER_H

// Analysed: 2026-04-30T00:00
//
// PowerUpManager — singleton tracking all power-ups (templates + active clones),
// screen effects, and per-frame composite modifier output slots.
// Binary size 0x90 (144 bytes). No vtable (concrete singleton).
//
// Binary addresses:
//   ctor (real)             0x00117d20
//   ctor (alias)            0x00117d60
//   ctor thunk              0x00104004
//   dtor (regular)          0x001187fc
//   dtor (deleting)         0x00118880
//   GetInstance             0x00118134
//   SetDefaults             0x00117a80
//   ClearScoreMultipliers   0x0011a218
//   Update                  0x001189b4  (wrapper: 0x000f3ccc)
//   Reset                   0x00119b08
//   ClearTimedPowers        0x00118904
//   ActivatePower           0x001197c4
//   ActivateScreenEffect    0x00119760
//   ClearScreenEffects      0x00117ed8
//   Load                    0x00119cb0
//   LoadTextures            0x0011840c
//   Draw                    0x00119384
//   ApplyDtMod              0x001204dc
//   SlowClock               0x001204cc
//   StopClock               0x00117a70
//   PowerupDtModMultiply    0x001286ec
//   AddToScoreGainAdd       0x0011d10c
//   AddToScoreLossAdd       0x0011d114
//   AddToScoreGainMultiply  0x0011d120
//   AddToScoreLossMultiply  0x0011d128
//   GetScoreGainMultiplier  0x0010ad34
//   GetScoreLossMultiplier  0x0010ad40
//   GetActiveProgression    0x00117b38
//   GetActiveSingle         0x00117cac
//   GetNumActiveTimedPowers 0x00117bb8

#include <cstdint>
#include <map>
#include <list>
#include "ScreenEffect.h"

class PowerUp;

// ScreenEffect is in ScreenEffect.h — included below.

class PowerUpManager {
public:
    static PowerUpManager* GetInstance() {
        static PowerUpManager s_instance;
        return &s_instance;
    }

    // @ 0x001189b4
    void Update(float dt);

    // @ 0x00117a80
    void SetDefaults();

    // @ 0x0011a218
    void ClearScoreMultipliers();

    // @ 0x00119b08
    void Reset(bool fullReset);

    // @ 0x00118904
    void ClearTimedPowers();

    // @ 0x001197c4
    void ActivatePower(uint32_t hash);

    // @ 0x00119760
    // TODO: ActivateScreenEffect full impl (Tier-2)
    bool ActivateScreenEffect(uint32_t hash);

    // @ 0x00117ed8
    void ClearScreenEffects();

    // @ 0x00119cb0
    void Load();

    // @ 0x0011840c
    void LoadTextures();

    // @ 0x00119384
    // TODO: Draw (Tier-2)
    void Draw();

    // @ 0x001204dc — m_DtMod *= scale
    void ApplyDtMod(float scale) { m_DtMod *= scale; }

    // @ 0x001204cc — m_field6c *= scale
    void SlowClock(float scale) { m_field6c *= scale; }

    // @ 0x00117a70 — m_field68 += duration
    void StopClock(float duration) { m_field68 += duration; }

    // @ 0x001286ec — m_field70 *= scale
    void PowerupDtModMultiply(float scale) { m_field70 *= scale; }

    // @ 0x0011d10c
    void AddToScoreGainAdd(int n)      { m_ScoreGainFactor   += n; }
    // @ 0x0011d114
    void AddToScoreLossAdd(int n)      { m_ScoreLossFactor   += n; }
    // @ 0x0011d120
    void AddToScoreGainMultiply(int n) { m_ScoreGainMult     *= n; }
    // @ 0x0011d128
    void AddToScoreLossMultiply(int n) { m_ScoreLossMult     *= n; }

    // @ 0x0010ad34 — returns m_ScoreGainMult * m_ScoreGainFactor
    int GetScoreGainMultiplier() const { return m_ScoreGainMult * m_ScoreGainFactor; }

    // @ 0x0010ad40 — returns m_ScoreLossMult * m_ScoreLossFactor
    int GetScoreLossMultiplier() const { return m_ScoreLossMult * m_ScoreLossFactor; }

    // @ 0x00117b38
    // TODO: GetActiveProgression full impl (Tier-2 / GetActiveProgression)
    float GetActiveProgression(float t) const;

    // @ 0x00117cac
    PowerUp* GetActiveSingle(uint32_t hash);

    // @ 0x00117bb8
    int GetNumActiveTimedPowers() const;

    // --- Struct fields (binary layout) ---

    // +0x00  m_AllPowerUps — every <powerup> parsed from XML, indexed by StringHash(name)
    std::map<uint32_t, PowerUp*> m_AllPowerUps;

    // +0x18  m_ActivePowerUps — currently-active clones
    std::list<PowerUp*> m_ActivePowerUps;

    // +0x20  m_ActiveByHash — quick lookup of active by name-hash
    std::map<uint32_t, PowerUp*> m_ActiveByHash;

    // +0x38  m_ScreenEffectPool — template ScreenEffects by hash (stored by value)
    std::map<uint32_t, ScreenEffect> m_ScreenEffectPool;

    // +0x50  m_ActiveScreenEffects — instances ticked each frame
    std::list<ScreenEffect> m_ActiveScreenEffects;

    // +0x58  m_PurchasablePowers — alias list of purchaseable templates (not owning)
    std::list<PowerUp*> m_PurchasablePowers;

    // +0x60  m_field60 — pointer cache to active special PowerUp (for HUD spotlighting)
    int m_field60;

    // +0x64  m_DtMod — composite time-scale multiplier (SetDefaults resets to 1.0)
    float m_DtMod;

    // +0x68  m_field68 — "stop clock" accumulator (SetDefaults resets to 0.0)
    float m_field68;

    // +0x6c  m_field6c — "slow clock" multiplier (SetDefaults resets to 1.0)
    float m_field6c;

    // +0x70  m_field70 — composite WaveModifier dt-mod (reset to 1.0 each frame)
    float m_field70;

    // +0x74  m_field74 — previous frame's m_field70 (carried forward in Update)
    float m_field74;

    // +0x78  m_ScoreGainMult — multiplicative score-gain (default 1)
    int m_ScoreGainMult;

    // +0x7c  m_ScoreGainFactor — additive score-gain (default 1)
    int m_ScoreGainFactor;

    // +0x80  m_ScoreLossMult — multiplicative score-loss (default 1)
    int m_ScoreLossMult;

    // +0x84  m_ScoreLossFactor — additive score-loss (default 1)
    int m_ScoreLossFactor;

    // +0x88  m_field88 — highest current-time-progress across non-purchaseable specials
    float m_field88;

    // +0x8c  (pad — unused)
    uint32_t _pad8c;

private:
    // @ 0x00117d20
    PowerUpManager();
    ~PowerUpManager();
};

#endif // FN_GAME_POWER_UP_MANAGER_H
