#ifndef FN_WAVE_MANAGER_H
#define FN_WAVE_MANAGER_H

// WaveManager — wave spawning subsystem.
//
// Full class at 0x0022ee80 (singleton). Binary function table and field
// offsets are documented in docs/functions/wave.md, docs/systems/wave-system.md,
// docs/structs/wave.md, docs/systems/wave-system-impl.md.
//
// Analysed: 2026-04-30T00:00

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
    float field_0x44;         // +0x44
    // +0x48
    int field_0x48;           // +0x48
    // +0x4c: per-player combo "blitz" timer (decays via GetWavedt; AddSpeed
    // pumps to 1.0f on every successful blitz multi-slice).
    // ASM-verified: 2026-05-22 binary @ 0x00123510 / 0x00122f50 (re-analyst).
    float m_ComboTimer[2];    // +0x4c (P0), +0x50 (P1)
    // +0x54: wave speed per player [2]
    float m_Speed[2];         // +0x54 (P0), +0x58 (P1)
    // +0x5c: per-player blitz score-level (int, 1..6+) / cold-timer (float, resets 3.0f).
    //   Binary aliases m_BlitzBonus[1] (int) with m_ColdTimer[0] (float) at +0x60:
    //   same 4-byte slot, int interpretation = blitz level, float = cold-down timer.
    //   Single-player uses P0 slot for blitz level (int) and the P1/ColdTimer slot
    //   for the cold-timer float. Union preserves binary layout exactly.
    // ASM-verified: 2026-05-22 binary @ 0x00123510 AddSpeed (asm-inspector).
    union {
        int   m_BlitzBonus[2];       // +0x5c: [0]=P0 blitz level (int), [1]=P1 (int; aliases m_ColdTimer[0])
        struct {
            int   _m_BlitzP0;        // +0x5c (use m_BlitzBonus[0] instead)
            float m_ColdTimer[1];    // +0x60: P0 cold-down timer (float; aliases m_BlitzBonus[1])
        };
    };
    // +0x64: bomb scale multiplier (BombScale power-up)
    float field_0x64;         // +0x64
    // +0x68: bomb chain spawn level (BombMultiplyer power-up)
    float spawnLevel;         // +0x68
    // +0x6c: fruit spawn multiplier (FruitMultiplyer power-up)
    float field_0x6c;         // +0x6c
    // +0x70: critical chance multiplier
    float m_CritChanceMult;   // +0x70
    // +0x74: global speed accumulator (Reset loads m_SpeedClampStart[gameMode])
    float field_0x74;         // +0x74
    // +0x78: dtMod from PowerUpManager (default 1.0)
    float field_0x78;         // +0x78
    // +0x7c: per-mode dtInc (speed accumulator increment). Parsed from <defaults> "dtInc".
    // binary @ 0x00125ac4: speed = field_0x74 + dt * m_DtIncPerMode[mode]
    float m_DtIncPerMode[4];      // +0x7c
    // +0x8c: per-mode globalDtStart lower bound. Parsed from <defaults> "globalDtStart".
    // DIFFERS: original values unknown; using 1.0f per mode as placeholder.
    float m_SpeedClampStart[4];   // +0x8c
    // +0x9c: per-mode speed upper bound. Parsed from <defaults> "globalDtMax".
    // DIFFERS: original values unknown; using 100.0f per mode as placeholder.
    float m_SpeedClampMax[4];     // +0x9c

    // +0xac: 4 vectors of WAVE_INFO* (one per game mode). Binary stride 0xc per mode.
    std::vector<WAVE_INFO*> m_WaveInfo[4];      // +0xac

    // +0xdc: per-mode default wave parameters (64 bytes each).
    DEFAULT_WAVE_INFO m_DefaultWaveInfo[4];     // +0xdc

    // +0x1dc: per-mode coin chance tables (8 bytes each).
    COIN_CHANCEINATOR m_CoinChanceinator[4];    // +0x1dc

    // +0x1fc: per-mode probability override lists (12 bytes each).
    std::vector<PROBABILITY_OVERIDE> m_ProbabilityOverride[4];  // +0x1fc

    // +0x22c/+0x230: current wave pointer (MP) or wave count (SP) — binary stores
    // the SP wave count in the m_pCurrentWave_P1 slot (+0x230) by aliasing.
    union {
        WAVE_INFO* m_pCurrentWave[2];  // +0x22c: [0]=P0 wave ptr, [1]=P1/SP wave count
        int        m_WaveCount[2];     // +0x22c: [0]=P0 count (aliases m_pCurrentWave[0]),
                                       //          [1]=SP wave count (aliases m_pCurrentWave[1])
    };

    // +0x234..+0x2d3: binary unnamed region (160 bytes).
    // Fields below are port-derived names for binary offsets within this range.

    // +0x234: P0 pre-spawn delay timer ("delay" XML attr).
    // Binary @ 0x0012598c reads [+0x234]; GetNextWave @ 0x001251ee writes [+0x234].
    float field_0x234;        // +0x234

    // +0x238: P0 wave-end wait timer ("wait" XML attr).
    // Binary @ 0x00125956 reads [+0x238]; GetNextWave @ 0x00125224 writes [+0x238].
    // (Binary also uses +0x238 as P1 delay alias and +0x23c as P1 wait alias, but
    //  SP always uses P0 only.)
    float field_0x238;        // +0x238

    // +0x23c: wave-was-spawned flag (IsWaveProcessing reads this).
    uint8_t field_0x23c;      // +0x23c
    // +0x23d: blitz spawned-this-game counter (byte).
    uint8_t field_0x23d;      // +0x23d
    // +0x23e: blitz force-spawned counter (byte).
    uint8_t field_0x23e;      // +0x23e
    uint8_t _pad23f;          // +0x23f

    // +0x240: blitz spawn time (float).
    float field_0x240;        // +0x240

    // +0x244..+0x2c3: fruit type queue, P0 only, 32 ints (-1 = empty slot).
    // Binary @ 0x00124cf4/0x0016cd08: 128-byte (0x80) queue for P0.
    int m_FruitQueue[32];     // +0x244

    // +0x2c4: P0 fruit queue active entry count.
    // +0x2c8: P1/secondary fruit queue active entry count (binary field_0x2c8).
    // Binary Reset @ 0x00125be4 clears both (was noted "not in port struct" -- now named).
    int m_FruitQueueSize[2];  // +0x2c4 (P0), +0x2c8 (P1)

    // +0x2cc: misc counter (binary unnamed; reset to 0 in Reset).
    int field_0x2cc;          // +0x2cc
    // +0x2d0: misc counter (binary unnamed; reset to 0 in Reset).
    int field_0x2d0;          // +0x2d0

    // +0x2d4: wave-step accumulator (fixed timestep, init 0.0f).
    float field_0x2d4;        // +0x2d4

    // Port specific: WaveQue state pointers — not in the binary WaveManager struct
    // (binary survival/combo mode stores these differently). Declared after the binary
    // layout so the static_assert below fires correctly for binary-identical offsets.
    // DIFFERS: port adds m_pWaveQue/m_pWaveQueItem beyond binary sizeof(WaveManager)=728.
    WaveQue*     m_pWaveQue;      // port-only, beyond +0x2d8
    WaveQueItem* m_pWaveQueItem;  // port-only

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

    // 0x001259d8 (89 lines): fixed-timestep pump + multiplier resets.
    void Update(float dt);

    // 0x00125390 (298 lines): one tick of wave spawning.
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

    // 0x001225a0 (248 lines): spawn N fruits.
    void SpawnFruit(long count, long fruitType, SPAWNER_INFO* spawner, int playerIdx);

    // 0x00122ad8: clear all unspawned spawner entries.
    void ClearUnspawned();

    // --- Rendering / HUD glue -----------------------------------------

    // 0x00122ae8: wave overlay draw.
    void Draw(int playerIdx);

    // 0x001217d4: clears cached speed-control HUDControl* if it matches.
    void DeleteSpeedControl(HUDControl* control);

    // --- Queries -------------------------------------------------------

    Math::Random& GetRandom() { return m_Random; }

    // 0x00121834: m_Speed[playerIdx].
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

    // 0x001286fc: field_0x64 *= mult
    void BombScale(float mult);

    // 0x0012870c: spawnLevel *= mult
    void BombMultiplyer(float mult);

    // 0x0012871c: field_0x6c *= mult
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

// Binary WaveManager is 728 bytes (0x2d8). Port adds m_pWaveQue/m_pWaveQueItem
// beyond that, so the full port sizeof is 736. The assert below checks that
// m_pWaveQue sits exactly at offset 728 (= binary boundary), verifying all
// binary-named members landed at the right offsets.
#ifdef __bada__
static_assert(offsetof(WaveManager, m_pWaveQue) == 728,
              "WaveManager binary-region layout mismatch (expect 728 bytes before port extensions)");
#endif

#endif  // FN_WAVE_MANAGER_H
