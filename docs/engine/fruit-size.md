## Fruit Collision Radius Formula

<!-- Analysed: 2026-04-15T16:00 -->

The collision radius is computed in `Fruit::Init @ 0x0017630e..0x0017631e`:

```
vldr s14, [r3, #0x244]   ; s14 = m_Scale          (XML "scale" attr)
vldr s15, [r3, #0x248]   ; s15 = m_CollisionScale (XML "collision" attr)
vmla s15, s13, s14       ; s15 += 0.52 * s14
vmul s15, s15, scale     ; s15 *= scaleParam (1.0 typical)
```

**Formula: `radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam`**

### Field Mappings (from FRUIT_INFO at `0x001F...`)

| Offset | Field | Default | XML Attr | Notes |
|--------|-------|---------|----------|-------|
| +0x244 | m_Scale | 1.0 | "scale" | Visual scale × 0.01 in rendering |
| +0x248 | m_CollisionScale | 25.0 | "collision" | Collision radius factor |

Every fruit in `FruitNinjaBada/Data/xml/fruitlist.xml` overrides both:
- **Watermelon**: `scale="75" collision="5"` → radius = 5 + 0.52×75 = **44**
- **Apple**: `scale="60" collision="5"` → radius = 5 + 0.52×60 = **36.2**
- **Mango**: `scale="65" collision="5"` → radius = 5 + 0.52×65 = **38.8**

The visual scale chain in `SetFruitType` reads `+0x244` directly and scales by 0.01:
```c
entity.scale = Vec3::One * (m_Scale * 0.01) * scaleParam;
```

**Important**: The fruit's collision sphere is updated in `Fruit::Update`, and the radius is NOT post-multiplied by any additional fruit-scale factor (a port-specific bug that incorrectly shrank hitboxes 25–50% has been fixed).