#ifndef FN_WAVE_STRUCTS_H
#define FN_WAVE_STRUCTS_H

#include "engine/xml/TiXmlElement.h"
namespace Math { class Random; }
class ExclusiveTag;   // v1.6.1 WaveManager::GetNextWave @0x0012573c candidate filter; see WaveInfo::m_ExclusiveTag

// Wave data structs — binary layouts (RE'd).
//
// WAVE_INFO         : 0x7c bytes
// SPAWNER_INFO      : 0x64 bytes
// DEFAULT_WAVE_INFO : 0x40 bytes
// COIN_CHANCEINATOR : 0x08 bytes
//
// Analysed: 2026-04-30T00:00
// Struct offsets realigned per wavemanager-init-asm-audit.md 2026-04-30.

#include <cstdint>
#include <cstring>
#include <list>
#include <vector>
#include <string>

// Spawn-side enum. ParsePlacement maps XML "placement" attr to this.
// v1.6.1 ParsePlacement @0x001291c0: BOTTOM=0, TOP=1, LEFT=2, RIGHT=3, LEFT_RIGHT=4.
// (There is no "BOTTOM_SLOW" string in the binary; type 1 is TOP.)
enum SpawnPlacement : uint8_t {
    PLACEMENT_BOTTOM      = 0,
    PLACEMENT_TOP         = 1,
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
    // +0x3c: min-growth-inc (revisit multiplier for the low spawn-count bound).
    // v1.6.1 SPAWNER_INFO::GetRandCount @0x0012df30 reads this: lo = (int)(m_SpawnMin + revisit*m_MinGrowthInc).
    // Never written by Init (both "mininc"/"maxinc" write +0x44), so it stays ctor 0 -> lo has no growth. Binary-faithful.
    float                    m_MinGrowthInc;     // +0x3c
    // +0x40: "max" attr
    float                    m_SpawnMax;         // +0x40
    // +0x44: max-growth-inc (revisit multiplier for the high bound in GetRandCount).
    // mininc AND maxinc BOTH write here (maxinc wins); single slot in binary.
    float                    m_GrowthInc;        // +0x44
    // +0x48: "delay" attr (chuck delay base, XML attr name "delay")
    float                    m_Delay;            // +0x48
    // +0x4c: "delayinc" attr
    float                    m_DelayInc;         // +0x4c
    // +0x50: remaining spawn count for this wave tick
    int                      m_RemainingCount;   // +0x50
    // +0x54: fractional spawn count accumulator
    float                    m_SpawnCountF;      // +0x54
    // +0x58: reserved -- no meaningful read site in the binary. Reset/Resume/SaveWaveInfo
    // only ever write 0 here (Resume restore @0x0012c4a0 sets it 0). Purpose unknown.
    int                      m_reserved58;       // +0x58
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
        // ASM-verified: v1.6.1 SPAWNER_INFO::SPAWNER_INFO @0x00122ec4 sets m_SpawnMin=0.0, m_SpawnMax=0.0
        , m_SpawnMin(0.0f), m_MinGrowthInc(0.0f), m_SpawnMax(0.0f)
        , m_GrowthInc(0.0f)
        , m_Delay(0.0f), m_DelayInc(0.0f)
        , m_RemainingCount(0), m_SpawnCountF(0.0f), m_reserved58(0)
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

    // ASM-spec v1.6.1 SPAWNER_INFO::SelectTypes @0x0012dcc8 (thunk veneer @0x00114654).
    // Re-rolls fruit-type indices from string-name vector.
    // Resolves each name in m_FruitTypeNames to a hash-based type index.
    // "bomb"/"Bomb" (case-insensitive) -> -2; "1fruit" (case-insensitive) ->
    // Fruit::RandomFruit(false) resolved once here; else Fruit::FruitType(name,false).
    void SelectTypes();

    // v1.6.1 SPAWNER_INFO::GetRandCount @0x0012df30. Returns a RANDOM spawn count in
    // [lo,hi]: lo=(int)(m_SpawnMin + revisit*m_MinGrowthInc), hi=(int)(m_SpawnMax +
    // revisit*m_GrowthInc); count = lo + ((hi-lo)<1 ? 0 : Rand32(hi-lo)), where the RNG
    // is WaveManager::GetInstance()->GetRandom(). (The previous port always used lo.)
    int  GetRandCount(float waveRevisitCounter);

    // v1.6.1 SPAWNER_INFO::ResetDelay @0x0012dfa0. m_SpawnTimer = max(0, m_Delay +
    // revisit*m_DelayInc). NOTE: the revisit term is ADDED (binary vmla), not subtracted.
    void ResetDelay(float waveRevisitCounter);

    // v1.6.1 SPAWNER_INFO::Reset @0x0012dfc8. Reset spawner for a new wave;
    // waveRevisitCounter = wave->m_RevisitCounter. Order: count=GetRandCount(revisit);
    // m_reserved58=0; m_RemainingCount/m_SpawnCountF=count; SelectTypes() (resolves ONE
    // fixed fruit type per wave for a "1fruit" spawner); ResetDelay(revisit).
    void Reset(float waveRevisitCounter);
};

// WaveInfo — size 0x7c (124 bytes).
// Field offsets per binary WaveManager::Init @ 0x0012393c and §6 of audit.
struct WaveInfo {
    // +0x00
    int                      m_ScoreThreshold;   // waveNo attr
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
    // +0x2c: reserved -- binary layout has a gap here; no read site. Purpose unknown.
    float                    m_reserved2c;       // +0x2c
    // +0x30: NextWaveDelay "waitSpinc" attr
    float                    m_NextWaveWaitSpInc; // +0x30
    // +0x34: wave revisit counter (incremented each GetNextWave selection; starts at 1.0).
    // GetNextWave bumps it on re-selection; drives WaveDt/delay growth & spawner Reset.
    // v1.6.1 GetNextWave @0x00125be4 / ResetWaveChances @0x0012b8c0
    float                    m_RevisitCounter;   // +0x34
    // +0x38: seeded from DEFAULT_WAVE_INFO::m_bWaitForEntities (XML "waitForEntities" on <defaults>);
    // also written by <NextWaveDelay waitForEntities> attr per-wave.
    uint8_t                  m_bWaitForEntities; // +0x38
    // +0x39: seeded from DEFAULT_WAVE_INFO::m_bWaitForProcessing; overridable per-wave.
    uint8_t                  m_bWaitForProcessing; // +0x39
    uint8_t                  _pad3a[2];
    // +0x3c: "chance" attr (selection weight)
    int                      m_Chance;           // +0x3c
    // +0x40: running copy of m_Chance; ResetWaveChances writes +0x40 = +0x3c
    // ASM-verified: v1.6.1 WaveManager::ResetWaveChances @0x0012b8c0 writes +0x40 = +0x3c.
    // NOTE: WaveInfo::WaveInfo(DefaultWaveInfo*) @0x0012a9f0 does NOT write +0x40; it is
    // seeded to m_Chance either by ResetWaveChances or by WaveManager::Init post-parse.
    // Replaces former port-tail field m_CurrentMax (which was off-struct).
    int                      m_CurrentChance;    // +0x40
    // +0x44: "chanceRegrowth" attr
    float                    m_ChanceRegrowth;   // +0x44
    // +0x48: running copy of m_ChanceRegrowth; ResetWaveChances writes +0x48 = +0x44
    // ASM-verified: v1.6.1 WaveManager::ResetWaveChances @0x0012b87c; type is float (0x3e800000 = 0.25)
    // Replaces former port-tail field m_CurrentRegrowth (which was off-struct).
    float                    m_CurrentRegrowth;  // +0x48
    // +0x4c: "games"/"gamesMin" attr
    int                      m_GamesMin;         // +0x4c
    // +0x50: "gamesMax" attr
    int                      m_GamesMax;         // +0x50
    // +0x54: ChooseFrom types vector
    std::vector<std::string> m_SpecialFruits;   // +0x54
    // +0x60: reserved -- only ever cleared to 0 during Init; no other access. Purpose unknown.
    int                      m_reserved60;       // +0x60
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

    // +0x78: exclusive-tag pointer -- GetNextWave's candidate-eligibility filter.
    // ASM-spec v1.6.1 WaveManager::GetNextWave @0x0012573c (candidate filter 0x001258f4-2c;
    // comparator 0x00108150 is an unresolved ARM veneer). Defunct: no shipped v1.6.1 wave XML
    // sets exclusiveTag -> m_ExclusiveTag is always null -> the comparator call is a structural
    // no-op. Field wired for parity; see WaveManager.cpp's CompareExclusiveTag stub (always-pass).
    ExclusiveTag*            m_ExclusiveTag;     // +0x78

    WaveInfo()
        : m_ScoreThreshold(0), m_EndScore(-1)
        , m_pSpawners(nullptr), m_SpawnerCount(0)
        , m_WaveDt(1.0f), m_WaveDtInc(0.0f), m_WaveDtSpInc(0.0f)
        , m_NextWaveSpeedLoss(0.0f)
        , m_NextWaveDelay(2.0f), m_NextWaveDelayInc(0.0f)
        , m_NextWaveWait(0.0f), m_reserved2c(0.0f)
        , m_NextWaveWaitSpInc(0.0f)
        // ASM-verified: 2026-07-06 v1.6.1 WaveInfo::WaveInfo(DefaultWaveInfo*) @0x0012a9f0 (re-analyst):
        // the ctor stores flM_RevisitCounter = 0.0 (not 1.0). ResetWaveChances @0x0012b8c0 later
        // sets it to 1.0 at Reset; the ctor default is 0.0.
        , m_RevisitCounter(0.0f)
        // v1.6.1 WaveInfo::WaveInfo(DefaultWaveInfo*) @0x0012a9f0 copies BOTH wait flags from <defaults>.
        , m_bWaitForEntities(1), m_bWaitForProcessing(1)
        // v1.6.1 WaveInfo::WaveInfo(DefaultWaveInfo*) @0x0012a9f0: m_Chance<-waveChance(10), m_ChanceRegrowth<-waveChanceRegrowth(0.25)
        , m_Chance(10), m_CurrentChance(10)
        , m_ChanceRegrowth(0.25f), m_CurrentRegrowth(0.25f)
        , m_GamesMin(-1), m_GamesMax(-1)
        , m_reserved60(0), m_CriticalChance(1.0f)
        , m_WaveIndex(0), m_pCoinChance(nullptr)
        , m_OverideProbabilityPool(100), m_TotalWeight(0)
        , m_ExclusiveTag(nullptr)
    {
        memset(_pad3a, 0, sizeof(_pad3a));
    }

    ~WaveInfo() {
        delete[] m_pSpawners;
        m_pSpawners = nullptr;
    }
};

// DEFAULT_WAVE_INFO — size 0x40 (64 bytes), stored at WaveManager+0xdc per mode.
// ASM-verified: 2026-07-06 v1.6.1 DefaultWaveInfo::Reset @0x00126fa8 (re-analyst) -- writes all defaults.
// WaveInfo::WaveInfo(DefaultWaveInfo*) @0x0012a9f0 copies these into per-wave WaveInfos.
struct DEFAULT_WAVE_INFO {
    int     m_WaveChance;            // +0x00 XML "waveChance"           default 10
    float   m_WaveChanceRegrowth;    // +0x04 XML "waveChanceGrowth"     default 0.25
    // +0x08/+0x0c disasm-verified SWAPPED vs earlier port: Init parses criticalChance->+0x08,
    // dt->+0x0c; the ctor @0x0012a9f0 flows +0x08 -> WaveInfo m_CriticalChance and +0x0c -> m_WaveDt.
    float   m_CritChanceVal;         // +0x08 XML "criticalChance"       default 1.0  (-> WAVE_INFO+0x64 m_CriticalChance)
    float   m_SpawnTimeScale;        // +0x0c XML "dt"                   default 1.0  (-> WAVE_INFO+0x10 m_WaveDt)
    float   m_DtInc;                 // +0x10 XML "dtInc"                default 0.0
    float   m_DtSpInc;               // +0x14 XML "dtSpInc"              default 0.0
    float   m_BeforeDelay;           // +0x18 XML "beforeDelay"          default 2.0
    float   m_BeforeDelayInc;        // +0x1c XML "beforeDelayInc"       default 0.0
    float   m_NextDelay;             // +0x20 XML "nextDelay"            default 0.0
    float   m_NextDelayInc;          // +0x24 XML "nextDelayInc"         default 0.0
    float   m_NextDelaySpInc;        // +0x28 XML "nextDelaySpInc"       default 0.0
    uint8_t m_bWaitForEntities;      // +0x2c XML "waitForEntities"       default 1    (-> WAVE_INFO+0x38)
    uint8_t m_bWaitForProcessing;    // +0x2d XML "waitForProcessing"    default 1    (-> WAVE_INFO+0x39)
    uint8_t _pad2e[2];               // +0x2e..+0x2f
    float   m_SpeedLoss;             // +0x30 XML "speedLoss"            default 0.0  (-> WAVE_INFO+0x1c m_NextWaveSpeedLoss)
    int     m_OverideProbabilityPool;// +0x34 XML "overideProbabiltyPool" default 100 (-> WAVE_INFO+0x70)
    // +0x38 default 0; set to 1 by WaveManager::Init when the <defaults> element carries
    // attribute players="1,2" (DAT_00124268="players", strcmp vs DAT_0012426c="1,2").
    // Used as a write-base shift: push_back targets m_WaveInfo[m_SecondaryModeOffset*4 + modeIdx]
    // and m_ProbabilityOverride[m_SecondaryModeOffset*4 + modeIdx] — i.e. waves parsed under a
    // players="1,2" <defaults> land in the +4 (mode-pair-2) bucket instead of the primary mode.
    // Defunct in shipped XML (no Data/xml/*wavelist.xml uses players="1,2"); rodata @ 0x1ba94a.
    int     m_SecondaryModeOffset;   // +0x38 default 0
    // +0x3c default -1; set to 2 in the same players="1,2" branch (paired with
    // m_DefaultWaveInfo[1].m_WaveChance=-1). Binary only WRITES +0x3c here; no separate reader
    // exists (the active-secondary-mode count travels via m_SecondaryModeOffset). Defunct.
    int     m_SecondaryModeWaveCount;// +0x3c default -1
    // Binary @ 0x0012393c (WaveManager::Init, players="1,2" <defaults> branch @ 0x001240d8).

    DEFAULT_WAVE_INFO()
        : m_WaveChance(10)
        , m_WaveChanceRegrowth(0.25f)
        , m_CritChanceVal(1.0f)
        , m_SpawnTimeScale(1.0f)
        , m_DtInc(0.0f)
        , m_DtSpInc(0.0f)
        , m_BeforeDelay(2.0f)
        , m_BeforeDelayInc(0.0f)
        , m_NextDelay(0.0f)
        , m_NextDelayInc(0.0f)
        , m_NextDelaySpInc(0.0f)
        , m_bWaitForEntities(1)
        , m_bWaitForProcessing(1)
        , m_SpeedLoss(0.0f)
        , m_OverideProbabilityPool(100)
        , m_SecondaryModeOffset(0)
        , m_SecondaryModeWaveCount(-1)
    {
        memset(_pad2e, 0, sizeof(_pad2e));
    }
};

#ifdef __bada__
static_assert(sizeof(DEFAULT_WAVE_INFO) == 0x40, "DEFAULT_WAVE_INFO must be 64 bytes");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_bWaitForEntities)   == 0x2c, "DEFAULT_WAVE_INFO::m_bWaitForEntities offset");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_bWaitForProcessing) == 0x2d, "DEFAULT_WAVE_INFO::m_bWaitForProcessing offset");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_SpeedLoss)          == 0x30, "DEFAULT_WAVE_INFO::m_SpeedLoss offset");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_OverideProbabilityPool) == 0x34, "DEFAULT_WAVE_INFO::m_OverideProbabilityPool offset");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_SecondaryModeOffset)    == 0x38, "DEFAULT_WAVE_INFO::m_SecondaryModeOffset offset");
static_assert(offsetof(DEFAULT_WAVE_INFO, m_SecondaryModeWaveCount) == 0x3c, "DEFAULT_WAVE_INFO::m_SecondaryModeWaveCount offset");
#endif

// COIN_CHANCEINATOR — size 0x08, stored at WaveManager+0x1dc per mode.
// Binary layout per GetCoins @ 0x00121778.
struct COIN_CHANCEINATOR {
    struct Entry {
        int      max;     // +0x00
        int      min;     // +0x04
        uint32_t chance;  // +0x08 (1-in-N gate; arg to Rand32)
    };

    Entry* m_pEntries;  // +0x00
    int    m_Count;     // +0x04

    COIN_CHANCEINATOR() : m_pEntries(0), m_Count(0) {}

    // Binary @ 0x00121778. Walks entries; for each, rolls Rand32(chance)==0 gate.
    // If gate passes and min<max, returns min + Rand32(max-min); else returns min.
    // Returns 0 if no entry passes.
    int GetCoins();
};

#ifdef __bada__
static_assert(sizeof(COIN_CHANCEINATOR) == 8, "COIN_CHANCEINATOR size mismatch");
static_assert(sizeof(COIN_CHANCEINATOR::Entry) == 0xc, "COIN_CHANCEINATOR::Entry size mismatch");
#endif

// PROBABILITY_OVERIDE — size 0x84 (v1.6.1; was 0x78 pre-+0x78 field). Binary ctor @ 0x00126870.
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
    // +0x68: valid type-queue count. SplitWords("types") return value (Parse @0x001231d8);
    // used as the Rand32 upper bound in GetType @0x001217e4 (m_TypeQueue[Rand32(m_TypeCount)]).
    int                      m_TypeCount;        // +0x68
    // +0x6c: power-up disable threshold (XML "disableWhenPowered"). Binary default 0.0.
    float                    m_DisableWhenPowered; // +0x6c
    // +0x70: minimum game count gate (XML "waveCount"). Binary default 0.
    int                      m_PerWaveCount;     // +0x70
    // +0x74: last selected type index (-1 = unset). SelectType picks randomly.
    int                      m_SelectedType;     // +0x74
    // +0x78: per-active-timed-power allow percentage (XML child <PowerAllowance
    // allowPercentage="N">, one per element). UpdateWave @0x00126124 indexes this by
    // min(GetNumActiveTimedPowers(), size-1) and rejects the override roll (falls through
    // to GlobalProbabilityOveride/RandomFruit) when the indexed percentage rolls below
    // Rand32(100). Empty on all shipped arcadewavelist.xml data -- reject never fires.
    std::vector<int>         m_PowerAllowance;   // +0x78

    PROBABILITY_OVERIDE()
        : m_PercentChance(0), m_PerWave(0), m_Counter(0)
        , m_TypeCount(0), m_DisableWhenPowered(0.0f)
        // DIFFERS: v1.6.1 PROBABILITY_OVERIDE::PROBABILITY_OVERIDE @0x0012abc0 writes literal pool word 0xfff0bdc0 to +0x70
        // (m_PerWaveCount). This is likely a pointer slot or pre-relocation GOT offset baked
        // into the literal pool; treating as int default 0 is safe because
        // WaveManager::Init always overwrites via QueryIntAttribute("waveCount") when the
        // XML attr is present. Gameplay is unaffected.
        , m_PerWaveCount(0), m_SelectedType(-1)
    {
        for (int i = 0; i < 20; ++i) m_TypeQueue[i] = -1;
    }

    // Parse XML attributes into this struct. binary @ 0x001231d8
    void Parse(TiXmlElement* xml);

    // ASM-spec v1.6.1 PROBABILITY_OVERIDE::SelectType @0x00121000. Populates m_TypeQueue[]
    // from m_Types names. Two lazy-init guarded statics: Bomb hash, 1fruit hash.
    // "bomb"/"Bomb" (case-insensitive) -> m_TypeQueue[i]=-2;
    // "1fruit" (case-insensitive) -> RandomFruit(false); else FruitType(name,false).
    void SelectType();

    // Binary @ 0x001217e4. Returns m_TypeQueue[Rand32(m_TypeCount)].
    // Does NOT call SelectType (SelectType is called once at Reset/NewGame).
    // TODO: latent — Data/xml/fruitlist.xml has no <FruitInfo name="starfruit">; FruitType("starfruit",false)==-1.
    // GetType() returns -1 for the starfruit override, which falls through to RandomFruit in callers.
    // Restore fruitlist.xml entry if starfruit override (arcadewavelist.xml OverideProbability) should work.
    int GetType();
};

#ifdef __bada__
// std::vector<int> is 12 bytes on 32-bit bada; host x64 std::vector is 24 bytes
// (0x78 + 24 = 0x90), so this assert is bada-only per project policy (offset/size
// asserts must not fire on 32-bit-assuming layouts under a 64-bit host build).
static_assert(sizeof(PROBABILITY_OVERIDE) == 0x84, "PROBABILITY_OVERIDE size mismatch");
#endif

// WAVE_INFO is the legacy port-side name. Binary struct is WaveInfo.
// Alias kept so existing callers (WaveManager.cpp etc.) compile unchanged.
typedef WaveInfo WAVE_INFO;

// WaveQueItem — binary size 0x1c (evidence: operator new(0x1c) in SetupWaveQue,
// _List_base<WaveQueItem>::_M_get_node @0x0012dab8 allocates 0x24 = 8 node header
// + 0x1c, copy-ctor @0x0012da08 copies exactly through +0x18). Port is 0x20 — see
// m_SpecialsCount DIFFERS below.
// Only used in gameMode==2 (Survival/Combo). SetupWaveQue populates this via AddWave.
struct WaveQueItem {
    // +0x00..+0x0b: per-spawn slot indices; push_back of {0,1,2} for each unit
    std::vector<int> m_SlotList;    // +0x00, 12 bytes
    // +0x0c: timing/ratio value (init 0.5f; overwritten with count1/totalWeight)
    float m_Fraction;               // +0x0c
    // +0x10: counter group (treated as int[3] starting at +0x10 by AddWave
    // @0x0012d014 via sp+(op+4)*4 and AddSpecials @0x0012cf00 via (op+6)*4)
    int m_Count0;                   // +0x10
    // +0x14: counter slot 1 (left/right balance tracker)
    int m_Count1;                   // +0x14
    // +0x18: counter slot 2; seeded from WaveInfo +0x68 (m_WaveIndex) but indexed
    // as counter[2] by AddWave/AddSpecials, and swapped with m_Count1 by
    // RandomiseOrder's mirroring
    int m_Count2;                   // +0x18
    // DIFFERS: original has no such field; AddSpecials @0x0012ced4 writes the
    // per-item cap at node+0x24, one word past the 0x1c-byte WaveQueItem
    // allocation (an OOB bug in v1.6.1). Port stores it explicitly instead,
    // making sizeof 0x20.
    // Per-item cap on AddSpecials placements (<2). Distinct from AddSpecials'
    // cross-item cooldown counter (which is a local, not stored here).
    int m_SpecialsCount;             // +0x1c (port-only)

    WaveQueItem() : m_Fraction(0.0f), m_Count0(0), m_Count1(0), m_Count2(0), m_SpecialsCount(0) {}

    // v1.6.1: PopPlayer — binary @ 0x0012cf6c.
    // Pops front of m_SlotList into *out. Returns true if an item was available.
    bool PopPlayer(int* out);

    // v1.6.1: PerformCatchup — binary @ 0x0012cdb0.
    // If |leftCount - rightCount| > 5 and Rand32(100) < 60, scans m_SlotList for
    // entries == (leftCount < rightCount ? 2 : 1) and randomly flips one to the
    // other side. Returns 1 if it did a flip, else 0.
    int PerformCatchup(int leftCount, int rightCount);
};

#ifdef __bada__
// Port value: 0x20. Binary WaveQueItem is 0x1c; the extra word is the port-only
// m_SpecialsCount (see its DIFFERS — fixes the binary's node+0x24 OOB write).
static_assert(sizeof(WaveQueItem) == 0x20, "WaveQueItem size mismatch");
#endif

// WaveQue — operator new(0xc) @ SetupWaveQue 0x00123494; ctor @ 0x00126b10.
// +0x00 std::list<WaveQueItem> m_Items (8-byte pre-C++11 sentinel layout)
// +0x08 float m_Budget  (ctor inits 0.0f via vstr s15,[r0,#0x8]; SetupWaveQue resets to 27.0f @0x001236ac)
struct WaveQue {
    // +0x00..+0x07: doubly-linked list of WaveQueItems (std::list, 8-byte sentinel layout in binary)
    std::list<WaveQueItem> m_Items;  // +0x00
    float m_Budget;                  // +0x08  init 0.0f; per-que spawn budget (reset to 27.0f at end of SetupWaveQue)

    WaveQue() : m_Budget(0.0f) {}

    // WaveQue::AddWave @0x0012d014.
    // Builds a WaveQueItem for wi and appends it to m_Items: rolls one policy code
    // (always one Rand32(100) draw), then assigns one spawner op per unit of
    // wi->m_TotalWeight into item.m_SlotList, tallying into m_Count0/1/2 and finally
    // setting m_Fraction. isLast selects the alternating 1,2,1,2,... policy (which
    // makes zero further draws and leaves m_Fraction at its 0.5f init).
    // RNG = WaveManager::GetInstance()->m_Random.
    //
    // Draw-count contract (matters: Rand32 shares one global sequence, so a wrong
    // count shifts every later draw in the game). N = wi->m_TotalWeight,
    // W = wi->m_WaveIndex, which seeds item.m_Count2:
    //   isLast              -> 1 draw
    //   policy 5/95/35/65   -> 1 + N draws
    //   policy 50 (balance) -> 1 + max(0, N - max(0, W-1)) draws; the balance arm
    //                          picks the trailing side outright, with no draw.
    //
    // DEAD IN v1.6.1: the sole caller, WaveManager::SetupWaveQue @0x00123458, has no
    // xrefs, so this never runs today. See the reachability note in WaveModifier.cpp.
    void AddWave(WaveInfo* wi, bool isLast);

    // WaveQue::PopWave — v1.6.1 @0x0012cfbc.
    // Pops front of m_Items into *out. Returns true if an item was available.
    bool PopWave(WaveQueItem* out);

    // WaveQue::RandomiseOrder — v1.6.1 @0x0012d1d0.
    // If mirror: doubles the queue by inserting a left/right-mirrored duplicate of
    // each item (slot ops 1<->2 flipped, m_Count1<->m_Count2 swapped) before it ->
    // [mirror0, orig0, mirror1, orig1, ...]. Originals are never mutated.
    void RandomiseOrder(bool mirror);

    // WaveQue::AddSpecials @0x0012ce8c.
    // Iterates all items; for each spawner-op with Rand32(100)<5 (or the cross-item idle
    // cooldown counter > 4), sets the op to 3 (special) if item.m_SpecialsCount<2, and
    // decrements the slot-selected counter (m_Count0/m_Count1/m_Count2).
    // RNG = WaveManager::GetInstance()->m_Random.
    void AddSpecials();
};

#ifdef __bada__
static_assert(sizeof(WaveQue) == 0x0c, "WaveQue size mismatch");
static_assert(sizeof(SPAWNER_INFO) == 0x64, "SPAWNER_INFO size mismatch"); // v1.6.1 SpawnModifier::Clone @0x0014be2c -- operator new(0x64) sizes SPAWNER_INFO
static_assert(sizeof(WaveInfo) == 0x7c, "WaveInfo size mismatch"); // v1.6.1 WaveInfo @0x122800 (MEDIUM confidence)
static_assert(offsetof(WaveInfo, m_ExclusiveTag) == 0x78, "WaveInfo m_ExclusiveTag offset mismatch");
#endif

#endif // FN_WAVE_STRUCTS_H
