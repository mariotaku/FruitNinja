#ifndef FN_WAVE_STRUCTS_H
#define FN_WAVE_STRUCTS_H

namespace tinyxml2 { class XMLElement; }
namespace Math { class Random; }

// Wave data structs — binary layouts from docs/structs/wave.md and
// docs/systems/wave-system.md.
//
// WAVE_INFO      : 0x78 bytes (docs §WAVE_INFO table)
// SPAWNER_INFO   : 0x64 bytes (docs §SPAWNER_INFO table)
// DEFAULT_WAVE_INFO : 0x40 bytes (docs §DEFAULT_WAVE_INFO table)
// COIN_CHANCEINATOR : 0x08 bytes (docs §wave.md)
//
// Analysed: 2026-04-30T00:00
// Struct offsets realigned per wavemanager-init-asm-audit.md 2026-04-30.

#include <cstdint>
#include <cstring>
#include <list>
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

// SPAWNER_INFO — size 0x64 (100 bytes).
// Field offsets per binary @ 0x001270ac (ctor) and WaveManager::Init @ 0x0012393c.
struct SPAWNER_INFO {
    // +0x00
    int*                     m_pFruitTypeHashes; // hash array for fruit types
    // +0x04
    std::vector<std::string> m_FruitTypeNames;   // SplitWords on "type" attr
    // +0x10
    int                      m_FruitTypeCount;
    // +0x14
    float                    m_TimeScale;        // never written by Init; ctor=1.0
    // +0x18..+0x20: gravity Vec3 (XML "gravity" attr -> ParseVector)
    // Binary ctor @ 0x001270ac initialises to (0, -1, 0).
    float                    m_Gravity_x;        // +0x18
    float                    m_Gravity_y;        // +0x1c: binary default -1.0
    float                    m_Gravity_z;        // +0x20
    // +0x24: velscale/velXscale target (binary DAT_001241ec/f0 -> +0x24)
    float                    m_VelXScale;        // +0x24
    // +0x28: velYscale target (binary DAT_001241f4 -> +0x28)
    float                    m_VelYScale;        // +0x28
    // +0x2c: horizmin target (binary DAT_001241e4 -> +0x2c)
    float                    m_HorizMin;         // +0x2c
    // +0x30: horizmax target (binary DAT_001241e8 -> +0x30)
    float                    m_HorizMax;         // +0x30
    // +0x34: placement enum (XML "placement" -> ParsePlacement)
    SpawnPlacement           m_SpawnType;        // +0x34
    uint8_t                  _pad35[3];
    // +0x38: "min" attr
    float                    m_SpawnMin;         // +0x38
    // +0x3c: binary ctor=0, not written by Init
    float                    m_SpawnMax_unused;  // +0x3c
    // +0x40: "max" attr
    float                    m_SpawnMax;         // +0x40
    // +0x44: mininc AND maxinc BOTH write here (maxinc wins); single slot in binary.
    float                    m_GrowthInc;        // +0x44
    // +0x48: "delay" attr (chuck delay base, XML attr name "delay")
    float                    m_Delay;            // +0x48
    // +0x4c: "delayinc" attr
    float                    m_DelayInc;         // +0x4c
    // +0x50: remaining spawn count for this wave tick
    int                      m_RemainingCount;   // +0x50
    // +0x54: fractional spawn count accumulator
    float                    m_SpawnCountF;      // +0x54
    // +0x58: unknown
    int                      m_field58;          // +0x58
    // +0x5c: countdown timer (decremented by dt*dtMod each tick)
    float                    m_SpawnTimer;       // +0x5c
    // +0x60: "mirror" attr (1 if attr present and != "false"; 0 if absent)
    uint8_t                  m_bMirror;          // +0x60
    uint8_t                  _pad61[3];

    SPAWNER_INFO()
        : m_pFruitTypeHashes(nullptr)
        , m_FruitTypeCount(0)
        , m_TimeScale(1.0f)
        , m_Gravity_x(0.0f), m_Gravity_y(-1.0f), m_Gravity_z(0.0f)
        , m_VelXScale(1.0f), m_VelYScale(1.0f)
        , m_HorizMin(-1.0f), m_HorizMax(1.0f)
        , m_SpawnType(PLACEMENT_BOTTOM)
        // ASM-verified: binary SPAWNER_INFO ctor @ 0x001270ac sets m_SpawnMin=0.0, m_SpawnMax=0.0
        , m_SpawnMin(0.0f), m_SpawnMax_unused(0.0f), m_SpawnMax(0.0f)
        , m_GrowthInc(0.0f)
        , m_Delay(0.0f), m_DelayInc(0.0f)
        , m_RemainingCount(0), m_SpawnCountF(0.0f), m_field58(0)
        , m_SpawnTimer(0.0f)
        , m_bMirror(0)
    {
        memset(_pad35, 0, sizeof(_pad35));
        memset(_pad61, 0, sizeof(_pad61));
    }

    ~SPAWNER_INFO() {
        delete[] m_pFruitTypeHashes;
        m_pFruitTypeHashes = nullptr;
    }

    // Binary @ 0x00122c64. Re-rolls fruit-type indices from string-name vector.
    // Resolves each name in m_FruitTypeNames to a hash-based type index.
    // BOMB / BOMB_PINEAPPLE -> -2; RANDOM -> Fruit::RandomFruit(false); else Fruit::FruitType.
    void SelectTypes();

    // Reset spawner for a new wave. waveRevisitCounter = wave->field_0x34.
    void Reset(float waveRevisitCounter) {
        float spawnCount = m_SpawnMin + m_GrowthInc * waveRevisitCounter;
        float spawnMax   = m_SpawnMax + m_GrowthInc * waveRevisitCounter;
        (void)spawnMax;
        m_SpawnCountF    = spawnCount;
        m_RemainingCount = (int)spawnCount;
        float delay = m_Delay - m_DelayInc * waveRevisitCounter;
        if (delay < 0.0f) delay = 0.0f;
        m_SpawnTimer = delay;
    }
};

// WAVE_INFO — size 0x78 (120 bytes).
// Field offsets per binary WaveManager::Init @ 0x0012393c and §6 of audit.
struct WAVE_INFO {
    // +0x00
    int                      m_ScoreThreshold;   // waveNo attr (mirror of m_WaveNumber)
    // +0x04
    int                      m_EndScore;         // "until" attr; -2=forever
    // +0x08
    SPAWNER_INFO*            m_pSpawners;
    // +0x0c
    int                      m_SpawnerCount;
    // +0x10: Wave_dt "dt" attr
    float                    m_WaveDt;           // +0x10
    // +0x14: Wave_dt "inc" attr
    float                    m_WaveDtInc;        // +0x14
    // +0x18: Wave_dt "spinc" attr
    float                    m_WaveDtSpInc;      // +0x18
    // +0x1c: NextWaveDelay "speedLoss" attr
    float                    m_NextWaveSpeedLoss; // +0x1c
    // +0x20: NextWaveDelay "delay" attr (cleared to 0 if wait > 0)
    float                    m_NextWaveDelay;    // +0x20
    // +0x24: NextWaveDelay "inc" attr (cleared to 0 if wait > 0)
    float                    m_NextWaveDelayInc; // +0x24
    // +0x28: NextWaveDelay "wait" attr
    float                    m_NextWaveWait;     // +0x28
    // +0x2c: (gap/padding — binary layout has a gap here)
    float                    m_field2c;          // +0x2c
    // +0x30: NextWaveDelay "waitSpinc" attr
    float                    m_NextWaveWaitSpInc; // +0x30
    // +0x34: wave revisit counter (incremented each GetNextWave selection)
    float                    field_0x34;         // +0x34
    // +0x38: NextWaveDelay "waitForEntities" (1 default; 0 only if attr == "false")
    uint8_t                  m_bWaitForEntities; // +0x38
    // +0x39: NextWaveDelay "waitForProcessing"
    uint8_t                  m_bWaitForProcessing; // +0x39
    uint8_t                  _pad3a[2];
    // +0x3c: "chance" attr (selection weight)
    int                      m_Chance;           // +0x3c
    // +0x40: running copy of m_Chance; ResetWaveChances writes +0x40 = +0x3c
    // ASM-verified: binary WAVE_INFO @ 0x00126748 (ctor) / 0x001249d0 (ResetWaveChances)
    // Replaces former port-tail field m_CurrentMax (which was off-struct).
    int                      m_CurrentChance;    // +0x40
    // +0x44: "chanceRegrowth" attr
    float                    m_ChanceRegrowth;   // +0x44
    // +0x48: running copy of m_ChanceRegrowth; ResetWaveChances writes +0x48 = +0x44
    // ASM-verified: binary @ 0x001249d0 (ResetWaveChances); type is float (0x3e800000 = 0.25)
    // Replaces former port-tail field m_CurrentRegrowth (which was off-struct).
    float                    m_CurrentRegrowth;  // +0x48
    // +0x4c: "games"/"gamesMin" attr
    int                      m_GamesMin;         // +0x4c
    // +0x50: "gamesMax" attr
    int                      m_GamesMax;         // +0x50
    // +0x54: ChooseFrom types vector
    std::vector<std::string> m_SpecialFruits;   // +0x54
    // +0x60: always cleared to 0 during Init
    int                      m_field60;          // +0x60
    // +0x64: "criticalChance" attr
    float                    m_CriticalChance;   // +0x64
    // +0x68: sequential wave index in mode list
    int                      m_WaveIndex;        // +0x68
    // +0x6c: coin chance pointer (COIN_CHANCEINATOR*)
    void*                    m_pCoinChance;      // +0x6c
    // +0x70: "overideProbabiltyPool" attr (typo matches binary); waveNo also writes here
    int                      m_OverideProbabilityPool; // +0x70
    // +0x74: total weight (sum of spawner (min+max)/2 contributions)
    int                      m_TotalWeight;      // +0x74

    // Port-internal: waveNo stored separately so m_OverideProbabilityPool keeps +0x70.
    // Not a binary struct field — used only for wave range selection logic.
    int                      m_WaveNumber;

    WAVE_INFO()
        : m_ScoreThreshold(0), m_EndScore(-1)
        , m_pSpawners(nullptr), m_SpawnerCount(0)
        , m_WaveDt(1.0f), m_WaveDtInc(0.0f), m_WaveDtSpInc(0.0f)
        , m_NextWaveSpeedLoss(0.0f)
        , m_NextWaveDelay(2.0f), m_NextWaveDelayInc(0.0f)
        , m_NextWaveWait(0.0f), m_field2c(0.0f)
        , m_NextWaveWaitSpInc(0.0f)
        // ASM-verified: binary WAVE_INFO ctor @ 0x00126748; revisit counter starts at 1.
        , field_0x34(1.0f)
        // ASM-verified: binary WAVE_INFO ctor @ 0x00126748 sets BOTH to 1.
        , m_bWaitForEntities(1), m_bWaitForProcessing(1)
        // ASM-verified: binary WAVE_INFO ctor @ 0x00126748; m_Chance=10, m_ChanceRegrowth=0.25
        , m_Chance(10), m_CurrentChance(10)
        , m_ChanceRegrowth(0.25f), m_CurrentRegrowth(0.25f)
        , m_GamesMin(-1), m_GamesMax(-1)
        , m_field60(0), m_CriticalChance(1.0f)
        , m_WaveIndex(0), m_pCoinChance(nullptr)
        , m_OverideProbabilityPool(100), m_TotalWeight(0)
        , m_WaveNumber(0)
    {
        memset(_pad3a, 0, sizeof(_pad3a));
    }

    ~WAVE_INFO() {
        delete[] m_pSpawners;
        m_pSpawners = nullptr;
    }
};

// DEFAULT_WAVE_INFO — size 0x40 (64 bytes), stored at WaveManager+0xdc per mode.
// Parsed from <defaults> element. Field names per binary audit DAT addresses.
struct DEFAULT_WAVE_INFO {
    // +0x00: "waveChance" attr
    int   m_WaveChance;         // +0x00
    // +0x04: "waveChanceRegrowth" attr (XML ships "waveChanceGrowth" — typo mismatch)
    float m_WaveChanceRegrowth; // +0x04
    // +0x08: "criticalChance" attr
    float m_CritChance;         // +0x08
    // +0x0c: "dt" attr (default Wave_dt base)
    float m_DefaultDt;          // +0x0c
    // +0x10: "dtInc" attr (per-wave dt increment default)
    float m_DtInc;              // +0x10
    // +0x14: "dtSpInc" attr
    float m_DtSpInc;            // +0x14
    // +0x18: "beforeDelay" attr
    float m_BeforeDelay;        // +0x18
    // +0x1c: "beforeDelayInc" attr
    float m_BeforeDelayInc;     // +0x1c
    // +0x20: "nextDelay" attr
    float m_NextDelay;          // +0x20
    // +0x24: "nextDelayInc" attr
    float m_NextDelayInc;       // +0x24
    // +0x28: "nextDelaySpInc" attr
    float m_NextDelaySpInc;     // +0x28
    // +0x2c: "speedLoss" attr
    float m_DefSpeedLoss;       // +0x2c
    // +0x30: "overideProbabiltyPool" attr (typo matches binary)
    int   m_OverideProbabilityPool; // +0x30

    DEFAULT_WAVE_INFO()
        : m_WaveChance(90)
        , m_WaveChanceRegrowth(0.33f)
        , m_CritChance(1.0f)
        , m_DefaultDt(0.9f)
        , m_DtInc(0.0f)
        , m_DtSpInc(0.0f)
        , m_BeforeDelay(0.0f)
        , m_BeforeDelayInc(0.0f)
        , m_NextDelay(0.0f)
        , m_NextDelayInc(0.0f)
        , m_NextDelaySpInc(0.0f)
        , m_DefSpeedLoss(0.0f)
        , m_OverideProbabilityPool(0)
    {}
};

// COIN_CHANCEINATOR — size 0x08, stored at WaveManager+0x1dc per mode.
struct COIN_CHANCEINATOR {
    float m_Chance;     // +0x00
    int   m_field04;    // +0x04
    COIN_CHANCEINATOR() : m_Chance(0.0f), m_field04(0) {}
};

// PROBABILITY_OVERIDE — size 0x78. Binary ctor @ 0x00126870.
// Field order matches binary layout exactly.
struct PROBABILITY_OVERIDE {
    // +0x00: selection weight (XML "percentageChance"). Binary stores as int.
    int                      m_PercentChance;    // +0x00
    // +0x04: max spawns per wave (XML "perWave"). Binary default 0.
    int                      m_PerWave;          // +0x04
    // +0x08: running per-wave counter (UpdateWave bumps; ResetWaveChances resets).
    int                      m_Counter;          // +0x08
    // +0x0c: fruit type names (XML "types"), 12 bytes (vector<string>).
    std::vector<std::string> m_Types;            // +0x0c
    // +0x18..+0x67: per-type spawn-tracking queue (20 ints, init -1).
    // Used by UpdateWave blitz-spawn loop to track which types were already spawned.
    int                      m_TypeQueue[20];    // +0x18
    // +0x68: reserved int (init 0).
    int                      m_field68;          // +0x68
    // +0x6c: power-up disable threshold (XML "disableWhenPowered"). Binary default 0.0.
    float                    m_DisableWhenPowered; // +0x6c
    // +0x70: minimum game count gate (XML "waveCount"). Binary default 0.
    int                      m_PerWaveCount;     // +0x70
    // +0x74: last selected type index (-1 = unset). SelectType picks randomly.
    int                      m_SelectedType;     // +0x74

    PROBABILITY_OVERIDE()
        : m_PercentChance(0), m_PerWave(0), m_Counter(0)
        , m_field68(0), m_DisableWhenPowered(0.0f)
        // DIFFERS: binary ctor @ 0x00126884 writes literal pool word 0xfff0bdc0 to +0x70
        // (m_PerWaveCount). This is likely a pointer slot or pre-relocation GOT offset baked
        // into the literal pool; treating as int default 0 is safe because
        // WaveManager::Init always overwrites via QueryIntAttribute("waveCount") when the
        // XML attr is present. Gameplay is unaffected.
        , m_PerWaveCount(0), m_SelectedType(-1)
    {
        for (int i = 0; i < 20; ++i) m_TypeQueue[i] = -1;
    }

    // Parse XML attributes into this struct. binary @ 0x001231d8
    void Parse(tinyxml2::XMLElement* xml);

    // Binary @ 0x00122b44. Populates m_TypeQueue[] from m_Types names.
    // Three lazy-init guarded statics: BOMB_HASH, BOMB_PINEAPPLE_HASH, RANDOM_HASH.
    // BOMB/BOMB_PINEAPPLE -> m_TypeQueue[i]=-2; RANDOM -> RandomFruit(false); else FruitType(name,false).
    void SelectType();

    // Binary @ 0x001217e4. Returns m_TypeQueue[Rand32(m_field68)].
    // Does NOT call SelectType (SelectType is called once at Reset/NewGame).
    int GetType();
};

// WaveQueItem — binary @ 0x001268fc ctor. Size 0x20 (32 bytes).
// Only used in gameMode==2 (Survival/Combo). SetupWaveQue populates this via AddWave.
struct WaveQueItem {
    // +0x00..+0x0b: spawner-op codes (1=normal, 2=random, 3=special)
    std::vector<int> m_SpawnerOps;  // +0x00, 12 bytes
    // +0x0c: padding / reserved
    int m_field0c;                  // +0x0c
    // +0x10: running counter (target-type tracking in AddWave loop)
    int m_field10;                  // +0x10
    // +0x14: unused slot
    int m_field14;                  // +0x14
    // +0x18: copy of WAVE_INFO::m_WaveIndex for this queue entry
    int m_WaveIndex;                // +0x18
    // +0x1c: reserved
    int m_field1c;                  // +0x1c

    WaveQueItem() : m_field0c(0), m_field10(0), m_field14(0), m_WaveIndex(0), m_field1c(0) {}
};

#ifdef __bada__
static_assert(sizeof(WaveQueItem) == 0x20, "WaveQueItem size mismatch");
#endif

// WaveQue — binary @ 0x00126b10 ctor. Size 0x0c (12 bytes).
// Only used in gameMode==2 (Survival/Combo). SetupWaveQue builds this from wave XML.
// After RandomiseOrder, field_0x08 is initialised to 27.0 (budget constant).
struct WaveQue {
    // +0x00..+0x07: doubly-linked list of WaveQueItems (std::list, 8-byte sentinel layout in binary)
    std::list<WaveQueItem> m_Items;  // +0x00
    // +0x08: wave budget float (initialised to 27.0f after RandomiseOrder in SetupWaveQue)
    float field_0x08;               // +0x08

    WaveQue() : field_0x08(0.0f) {}

    // WaveQue::AddWave — binary @ 0x00124334.
    // Builds a WaveQueItem for wi and appends it to m_Items.
    // isLast controls the alternating-spawn policy code.
    // rng = WaveManager::GetInstance()->GetRandom() (binary uses global Random pointer).
    void AddWave(WAVE_INFO* wi, bool isLast, Math::Random& rng);

    // WaveQue::PopWave — binary @ 0x00123258.
    // Pops front of m_Items into *out. Returns true if an item was available.
    bool PopWave(WaveQueItem* out);

    // WaveQue::RandomiseOrder — binary @ 0x00124464.
    // Walks the list and alternately flips spawner-ops 1<->2 at every other position.
    void RandomiseOrder(bool doSwap);

    // WaveQue::AddSpecials — binary @ 0x00121b20.
    // Iterates all items; for each spawner-op with Rand32(100)<5 (or counter>=4),
    // sets the op to 3 (special) if specialsCount<2.
    // rng = WaveManager::GetInstance()->GetRandom() (binary uses global Random pointer).
    void AddSpecials(Math::Random& rng);
};

#ifdef __bada__
static_assert(sizeof(WaveQue) == 0x0c, "WaveQue size mismatch");
#endif

#endif // FN_WAVE_STRUCTS_H
