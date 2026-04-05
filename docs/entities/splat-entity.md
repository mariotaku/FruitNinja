# SplatEntity System

## DrawActiveSplats (0x180344, 45 lines)

Splats are rendered as a single batched triangle list:

```
1. Iterate splat pool: count active splats (field_0x75 != 0 AND type >= 0)
2. For each active: call splat->vtable->Draw() to build vertex data
3. If any active splats:
   a. Set splat texture
   b. Reset matrix stack
   c. Translate by global offset (camera-relative)
   d. Upload matrices
   e. Mesh::DrawTriList(vertices, count*6, false, null)
   f. Unset texture
```

Splats use `QUADCUSTOMVERTEX` -- custom vertex format with position + colour + UV.
Each splat = 2 triangles = 6 vertices.

## SplatEntity Pool

- Pool-based: `GetFree()` / `CreatePool(count)`
- Each splat: 0x78 bytes (stride = 0x1e x 4)
- Key fields: position(+0x38), velocity(+0x5c), type(+0x70), active(+0x75)
- 6 splat variants (types 0-5) chosen randomly based on position/velocity

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| SplatEntity::DrawActiveSplats | 0x00180344 | 45 | Batch render all splats |
| SplatEntity::Update | 0x0017f774 | 267 | Physics + type selection |

---

## See Also

- [Entity base](entity-base.md) -- Mortar::Entity base class
- [BombFlash](bomb-flash.md) -- Bomb flash effect
- [BombBlast](bomb-blast.md) -- Bomb explosion effect
