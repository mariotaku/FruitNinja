# SplatEntity System

<!-- Analysed: 2026-04-15T16:00 -->

Splat entities are short-lived visual particles spawned when fruits are sliced. They fly outward, bounce, and fade over ~4 seconds.

## SplatEntity Struct (0x78 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 0x3c | Entity | super | Base Entity (60 bytes) |
| +0x04 | 4 | float | m_ColourPhase | Fruit→base colour lerp; 0 for fruit-coloured, 0.75 for default |
| +0x08 | 4 | BGRA | m_Col | Colour at spawn |
| +0x0c | 4 | float | m_AlphaBase | Base alpha for fade curve |
| +0x10 | 4 | float | m_Angle | Random 0–359°, unused after init |
| +0x18 | 1 | byte | m_bParam3 | "special" spawn (crit) — biases RNG for landing type |
| +0x19 | 1 | byte | m_bSpecial | From FruitInfo.m_bSpecial |
| +0x1c | 12 | Vec3 | m_AxisA | (cos(angle), sin(angle), 0) × 0.5 for billboard rotation |
| +0x28 | 12 | Vec3 | m_AxisB | (cos(angle+180°), sin(angle+180°), 0) × 0.5 |
| +0x34 | 1 | byte | m_bFlipV | RandUint(2) != 0 |
| +0x44 | 12 | Vec3 | m_Scale | (sc, -sc, sc), sc = Rand(10) + 10 |
| +0x68 | 4 | float | m_Life | Lifetime; Rand(2.5) + 3.75 |
| +0x6c | 4 | float | m_DecayRate | Fade rate; Rand(0.25) + 0.375 |
| +0x70 | 4 | int | m_FruitType | For `FruitTypeColour` lookup |
| +0x74 | 1 | int8 | m_SplatType | -1 airborne, 0–5 landed variant |
| +0x75 | 1 | byte | m_bAlive | Pool iteration flag (1 = active) |

## MakeSplat (0x0017f2f0)

Spawns a splat at position with velocity. **Velocity transform**:
```c
m_Vel = vel;
float speed = |m_Vel|;
m_Vel.y    *= 1.5;
m_Vel.z     = speed * -0.5 - 150.0 - Rand(10.0);  // DAT_0017f568 = 150.0
m_Vel      *= 6.0;
```

Constants:
- DAT_0017f568 = 150.0
- DAT_0017f56c = 182.0
- DAT_0017f570 = 180.0

## Update (0x0017f774)

Physics integration each frame; once `pos.z < -50` (DAT_0017faa8), picks a splat type variant via RNG tree:

**Normal splats**:
- 1/4 chance: type = `Rand(2)==0 ? 2 : 3` (large)
- Otherwise: type = `Rand(6)!=0 ? 0 : 1` (small)

**m_bParam3 override** (1/2 chance):
- type = `Rand(2)==0 ? 4 : 5`

## UV Atlas (6 variants, 0x001bd014)

Each variant: 4 floats `(u0, v0, u1, v1)` for quad UV bounds.

| Index | u0 | v0 | u1 | v1 | Notes |
|-------|-----|-----|-----|-----|-------|
| 0 | 0.0 | 0.5 | 0.0 | 0.25 | Small A |
| 1 | 0.5 | 1.0 | 0.0 | 0.25 | Small B |
| 2 | 0.0 | 0.5 | 0.25 | 0.5 | Large A |
| 3 | 0.5 | 1.0 | 0.25 | 0.5 | Large B |
| 4 | 0.0 | 1.0 | 0.5 | 0.75 | Special A |
| 5 | 0.0 | 1.0 | 0.75 | 1.0 | Special B |

**Special handling**:
- `m_bSpecial`: U offset +0.5 for types 0–3
- `m_bFlipV`: Swap V edges

## DrawActiveSplats (0x00180344, 45 lines)

Renders all active splats as a single batched triangle list:

1. Iterate splat pool: count active splats (m_bAlive != 0 AND m_SplatType >= 0)
2. For each: build vertex data from pos ± axisA×scale.x ± axisB×scale.y
3. Apply colour lerp: `fade = clamp(2 * m_ColourPhase, 0, 1)` between fruit colour and base RGBA
4. Single `Mesh::DrawTriList` call with splat atlas texture

Each splat = 2 triangles = 6 QUADCUSTOMVERTEX vertices.

---

## See Also

- [Entity base](entity-base.md) -- Mortar::Entity base class
- [Fruit](fruit.md) -- Fruit::Slice spawns splats
- [SlashEntity](slash-entity.md) -- Blade trail rendering
