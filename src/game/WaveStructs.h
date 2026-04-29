#ifndef FN_WAVE_STRUCTS_H
#define FN_WAVE_STRUCTS_H

// Wave data structs — binary layouts from docs/structs/wave.md and
// docs/systems/wave-system.md.
//
// WAVE_INFO      : 0x78 bytes (docs §WAVE_INFO table)
// SPAWNER_INFO   : 0x64 bytes (docs §SPAWNER_INFO table)
// DEFAULT_WAVE_INFO : 0x40 bytes (docs §DEFAULT_WAVE_INFO table)
// COIN_CHANCEINATOR : 0x08 bytes (docs §wave.md)
//
// Analysed: 2026-04-30T00:00

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// Spawn-side enum. ParsePlacement maps XML "side" attr to this.
// 0=bottom, 1=bottom-slow, 2=left, 3=right, 4=random-side
enum SpawnPlacement : uint8_t {
    PLACEMENT_BOTTOM      = 0,
    PLACEMENT_BOTTOM_SLOW = 1,
    PLACEMENT_LEFT        = 2,
    PLACEMENT_RIGHT       = 3,
    PLACEMENT_RANDOM_SIDE = 4,
};

// SPAWNER_INFO — size 0x64 (100 bytes), per docs/systems/wave-system.md
// Matches offsets cited in wave-system-impl.md SpawnBomb and SpawnFruit specs.
struct SPAWNER_INFO {
    // +0x00: hash array for fruit types (allocated from type name list)
    int*                 m_pFruitTypeHashes; // +0x00
    // +0x04: fruit type name list (from SplitWords on "types" attr)
    std::vector<std::string> m_FruitTypeNames; // +0x04
    // +0x10: count of distinct fruit types in "types" attr
    int                  m_FruitTypeCount;   // +0x10

    float                m_TimeScale;        // +0x14: "timescale" attr
    float                m_Offset_x;         // +0x18: "offset" vec3 x
    float                m_Offset_y;         // +0x1c
    float                m_Offset_z;         // +0x20
    float                m_MinAngle;         // +0x24: "minangle" / "horizmin"
    float                m_MaxAngle;         // +0x28: "maxangle" / "horizmax"
    float                m_MinVel;           // +0x2c: "minvel" / velYscale
    float                m_MaxVel;           // +0x30: "maxvel"
    SpawnPlacement       m_SpawnType;        // +0x34: side attr via ParsePlacement
    uint8_t              _pad35[3];

    float                m_SpawnMin;         // +0x38: "min" attr
    float                m_SpawnMax_unused;  // +0x3c (unused per docs)
    float                m_SpawnMax;         // +0x40: "max" attr
    float                m_Speed;            // +0x44: "speed" attr
    float                m_Gravity;          // +0x48: "gravity" attr
    float                m_field4c;          // +0x4c: secondary timescale
    // +0x50..+0x5b: wave-revisit counter / spawn accumulators
    float                m_SpawnTimer;       // +0x50: countdown timer
    int                  m_RemainingCount;   // +0x54: spawns left this tick
    float                m_SpawnCountF;      // +0x58: fractional spawn count
    float                m_ZOffset;          // +0x5c: "offset" z or "delay"
    uint8_t              m_bForceOnce;       // +0x60: "forceonce" attr
    uint8_t              _pad61[3];

    // Grow fields from XML (mininc/maxinc/delayinc)
    float                m_MinInc;           // not in binary struct — loaded as runtime ramp
    float                m_MaxInc;           // ditto
    float                m_DelayInc;         // ditto

    SPAWNER_INFO()
        : m_pFruitTypeHashes(nullptr)
        , m_FruitTypeCount(0)
        , m_TimeScale(1.0f)
        , m_Offset_x(0.0f), m_Offset_y(0.0f), m_Offset_z(0.0f)
        , m_MinAngle(-1.0f), m_MaxAngle(1.0f)
        , m_MinVel(1.0f), m_MaxVel(1.0f)
        , m_SpawnType(PLACEMENT_BOTTOM)
        , m_SpawnMin(1.0f), m_SpawnMax_unused(0.0f), m_SpawnMax(1.0f)
        , m_Speed(1.0f), m_Gravity(1.0f), m_field4c(0.0f)
        , m_SpawnTimer(0.0f), m_RemainingCount(0), m_SpawnCountF(0.0f)
        , m_ZOffset(0.0f), m_bForceOnce(0)
        , m_MinInc(0.0f), m_MaxInc(0.0f), m_DelayInc(0.0f)
    {
        memset(_pad35, 0, sizeof(_pad35));
        memset(_pad61, 0, sizeof(_pad61));
    }

    ~SPAWNER_INFO() {
        delete[] m_pFruitTypeHashes;
        m_pFruitTypeHashes = nullptr;
    }

    // Reset spawner for a new wave tick. field_0x34 = wave-revisit counter.
    void Reset(float waveRevisitCounter) {
        float spawnCount = m_SpawnMin + m_MinInc * waveRevisitCounter;
        float spawnMax   = m_SpawnMax + m_MaxInc * waveRevisitCounter;
        if (spawnCount < spawnMax) {
            // roll count in [spawnCount..spawnMax]
            // Caller must use WaveManager's RNG; store range for caller to sample.
            // For simplicity, store the computed float; caller casts to int.
        }
        m_SpawnCountF   = spawnCount;
        m_RemainingCount = (int)spawnCount;
        float delay = m_ZOffset - m_DelayInc * waveRevisitCounter;
        if (delay < 0.0f) delay = 0.0f;
        m_SpawnTimer = delay;
    }
};

// WAVE_INFO — size 0x78 (120 bytes), per docs/systems/wave-system.md
struct WAVE_INFO {
    // +0x00
    int                  m_ScoreThreshold;   // waveNo attr
    // +0x04
    int                  m_EndScore;         // until attr; -2=forever
    // +0x08
    SPAWNER_INFO*        m_pSpawners;        // allocated array
    // +0x0c
    int                  m_SpawnerCount;

    // Wave_dt fields (+0x10..+0x18 from docs; named to match GetWavedt usage)
    float                m_BombScale1;       // +0x10: wave_dt base
    float                wave_dt_inc;        // +0x14: dt increment per revisit
    float                delaySpeedScale;    // +0x18: delay speed-scale modifier

    // Bomb params (+0x1c..+0x30)
    float                m_BombGravity;      // +0x18 (DIFFERS: overlaps with delaySpeedScale in docs; using seq offset)
    float                m_BombSpeed;        // +0x1c
    float                m_BombSpeedMax;     // +0x20
    float                m_BombMinAngle;     // +0x24
    float                m_BombMaxAngle;     // +0x28
    float                m_BombField30;      // +0x30

    // +0x34: wave revisit counter (incremented each time this wave is selected)
    float                field_0x34;

    // +0x38: allow-bombs flags
    uint8_t              m_bAllowBombs;      // +0x38
    uint8_t              m_bAllowBombsFrenzy;// +0x39
    uint8_t              _pad3a[2];

    // +0x3c
    int                  m_MinScore;         // minscore attr
    // +0x44
    float                m_WaveDelay;        // wavedelay attr (= nextWaveDelay)
    // +0x4c
    int                  m_BombMin;          // bombmin/bombcount attr
    // +0x50
    int                  m_BombMax;          // bombmax attr
    // +0x54: special fruit list (vector<string>)
    std::vector<std::string> m_SpecialFruits;
    // +0x60
    int                  m_field60;
    // +0x64: critical chance (0..100 scale)
    float                m_CriticalChance;
    // +0x68: wave index (sequential index in mode list)
    int                  m_WaveIndex;
    // +0x6c: coin chance (pointer)
    void*                m_pCoinChance;      // COIN_CHANCEINATOR* (forward decl)
    // +0x70: waveNo (from XML attr "waveNo")
    int                  m_WaveNumber;
    // +0x74: total weight (sum of spawner min+max / 2)
    int                  m_TotalWeight;

    // Extra fields parsed from actual XML (not in binary struct, used at parse time)
    int                  m_Chance;           // "chance" attr (selection weight)
    float                m_ChanceRegrowth;   // "chanceRegrowth" attr
    int                  m_CurrentMax;       // grows toward m_Chance via regrowth
    int                  m_GamesMin;         // gamesMin attr
    int                  m_GamesMax;         // gamesMax attr

    // ChooseFrom list (parsed from <ChooseFrom> child element)
    std::vector<std::string> m_ChooseFrom;

    WAVE_INFO()
        : m_ScoreThreshold(0), m_EndScore(-2)
        , m_pSpawners(nullptr), m_SpawnerCount(0)
        , m_BombScale1(0.9f), wave_dt_inc(0.0f), delaySpeedScale(0.0f)
        , m_BombGravity(1.0f), m_BombSpeed(1.0f), m_BombSpeedMax(1.0f)
        , m_BombMinAngle(-1.0f), m_BombMaxAngle(1.0f), m_BombField30(0.0f)
        , field_0x34(0.0f)
        , m_bAllowBombs(1), m_bAllowBombsFrenzy(1)
        , m_MinScore(0), m_WaveDelay(0.6f)
        , m_BombMin(0), m_BombMax(0)
        , m_field60(0), m_CriticalChance(1.0f)
        , m_WaveIndex(0), m_pCoinChance(nullptr)
        , m_WaveNumber(0), m_TotalWeight(0)
        , m_Chance(90), m_ChanceRegrowth(0.33f), m_CurrentMax(0)
        , m_GamesMin(0), m_GamesMax(0)
    {
        memset(_pad3a, 0, sizeof(_pad3a));
    }

    ~WAVE_INFO() {
        delete[] m_pSpawners;
        m_pSpawners = nullptr;
    }
};

// DEFAULT_WAVE_INFO — size 0x40 (64 bytes), stored at WaveManager+0xdc per mode
// Parsed from <defaults> element. Provides base values each WAVE_INFO inherits.
// DIFFERS: actual XML uses very different attributes from the older spec's table.
// The older spec cites attributes like "count","critchance","wavedelay" etc.
// The actual XML files use "waveChance","waveChanceGrowth","criticalChance" etc.
struct DEFAULT_WAVE_INFO {
    int   m_DefaultCount;       // +0x00: "count" attr (DIFFERS: not in actual XMLs)
    float m_CritChance;         // +0x04: "criticalChance" attr
    float m_WaveDelay;          // +0x08: "wavedelay" attr (DIFFERS: not in actual XMLs)
    float m_SpawnTimeScale;     // +0x0c: "spawntimescale" (DIFFERS: not in actual XMLs)
    float m_BombScale;          // +0x10: "bombscale" (DIFFERS: not in actual XMLs)
    float m_BombGravity;        // +0x14: "bombgravity"
    float m_BombSpeed;          // +0x18: "bombspeed"
    float m_BombSpeedMax;       // +0x1c: "bombspeedmax"
    float m_BombMin;            // +0x20: "bombmin"
    float m_BombMax;            // +0x24: "bombmax"
    float m_CritChanceMod;      // +0x28: "critchancemod"
    float m_field2c;            // +0x2c
    int   m_field30;            // +0x30
    bool  m_bAllowBombs;        // +0x34: "allowbombs"
    bool  m_bAllowBombsFrenzy;  // +0x35: "allowbombsfrenzy"

    // Extra fields from actual XML defaults elements
    int   m_WaveChance;         // "waveChance" attr
    float m_WaveChanceGrowth;   // "waveChanceGrowth" attr

    DEFAULT_WAVE_INFO()
        : m_DefaultCount(0)
        , m_CritChance(1.0f)
        , m_WaveDelay(0.6f)
        , m_SpawnTimeScale(1.0f)
        , m_BombScale(1.0f)
        , m_BombGravity(1.0f)
        , m_BombSpeed(1.0f)
        , m_BombSpeedMax(1.0f)
        , m_BombMin(0.0f)
        , m_BombMax(0.0f)
        , m_CritChanceMod(1.0f)
        , m_field2c(0.0f)
        , m_field30(0)
        , m_bAllowBombs(true)
        , m_bAllowBombsFrenzy(true)
        , m_WaveChance(90)
        , m_WaveChanceGrowth(0.33f)
    {}
};

// COIN_CHANCEINATOR — size 0x08, stored at WaveManager+0x1dc per mode
// DIFFERS: <coinchance> element not present in actual XML files observed.
// Keeping the struct for binary completeness; Init will skip if not found.
struct COIN_CHANCEINATOR {
    float m_Chance;     // +0x00: coin spawn probability
    int   m_field04;    // +0x04
    COIN_CHANCEINATOR() : m_Chance(0.0f), m_field04(0) {}
};

// PROBABILITY_OVERIDE — used by UpdateWave for power-up fruit overrides.
// Not parsed from the main wave XMLs (only from arcadewavelist.xml).
// Kept as forward-declared opaque in WaveManager.h; full layout here for
// completeness when that subsystem is ported.
struct PROBABILITY_OVERIDE {
    std::vector<std::string> m_Types;  // fruit type names
    float                    m_PercentChance;
    int                      m_PerWave;
    int                      m_WaveCount;
    float                    m_DisableWhenPowered;
    int                      m_Counter;
    int                      m_SelectedType;  // resolved fruit type index
    int                      field_0x08;

    PROBABILITY_OVERIDE()
        : m_PercentChance(0.0f), m_PerWave(1), m_WaveCount(0)
        , m_DisableWhenPowered(0.0f), m_Counter(0), m_SelectedType(-1)
        , field_0x08(0)
    {}

    // Re-roll which fruit type this override applies to.
    void SelectType() { /* TODO: Fruit::FruitType random select */ }
};

#endif // FN_WAVE_STRUCTS_H
