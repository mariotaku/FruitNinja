# Data Structs (FRUIT_INFO, FruitSaveData)

## FRUIT_INFO (size = 0x330 = 816 bytes)

<!-- Analysed: 2026-04-13T14:00 -->

Array allocated as `8 + count × 0x330` bytes (8-byte header: stride=0x330, count). Loaded from XML by `Fruit::LoadInfo` (0x17987c, 527 lines). Field offsets re-verified 2026-04-13 by tracing every `strcpy`/`QueryFloatAttribute`/`QueryIntAttribute` in the decompile.

> **Correction 2026-04-13**: Previous versions of this doc had `m_Scale`/`m_CollisionScale` swapped, and several string field names at +0x40/+0x80/+0xC0/+0x200/+0x278 were incorrect. The mapping below is verified against the LoadInfo decompile sequence.

### String Fields (char[0x40] = 64 bytes each, unless noted)

| Offset | Size | Name | XML Source |
|--------|------|------|------------|
| +0x000 | 0x40 | m_Name | "name" attr (e.g. "apple"); used as default for other strings |
| +0x040 | 0x40 | m_SingularEnglish | "singularEnglish" attr (e.g. "apple"; fallback: m_Name) |
| +0x080 | 0x40 | m_PluralEnglish | "pluralEnglish" attr (e.g. "apples"; fallback: sprintf("%ss", m_Name)) |
| +0x0C0 | 0x40 | m_Singular | "singular" attr (localisation key, e.g. "FRUITNAME_APPLE"; fallback: m_Name) |
| +0x100 | 0x40 | m_Plural | "plural" attr (localisation key, e.g. "FRUITNAME_PLURAL_APPLE"; fallback: sprintf("%ss", m_Name)) |
| +0x140 | 0x40 | m_TotalStatKey | sprintf("%s_total", m_Name); hash → +0x260 |
| +0x180 | 0x40 | m_PointTotalKey | sprintf("%s_point_total", m_Name); hash → +0x264 |
| +0x1C0 | 0x40 | m_DropsKey | sprintf("%s_drops", m_Name); hash → +0x268 |
| +0x200 | 0x40 | m_ModelName | "modelName" attr (e.g. "banana_speed"; fallback: m_Name) |
| +0x278 | 0x40 | m_FactTexture | "factTexture" attr (e.g. "sml_ap"; may be empty) |

### Colour Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x240 | Colour (BGRA) | m_FruitColour | "colour" attr: "R,G,B,A" (e.g. "248,255,164,130") → bytes B,G,R,A |
| +0x2F8 | Colour (BGRA) | m_FactColour | "factColour" attr: "R,G,B" → bytes B,G,R,0xFF |

### Float Fields (from XML QueryFloatAttribute)

<!-- Analysed: 2026-04-15T16:00 -->

> **Verified 2026-04-15**: Field offsets and names confirmed by RE analysis of `Fruit::Init` (0x0017630e–0x0017631e). The collision radius formula is `radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam`, see `docs/engine/fruit-size.md` for details.

| Offset | Type | Name | Default | XML Attr | Notes |
|--------|------|------|---------|----------|-------|
| +0x244 | float | **m_Scale** | 1.0f | "scale" | Visual scale × 0.01 in SetFruitType (e.g. watermelon=75 → 0.75) |
| +0x248 | float | **m_CollisionScale** | 25.0f | "collision" | Collision radius base (e.g. watermelon=5); radius = collision + 0.52×scale |
| +0x24C | float | m_HitInfluence | 0.75f | "hitInfluence" | Hit influence multiplier |

### Hash Fields (uint32, computed from strings)

| Offset | Type | Name | Source |
|--------|------|------|--------|
| +0x250 | uint | m_NameHash | StringHash(m_Name lowercase) |
| +0x254 | uint | m_NameHashUpper | StringHash(m_Name with first char uppercased) |
| +0x258 | uint | m_TrailHash | StringHash(sprintf("%s_trail", m_Name)) |
| +0x25C | uint | m_SlicedHash | StringHash(sprintf("%s_sliced", m_Name)) |
| +0x260 | uint | m_TotalStatHash | StringHash(m_TotalStatKey at +0x140) |
| +0x264 | uint | m_PointTotalHash | StringHash(m_PointTotalKey at +0x180) |
| +0x268 | uint | m_DropsHash | StringHash(m_DropsKey at +0x1C0) |

### Bool/Flag Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x26C | byte | m_bHasSplatSeeds | "hasSplatSeeds"/"splats" attr: strcmp("true") |
| +0x2FC | byte | m_bOnSide | "onside"/"onSide" attr: strcmp("true") |
| +0x318 | byte | m_bNoCritical | "noCritical" attr; also cleared if colour.a==0 or score >= max |
| +0x319 | byte | m_bSpecial | QueryIntAttribute == 1 ("onlySprinkle" likely) |

### Int Fields (from XML QueryIntAttribute)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x308 | int | m_Chance | "chance" attr; spawn weight (e.g. 100; dragon=0, coconut=50) |
| +0x314 | int | m_Score | "score" attr; base points on slice (e.g. dragon=50) |
| +0x324 | int | m_CoinsMin | "coinsMin" attr; random bonus range start |
| +0x328 | int | m_CoinsMax | "coinsMax" attr; random bonus max |

### Fact Strings

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x270 | int | m_FactCount | Number of child `<fact>` XML elements |
| +0x274 | char** | m_pFacts | Heap-allocated array of 0x100-byte strings |

### Impact Sounds

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x31C | ImpactSound* | m_pSounds | Heap-allocated array of ImpactSound structs |
| +0x320 | int | m_SoundCount | Number of impact sound entries |

**ImpactSound** (0xC = 12 bytes):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | char* | m_SoundName | Heap-allocated SFX name string |
| +0x04 | int | m_Weight | Probability weight |
| +0x08 | int | m_CumulativeWeight | Running total of weights |

Default (if no `<impact_sound>` elements in XML): 1 ImpactSound with auto-generated name from fruit name (capitalised first letter).

### Power-Ups

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x32C | FRUIT_POWERS* | m_pPowers | Null if no `<power>` elements for this fruit |

**FRUIT_POWERS** (8 bytes):

| Offset | Type | Name |
|--------|------|------|
| +0x00 | FRUIT_POWER* | m_pArray |
| +0x04 | uint | m_Count |

**FRUIT_POWER** (0xC = 12 bytes):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | uint | m_PowerHash | StringHash of power-up name attr |
| +0x04 | int | m_Weight | Probability weight |
| +0x08 | uint | m_CumulativeWeight | Running total |

### Texture SmartPtrs

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x300 | SmartPtr\<Texture\> | m_HudTexture | LoadLocalisedTexture(sprintf("hud_%s.tex", name)) |
| +0x304 | SmartPtr\<Texture\> | m_ZenTexture | LoadLocalisedTexture(sprintf("zen_%s.tex", name)) |

### Gaps

| Offset | Notes |
|--------|-------|
| +0x30C..+0x313 | 8 bytes: not written by LoadInfo; runtime cache or padding |

---

## Bomb Config (from fruitlist.xml)

<!-- Analysed: 2026-04-12T00:00 -->

Parsed by `Fruit::LoadInfo` (0x0017987c, lines 126-132) from `<bomb size="55" collision="35"/>` element in fruitlist.xml. Stored at BSS 0x001F43B8 (accessed via GOT+0x7990).

| Offset | Size | Type | Name | XML Attr | Value | Notes |
|--------|------|------|------|----------|-------|-------|
| +0x88 | 4 | float | bombVisualScale | "size" | 55.0 | Visual scale multiplier for bomb rendering |
| +0x8C | 4 | float | bombCollisionSize | "collision" | 35.0 | Collision radius multiplier |

**Used by Bomb::Init (0x00172504)**:

- Visual scale: `Vec3::One × bombVisualScale × 0.01 × scaleFactor` = uniform scale `0.55 × scaleFactor`
- Collision radius: `bombCollisionSize × 0.5 × scaleFactor` = `17.5 × scaleFactor`

The bomb config is accessed via pointer indirection at GOT+0x7990 → BSS 0x001F43B8. Both values are parsed from XML using `atof()` on string attributes.

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
</fruitInfoFile>
```

---

## See Also

- [Fruit entity](../entities/fruit.md) — LoadInfo, SetFruitType, CollisionResponse
- [Save system](../systems/save-system.md) — FruitSaveData persistence
- [Resources](../resources.md) — fruitlist.xml file format
