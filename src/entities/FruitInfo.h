#ifndef FN_FRUIT_INFO_H
#define FN_FRUIT_INFO_H

//
// FRUIT_INFO — per-fruit-type data loaded from Data/xml/fruitlist.xml
// Original: 0x330 bytes per entry, allocated as 8 + count * 0x330
// Loaded by Fruit::LoadInfo (0x17987c, 527 lines)
// See docs/structs/data.md for full layout
//
// Analysed: 2026-04-13T14:00
// Correction 2026-04-13: field offsets for m_Scale/m_CollisionScale and the
// string fields at +0x40/+0x80/+0xC0/+0x200/+0x278 were previously wrong;
// fixed to match LoadInfo decompile sequence.

#include <cstdint>
#include "util/SmartPtr.h"
#include "asset/Texture.h"

// Impact sound entry (0xC = 12 bytes)
struct ImpactSound {
    char* m_SoundName;         // +0x00: heap-allocated SFX name
    int   m_Weight;            // +0x04: probability weight
    int   m_CumulativeWeight;  // +0x08: running total
    ImpactSound() : m_SoundName(NULL), m_Weight(0), m_CumulativeWeight(0) {}
};

// Power-up entry (0xC = 12 bytes)
struct FruitPower {
    uint32_t m_PowerHash;        // +0x00: StringHash of power name
    int      m_Weight;           // +0x04: probability weight
    uint32_t m_CumulativeWeight; // +0x08: running total
    FruitPower() : m_PowerHash(0), m_Weight(0), m_CumulativeWeight(0) {}
};

// Power-up container (8 bytes)
struct FruitPowers {
    FruitPower* m_pArray;  // +0x00
    uint32_t    m_Count;   // +0x04
    FruitPowers() : m_pArray(NULL), m_Count(0) {}
};

// Matches FRUIT_INFO (0x330 = 816 bytes per entry)
struct FruitInfo {
    // String fields (char[0x40] = 64 bytes each)
    char m_Name[64];              // +0x000: "name" attr
    char m_SingularEnglish[64];   // +0x040: "singularEnglish" attr (fallback: m_Name)
    char m_PluralEnglish[64];     // +0x080: "pluralEnglish" attr (fallback: sprintf("%ss", m_Name))
    char m_Singular[64];          // +0x0C0: "singular" attr (localisation key; fallback: m_Name)
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
    uint8_t pad_26d[3];

    // Fact strings
    int    m_FactCount;           // +0x270: count of <fact> children
    char** m_pFacts;              // +0x274: array of 0x100-byte strings

    // factTexture + padding (char[64] + 64 unused bytes)
    char    m_FactTexture[64];    // +0x278: "factTexture" attr
    uint8_t pad_2b8[64];          // +0x2B8..+0x2F7: unknown/padding

    // Colours + flags
    uint8_t m_FactColour[4];      // +0x2F8: "factColour" attr (R,G,B → B,G,R,0xFF)
    uint8_t m_bOnSide;            // +0x2FC: "onside"/"onSide" attr
    uint8_t pad_2fd[3];

    // Textures
    SmartPtr<Mortar::Texture> m_HudTexture;  // +0x300: "hud_%s.tex"
    SmartPtr<Mortar::Texture> m_ZenTexture;  // +0x304: "zen_%s.tex"

    // Int fields
    int     m_Chance;             // +0x308: "chance" attr
    uint8_t pad_30c[8];           // +0x30C..+0x313: runtime cache
    int     m_Score;              // +0x314: "score" attr
    uint8_t m_bNoCritical;        // +0x318: "noCritical" attr
    uint8_t m_bSpecial;           // +0x319: flag (QueryIntAttribute == 1)
    uint8_t pad_31a[2];

    // Impact sounds
    ImpactSound* m_pSounds;       // +0x31C
    int          m_SoundCount;    // +0x320

    int m_CoinsMin;               // +0x324: "coinsMin" attr
    int m_CoinsMax;               // +0x328: "coinsMax" attr

    // Power-ups
    FruitPowers* m_pPowers;       // +0x32C
};

// Maximum fruit types
static const int FRUIT_INFO_MAX = 32;  // 22 in Bada XML, room for extras

// Full loader (called by Fruit::LoadInfo)
void FruitInfo_Load(const char* xmlPath);

// Access loaded data
const FruitInfo* FruitInfo_Get(int type);
int FruitInfo_GetCount();
FruitInfo* FruitInfo_GetArray();

// Global bomb settings from the <bomb size="..." collision="..."/> element
// in fruitlist.xml. Binary stores these on g_pFruitInfo+0x88/+0x8C.
float FruitInfo_GetBombSize();
float FruitInfo_GetBombCollision();

#endif
