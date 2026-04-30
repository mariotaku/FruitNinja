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

class WaveManager {
public:
    // +0x000: RNG instance. Used for wave selection, spawn angle/count RNG.
    // Binary: Math::Random at +0x00 (24 bytes).
    Random m_Random;

    // +0x035: wave-active flag
    uint8_t field_0x35;
    // +0x036: reset flag
    uint8_t field_0x36;
    // +0x037: misc flag
    uint8_t field_0x37;
    // +0x038: last selected wave index (int, -1 = none)
    int field_0x38;
    // +0x040: play-time accumulators
    float field_0x40;
    float field_0x44;
    // +0x048
    int field_0x48;
    // +0x04c: combo timer (per-player stride 4)
    float field_0x4c;
    float field_0x4c_p1;
    // +0x054: wave speed per player [2]
    float m_Speed[2];            // +0x54, +0x58
    // +0x058: boosted speed slot (overlaps m_Speed[1], per AddSpeed note)
    // field_0x58 == &m_Speed[1]  -- used by AddSpeed for player 0
    // +0x05c
    int field_0x5c;
    // +0x060: combo timer 2 per player
    float field_0x60;
    float field_0x60_p1;
    // +0x064: fruit-multiplier (BombScale power-up)
    float field_0x64;
    // +0x068: bomb chain spawn level (BombMultiplyer power-up)
    float spawnLevel;
    // +0x06c: fruit spawn multiplier (FruitMultiplyer power-up)
    float field_0x6c;
    // +0x070: critical chance multiplier
    float m_CritChanceMult;
    // +0x074: speed accumulator (Reset loads m_SpeedMultPerMode[gameMode])
    float field_0x74;
    // +0x078: dtMod from PowerUpManager (default 1.0)
    float field_0x78;

    // +0x0ac: waveInfos — 4 vectors of WAVE_INFO* (one per game mode).
    // Binary: base at +0xac, stride 0xc per mode (std::vector layout).
    // Port: flat array of 4 vectors.
    std::vector<WAVE_INFO*> waveInfos[4];

    // +0x0dc: DEFAULT_WAVE_INFO[4] (each 0x40 bytes = 64 bytes)
    DEFAULT_WAVE_INFO defaultWaveInfo[4];

    // +0x1dc: COIN_CHANCEINATOR[4] (each 0x08 bytes)
    COIN_CHANCEINATOR coinChance[4];

    // +0x1fc: PROBABILITY_OVERIDE lists per game mode
    std::vector<PROBABILITY_OVERIDE> probOverrides[4];

    // +0x22c: current wave pointer per player [2]
    WAVE_INFO* m_pCurrentWave[2];

    // +0x230 (alias): wave count per player. GetNextWave increments.
    // Binary uses the same storage as m_pCurrentWave_P1 for count in SP.
    int m_WaveCount[2];

    // +0x234: per-player wave delay accumulator
    float field_0x234;
    float field_0x238;

    // +0x23c: per-player "wave-was-spawned" flag (IsWaveProcessing reads this)
    uint8_t field_0x23c;
    uint8_t field_0x23d;    // PROBABILITY_OVERIDE flags
    uint8_t field_0x23e;
    uint8_t _pad23f;

    // +0x240: random delay
    float field_0x240;

    // +0x244..+0x2c3: m_FruitQueue[2][32] — fruit type queue per player
    int m_FruitQueue[2][32];

    // +0x2c4: fruit queue sizes
    int m_FruitQueueSize[2];

    // +0x2cc: misc counters
    int field_0x2cc;
    int field_0x2d0;

    // +0x2d4: wave-step accumulator (fixed timestep)
    float field_0x2d4;

    // Wave queue (survival/combo modes — null in normal play)
    WaveQue*     m_pWaveQue;
    WaveQueItem* m_pWaveQueItem;

    // Score threshold per player (for ChooseFrom logic)
    int m_ScoreThreshold[2];

    // Per-player next-wave delay
    float m_NextWaveDelay[2];

    // Per-player wave timer countdown
    float m_WaveTimer[2];

    // Per-mode dtInc (speed accumulator multiplier). binary field_0x7c[4].
    // Parsed from <defaults> "dtInc" attr per mode. DIFFERS: was m_SpeedMultPerMode at +0x8c (wrong field).
    // binary @ 0x00125ac4: speed = field_0x74 + dt * +0x7c[mode]
    float m_DtIncPerMode[4];

    // Per-mode globalDtStart lower bound. binary field_0x8c[4]. placeholder.
    // DIFFERS: was named m_SpeedMultPerMode (mis-mapped to dtInc slot above).
    float m_SpeedMultPerMode[4];

    // Per-mode speed lower bound (binary field_0x8c[4], 0x125ba2-0x125aa6).
    // TODO: per-mode bounds need RE -- using 1.0 as placeholder.
    // DIFFERS: original values unknown from DAT; using 1.0f per mode.
    float field_0x8c[4];

    // Per-mode speed upper bound (binary field_0x9c[4], 0x125ba2-0x125aa6).
    // TODO: per-mode bounds need RE -- using 100.0 as placeholder.
    // DIFFERS: original values unknown from DAT; using 100.0f per mode.
    float field_0x9c[4];

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

    Random& GetRandom() { return m_Random; }

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

private:
    // Parse placement string to SpawnPlacement enum.
    static SpawnPlacement ParsePlacement(const char* side);

    // Split comma-separated string into tokens, trimming whitespace.
    static void SplitWords(const char* str, std::vector<std::string>& out);
};

#endif  // FN_WAVE_MANAGER_H
