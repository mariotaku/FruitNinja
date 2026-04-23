#ifndef FN_WAVE_MANAGER_H
#define FN_WAVE_MANAGER_H

// WaveManager — stubs for the wave/spawn subsystem.
//
// Full class at 0x0022ee80 (singleton). Binary function table and field
// offsets are documented in docs/functions/wave.md, docs/systems/wave-system.md,
// docs/structs/wave.md.
//
// All members below are signature-only stubs so the rest of the port (Bomb
// chain spawn, Fruit critical RNG, GameInit draw/destroy hooks, etc.) can
// call into WaveManager without ifdefs. Every body is a no-op or returns a
// safe default; every method comment cites the original binary address so
// real logic can be filled in later.
//
// Analysed: 2026-04-23T00:00

// Forward-declare binary structs so the header stays lean. Real layouts
// live in docs/structs/wave.md (WAVE_INFO 0x78, SPAWNER_INFO 0x64,
// DEFAULT_WAVE_INFO 0x40, COIN_CHANCEINATOR 0x08, PROBABILITY_OVERIDE).
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
    // --- Fields accessed cross-module. Keep public for now; mirror the
    // binary layout where needed. ---

    // +0x68: bomb chain-spawn level. Read by Bomb::Update when a bomb's
    // fuse runs out (see Bomb.cpp chain-bomb path).
    float spawnLevel;        // default 0.0f

    // +0x70: global critical-chance multiplier (used by CriticalChanceMod,
    // GetCriticalChance). Default 1.0f.
    float m_CritChanceMult;

    // --- Construction / singleton --------------------------------------

    // 0x0012328c / 0x00123398 / 0x000f3660 (thunk): constructs RNG, zero-
    // initialises wave vectors and default-wave-info table.
    WaveManager();

    // 0x00121c78 / 0x00121d1c: calls Destroy() then destroys sub-objects.
    ~WaveManager();

    // 0x00123328: Meyer's singleton at 0x0022ee80.
    static WaveManager* GetInstance();

    // --- Lifecycle -----------------------------------------------------

    // 0x0012393c (470 lines): loads xml/originalWaveList.xml per game mode,
    // builds WAVE_INFO/SPAWNER_INFO/COIN_CHANCEINATOR/PROBABILITY_OVERIDE.
    void Init();

    // 0x00121bf0: frees WAVE_INFO list, WaveQue, WaveQueItem.
    void Destroy();

    // 0x00125be4: full state reset between games. fullReset re-randomises
    // wave chances and clears fruit/bomb queues.
    void Reset(bool fullReset);

    // 0x00124b1c: restore state from FruitSaveData (re-spawns saved
    // entities, calls SkipToPause/SkipToGameOver).
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

    // 0x001259d8 (89 lines): per-frame update — resets multipliers,
    // calls PowerUpManager::Update, advances wave timer, pumps UpdateWave
    // on fixed timestep, checks completion conditions.
    void Update(float dt);

    // 0x00125390 (298 lines): one tick of wave processing — iterates
    // spawners, decrements timers, calls SpawnFruit/SpawnBomb, rolls
    // probability overrides, transitions to GetNextWave when drained.
    void UpdateWave(float dt, int playerIdx, int unk);

    // 0x00122f50: blitz-combo speed update (includes AddControl for the
    // speed-control HUD meter).
    void UpdateComboSpeed(float dt);

    // --- Wave progression ---------------------------------------------

    // 0x00124f10 (227 lines): advance to next WAVE_INFO using score-based
    // selection or the pre-built WaveQue (survival/combo modes).
    void GetNextWave(int playerIdx);

    // 0x00125340: seek to a specific wave number; used by Resume.
    void SetCurrentWave(int waveNo, float delay, int playerIdx);

    // 0x00124564 (142 lines): build the wave queue for survival/combo.
    void SetupWaveQue();

    // --- Spawning -----------------------------------------------------

    // 0x00121fa8: spawn N bombs from a spawner (or chain-spawn via nullptr).
    // Called from Bomb::Update chain-bomb path when spawnLevel >= 2.
    void SpawnBomb(long count, long type, SPAWNER_INFO* spawner, int playerIdx);

    // 0x001225a0 (248 lines): spawn N fruits from a spawner.
    void SpawnFruit(long count, long fruitType, SPAWNER_INFO* spawner, int playerIdx);

    // 0x00122ad8: clear all unspawned spawner entries.
    void ClearUnspawned();

    // --- Rendering / HUD glue -----------------------------------------

    // 0x00122ae8: wave overlay draw (wave name banner, etc.).
    void Draw(int playerIdx);

    // 0x001217d4: clears cached speed-control HUDControl* if it matches.
    void DeleteSpeedControl(HUDControl* control);

    // --- Queries -------------------------------------------------------

    // 0x00121834: m_Speed[playerIdx].
    float GetSpeed(int playerIdx);

    // 0x001218dc: effective wave dt (clamped using speed + WAVE_INFO fields).
    float GetWavedt(int playerIdx);

    // 0x001219c4: returns waveCritChance * m_CritChanceMult, or 1.0f if no
    // current wave.
    float GetCriticalChance(int playerIdx);

    // 0x001219e4: returns true if a slice should be "critical" this tick.
    bool  CriticalMode(int playerIdx);

    // 0x00121840: combo bonus progression [0..1].
    float GetComboBonusProgression(int playerIdx);

    // 0x0012180c: override list slice for current wave + player.
    PROBABILITY_OVERIDE* GetCurrentOverideList(int playerIdx);

    // 0x00122a40: true while fruit/bombs still active for player (used by
    // UpdateWave to defer GetNextWave until the wave drains).
    bool  IsWaveProcessing(int playerIdx);

    // --- Mutators -----------------------------------------------------

    // 0x001218ac: increments speed-loss timer (clamped to 1.0).
    void AddToSpeedLossTime(float amount, int playerIdx);

    // 0x00122e94: resets m_Speed + combo fields; clears blitz_bonus total.
    void ResetSpeed(int playerIdx);

    // 0x00123510: add to combo speed; triggers blitz SFX/score.
    void AddSpeed(float amount, int playerIdx);

    // 0x00122af8: network sync receive (multiplayer).
    void RecievedSync(int waveIdx, float score);

    // --- Power-up modifiers (PowerUpManager::Update calls these) ------

    // 0x001286fc: `field_0x64 *= mult` — scales bomb spawn rate.
    void BombScale(float mult);

    // 0x0012870c: `spawnLevel *= mult` — scales bomb chain level.
    void BombMultiplyer(float mult);

    // 0x0012871c: `field_0x6c *= mult` — scales fruit spawn rate.
    void FruitMultiplyer(float mult);

    // 0x0012872c: `m_CritChanceMult *= mult`.
    void CriticalChanceMod(float mult);

    // --- Networking stubs (defunct online MP) ------------------------

    // 0x001217e0: UpdateNetworking always returns 0 — online MP is defunct.
    static int  UpdateNetworking(float dt, int playerIdx);

    // 0x0012197c: SendWaveSyncPacket — empty.
    static void SendWaveSyncPacket();

    // 0x00121980: ShouldDisplayNetworkWaitIndicator — always false.
    static bool ShouldDisplayNetworkWaitIndicator();

    // 0x00121a1c: RequestCoins — calls COIN_CHANCEINATOR::GetCoins().
    static void RequestCoins();
};

#endif  // FN_WAVE_MANAGER_H
