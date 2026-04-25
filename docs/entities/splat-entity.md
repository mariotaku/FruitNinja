# SplatEntity System

<!-- Analysed: 2026-04-25T12:00 -->

Splat entities are short-lived visual particles spawned when fruits are sliced. They fly outward, bounce, and fade over ~4 seconds.

## SplatEntity Struct (0x78 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 0x3c | Entity | super | Base Entity (60 bytes) |
| +0x04 | 4 | float | m_ColourPhase | Fruit→base colour lerp; 0 for fruit-coloured, 0.75 for default |
| +0x08 | 4 | BGRA | m_Col | Colour at spawn |
| +0x0c | 4 | float | m_AlphaBase | Base alpha for fade curve |
| +0x10 | 4 | float | m_Angle | Random 0-359 degrees, unused after init |
| +0x18 | 1 | byte | m_bParam3 | "special" spawn (crit) -- biases RNG for landing type |
| +0x19 | 1 | byte | m_bSpecial | From FruitInfo.m_bSpecial |
| +0x1c | 12 | Vec3 | m_AxisA | (cos(angle), sin(angle), 0) x 0.5 for billboard rotation |
| +0x28 | 12 | Vec3 | m_AxisB | (cos(angle+180), sin(angle+180), 0) x 0.5 |
| +0x34 | 1 | byte | m_bFlipV | RandUint(2) != 0 |
| +0x44 | 12 | Vec3 | m_Scale | (sc, -sc, sc), sc = Rand(10) + 10 |
| +0x68 | 4 | float | m_Life | Lifetime; Rand(2.5) + 3.75 |
| +0x6c | 4 | float | m_DecayRate | Fade rate; Rand(0.25) + 0.375 |
| +0x70 | 4 | int | m_FruitType | For `FruitTypeColour` lookup |
| +0x74 | 1 | int8 | m_SplatType | -1 airborne, 0-5 landed variant |
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
- `m_bSpecial`: U offset +0.5 for types 0-3
- `m_bFlipV`: Swap V edges

## DrawActiveSplats (0x00180344, 45 lines)

Renders all active splats as a single batched triangle list:

1. Iterate splat pool: count active splats (m_bAlive != 0 AND m_SplatType >= 0)
2. For each: build vertex data from pos +/- axisA*scale.x +/- axisB*scale.y
3. Apply colour lerp: `fade = clamp(2 * m_ColourPhase, 0, 1)` between fruit colour and base RGBA
4. Single `Mesh::DrawTriList` call with splat atlas texture

Each splat = 2 triangles = 6 QUADCUSTOMVERTEX vertices.

---

## Static API Completeness

<!-- Analysed: 2026-04-25T12:00 -->

Full symbol scan (`search_functions "Splat"`) against Ghidra FruitNinja.exe.
Only the canonical implementations (highest address range, Splat.cpp translation unit)
are listed; thunks at lower addresses (0x000f…/0x0010…) are linker-emitted call stubs
that delegate to these via GOT pointer.

| Binary symbol | Binary addr | Port name | Status |
|---|---|---|---|
| SplatEntity::CreatePool(int) | 0x0017ef34 | `CreatePool` | ported |
| SplatEntity::GetFree() | 0x0017ee4c | `GetFree` | ported |
| SplatEntity::NumActiveSplats() | 0x0017ee34 | *(none)* | **MISSING** |
| SplatEntity::RemoveAllSplats() | 0x0017eea4 | `RemoveAll` | **MIS-NAMED** |
| SplatEntity::CleanUp() | 0x0017eee0 | `ReleaseContent` | **MIS-NAMED** |
| SplatEntity::LoadContent() | 0x001802f4 | `LoadContent` | ported (name ok) |
| SplatEntity::UpdateActiveSplats(float) | 0x0017fd68 | `UpdateActive` | **MIS-NAMED** |
| SplatEntity::DrawActiveSplats() | 0x00180344 | `DrawActive` | **MIS-NAMED** |
| SplatEntity::MakeSplat(...) | 0x0017f2f0 | `MakeSplat` | ported |
| SplatEntity::Update(float) | 0x0017f774 | `UpdateSplat` | ported (instance method, name ok) |
| SplatEntity::DrawSplat() | 0x0017f008 | *(none)* | **MISSING** (virtual, called via vtable) |
| SplatEntity::DrawUpdate(float) | 0x0017ee2c | *(none)* | **MISSING** (virtual no-op override) |
| SplatEntity() ctor | 0x0017ed58/0x0017ed8c | `SplatEntity()` | ported |
| ~SplatEntity() dtor | 0x0017edd4/0x0017ee00 | `~SplatEntity()` | ported |
| ZeroInit_SplatEntity | 0x0017e41c | *(file-scope helper)* | internal |
| RandUint_Splat | 0x0017f270 | *(file-scope helper)* | internal |
| RandFloat_Splat | 0x0017f2bc | *(file-scope helper)* | internal |
| SmartPtrNull_Tex_Splat | 0x0017f584 | *(file-scope helper)* | internal |
| MakeSFXDelegate_Splat | 0x0017f5c0 | *(file-scope helper)* | internal |
| CleanUpSplat() | 0x0017f590 | *(file-scope cleanup)* | internal |
| PlaySplat(int) | 0x0017f5ec | *(file-scope sound)* | internal |
| CleanupSplat() / SplatEffect | 0x001804f4 | *(SplatEffect helper)* | SplatEffect, not SplatEntity |

### Naming discrepancies

The port uses shortened names for the four static dispatch functions. The binary's
mangled symbols confirm the full names:

| Port name | Binary name | Fix needed |
|---|---|---|
| `RemoveAll` | `RemoveAllSplats` | rename static method |
| `ReleaseContent` | `CleanUp` | rename static method |
| `UpdateActive` | `UpdateActiveSplats` | rename static method |
| `DrawActive` | `DrawActiveSplats` | rename static method |

### NumActiveSplats (0x0017ee34) -- MISSING

```c
// Static method (Ghidra incorrectly marks __thiscall; the `this` arg is
// ignored -- the implementation only reads GOT-relative pool globals).
// Returns the pool's current active-splat count as a signed int.
// Called from ShopScreen::Update (0x0015e212) and 3x from UpdateActiveSplats.
static int NumActiveSplats();
```

Implementation reads the pool capacity field via GOT-relative addressing
(`DAT_0017ee44 + 0x17ee3c + DAT_0017ee48`) -- equivalent to
`s_pool->m_Count` (or `m_ActiveCount`) in the MemoryPool struct.
ShopScreen::Update uses it as:
```c
if (SplatEntity::NumActiveSplats() == 0) {
    this->field_0x80 = 0x40;  // mark state transition
}
```
The port's `UpdateActive` inlines an active-check loop but does not expose
the count as a callable function. Because `ShopScreen::Update` calls this
as a real function, it must be a standalone `static int NumActiveSplats()`.

Callers:
- `ShopScreen::Update @ 0x0015e212`
- `UpdateActiveSplats @ 0x0017fe54, 0x0017fe84, 0x0017feb8` (3 call sites)

### DrawSplat (0x0017f008) -- MISSING (virtual method)

```c
// Per-instance render: writes 6 QUADCUSTOMVERTEX entries into the shared
// vertex buffer at the offset determined by pool position.
// Called indirectly via the SplatEntity vtable -- DrawActiveSplats
// iterates the pool and calls DrawSplat() on each alive splat with
// m_SplatType >= 0.
void DrawSplat();
```

This is a virtual method (data xref at vtable entry `0x001ea600`).
It computes UV coordinates from `m_FruitType` and the atlas table,
handles `m_bSpecial` (+0.5 U offset) and `m_bFlipV` (swap V edges),
then writes the 6-vertex triangle pair directly into the pre-allocated
shared vertex buffer.  The port's `DrawActive` loop currently must be
calling an equivalent inline path; the binary keeps it as a virtual method.

The port needs this as `void DrawSplat()` (instance method, `virtual` in the
class, overriding `Entity::DrawSplat` or similar base slot).

### DrawUpdate (0x0017ee2c) -- MISSING (virtual no-op)

```c
// Virtual override of Entity::DrawUpdate(float). Body is a single `bx lr`
// (return immediately). Exists to fill the vtable slot so the base-class
// dispatch does not call a stale pointer.
virtual void DrawUpdate(float dt);
```

Must be declared `virtual` in the class definition with an empty body.
Callers: vtable dispatch only (no direct call sites found).

---

## See Also

- [Entity base](entity-base.md) -- Mortar::Entity base class
- [Fruit](fruit.md) -- Fruit::Slice spawns splats
- [SlashEntity](slash-entity.md) -- Blade trail rendering
