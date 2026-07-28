#ifndef FN_GAME_POWER_UP_MANAGER_H
#define FN_GAME_POWER_UP_MANAGER_H

// Analysed: 2026-05-04T00:00
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
//   ClearScoreMultipliers   0x00114900
//   Update                  0x001189b4  (wrapper: 0x000f3ccc)
//   Reset                   0x00142e08  (v1.6.1 verified)
//   ClearTimedPowers        0x0014136c
//   ActivatePower           0x001197c4
//   ActivateScreenEffect    0x00119760
//   ClearScreenEffects      0x00117ed8
//   Load                    0x00119cb0
//   LoadTextures            0x0011840c
//   Draw                    0x00119384
//   ApplyDtMod              0x0014d9c0  (v1.6.1 verified)
//   SlowClock               0x0014d9b0  (v1.6.1 verified)
//   StopClock               0x00117a70
//   PowerupDtModMultiply    0x00150cf0  (v1.6.1 verified)
//   AddToScoreGainAdd       0x0011d10c
//   AddToScoreLossAdd       0x0011d114
//   AddToScoreGainMultiply  0x0011d120
//   AddToScoreLossMultiply  0x0011d128
//   GetScoreGainMultiplier  0x0010ad34
//   GetScoreLossMultiplier  0x0010ad40
//   GetActiveProgression    0x00117b38
//   GetActiveSingle         0x00117cac
//   GetNumActiveTimedPowers 0x00117bb8
//   Release (private)       0x00118724
//   SaveActivePowerUps      0x00117df8
//   LoadActivePowerUps      0x001199d4
//   ActivatePurchase        0x001193d0

#include <cstdint>
#include <map>
#include <list>
#include "ScreenEffect.h"
#include "math/_Vector3.h"

class PowerUp;
#include "engine/xml/TiXmlElement.h"

// std::list is 8 bytes (Sourcery 2010q1 pre-C++11). Offsets below are binary-verified.

class PowerUpManager {
public:
    static PowerUpManager* GetInstance();

    // v1.6.1 PowerUpManager::Update @0x00141484
    void Update(float dt);

    // v1.6.1 PowerUpManager::SetDefaults @0x0013feb8
    void SetDefaults();

    // v1.6.1 PowerUpManager::ClearScoreMultipliers @0x00114900
    void ClearScoreMultipliers();

    // v1.6.1 PowerUpManager::Reset @ 0x00142e08
    void Reset(bool fullReset);

    // v1.6.1 PowerUpManager::ClearTimedPowers @0x0014136c
    void ClearTimedPowers();

    // v1.6.1 ActivatePower @0x00142934 — 3-arg form; returns active clone (or nullptr)
    PowerUp* ActivatePower(uint32_t hash, _Vector3<float> position, float* purchaseExtra);

    // @ 0x001193d0 — re-arm a purchased PowerUp from its template
    void ActivatePurchase(PowerUp* p);

    // @ 0x00119760
    bool ActivateScreenEffect(uint32_t hash);

    // @ 0x00117ed8
    void ClearScreenEffects();

    // @ 0x00119cb0
    void Load();

    // @ 0x0011840c
    void LoadTextures();

    // @ 0x00119384
    void Draw();

    // @ 0x00117df8 — emit active power-up state to XML
    void SaveActivePowerUps(TiXmlElement* parent);

    // @ 0x001199d4 — restore active power-up state from XML
    void LoadActivePowerUps(TiXmlElement* parent, int gameMode);

    // @ 0x0014d9c0 (v1.6.1) — m_DtMod *= scale
    void ApplyDtMod(float scale) { m_DtMod *= scale; }

    // @ 0x0014d9b0 (v1.6.1) — m_SlowClockMult *= scale
    void SlowClock(float scale) { m_SlowClockMult *= scale; }

    // @ 0x00117a70 — m_StopClockAccum += duration
    void StopClock(float duration);

    // @ 0x00150cf0 (v1.6.1) — m_WaveDtModCur *= scale
    void PowerupDtModMultiply(float scale) { m_WaveDtModCur *= scale; }

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
    float GetActiveProgression(float t);

    // @ 0x00117cac
    PowerUp* GetActiveSingle(uint32_t hash);

    // @ 0x00117bb8
    int GetNumActiveTimedPowers();

    // --- Struct fields (binary layout) ---

    // +0x00  m_AllPowerUps — every <powerup> parsed from XML, indexed by StringHash(name)
    std::map<uint32_t, PowerUp*> m_AllPowerUps;

    // +0x18  m_ActivePowerUps — currently-active clones (8-byte list in binary)
    std::list<PowerUp*> m_ActivePowerUps;

    // +0x20  m_ActiveByHash — quick lookup of active by name-hash
    std::map<uint32_t, PowerUp*> m_ActiveByHash;

    // +0x38  m_ScreenEffectPool — template ScreenEffects by hash (stored by value)
    std::map<uint32_t, ScreenEffect> m_ScreenEffectPool;

    // +0x50  m_ActiveScreenEffects — instances ticked each frame (8-byte list in binary)
    std::list<ScreenEffect> m_ActiveScreenEffects;

    // +0x58  m_PurchasablePowers — alias list of purchaseable templates (not owning; 8-byte list)
    std::list<PowerUp*> m_PurchasablePowers;

    // +0x60  m_pActiveSpecial — pointer cache to active special PowerUp (for HUD spotlighting)
    PowerUp* m_pActiveSpecial;

    // +0x64  m_DtMod — composite time-scale multiplier (SetDefaults resets to 1.0)
    float m_DtMod;

    // +0x68  m_StopClockAccum — "stop clock" accumulator (SetDefaults resets to 0.0)
    float m_StopClockAccum;

    // +0x6c  m_SlowClockMult — "slow clock" multiplier (SetDefaults resets to 1.0)
    float m_SlowClockMult;

    // +0x70  m_WaveDtModCur — composite WaveModifier dt-mod (reset to 1.0 each frame)
    float m_WaveDtModCur;

    // +0x74  m_WaveDtModPrev — previous frame's m_WaveDtModCur (carried forward in Update)
    float m_WaveDtModPrev;

    // +0x78  m_ScoreGainMult — multiplicative score-gain (default 1)
    int m_ScoreGainMult;

    // +0x7c  m_ScoreGainFactor — additive score-gain (default 1)
    int m_ScoreGainFactor;

    // +0x80  m_ScoreLossMult — multiplicative score-loss (default 1)
    int m_ScoreLossMult;

    // +0x84  m_ScoreLossFactor — additive score-loss (default 1)
    int m_ScoreLossFactor;

    // +0x88  m_HighestActiveProgress — highest current-time-progress across non-purchaseable specials
    float m_HighestActiveProgress;

    // +0x8c  (pad — unused; no write or read found in any binary method)
    uint32_t _pad8c;

private:
    // @ 0x00117d20
    PowerUpManager();
    ~PowerUpManager();

    // @ 0x00118724 — drain all containers; called by dtor
    void Release();

public:
    // @ 0x00117c50 — iterator-style walk: returns first purchasable, sets outIt
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117c50 (re-analyst)
    PowerUp* GetFirstPurchasable(std::list<PowerUp*>::iterator& outIt);

    // @ 0x00117bf4 — iterator-style walk: advances it, returns next purchasable
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117bf4 (re-analyst)
    PowerUp* GetNextPurchasable(std::list<PowerUp*>::iterator& it);

    // v1.6.1 PowerUpManager::SetAppropriateScoreCallback @0x001417c0 — bind active
    // ScoreModifier's OnScore delegate or reset to default
    // ASM-verified: 2026-05-18 v1.6.1 PowerUpManager::SetAppropriateScoreCallback @ 0x001417c0 (re-analyst)
    void SetAppropriateScoreCallback();

    // @ 0x0011836c — walk m_AllPowerUps and m_ScreenEffectPool, call UnloadTextures on each
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0011836c (re-analyst)
    void UnloadTextures();
};

// Binary file-statics hoisted to translation-unit scope in PowerUpManager.cpp.
// Declared extern here so GameInit.cpp (the m_DtMod consumer) can reach them
// without adding a new accessor method to the binary's public API.
extern float g_DtModDecayTimer;  // binary @ 0x001f3d84
extern float g_DtModDecayRate;   // binary @ 0x001f3d88
extern float g_PUM_WaveStress;   // binary @ 0x001f3da8

// Offsets reflect binary's 8B std::list (R4 W1 RE). Cross-toolchain runs unpatched.
// sizeof is binary-faithful 0x90: trailing fields hoisted to file-statics in PowerUpManager.cpp.
// The assert fires on the Bada/cross-build ABI where STL container sizes match the binary.
#include <cstddef>
#ifdef __bada__
static_assert(sizeof(PowerUpManager) == 0x90, "PowerUpManager size mismatch");
static_assert(offsetof(PowerUpManager, m_ActivePowerUps)      == 0x18, "m_ActivePowerUps offset");
static_assert(offsetof(PowerUpManager, m_ActiveByHash)        == 0x20, "m_ActiveByHash offset");
static_assert(offsetof(PowerUpManager, m_ScreenEffectPool)    == 0x38, "m_ScreenEffectPool offset");
static_assert(offsetof(PowerUpManager, m_ActiveScreenEffects) == 0x50, "m_ActiveScreenEffects offset");
static_assert(offsetof(PowerUpManager, m_PurchasablePowers)   == 0x58, "m_PurchasablePowers offset");
static_assert(offsetof(PowerUpManager, m_pActiveSpecial)      == 0x60, "m_pActiveSpecial offset");
static_assert(offsetof(PowerUpManager, m_HighestActiveProgress)== 0x88,"m_HighestActiveProgress offset");
#endif

#endif // FN_GAME_POWER_UP_MANAGER_H
