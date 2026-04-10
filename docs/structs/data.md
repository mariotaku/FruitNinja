# Data Structs (FRUIT_INFO, FruitSaveData)

## FRUIT_INFO (size = 0x330 = 816 bytes)

<!-- Analysed: 2026-04-10T13:00 -->

Array allocated as `8 + count × 0x330` bytes (8-byte header: stride=0x330, count). Loaded from XML by `Fruit::LoadInfo` (0x17987c, 509 lines). Verified against full decompile 2026-04-10.

### String Fields (char[0x40] = 64 bytes each)

| Offset | Size | Name | XML Source |
|--------|------|------|------------|
| +0x000 | 0x40 | m_Name | "name" attr (e.g. "apple"); used as default for other strings |
| +0x040 | 0x40 | m_Singular | "singular" attr (localisation key, e.g. "FRUITNAME_APPLE"; fallback: m_Name) |
| +0x080 | 0x40 | m_ModelName | "modelName" attr (e.g. "banana_speed"; fallback: m_Name) |
| +0x0c0 | 0x40 | m_FactTexture | "factTexture" attr (e.g. "sml_ap"; fallback: m_Name) |
| +0x100 | 0x40 | m_Plural | "plural" attr (localisation key, e.g. "FRUITNAME_PLURAL_APPLE"; fallback: sprintf("%ss", m_Name)) |
| +0x140 | 0x40 | m_TotalStatKey | sprintf("%s_total", m_Name); hash → +0x260 |
| +0x180 | 0x40 | m_PointTotalKey | sprintf("%s_point_total", m_Name); hash → +0x264 |
| +0x1c0 | 0x40 | m_DropsKey | sprintf("%s_drops", m_Name); hash → +0x268 |
| +0x200 | 0x40 | m_SingularEnglish | "singularEnglish" attr (e.g. "apple"; fallback: m_Name) |

### Colour Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x240 | Colour (BGRA) | m_FruitColour | "colour" attr: "R,G,B,A" (e.g. "248,255,164,130") → bytes B,G,R,A |
| +0x2f8 | Colour (BGRA) | m_FactColour | "factColour" attr: "R,G,B" (e.g. "189,238,58") → bytes B,G,R,0xFF |

### Hash Fields (uint32, computed from strings)

| Offset | Type | Name | Source |
|--------|------|------|--------|
| +0x250 | uint | m_NameHash | StringHash(m_Name lowercase) |
| +0x254 | uint | m_NameHashUpper | StringHash(m_Name with first char uppercased) |
| +0x258 | uint | m_TrailHash | StringHash(sprintf("%s_trail", m_Name)) |
| +0x25c | uint | m_SlicedHash | StringHash(sprintf("%s_sliced", m_Name)) |
| +0x260 | uint | m_TotalStatHash | StringHash(m_TotalStatKey at +0x140) |
| +0x264 | uint | m_PointTotalHash | StringHash(m_PointTotalKey at +0x180) |
| +0x268 | uint | m_DropsHash | StringHash(m_DropsKey at +0x1C0) |

### Float Fields (from XML QueryFloatAttribute)

| Offset | Type | Name | Default | Notes |
|--------|------|------|---------|-------|
| +0x244 | float | m_CollisionScale | 25.0f | "collision" attr; collision radius factor |
| +0x248 | float | m_Scale | 1.0f | "scale" attr; visual scale × 0.01 in SetFruitType |
| +0x24c | float | m_HitInfluence | 0.75f | "hitInfluence" attr; size multiplier |

### Bool/Flag Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x26c | byte | m_bHasSplatSeeds | "hasSplatSeeds" attr; strcmp("true") → 1 (NOTE: XML uses "1" not "true" — may be a separate check) |
| +0x2fc | byte | m_bOnSide | "onside"/"onSide" attr; strcmp("true") → 1 |
| +0x318 | byte | m_bNoCritical | "noCritical" attr; strcmp("true") → 1; also cleared if colour.a==0 or score >= max |
| +0x319 | byte | m_bSpecial | QueryIntAttribute == 1 (exact attr name TBD — may be "onlySprinkle" or separate flag) |

### Int Fields (from XML QueryIntAttribute)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x308 | int | m_Chance | "chance" attr; spawn weight (e.g. 100; dragon=0, coconut=50) |
| +0x314 | int | m_Score | "score" attr; base points on slice (e.g. dragon=50; most fruits=0 → default scoring) |
| +0x324 | int | m_CoinsMin | "coinsMin" attr; random bonus range start |
| +0x328 | int | m_CoinsMax | "coinsMax" attr; random bonus max |

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
| +0x300 | SmartPtr\<Texture\> | m_HudTexture | LoadLocalisedTexture(sprintf("hud_%s.tex", name)) |
| +0x304 | SmartPtr\<Texture\> | m_ZenTexture | LoadLocalisedTexture(sprintf("zen_%s.tex", name)) |

### Previously Unmapped — Now Resolved

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x278 | char[0x80] | m_PluralEnglish | "pluralEnglish" attr (e.g. "apples"; 128 bytes) |
| +0x30C..+0x313 | — | (runtime only) | Not set in LoadInfo; possibly cached values or padding |

**Coverage: ~98% mapped** — all XML attributes resolved. Only +0x30C..+0x313 = 8 bytes unknown (likely runtime cache or padding). All string format patterns verified from binary string table at 0x1BCDA5-0x1BCF15.

### XML Structure

```xml
<!-- Verified against FruitNinjaBada/Data/xml/fruitlist.xml -->
<fruitInfoFile version="1.0.0">
  <critical chance="50" chance_inc="30" score="10" colour="0,140,245,170"
            scale="1.25" splats="15" spread="1.25" disappear_speed="1"/>
  <bomb size="55" collision="35"/>
  <FruitInfo name="apple" singular="FRUITNAME_APPLE" plural="FRUITNAME_PLURAL_APPLE"
             singularEnglish="apple" pluralEnglish="apples"
             chance="100" scale="60" colour="248,255,164,130" collision="5"
             onSide="true" factColour="189,238,58" factTexture="sml_ap" onlySprinkle="true">
    <fact>FRUIT_FACT_00</fact>
    <impact_sound>Impact-Apple</impact_sound>
    <power name="speed"/>
  </FruitInfo>
  <!-- 16 regular fruits + 5 special (black_pineapple, vs_watermelon, frenzy, freeze, scorex2,
       banana_locked, openfeint) = 22 total FruitInfo entries -->
</fruitInfoFile>
```

---

## See Also

- [Fruit entity](../entities/fruit.md) -- LoadInfo, CollisionResponse
- [Save system](../systems/save-system.md) -- FruitSaveData persistence
- [Resources](../resources.md) -- fruitlist.xml file format
