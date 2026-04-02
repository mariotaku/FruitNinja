# Visual Effects (Splats, BombFlash, BombBlast)

## SplatEntity System

### DrawActiveSplats (0x180344, 45 lines)

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

Splats use `QUADCUSTOMVERTEX` — custom vertex format with position + colour + UV.
Each splat = 2 triangles = 6 vertices.

### SplatEntity Pool

- Pool-based: `GetFree()` / `CreatePool(count)`
- Each splat: 0x78 bytes (stride = 0x1e × 4)
- Key fields: position(+0x38), velocity(+0x5c), type(+0x70), active(+0x75)
- 6 splat variants (types 0-5) chosen randomly based on position/velocity

## BombFlash System

### BombFlash::Update (0x171038, 61 lines)

Visual flash overlay when a bomb is hit. Animates size and alpha:

```c
float t = (timer + dt) / DURATION;
timer += dt;

// Quadratic scale animation
scale.x = BASE_X + t * ACCEL_X * t;
scale.y = BASE_Y + t * ACCEL_Y * t;

// Alpha fade: ramp up → hold → ramp down
if (timer < FADE_IN_TIME) {
    alpha = maxAlpha * (timer / FADE_IN_TIME);
} else if (timer > HOLD_END_TIME) {
    float fadeProgress = (DURATION - timer) / FADE_OUT_DURATION;
    alpha = maxAlpha * fadeProgress * fadeProgress;
}

if (timer > DURATION) {
    active = false;
    timer = 0;
}
```

### BombFlash Struct (estimated ~0x44 bytes)

| Offset | Type | Name |
|--------|------|------|
| +0x04 | float | m_Timer |
| +0x0b | byte | m_MaxAlpha |
| +0x0f | byte | m_CurrentAlpha |
| +0x34 | float | m_Scale_x |
| +0x38 | float | m_Scale_y |
| +0x3c | float | m_Scale_z |
| +0x40 | byte | m_bActive |

### BombFlash::MakeFlash (0x1723f4, 11 params)

Creates a flash with position, colour, alpha, and animation parameters.

## BombBlast System

### BombBlast::Update (0x171170, 32 lines)

Explosion effect spawned when a bomb's fuse timer expires (entity type 4):

```c
// Expand blast radius
this->field_0x28 += Game.dt * EXPAND_RATE;
this->field_0x6c += Game.dt;  // lifetime

// Update two velocity components
vel1 = field_0x54 * dt;  → field_0x3c/0x40/0x44
vel2 = field_0x60 * dt;  → field_0x48/0x4c/0x50

// Scale growth
field_0x2c += SCALE_RATE * Game.dt;

// Kill after 3 seconds
if (field_0x6c > 3.0) {
    flags |= 0x10;  // mark for removal
}
```

### BombBlast Struct (estimated ~0x70 bytes)

| Offset | Type | Name |
|--------|------|------|
| +0x0c | byte | flags (bit 4 = dead) |
| +0x28 | float | m_BlastRadius |
| +0x2c | float | m_Scale |
| +0x3c | Vec3 | m_PosA (from vel1) |
| +0x48 | Vec3 | m_PosB (from vel2) |
| +0x54 | Vec3 | m_Vel1 |
| +0x60 | Vec3 | m_Vel2 |
| +0x6c | float | m_Lifetime (kills at 3.0) |

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| SplatEntity::DrawActiveSplats | 0x00180344 | 45 | Batch render all splats |
| SplatEntity::Update | 0x0017f774 | 267 | Physics + type selection |
| BombFlash::Update | 0x00171038 | 61 | Flash scale + alpha animation |
| BombFlash::MakeFlash | 0x001723f4 | — | Create new flash (11 params) |
| BombFlash::DrawActiveFlashes | 0x0017102c | — | Render flashes |
| BombBlast::Update | 0x00171170 | 32 | Expansion + lifetime |
| BombBlast::DrawActiveBlasts | 0x00171aa0 | — | Render blasts |
| BombBlast::DrawBlast | 0x00171354 | — | Single blast render |

---

## See Also

- [Screens & effects functions](../functions/screens-effects.md) -- effect pseudocode
- [Entity structs](../structs/entities.md) -- BombFlash, BombBlast structs
