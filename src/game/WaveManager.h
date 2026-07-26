#ifndef FN_WAVE_MANAGER_H
#define FN_WAVE_MANAGER_H

// WaveManager — wave spawning subsystem.
//
// Binary class at 0x0022ee80 (singleton), 752 bytes (0x2f0).
// v1.6.1 WaveManager ctor @ 0x00123ef8

#include "math/Random.h"
#include "game/WaveStructs.h"
#include <vector>
#include <cstdint>

struct WaveInfo;
struct SPAWNER_INFO;
struct PROBABILITY_OVERIDE;
struct DEFAULT_WAVE_INFO;
struct COIN_CHANCEINATOR;
struct WaveQue;
struct WaveQueItem;
class  FruitSaveData;
class  GlobalProbabilityOveride;
class HUDControl;
class HUDControl3d;
class SpeedControl;
namespace Mortar { class Entity; }

class WaveManager {
public:
    // +0x00: two distinct binary slots.
    // [0] +0x00: MP/networking spawn-suppression gate. UpdateWave reads this as a
    //            byte gate: if (*this+0x00 != 0) skip spawn loop. Reset() writes 0.
    //            In single-player this never gets written non-zero -> gate always open.
    //            Binary: ldrb r3,[r0,#0x0]; cmp r3,r2; bne epilogue (UpdateWave @0x00125dac).
    // [1] +0x04: lazy arcade SpeedControl HUD widget pointer. UpdateComboSpeed allocates it;
    //            DeleteSpeedControl nulls it. Binary @ 0x00122f50 (UpdateComboSpeed): stores
    //            into [r0,#0x4]. Binary @ 0x001217d4 (DeleteSpeedControl): ldr from [r0,#0x4].
    // BUG HISTORY: port incorrectly used slot [0] for BOTH, so UpdateComboSpeed
    //              filling [0] permanently closed the gate -> 1-fruit-then-stall in Arcade.
    HUDControl3d* m_SpeedControl[2];   // +0x00 (gate), +0x04 (SpeedControl widget)

    // +0x08: RNG instance.
    // DIFFERS: binary fetches Random via a GOT-relative global pointer; port embeds
    // it as a member so that m_SpeedControl correctly occupies +0x00/+0x04.
    // +0x08..+0x1f (24 bytes); bridges into the binary's +0x08..+0x34 unnamed region.
    Math::Random m_Random;   // Port specific: +0x08

    // +0x20..+0x23: binary's m_pRandom GOT-relative pointer slot (not used in port;
    // port embeds m_Random above instead of fetching via pointer).
    uint8_t _pad_0x20[4];     // +0x20

    // +0x24: wave queue item pointer. Binary: WaveQueItem* @ +0x24.
    // v1.6.1 WaveManager::Destroy @0x00123b54 / SetupWaveQue @0x00124564
    WaveQueItem* m_pWaveQueItem;  // +0x24

    // +0x28: wave queue pointer. Binary: WaveQue* @ +0x28.
    // v1.6.1 WaveManager::Destroy @0x00123b54 / SetupWaveQue @0x00124564
    WaveQue* m_pWaveQue;          // +0x28

    // +0x2c..+0x34: unnamed binary padding (9 bytes) to reach m_SyncLocalReady at +0x35.
    uint8_t _pad_0x2c[9];     // +0x2c

    // +0x35: DEFUNCT MP wave-sync "local ready" flag (binary offset 0x39).
    // Reset writes 1; ShouldDisplayNetworkWaitIndicator @0x00123130 returns true only
    // when this == 0 (i.e. local wave not yet ready). SP never displays the indicator.
    // v1.6.1 WaveManager::Reset @0x0012ba78 / ShouldDisplayNetworkWaitIndicator @0x00123130
    uint8_t m_SyncLocalReady;  // +0x35
    // +0x36: DEFUNCT MP wave-sync "remote pending" flag (binary offset 0x3a).
    // Reset writes 0; ShouldDisplayNetworkWaitIndicator requires this != 0 to show wait UI.
    // v1.6.1 ShouldDisplayNetworkWaitIndicator @0x00123130
    uint8_t m_SyncRemotePending; // +0x36
    // +0x37: DEFUNCT MP "sync received" flag (binary offset 0x3b).
    // RecievedSync @0x00123444 sets this = 1 on an inbound wave-sync packet; Reset clears it.
    // v1.6.1 WaveManager::RecievedSync @0x00123444
    uint8_t m_SyncReceived;    // +0x37
    // +0x38: DEFUNCT MP received wave index (int, binary offset 0x3c, -1 = none).
    // RecievedSync stores the inbound waveIdx param here; Reset writes -1.
    // v1.6.1 WaveManager::RecievedSync @0x00123444
    int m_SyncWaveIdx;         // +0x38
    // +0x3c: gap to reach +0x40
    uint8_t _pad_0x3c[4];    // +0x3c
    // +0x40: DEFUNCT MP received score (float). RecievedSync stores the inbound score param
    // here when score < 999.0. No SP reader. v1.6.1 WaveManager::RecievedSync @0x00123444
    float m_SyncScore;         // +0x40
    // +0x44: DEFUNCT net timer A (binary m_NetTimerA @0x44; MP retry timer; dead code in SP).
    // RecievedSync zeroes it; UpdateWave bumps it when game_work.fM_bMPRetryPending.
    // v1.6.1 WaveManager::RecievedSync @0x00123444 / UpdateWave @0x00125d7c
    float m_NetTimerA;         // +0x44
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

    // +0x80: dt * combo divisor for PowerUpManager::Update; also the target of
    // SetAbsoluteDtMod @0x001bee08 (super-fruit finale slow-mo: 0.1 during
    // pre-roll, eased back to 1.0 by UpdateExplosion, 1.0 on finale end/reset).
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

    // +0x244: per-player wave-active flag (IsWaveProcessing @0x001232c4 reads m_NextWaveDelay[p+8],
    // i.e. byte at 0x23c+p+8 = 0x244 for p0). Set to 1 by Reset and after each spawn in UpdateWave;
    // cleared to 0 by IsWaveProcessing when no entities/timers remain.
    // v1.6.1 IsWaveProcessing @0x001232c4 / UpdateWave @0x00125d7c (was field_0x23c)
    uint8_t m_WaveActive;               // +0x244 (was field_0x244 / field_0x23c)
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

    // +0x2d8: DEFUNCT MP synced-at score snapshot (int). RecievedSync @0x00123444 stores
    // GetCurrentScore(2) here when an inbound wave-sync packet arrives; Reset clears to 0.
    // v1.6.1 WaveManager::RecievedSync @0x00123444 / Reset @0x0012ba78
    int m_SyncScoreSnapshot;            // +0x2d8 (was field_0x2d8 / field_0x2d0)

    // +0x2dc: fixed-timestep accumulator (init 0.0f).
    float m_StepAccumulator;            // +0x2dc (was field_0x2d4)

    // +0x2e0: saved-wave-delay shuttle (binary treats as float; NOT a pointer — port had
    // m_pWaveQue here!). SaveWaveInfo @0x001254b0 copies this into FruitSaveData::m_WaveDelay;
    // Resume @0x0012bf58 copies m_WaveDelay back into it. Holds the P0 pre-spawn delay across
    // save/restore. Reset clears to 0. Kept as int (4 bytes, same layout); the port doesn't
    // read it arithmetically. v1.6.1 SaveWaveInfo @0x001254b0 / Resume @0x0012bf58
    int m_SavedWaveDelay;               // +0x2e0 (was field_0x2e0)

    // +0x2e4: global probability override list (12 bytes on __bada__).
    // v1.6.1 WaveManager field @ 0x00125be4
    std::vector<GlobalProbabilityOveride*> m_GlobalProbabilityOverride;  // +0x2e4

    // --- Binary region END: +0x2f0 (752 bytes) --------------------------

    // --- Construction / singleton --------------------------------------

    WaveManager();
    ~WaveManager();

    static WaveManager* GetInstance();

    // --- Lifecycle -----------------------------------------------------

    // 0x0012393c: loads xml/<mode>WaveList.xml for each mode,
    // builds WAVE_INFO/SPAWNER_INFO arrays.
    void Init();

    // v1.6.1 WaveManager::Destroy @0x00123b54: frees WAVE_INFO list, GlobalProbabilityOveride list, WaveQue, WaveQueItem.
    void Destroy();

    // 0x00125be4: full state reset between games.
    void Reset(bool fullReset);

    // 0x0012bf58: restore state from FruitSaveData.
    void Resume();

    // 0x001247f0: serialise current wave state into FruitSaveData.
    int  SaveWaveInfo(FruitSaveData* save);

    // 0x00121f74 / 0x00121f90: static entry points.
    static void GameOver();
    static void NewGame();


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

    // 0x001247c4: spawn N bombs. spawner=nullptr for default-bottom spawn.
    // ASM-spec v1.6.1 WaveManager::SpawnBomb @0x001247c4: (long count, SPAWNER_INFO* spawner, int playerIdx)
    void SpawnBomb(long count, SPAWNER_INFO* spawner, int playerIdx);

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

    // v1.6.1 WaveManager::CriticalMode @0x00123194: returns true if a slice should be "critical" this tick.
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

    // v1.6.1 WaveManager::SetAbsoluteDtMod @0x001bee08: writes +0x80
    // (m_ComboSpeedDivisor) -- NOT m_SpeedScale (+0x7c), which Update overwrites
    // from PowerUpManager::m_DtMod every frame. GetWavedt multiplies its result
    // by this field, so the super-fruit finale's 0.1 write is what actually
    // slows/speeds global wave time.
    void SetAbsoluteDtMod(float v);

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
    // Binary return value used by PROBABILITY_OVERIDE::Parse to set m_TypeCount.
    static int SplitWords(const char* str, std::vector<std::string>& out);

    // v1.6.1 WaveManager::ParseGlobalProbabilityOverides @0x00129718
    // Loads globalprobabilities.xml and populates m_GlobalProbabilityOverride.
    void ParseGlobalProbabilityOverides(const char* path);

    // v1.6.1 WaveManager::CheckForGlobalProbabilityOveride @0x00123228
    // Iterates m_GlobalProbabilityOverride; returns the first GPO that fires
    // (writes outType) or null if none fire.
    GlobalProbabilityOveride* CheckForGlobalProbabilityOveride(int& outType);

private:
};

// Free functions (binary: _Z13PowersEnabledv @ 0x0011a034, _Z14ParsePlacementPKc).
bool PowersEnabled();
SpawnPlacement ParsePlacement(const char* side);

// ParseSpawner -- v1.6.1 @0x00129314. Parses a <Spawn> element into SPAWNER_INFO.
// Returns 1 if the element's tag hashes to StringHash("Spawn"), else 0.
int ParseSpawner(TiXmlElement* elem, SPAWNER_INFO* out);

// v1.6.1 GetRandomPowerSpawner @0x0012403c: returns a pointer to one of three function-local
// static SPAWNER_INFO entries (bottom-center, right-side, left-side), lazily initialised.
// includeCenter=true picks from all 3 entries; false skips entry 0 (center) and picks from
// entries 1/2 (right/left) only.
SPAWNER_INFO* GetRandomPowerSpawner(bool includeCenter);

// v1.6.1 ReachedEnd @0x1253c0: saves "limitsReached" stat, plays "time-up" SFX, calls GameOver.
void ReachedEnd();

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
// WaveQue pointer fields at binary offsets +0x24/+0x28.
// v1.6.1 WaveManager::Destroy @0x00123b54
static_assert(offsetof(WaveManager, m_pWaveQueItem) == 0x24,
              "WaveManager m_pWaveQueItem must be at +0x24");
static_assert(offsetof(WaveManager, m_pWaveQue) == 0x28,
              "WaveManager m_pWaveQue must be at +0x28");

// Guard the tail so the wasm32 build catches future field drift.
// v1.6.1 WaveManager ctor @0x00123ef8 / GetInstance @0x00123fa4 (static singleton, 0x2f0)
static_assert(offsetof(WaveManager, m_pCurrentWave)              == 0x234, "");
static_assert(offsetof(WaveManager, m_NextWaveDelay_P0)          == 0x23c, "");
static_assert(offsetof(WaveManager, m_FruitQueue)                == 0x24c, "");
static_assert(offsetof(WaveManager, m_GlobalProbabilityOverride) == 0x2e4, "");
static_assert(sizeof(WaveManager)                                == 0x2f0, "");
#endif

#endif  // FN_WAVE_MANAGER_H
