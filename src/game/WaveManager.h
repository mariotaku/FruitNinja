#ifndef FN_WAVE_MANAGER_H
#define FN_WAVE_MANAGER_H

// WaveManager — wave spawning subsystem.
//
// Binary class at 0x0022ee80 (singleton), 752 bytes (0x2f0).
// Port extends beyond binary region with m_pWaveQue/m_pWaveQueItem.
//
// Analysed: 2026-04-30T00:00 (updated 2026-06-18: corrected array offsets,
// renamed fields to match binary, added missing tail fields).

#include "math/Random.h"
#include "game/WaveStructs.h"
#include <vector>
#include <cstdint>

struct WAVE_INFO;
struct SPAWNER_INFO;
struct PROBABILITY_OVERIDE;
struct DEFAULT_WAVE_INFO;
struct COIN_CHANCEINATOR;
struct WaveQue;
struct WaveQueItem;
class  FruitSaveData;
class HUDControl;
class HUDControl3d;
class SpeedControl;
namespace Mortar { class Entity; }

class WaveManager {
public:
    // +0x00: cached SpeedControl HUD widget pointers, one per player.
    // Binary @ 0x001217d4 (DeleteSpeedControl): ldr from [r0, #0x00].
    // Binary @ 0x00122f50 (UpdateComboSpeed): slot 0 allocated lazily.
    // Slot 1 is never populated (binary @ 0x001217d4 checks only slot 0).
    HUDControl3d* m_SpeedControl[2];   // +0x00, +0x04

    // +0x08: RNG instance.
    // DIFFERS: binary fetches Random via a GOT-relative global pointer; port embeds
    // it as a member so that m_SpeedControl correctly occupies +0x00/+0x04.
    // +0x08..+0x1f (24 bytes); bridges into the binary's +0x08..+0x34 unnamed region.
    Math::Random m_Random;   // Port specific: +0x08

    // +0x20..+0x34: remaining unnamed binary region (21 bytes of padding).
    uint8_t _pad_0x20[0x15];  // +0x20

    // +0x35: wave-active flag
    uint8_t field_0x35;       // +0x35
    // +0x36: reset flag
    uint8_t field_0x36;       // +0x36
    // +0x37: misc flag
    uint8_t field_0x37;       // +0x37
    // +0x38: last selected wave index (int, -1 = none)
    int field_0x38;           // +0x38
    // +0x3c: gap to reach +0x40
    uint8_t _pad_0x3c[4];    // +0x3c
    // +0x40: play-time accumulators
    float field_0x40;         // +0x40
    // +0x44: defunct net timer A (binary: multiplayer retry timer A; dead code in SP)
    float field_0x44;         // +0x44
    // +0x48: defunct net timer B (binary: multiplayer retry timer B; dead code in SP)
    float m_NetTimerB;        // +0x48
    // +0x4c: unnamed binary padding (4 bytes between net timers and combo timer)
    uint8_t _pad4c[4];        // +0x4c
    // +0x50: blitz combo timer (decays via GetWavedt; AddSpeed sets to 1.0 on slice).
    // v1.6.1 AddSpeed @0x00124f48, UpdateComboSpeed @0x001238dc
    float m_ComboTimer;       // +0x50
    // +0x54: binary padding (4 bytes between combo timer and combo speed)
    uint8_t _pad54[4];        // +0x54
    // +0x58: displayed/eased combo speed (P0). Eased toward m_TargetComboSpeed.
    // v1.6.1 UpdateComboSpeed @0x001238dc
    float m_ComboSpeed;       // +0x58
    // +0x5c: AddSpeed accumulator (target speed), clamped [0, 14].
    // v1.6.1 AddSpeed @0x00124f48
    float m_TargetComboSpeed; // +0x5c
    // +0x60: blitz tier level (int, 1..6+). Written by AddToTotal("blitz_bonus").
    // v1.6.1 AddSpeed @0x00124f48
    int   m_BlitzLevel;       // +0x60
    // +0x64: cold-down timer (float, set to 3.0 on each tier; counts down to 0).
    // v1.6.1 AddSpeed @0x00124f48, GetComboBonusProgression @0x00122fb0
    float m_ColdTimer;        // +0x64
    // +0x68: bomb chain spawn level multiplier (BombMultiplyer power-up; reset 1.0 each frame)
    // v1.6.1 WaveManager::Update @0x001267a0
    float m_SpawnLevel;       // +0x68
    // +0x6c: bomb spawn chance multiplier (BombScale power-up; reset 1.0 each frame)
    // v1.6.1 WaveManager::Update @0x001267a0
    float m_BombChance;       // +0x6c
    // +0x70: fruit spawn multiplier (FruitMultiplyer power-up; reset 1.0 each frame)
    // v1.6.1 WaveManager::Update @0x001267a0
    float m_FruitChance;      // +0x70
    // +0x74: critical chance multiplier (CriticalChanceMod power-up; reset 1.0 each frame)
    // v1.6.1 WaveManager::Update @0x001267a0, GetCriticalChance @0x00123174
    float m_CritChanceMult;   // +0x74

    // +0x78: speed accumulator (increment base for dtInc). Binary @ 0x001267a0
    // accumulates into +0x78; NOT a dtMod from PowerUpManager (that's at +0x7c).
    float m_SpeedAccum;           // +0x78 (was field_0x78)

    // +0x7c: dtMod from PowerUpManager (default 1.0 speed multiplier).
    // Binary: PowerUpManager::Update writes dtMod into +0x7c.
    // v1.6.1 WaveManager field @ 0x001267a0
    float m_SpeedScale;           // +0x7c

    // +0x80: dt * combo divisor for PowerUpManager::Update.
    // v1.6.1 WaveManager field @ 0x001267a0
    float m_ComboSpeedDivisor;    // +0x80

    // +0x84: per-mode dtInc (speed accumulator increment). Parsed from <defaults> "globalDtInc".
    // binary @ 0x00125ac4: speed = m_SpeedAccum + dt * m_DtIncPerMode[mode]
    float m_DtIncPerMode[4];      // +0x84
    // +0x94: per-mode globalDtStart lower bound. Parsed from <defaults> "globalDtStart".
    // DIFFERS: original values unknown; using 1.0f per mode as placeholder.
    // v1.6.1 WaveManager field @ 0x00125ac4
    float m_SpeedClampStart[4];   // +0x94
    // +0xa4: per-mode speed upper bound. Parsed from <defaults> "globalDtMax".
    // DIFFERS: original values unknown; using 100.0f per mode as placeholder.
    // v1.6.1 WaveManager field @ 0x00125ac4
    float m_SpeedClampMax[4];     // +0xa4

    // +0xb4: 4 vectors of WAVE_INFO* (one per game mode). Binary stride 0xc per mode.
    // Binary ctor @ 0x00123ef8: field at +0xb4.
    // WAS WRONG comment: +0xac. Actual __bada__ offset: +0xb4.
    std::vector<WAVE_INFO*> m_WaveInfo[4];      // +0xb4

    // +0xe4: per-mode default wave parameters (64 bytes each).
    // WAS WRONG comment: +0xdc. Actual __bada__ offset: +0xe4.
    DEFAULT_WAVE_INFO m_DefaultWaveInfo[4];     // +0xe4

    // +0x1e4: per-mode coin chance tables (8 bytes each).
    // WAS WRONG comment: +0x1dc. Actual __bada__ offset: +0x1e4.
    COIN_CHANCEINATOR m_CoinChanceinator[4];    // +0x1e4

    // +0x204: per-mode probability override lists (12 bytes each on __bada__).
    // WAS WRONG comment: +0x1fc. Actual __bada__ offset: +0x204.
    std::vector<PROBABILITY_OVERIDE> m_ProbabilityOverride[4];  // +0x204

    // +0x234: Per-player aliased region.
    // In the 32-bit binary, m_pCurrentWave[2] (8 bytes) at +0x234 is aliased
    // with m_WaveCount[2]. On __bada__, both arrays occupy the same 8 bytes:
    //   m_pCurrentWave[0] at +0x234 (pointer)
    //   m_pCurrentWave[1] / m_WaveCount[0] at +0x238 (aliased)
    //   m_WaveCount[1] at +0x23c (last 4 bytes of union)
    // The floats at +0x23c and +0x240 are separate (not part of the union),
    // but in the binary they alias m_WaveCount[1] / m_NextWaveDelay[0].
    // On the 64-bit host a pointer is 8 bytes and a 4-byte int cannot alias
    // it correctly -- the union would corrupt m_pCurrentWave[0]'s high 4 bytes
    // every time m_WaveCount[1] is written. Host uses separate fields.
    // ASM-verified: 2026-06-18 v1.6.1 WaveManager ctor @ 0x00123ef8
#if defined(__bada__)
    union {
        WAVE_INFO* m_pCurrentWave[2];   // +0x234 (P0), +0x238 (P1) — 8 bytes on __bada__
        int        m_WaveCount[2];      // +0x238 (P0, aliases pCurrentWave[1]), +0x23c (P1)
    };
#else
    WAVE_INFO* m_pCurrentWave[2];       // host: 16 bytes (8 per ptr)
    int        m_WaveCount[2];          // host: 8 bytes
#endif

    // +0x23c: P0 pre-spawn delay timer ("delay" XML attr).
    // Binary @ 0x0012598c reads [+0x23c]; GetNextWave @ 0x001251ee writes [+0x23c].
    // WAS field_0x234 (wrong offset in comment). Actual __bada__ offset: +0x23c.
    // Binary aliases this slot with m_WaveCount[1] at +0x23c.
    float m_NextWaveDelay_P0;           // +0x23c (was field_0x234)

    // +0x240: P0 wave-end wait timer ("wait" XML attr).
    // Binary @ 0x00125956 reads [+0x240]; GetNextWave @ 0x00125224 writes [+0x240].
    // WAS field_0x238 (wrong offset in comment). Actual __bada__ offset: +0x240.
    // Binary uses this as P1's m_NextWaveDelay[1] in multiplayer aliasing.
    float m_NextWaveDelay_P1;           // +0x240 (was field_0x238)

    // +0x244: wave-was-spawned flag (IsWaveProcessing reads this).
    // WAS field_0x23c (wrong offset in comment). Actual __bada__ offset: +0x244.
    uint8_t field_0x244;                // +0x244 (was field_0x23c)
    // +0x245: blitz spawned-this-game counter (byte).
    // v1.6.1 @ 0x00125be4 / 0x00124cf4
    uint8_t m_BlitzSpawnCount;          // +0x245 (was field_0x23d)
    // +0x246: blitz force-spawned counter (byte).
    // v1.6.1 @ 0x00125be4 / 0x00124cf4
    uint8_t m_BlitzState;              // +0x246 (was field_0x23e)
    uint8_t _pad247;                    // +0x247 (was _pad23f)

    // +0x248: blitz spawn time (float).
    // WAS field_0x240 (wrong offset in comment). Actual __bada__ offset: +0x248.
    float m_NextBlitzTime;              // +0x248 (was field_0x240)

    // +0x24c..+0x2cb: fruit type queue, P0 only, 32 ints (-1 = empty slot).
    // Binary @ 0x00124cf4/0x0016cd08: 128-byte (0x80) queue for P0.
    // WAS WRONG comment: +0x244. Actual __bada__ offset: +0x24c.
    int m_FruitQueue[32];               // +0x24c

    // +0x2cc: max wave id for P0 (int).
    // Binary: single int, NO aliasing with P1 count.
    // WAS m_FruitQueueSize[0]. Actual __bada__ offset: +0x2cc.
    // v1.6.1 WaveManager field @ 0x00125be4
    int m_MaxWaveIdP0;                  // +0x2cc (was m_FruitQueueSize[0])

    // +0x2d0: recent-fruit backtrack queue size per player [2].
    // Binary: two int counters for P0 and P1 fruit history.
    // WAS m_FruitQueueSize[1] at +0x2d0 and field_0x2cc at +0x2d4.
    // v1.6.1 WaveManager Reset @ 0x00125be4
    int m_RecentFruitCount[2];          // +0x2d0 ([0] was m_FruitQueueSize[1], [1] was field_0x2cc)

    // +0x2d8: misc counter (binary unnamed; reset to 0 in Reset).
    int field_0x2d8;                    // +0x2d8 (was field_0x2d0)

    // +0x2dc: fixed-timestep accumulator (init 0.0f).
    float m_StepAccumulator;            // +0x2dc (was field_0x2d4)

    // +0x2e0: misc counter (int, NOT a pointer — port had m_pWaveQue here!).
    // Binary unnamed field; reset to 0 in binary @ 0x00125be4.
    int field_0x2e0;                    // +0x2e0 (NEW)

    // +0x2e4: global probability override list (12 bytes on __bada__).
    // Binary field at +0x2e4 (12 bytes = std::vector<void*> on 32-bit).
    // DIFFERS: port uses void* because element type is not yet RE'd.
    // v1.6.1 WaveManager field @ 0x00125be4
    std::vector<void*> m_GlobalProbabilityOverride;  // +0x2e4 (NEW)

    // --- Binary region END: +0x2f0 (752 bytes) --------------------------

    // Port extensions (beyond binary region):
    // DIFFERS: port adds m_pWaveQue/m_pWaveQueItem beyond binary sizeof(WaveManager)=752.
    // Moved from +0x2e0 (which was a bug — that's binary field_0x2e0).
    WaveQue*     m_pWaveQue;            // port-only
    WaveQueItem* m_pWaveQueItem;        // port-only

    // --- Construction / singleton --------------------------------------

    WaveManager();
    ~WaveManager();

    static WaveManager* GetInstance();

    // --- Lifecycle -----------------------------------------------------

    // 0x0012393c: loads xml/<mode>WaveList.xml for each mode,
    // builds WAVE_INFO/SPAWNER_INFO arrays.
    void Init();

    // 0x00121bf0: frees WAVE_INFO list, WaveQue, WaveQueItem.
    void Destroy();

    // 0x00125be4: full state reset between games.
    void Reset(bool fullReset);

    // 0x00124b1c: restore state from FruitSaveData.
    void Resume();

    // 0x001247f0: serialise current wave state into FruitSaveData.
    int  SaveWaveInfo(FruitSaveData* save);

    // 0x00121f74 / 0x00121f90: static entry points.
    static void GameOver();
    static void NewGame();

    // Gate for PowerUpManager calls in GameOver/NewGame.
    // TODO: RE exact binary address and condition; stubbed true (binary default).
    static bool PowersEnabled();

    // 0x00121ed8: clears per-entity speed-control list.
    void ResetGlobalDt(float dt);

    // 0x001249d0: re-randomises per-wave spawn chance pool.
    void ResetWaveChances();

    // --- Per-frame update ---------------------------------------------

    // v1.6.1 WaveManager::Update @0x001267a0 (89 lines): fixed-timestep pump + multiplier resets.
    void Update(float dt);

    // v1.6.1 WaveManager::UpdateWave @0x00125d7c (298 lines): one tick of wave spawning.
    void UpdateWave(float dt, int playerIdx, int unk);

    // 0x00122f50: blitz-combo speed update.
    void UpdateComboSpeed(float dt);

    // --- Wave progression ---------------------------------------------

    // 0x00124f10 (227 lines): advance to next WAVE_INFO.
    void GetNextWave(int playerIdx);

    // 0x00125340: seek to a specific wave number.
    void SetCurrentWave(int waveNo, float delay, int playerIdx);

    // 0x00124564 (142 lines): build the wave queue for survival/combo.
    void SetupWaveQue();

    // --- Spawning -----------------------------------------------------

    // 0x00121fa8: spawn N bombs.
    void SpawnBomb(long count, long type, SPAWNER_INFO* spawner, int playerIdx);

    // 0x001225a0 (248 lines): spawn N fruits. Returns the last spawned Entity*
    // (binary @ 0x00124298 returns this_00); NULL if count < 1 or no entity allocated.
    Mortar::Entity* SpawnFruit(long count, long fruitType, SPAWNER_INFO* spawner, int playerIdx);

    // 0x00122ad8: clear all unspawned spawner entries.
    void ClearUnspawned();

    // --- Rendering / HUD glue -----------------------------------------

    // 0x00122ae8: wave overlay draw.
    void Draw(int playerIdx);

    // 0x001217d4: clears cached speed-control HUDControl* if it matches.
    void DeleteSpeedControl(HUDControl* control);

    // --- Queries -------------------------------------------------------

    Math::Random& GetRandom() { return m_Random; }

    // v1.6.1 @0x00122fa0: m_ComboSpeed (P0 displayed speed).
    float GetSpeed(int playerIdx);

    // 0x001218dc: effective wave dt (clamped).
    float GetWavedt(int playerIdx);

    // 0x001219c4: returns waveCritChance * m_CritChanceMult.
    float GetCriticalChance(int playerIdx);

    // 0x001219e4: returns true if a slice should be "critical" this tick.
    bool  CriticalMode(int playerIdx);

    // 0x00121840: combo bonus progression [0..1].
    float GetComboBonusProgression(int playerIdx);

    // 0x0012180c: override list slice for current wave + player.
    PROBABILITY_OVERIDE* GetCurrentOverideList(int playerIdx);

    // 0x00122a40: true while fruit/bombs still active for player.
    bool  IsWaveProcessing(int playerIdx);

    // --- Mutators -----------------------------------------------------

    // 0x001218ac: increments speed-loss timer.
    void AddToSpeedLossTime(float amount, int playerIdx);

    // 0x00122e94: resets m_Speed + combo fields.
    void ResetSpeed(int playerIdx);

    // 0x00123510: add to combo speed; triggers blitz SFX/score.
    void AddSpeed(float amount, int playerIdx);

    // 0x00122af8: network sync receive (multiplayer).
    void RecievedSync(int waveIdx, float score);

    // --- Power-up modifiers (PowerUpManager::Update calls these) ------

    // 0x001286fc: m_BombChance *= mult
    void BombScale(float mult);

    // 0x0012870c: m_SpawnLevel *= mult
    void BombMultiplyer(float mult);

    // 0x0012871c: m_FruitChance *= mult
    void FruitMultiplyer(float mult);

    // 0x0012872c: m_CritChanceMult *= mult
    void CriticalChanceMod(float mult);

    // --- Networking stubs (defunct online MP) ------------------------

    // 0x001217e0: always returns 0.
    static int  UpdateNetworking(float dt, int playerIdx);

    // 0x0012197c: empty.
    static void SendWaveSyncPacket();

    // 0x00121980: always false.
    static bool ShouldDisplayNetworkWaitIndicator();

    // 0x00121a1c: calls COIN_CHANCEINATOR::GetCoins().
    static void RequestCoins();

    // Split whitespace-separated string into tokens. Returns the count written.
    // Binary return value used by PROBABILITY_OVERIDE::Parse to set m_field68.
    static int SplitWords(const char* str, std::vector<std::string>& out);

private:
    // Parse placement string to SpawnPlacement enum.
    static SpawnPlacement ParsePlacement(const char* side);
};

// Layout asserts for the re-laid-out +0x4c..+0x77 block and the tail.
// The -4 byte shift absorbed entirely within +0x4c..+0x77; +0x78 onward unchanged.
// v1.6.1 binary struct @ 0x00123ef8 ctor + AddSpeed @0x00124f48 + Update @0x001267a0
#ifdef __bada__
static_assert(offsetof(WaveManager, m_ComboTimer)        == 0x50,
              "WaveManager m_ComboTimer offset mismatch");
static_assert(offsetof(WaveManager, m_ComboSpeed)        == 0x58,
              "WaveManager m_ComboSpeed offset mismatch");
static_assert(offsetof(WaveManager, m_TargetComboSpeed)  == 0x5c,
              "WaveManager m_TargetComboSpeed offset mismatch");
static_assert(offsetof(WaveManager, m_BlitzLevel)        == 0x60,
              "WaveManager m_BlitzLevel offset mismatch");
static_assert(offsetof(WaveManager, m_ColdTimer)         == 0x64,
              "WaveManager m_ColdTimer offset mismatch");
static_assert(offsetof(WaveManager, m_BombChance)        == 0x6c,
              "WaveManager m_BombChance offset mismatch");
static_assert(offsetof(WaveManager, m_FruitChance)       == 0x70,
              "WaveManager m_FruitChance offset mismatch");
static_assert(offsetof(WaveManager, m_CritChanceMult)    == 0x74,
              "WaveManager m_CritChanceMult offset mismatch");
static_assert(offsetof(WaveManager, m_SpeedAccum)        == 0x78,
              "WaveManager m_SpeedAccum offset mismatch");
// Tail assert: binary region boundary at 752 bytes (port extensions follow).
static_assert(offsetof(WaveManager, m_pWaveQue) == 752,
              "WaveManager binary-region layout mismatch (expect 752 bytes before port extensions)");
#endif

#endif  // FN_WAVE_MANAGER_H
