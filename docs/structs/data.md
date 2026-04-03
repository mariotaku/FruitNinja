# Data Structs (FRUIT_INFO, FruitSaveData)

## FRUIT_INFO (size = 0x330 = 816 bytes)

Array allocated as `8 + count × 0x330` bytes (8-byte header: stride=0x330, count). Loaded from XML by `Fruit::LoadInfo` (0x17987c, 530 lines).

### String Fields (char[0x40] = 64 bytes each)

| Offset | Size | Name | XML Source |
|--------|------|------|------------|
| +0x000 | 0x40 | m_Name | "name" attribute; also used as default for other strings |
| +0x040 | 0x40 | m_AltTextureName | Attribute (fallback: m_Name) |
| +0x080 | 0x40 | m_ModelPath | Attribute (fallback: "FN_FACT_%s" with m_Name) |
| +0x0c0 | 0x40 | m_LocalisedName | Attribute (fallback: m_Name) |
| +0x100 | 0x40 | m_FactText | Attribute (fallback: "FN_FACT_%s" with m_Name) |
| +0x140 | 0x40 | m_StatCategory | sprintf("%s_sliced", m_Name) |
| +0x180 | 0x40 | m_StatName2 | sprintf(pattern, m_Name) |
| +0x1c0 | 0x40 | m_StatName3 | sprintf(pattern, m_Name) |
| +0x200 | 0x40 | m_ExtraString | Attribute (fallback: m_Name) |

### Colour Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x240 | Colour (BGRA) | m_FruitColour | Parsed from "colour" attr: "R,G,B,A" → bytes B,G,R,A |
| +0x2f8 | Colour (BGRA) | m_SecondColour | Parsed from attr: "R,G,B" → bytes B,G,R,0xFF |

### Hash Fields (uint32, computed from strings)

| Offset | Type | Name | Source |
|--------|------|------|--------|
| +0x250 | uint | m_NameHash | StringHash(m_Name) |
| +0x254 | uint | m_NameHashUpper | StringHash(m_Name with first char uppercased) |
| +0x258 | uint | m_PatternHash1 | StringHash(sprintf(pattern1, m_Name)) |
| +0x25c | uint | m_PatternHash2 | StringHash(sprintf(pattern2, m_Name)) |
| +0x260 | uint | m_StatCategoryHash | StringHash(m_StatCategory) |
| +0x264 | uint | m_StatName2Hash | StringHash(m_StatName2) |
| +0x268 | uint | m_StatName3Hash | StringHash(m_StatName3) |

### Float Fields (from XML QueryFloatAttribute)

| Offset | Type | Name | Default | Notes |
|--------|------|------|---------|-------|
| +0x244 | float | m_CollisionScale | 25.0f | Collision radius scaling factor; also used as display scale. radius = m_CollisionBase + const × m_CollisionScale |
| +0x248 | float | m_CollisionBase | 1.0f | Base collision radius; radius ≤ 0 means fruit cannot be sliced |
| +0x24c | float | m_SizeMult | 0.75f | Size multiplier |

### Bool/Flag Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x26c | byte | m_bFlag26c | From XML attr; strcmp("true") → 1 |
| +0x2fc | byte | m_bFlag2fc | From XML attr; strcmp("true") → 1 |
| +0x318 | byte | m_bScorable | From XML attr; cleared if m_FruitColour.a==0 or baseScore >= max |
| +0x319 | byte | m_bSpecial | From XML QueryIntAttribute (== 1) |

### Int Fields (from XML QueryIntAttribute)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x308 | int | m_IntField308 | Zeroed, then from XML |
| +0x314 | int | m_BaseScore | Points awarded on normal slice |
| +0x324 | int | m_RandBonusBase | Random bonus range start |
| +0x328 | int | m_RandBonusMax | Random bonus max; initially = m_RandBonusBase, then overridden |

### Fact Strings

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x270 | int | m_FactCount | Number of child "fact" XML elements |
| +0x274 | char** | m_pFacts | Ptr to array of 0x100-byte strings (heap) |

### Impact Sounds

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x320 | int | m_SoundCount | Number of impact sound entries (field 800) |
| +0x31c | ImpactSound* | m_pSounds | Ptr to array of ImpactSound structs |

**ImpactSound** (0xc = 12 bytes):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | char* | m_SoundName | Heap-allocated SFX name string |
| +0x04 | int | m_Weight | Probability weight |
| +0x08 | int | m_CumulativeWeight | Running total of weights |

Default (if no sound elements in XML): 1 ImpactSound with auto-generated name from fruit name.

### Power-Ups

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x32c | FRUIT_POWERS* | m_pPowers | Null if no power-ups for this fruit |

**FRUIT_POWERS** (8 bytes):

| Offset | Type | Name |
|--------|------|------|
| +0x00 | FRUIT_POWER* | m_pArray |
| +0x04 | uint | m_Count |

**FRUIT_POWER** (0xc = 12 bytes):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | uint | m_PowerHash | StringHash of power-up name |
| +0x04 | int | m_Weight | Probability weight |
| +0x08 | uint | m_CumulativeWeight | Running total |

### Texture SmartPtrs

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x300 | SmartPtr\<Texture\> | m_FruitTexture | Main fruit texture (localised) |
| +0x304 | SmartPtr\<Texture\> | m_FruitTexture2 | Variant texture (fact card?) |

### Unmapped Regions

| Range | Size | Notes |
|-------|------|-------|
| +0x278..+0x2F7 | 128 bytes | Contains string at +0x278 (from XML attr at DAT_0017a310) |
| +0x308..+0x313 | 12 bytes | int fields (partially mapped: +0x308, +0x309..0x30b) |
| +0x30C..+0x313 | 8 bytes | SmartPtr or additional data |

### XML Structure

```xml
<fruitdata>
  <globals colour="R,G,B,A" attr1="int" attr2="int" ... scale="float" speed="float" size="float"/>
  <physics attr1="float" attr2="float"/>
  <fruit name="apple" colour="R,G,B,A" secondcolour="R,G,B" ...
         scale="25.0" speed="1.0" sizemult="0.75" basescore="1" 
         bonusbase="0" bonusmax="0" special="0" scorable="true" ...>
    <fact>Some fun fact about apples</fact>
    <fact>Another fact</fact>
    <sound score="1">sfx_apple_hit</sound>
    <sound score="2">sfx_apple_hit_2</sound>
    <power name="frenzy" score="1"/>
  </fruit>
  <!-- more fruit elements... -->
</fruitdata>
```

---

## See Also

- [Fruit functions](../functions/fruit.md) -- LoadInfo, CollisionResponse
- [Save system](../systems/save-system.md) -- FruitSaveData persistence
- [Resources](../resources.md) -- fruitlist.xml file format
