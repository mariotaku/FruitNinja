#ifndef FN_FRUIT_INFO_H
#define FN_FRUIT_INFO_H

//
// FRUIT_INFO — per-fruit-type data loaded from Data/xml/fruitlist.xml
// Original: 0x338 bytes per entry, allocated as 8 + count * 0x338
// (v1.6.1 Fruit::FruitType @0x001db6c8 array stride; LoadInfo operator_new(0x338))
// Loaded by Fruit::LoadInfo (0x17987c, 527 lines)
//
// Analysed: 2026-04-13T14:00
// Layout correction 2026-05-03: field offsets for m_Singular/m_PluralEnglish
// were swapped (binary has m_Singular at +0x080, m_PluralEnglish at +0x0C0);
// m_HudTexture/m_ZenTexture renamed to m_pFruitTexture/m_pFruitTexture2;
// pad_30c[8] replaced with m_CumWeight+m_CumCritWeight;
// m_bNoCritical renamed to m_bScorable (semantics: positive = can score a critical).

#include <cstdint>
#include <cstddef>
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/Colour.h"

// Impact sound entry (0xC = 12 bytes)
// Binary: ImpactSound struct, sizeof confirmed = 12
struct ImpactSound {
    char* m_SoundName;         // +0x00: heap-allocated SFX name
    int   m_Weight;            // +0x04: probability weight
    int   m_CumulativeWeight;  // +0x08: running total
    ImpactSound() : m_SoundName(nullptr), m_Weight(0), m_CumulativeWeight(0) {}
};

// Power-up entry (0xC = 12 bytes)
// Binary: FRUIT_POWER struct
struct FRUIT_POWER {
    uint32_t m_PowerHash;        // +0x00: StringHash of power name
    int      m_Weight;           // +0x04: probability weight
    uint32_t m_CumulativeWeight; // +0x08: running total
    FRUIT_POWER() : m_PowerHash(0), m_Weight(0), m_CumulativeWeight(0) {}
};

// Port alias so existing code using FruitPower still compiles
typedef FRUIT_POWER FruitPower;

// Power-up container (8 bytes)
// Binary: FRUIT_POWERS struct
struct FRUIT_POWERS {
    FRUIT_POWER* m_pArray;  // +0x00
    uint32_t     m_Count;   // +0x04
    FRUIT_POWERS() : m_pArray(nullptr), m_Count(0) {}

    // Binary @ 0x00175714. Returns true if any power in m_pArray is currently active.
    bool AnyActivePowers();

    // Binary @ 0x0017a7d8. Weighted random pick; returns m_PowerHash of selected entry.
    uint32_t RandomPower();
};

// Port alias so existing code using FruitPowers still compiles
typedef FRUIT_POWERS FruitPowers;

// Matches FRUIT_INFO (0x338 = 824 bytes per entry)
// LoadInfo @ 0x0017987c
struct FruitInfo {
    // String fields (char[0x40] = 64 bytes each)
    char m_Name[64];              // +0x000: "name" attr
    char m_SingularEnglish[64];   // +0x040: "singularEnglish" attr (fallback: m_Name)
    char m_Singular[64];          // +0x080: "singular" attr (localisation key; fallback: m_Name)
    char m_PluralEnglish[64];     // +0x0C0: "pluralEnglish" attr (fallback: sprintf("%ss", m_Name))
    char m_Plural[64];            // +0x100: "plural" attr (localisation key; fallback: sprintf("%ss", m_Name))
    char m_TotalStatKey[64];      // +0x140: sprintf("%s_total", m_Name)
    char m_PointTotalKey[64];     // +0x180: sprintf("%s_point_total", m_Name)
    char m_DropsKey[64];          // +0x1C0: sprintf("%s_drops", m_Name)
    char m_ModelName[64];         // +0x200: "modelName" attr (fallback: m_Name)

    // Colour fields
    uint8_t m_FruitColour[4];     // +0x240: "colour" attr BGRA (parsed from "R,G,B,A")

    // Float fields (order confirmed from LoadInfo decompile 2026-04-13)
    float m_Scale;                // +0x244: "scale" attr (default 1.0)
    float m_CollisionScale;       // +0x248: "collision" attr (default 25.0)
    float m_HitInfluence;         // +0x24C: "hitInfluence" attr (default 0.75)

    // Hash fields (computed from strings)
    uint32_t m_NameHash;          // +0x250: StringHash(m_Name lowercase)
    uint32_t m_NameHashUpper;     // +0x254: StringHash(m_Name with first char upper)
    uint32_t m_TrailHash;         // +0x258: StringHash("%s_trail")
    uint32_t m_SlicedHash;        // +0x25C: StringHash("%s_sliced")
    uint32_t m_TotalStatHash;     // +0x260: StringHash(m_TotalStatKey)
    uint32_t m_PointTotalHash;    // +0x264: StringHash(m_PointTotalKey)
    uint32_t m_DropsHash;         // +0x268: StringHash(m_DropsKey)

    // Flags
    uint8_t m_bHasSplatSeeds;     // +0x26C: "hasSplatSeeds"/"splats" attr
    uint8_t _pad_26D[3];

    // Fact strings
    int    m_FactCount;           // +0x270: count of <fact> children
    char** m_pFacts;              // +0x274: array of 0x100-byte strings

    // factTexture + padding (char[64] + 64 unused bytes)
    char    m_FactTexture[64];    // +0x278: "factTexture" attr
    uint8_t _pad_2B8[64];         // +0x2B8..+0x2F7: unknown/padding

    // Colours + flags
    uint8_t m_FactColour[4];      // +0x2F8: "factColour" attr (R,G,B -> B,G,R,0xFF)
    uint8_t m_bOnSide;            // +0x2FC: "onside"/"onSide" attr
    uint8_t _pad_2FD[3];

    // Textures (+0x300, +0x304)
    // Binary names: m_pFruitTexture (HUD), m_pFruitTexture2 (Zen).
    // Port keeps m_HudTexture/m_ZenTexture for backward compatibility with locked consumers.
    Mortar::SmartPtr<Mortar::Texture> m_HudTexture;    // +0x300: "hud_%s.tex" (HUD thumbnail) — binary: m_pFruitTexture
    Mortar::SmartPtr<Mortar::Texture> m_ZenTexture;    // +0x304: "zen_%s.tex" (Zen mode)      — binary: m_pFruitTexture2

    // Spawn weight + runtime caches
    int m_Chance;                 // +0x308: "chance" attr
    int m_CumWeight;              // +0x30C: runtime cache for RandomFruit cumulative weight
    int m_CumCritWeight;          // +0x310: runtime cache for RandomFruit critical cumulative weight
    int m_Score;                  // +0x314: "score" attr

    // Scoring flags
    // m_bScorable: cleared if score >= global threshold (semantics: 1 = can score a critical hit)
    // Binary field name was "m_bNoCritical" in Ghidra but polarity is inverted relative to
    // the "noCritical" XML attr. See LoadInfo @ 0x0017987c for the store sequence.
    uint8_t m_bScorable;          // +0x318
    uint8_t m_bSpecial;           // +0x319: from "noCritical" attr (QueryIntAttribute == 1)
    uint8_t _pad_31A[2];

    // Impact sounds
    ImpactSound* m_pSounds;       // +0x31C
    int          m_SoundCount;    // +0x320

    int m_CoinsMin;               // +0x324: "coinsMin" attr
    int m_CoinsMax;               // +0x328: "coinsMax" attr  // Ghidra's "m_RandBonusMax" is wrong

    // Power-ups
    FRUIT_POWERS* m_pPowers;      // +0x32C

    // Super-fruit flag: byte at +0x330, set iff m_Score==0 (no score attr).
    // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084.
    // Binary: FRUIT_INFO+0x330 gate in SuperFruitControl::SuperFruitThrown
    // and SuperFruitControl::SuperFruitSliced (@ 0x001bbf48 / 0x001be630).
    uint8_t m_bIsSuperFruit;      // +0x330
    uint8_t _pad_331[3];          // +0x331..+0x333 (alignment pad)

    // TODO: v1.6.1 0x001e1084 (Fruit::LoadInfo) -- what populates this field is not
    // yet RE'd. Zeroed in the binary FRUIT_INFO ctor (v1.6.1 @0x001e3d88); gates
    // diffuse-map attach in Fruit::LoadFruitModels (v1.6.1 @0x001e0b30/0x001e0cb8)
    // via `fruitInfo[i]+0x334 != 0`. Placeholder name pending purpose RE.
    int32_t m_Field334;           // +0x334
};

// Layout asserts.
// Fields before the SmartPtr members (offsets 0x000..0x2FC) are pointer-size-
// independent and pass on both 32-bit and 64-bit builds.
// Fields at 0x300+ depend on Mortar::SmartPtr<T> being 4 bytes (ARM32), which is only
// true on the cross-compile (32-bit) path. On the 64-bit port build SmartPtr
// is 8 bytes, so the asserts from m_HudTexture onward are guarded to 32-bit.
#ifdef __bada__
static_assert(__builtin_offsetof(FruitInfo, m_Name)            == 0x000, "");
static_assert(__builtin_offsetof(FruitInfo, m_SingularEnglish) == 0x040, "");
static_assert(__builtin_offsetof(FruitInfo, m_Singular)        == 0x080, "");
static_assert(__builtin_offsetof(FruitInfo, m_PluralEnglish)   == 0x0C0, "");
static_assert(__builtin_offsetof(FruitInfo, m_Plural)          == 0x100, "");
static_assert(__builtin_offsetof(FruitInfo, m_TotalStatKey)    == 0x140, "");
static_assert(__builtin_offsetof(FruitInfo, m_PointTotalKey)   == 0x180, "");
static_assert(__builtin_offsetof(FruitInfo, m_DropsKey)        == 0x1C0, "");
static_assert(__builtin_offsetof(FruitInfo, m_ModelName)       == 0x200, "");
static_assert(__builtin_offsetof(FruitInfo, m_FruitColour)     == 0x240, "");
static_assert(__builtin_offsetof(FruitInfo, m_Scale)           == 0x244, "");
static_assert(__builtin_offsetof(FruitInfo, m_CollisionScale)  == 0x248, "");
static_assert(__builtin_offsetof(FruitInfo, m_HitInfluence)    == 0x24C, "");
static_assert(__builtin_offsetof(FruitInfo, m_NameHash)        == 0x250, "");
static_assert(__builtin_offsetof(FruitInfo, m_NameHashUpper)   == 0x254, "");
static_assert(__builtin_offsetof(FruitInfo, m_TrailHash)       == 0x258, "");
static_assert(__builtin_offsetof(FruitInfo, m_SlicedHash)      == 0x25C, "");
static_assert(__builtin_offsetof(FruitInfo, m_TotalStatHash)   == 0x260, "");
static_assert(__builtin_offsetof(FruitInfo, m_PointTotalHash)  == 0x264, "");
static_assert(__builtin_offsetof(FruitInfo, m_DropsHash)       == 0x268, "");
static_assert(__builtin_offsetof(FruitInfo, m_bHasSplatSeeds)  == 0x26C, "");
static_assert(__builtin_offsetof(FruitInfo, m_FactCount)       == 0x270, "");
static_assert(sizeof(FruitInfo) == 0x338, "FruitInfo size mismatch -- v1.6.1 Fruit::FruitType @0x001db6c8 stride is 0x338");
static_assert(__builtin_offsetof(FruitInfo, m_pFacts)          == 0x274, "");
static_assert(__builtin_offsetof(FruitInfo, m_FactTexture)     == 0x278, "");
static_assert(__builtin_offsetof(FruitInfo, m_FactColour)      == 0x2F8, "");
static_assert(__builtin_offsetof(FruitInfo, m_bOnSide)         == 0x2FC, "");
static_assert(__builtin_offsetof(FruitInfo, m_HudTexture)      == 0x300, "");  // binary: m_pFruitTexture
static_assert(__builtin_offsetof(FruitInfo, m_ZenTexture)      == 0x304, "");  // binary: m_pFruitTexture2
static_assert(__builtin_offsetof(FruitInfo, m_Chance)          == 0x308, "");
static_assert(__builtin_offsetof(FruitInfo, m_CumWeight)       == 0x30C, "");
static_assert(__builtin_offsetof(FruitInfo, m_CumCritWeight)   == 0x310, "");
static_assert(__builtin_offsetof(FruitInfo, m_Score)           == 0x314, "");
static_assert(__builtin_offsetof(FruitInfo, m_bScorable)       == 0x318, "");
static_assert(__builtin_offsetof(FruitInfo, m_bSpecial)        == 0x319, "");
static_assert(__builtin_offsetof(FruitInfo, m_pSounds)         == 0x31C, "");
static_assert(__builtin_offsetof(FruitInfo, m_SoundCount)      == 0x320, "");
static_assert(__builtin_offsetof(FruitInfo, m_CoinsMin)        == 0x324, "");
static_assert(__builtin_offsetof(FruitInfo, m_CoinsMax)        == 0x328, "");
static_assert(__builtin_offsetof(FruitInfo, m_pPowers)         == 0x32C, "");
static_assert(__builtin_offsetof(FruitInfo, m_bIsSuperFruit)   == 0x330, "");
static_assert(__builtin_offsetof(FruitInfo, m_Field334)        == 0x334, "");
#endif

// Maximum fruit types
static const int FRUIT_INFO_MAX = 32;  // 22 in Bada XML, room for extras

// Full loader (called by Fruit::LoadInfo)
void FruitInfo_Load(const char* xmlPath);

#if defined(FN_BLOCK_PRELOAD)
// Boot trim (task #59). fruit_shadow.tex loads at boot (menu
// fruit may cast shadows); only the per-fruit hud_%s.tex/zen_%s.tex icons
// are gameplay-only (never shown at menu) and stay deferred here. Assigns
// straight into the FruitInfo array's own m_HudTexture/m_ZenTexture members
// -- their natural strong-ref home, matching the binary's ownership; only
// load *timing* differs. Called from Fruit::LoadHudTextures(), which is
// called once from BlockLoader::PreloadBlock(RES_BLOCK_INGAME). Safe to
// call more than once: LoadLocalisedTexture cache-checks.
void FruitInfo_LoadHudTextures();
#endif

// --- Runtime FRUIT_INFO table: the binary's two .bss globals ---
//
// v1.6.1 reads both of these as plain globals at every consumer. There is NO
// accessor function for the count anywhere in the binary (all ~15 count sites
// -- Fruit::FruitType, MenuButton::Update, ShopScreen::Update, RandomFruit,
// WaveManager::UpdateWave, ... -- are a direct `ldr` on 0x00332a1c), so the
// port exposes them as globals too and reads them directly.
//
// g_FruitInfoCount -- v1.6.1 .bss @0x00332a1c. The RUNTIME number of
//   <FruitInfo> elements parsed from fruitlist.xml. (Ghidra labels it
//   MAX_FRUIT_TYPES, which is misleading -- it is not a capacity.) Written
//   ONLY inside FruitInfo_Load: `= 0` (v1.6.1 Fruit::LoadInfo @0x001e13c8),
//   then `++` per parsed element (@0x001e13e4).
//
//   It doubles as the BOMB fruit-type sentinel: MenuButton / ShopScreen /
//   DojoScreen / GameModeScreen / MainScreen / GameOverScreen pass exactly
//   this value as a "fruit type" to force a Bomb entity, and Fruit::Slice
//   passes `m_FruitType + g_FruitInfoCount` to SplatEntity::MakeSplat to
//   trigger the critical flash. Any consumer that can hold such an
//   out-of-range value MUST range-check against g_FruitInfoCount before
//   indexing g_pFruitInfo -- FruitInfo_Get does not.
//
// g_pFruitInfo -- v1.6.1 .bss @0x00332a28, written by FruitInfo_Load
//   (@0x001e14ac). Points at ELEMENT 0 of the table, so `g_pFruitInfo[type]`
//   is the whole addressing story.
//   DIFFERS: original = `operator new(count * 0x338 + 8)` with an 8-byte
//   prefix holding the stride (0x338) and a redundant count, the global
//   pointing at buffer+8 (v1.6.1 Fruit::LoadInfo @0x001e1084); the port keeps
//   a fixed FRUIT_INFO_MAX-sized static array and points at its element 0,
//   because nothing in the port reads that prefix. The ACCESS pattern is
//   identical; only the allocation differs.
extern int        g_FruitInfoCount;
extern FruitInfo* g_pFruitInfo;

// v1.6.1 Fruit::FruitInfo @0x001da5c0 -- the whole body is
// `return g_pFruitInfo + type * sizeof(FruitInfo);` (8 instructions, one MLA).
// NO bounds check, NO null return, NO clamp: the result is never null, so do
// not test it. Pass only a type in [0, g_FruitInfoCount).
const FruitInfo* FruitInfo_Get(int type);

// Global bomb settings from the <bomb size="..." collision="..."/> element
// in fruitlist.xml. Binary stores these at +0x88/+0x8C off a separate globals
// block (NOT off the g_pFruitInfo table); the port keeps them as file statics.
float FruitInfo_GetBombSize();
float FruitInfo_GetBombCollision();

// Global critical-hit tuning from the <critical .../> element in fruitlist.xml.
// ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084 (critical block @0x1e128c-0x1e12a4).
// These are game globals (NOT per-FruitInfo fields) consumed by
// Fruit::CollisionResponse @0x001dd500 for the critical-hit roll, score bonus
// and CriticalFlash tint colour, and by Fruit::Slice @0x001dcba0 for the
// juice-burst count / spread / scale.
//
// The scalar members of the family live on class Fruit as the binary's plain
// globals -- Fruit::CRITICAL_SPLATS / _SPLAT_SCALE / _SPLAT_SPREAD /
// _DISAPPEAR_SPEED / CRITICAL_SCORE / CRITICAL_CHANCE / CRITICAL_CHANCE_START_INC
// / NEW_LIFE_AT (see the contract block in Fruit.h). FruitInfo_Load writes them
// directly; consumers read them directly. There are deliberately no accessors:
// an accessor call is a structural divergence from the binary's global load in
// every consumer.
//
// Never copy any of them as a literal into a consumer either. The binary's
// .data initialisers are pre-XML link-time values that Fruit::LoadInfo
// overwrites before any slice happens; the shipped fruitlist.xml values are
// what actually run. Splats went 10 -> 15, scale 1.5 -> 1.25, spread 1.2 -> 1.25
// when that trap was fixed (#114).
//
// "colour" CSV -> direct R,G,B,A field order (NOT byte-swapped like the
// per-fruit m_FruitColour BGRA parse above). Staged in a file static here and
// copied into Fruit::CRITICAL_COLOUR by Fruit::LoadInfo; consumers already read
// that global directly.
const Colour& FruitInfo_GetCriticalColour();

// Shadow texture (FRUIT_INFO_HEADER->shadowTex equivalent, binary +0xC0).
// Loaded by FruitInfo_Load step 0 from "fruit_shadow.tex".
// Returns nullptr if not loaded yet.
Mortar::Texture* FruitInfo_GetShadowTex();

#endif
