# BombBlast Entity

Entity type 4. Visual shockwave ring spawned when a game bomb explodes. Vtable base: 0x001EA420.

## BombBlast Struct (0x70 / 112 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x0c | byte | flags | bit 4 = dead |
| +0x28 | float | m_BlastRadius | Expanding radius |
| +0x2c | float | m_Scale | Growing scale |
| +0x36 | ushort | m_Angle | Random 16-bit angle from Init |
| +0x3c | Vec3 | m_PosA | Position along vel1 direction |
| +0x48 | Vec3 | m_PosB | Position along vel2 direction |
| +0x54 | Vec3 | m_Vel1 | Expansion velocity (from angle) |
| +0x60 | Vec3 | m_Vel2 | Expansion velocity (perpendicular) |
| +0x6c | float | m_Lifetime | Kills at 3.0s |

## BombBlast Vtable

| Index | Address | Method | Notes |
|-------|---------|--------|-------|
| [0] | 0x001715a4 | ~BombBlast (non-deleting) | Resets vtable, calls Entity::~Entity |
| [1] | 0x001715ec | ~BombBlast (deleting) | Same + operator_delete |
| [2] | 0x001718ac | BombBlast::Init | Sets up ring velocities and scale |
| [3] | 0x0019d5e8 | Entity::Release | (base class) |
| [4] | 0x00171170 | BombBlast::Update | Expands ring, checks lifetime |
| [5] | 0x00171034 | BombBlast::Draw | Empty (rendered elsewhere) |
| [6] | 0x00171030 | BombBlast::DrawUpdate | Empty |
| [7] | 0x0019d600 | Entity::OnAdd | (base class) |
| [8] | 0x0019d800 | Entity::OnRemove | (base class) |

---

## Instance Methods

### BombBlast::BombBlast (0x00171618, 0x00171648) -- constructors

```c
BombBlast::BombBlast() {
    Entity::Entity();
    vtable = &BombBlast_vtable;
    flags &= 0xEE;   // clear collision (bit 0) and kill (bit 4) flags
}
```

Pseudocode summary:
- Constructs Entity base, sets BombBlast vtable
- Clears collision and kill flags (0xEE = ~0x11)

### ~BombBlast (0x001715a4) -- non-deleting destructor

```c
~BombBlast() {
    vtable = &BombBlast_vtable;   // reset vtable
    Entity::~Entity();
}
```

### ~BombBlast (0x001715ec) -- deleting destructor

Same + calls `operator_delete(this)`.

### BombBlast::Init (0x001718ac)

`void BombBlast::Init(void *p1, long p2, Vec3 *p3)`

Note: `p1` is actually `this` (passed as first arg after thiscall `this`).

```c
void BombBlast::Init(void *self, long p2, Vec3 *p3) {
    self->field_0x6c = 0.0f;    // Z position = 0

    // Random angle (0..360 degrees in 16-bit angle units)
    uint randVal = Random::Rand32(524287);    // DAT_001719d4 = 0x7FFFF
    ushort angle = (randVal / 262143.0f) * 360.0f * 182.0f;  // normalize to 16-bit
    self->m_Angle = angle;    // +0x36

    // Velocity direction 1: from angle
    Vec3 vel1 = Vec3(Cos(angle), Sin(angle), 0.0f) * 0.5f;
    self->m_Vel1 = vel1;       // at +0x54

    // Velocity direction 2: perpendicular (angle + 90 degrees = +0x3FFC)
    Vec3 vel2 = Vec3(Cos(angle + 0x3FFC), Sin(angle + 0x3FFC), 0.0f);
    self->m_Vel2 = vel2;       // at +0x60

    // Initial positions = velocity directions (start at center)
    self->m_PosA = vel1;       // at +0x3c (copies from +0x54)
    self->m_PosB = vel2;       // at +0x48 (copies from +0x60)

    // Scale
    self->m_Scale = Vec3(5.0f, DAT_scale, 1.0f);  // at +0x28
    self->flags &= 0xEE;      // clear collision + kill
}
```

Pseudocode summary:
- Random angle determines two perpendicular expansion directions
- Vel1 at half-speed along angle, Vel2 at full-speed perpendicular
- Initial positions start at the velocities (offset from center)
- Used by Bomb::Update to spawn expanding shockwave rings

### BombBlast::Update (0x00171170)

`void __thiscall BombBlast::Update(BombBlast *this, float dt)`

Explosion effect spawned when a bomb's fuse timer expires:

```c
void BombBlast::Update(float dt) {
    float gameDt = Game.dt;  // at Game+0x38

    m_BlastRadius += gameDt * 100.0f;    // DAT_0017120c = 100.0 (radius growth)
    m_Lifetime += gameDt;

    // Expand position A along vel1 direction
    m_PosA = m_Vel1 * DAT_scale;   // scaled outward

    // Grow scale
    m_Scale += 2500.0f * gameDt;         // DAT_00171210 = 2500.0

    // Expand position B along vel2 direction
    m_PosB = m_Vel2 * DAT_scale;   // scaled outward

    // Kill after 3 seconds
    if (m_Lifetime > 3.0f) {
        flags |= 0x10;   // mark for removal
    }
}
```

Pseudocode summary:
- Expands blast radius at 100 units/sec, scale at 2500 units/sec
- Two position vectors expand along perpendicular axes
- Self-destructs after 3 seconds lifetime

### BombBlast::Draw (0x00171034) -- Empty

```c
void BombBlast::Draw() { return; }
```

### BombBlast::DrawUpdate (0x00171030) -- Empty

```c
void BombBlast::DrawUpdate(float dt) { return; }
```

Note: BombBlast rendering is likely handled by DrawBombHit or another system that reads BombBlast entity positions.

---

## External Rendering

### DrawActiveBlasts (0x00171aa0)

Renders all active BombBlast entities. Called by the rendering pipeline (not the entity vtable).

### DrawBlast (0x00171354)

Renders a single BombBlast shockwave ring.

---

## Lifecycle

BombBlast entities are spawned by [Bomb::Update](bomb.md) when `activeFlag != 0` and `m_bBombFlag88 == 0` (non-menu hit). The spawn timer interval is 0.05s (DAT_00172c9c). They are bulk-removed by `RemoveFlashEntities` when `Game.bombTimer` crosses the 1.55s threshold.

## Key Constants

| DAT Address | Value | Purpose |
|-------------|-------|---------|
| 0x001719d4 | 0x7FFFF | Random angle range |
| 0x0017120c | 100.0f | Blast radius growth rate |
| 0x00171210 | 2500.0f | Scale growth rate |
| 3.0f | 3.0f | Lifetime before kill |

---

## See Also

- [Bomb entity](bomb.md) -- spawns BombBlast on hit
- [BombFlash system](bomb-flash.md) -- flash overlay effects
- [Entity base struct](entity-base.md) -- Mortar::Entity base class
- [SplatEntity](splat-entity.md) -- juice splat effect
