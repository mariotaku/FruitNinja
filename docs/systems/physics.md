# Fruit Physics

## Fruit Physics (Fruit::Update — 0x177680, 412 lines)

### Unsliced Fruit (m_bSliced == 0)

**Phase 1: Launch delay** (`field_0x80 > 0`):
- Counts down by `Game.dt` each frame
- Plays warning SFX when crossing threshold
- At 0: sets up trail particles (`SetTrailParticles`), may spawn additional fruits via `WaveManager::SpawnFruit`

**Phase 2: Ballistic flight** (`field_0x7c != 0`):
```
scaledDt = dt * field_0x94   // time scale (from SPAWNER_INFO)
gravity  = field_0x9c        // Vec3, default (0, -12, 0)

pos += vel * scaledDt + 0.5 * gravity * scaledDt²
vel += gravity * scaledDt
pos += rotAxis * scaledDt    // field_0x84 visual offset
```

**Phase 3: Position backup**:
```
field_0xb8 = pos   // m_SecondPos (used for half-B after slicing)
field_0xc4 = vel   // velocity backup
```

**Phase 4: Slice timer** (`field_0x6c > 0`):
- Counts down; at 0 calls `Slice()` to split the fruit
- Set by `CollisionResponse`: base value × 2.5 (normal) or × 0.5 (critical)

**Phase 5**: `UpdateBombAvoidance()` — repels fruit from nearby bombs

### Sliced Fruit (m_bSliced != 0)

Two fruit halves with independent physics:

| Property | Half A | Half B |
|----------|--------|--------|
| Position | Entity.pos (+0x10) | field_0xb8 |
| Velocity | Entity.vel (+0x1c) | field_0xc4 |
| Rotation | field_0xd0 (Quaternion) | field_0xe0 (Quaternion) |
| RotVel | field_0xf0 (Vec3) | field_0xfc (Vec3) |
| Emitter | field_0x40 | field_0x44 |

**Gravity ramp-up**: After slicing, gravity magnitude increases each frame:
- Normal: `+= DAT * dt * 4.5`
- Special (field_0x10c): `+= DAT * dt * 6.5`

**Per-half physics**:
```
vel += gravity * dt
pos += vel * dt
```

**Rotation update** (per half, using 3-axis decomposition):
```
qX = CreateFromAxisAngle(axisX, rotVel.x * dt * scale)
qY = CreateFromAxisAngle(axisY, rotVel.y * dt * scale)
qZ = CreateFromAxisAngle(axisZ, rotVel.z * dt * scale)
rotation = rotation * qX * qY * qZ
Normalise(rotation)
```

**Offscreen check**: `CheckHasGoneOffscreen()` → `KillFruit(true)`

**Collision shape**: Updated from entity position each frame (`m_Col.center = pos`)

### Spawn Pipeline (WaveManager::SpawnFruit — 0x1225a0, 248 lines)

```
For each fruit to spawn (count = param_1):
  1. Angle from SPAWNER_INFO (+0x2c=min, +0x30=max), random within range
     angle_u16 = (int)((angleIdx / scale) * halfRange + noise) * 0xb6

  2. Speed = random(0..1.5) + 9.5  (range 9.5..11.0)
     velX = sin(angle) * speed * spawnerInfo[+0x24]
     velY = cos(angle) * speed * spawnerInfo[+0x28]

  3. Spawn type from spawnerInfo[+0x34]:
     0 (default): Bottom spawn, standard velocity
     1: Bottom, half horizontal speed
     2: Left side spawn (x mirrored)
     3: Right side spawn
     4: Random left or right

  4. Create entity: ActorManager::Add(type=0, active=true)
     Set entity.pos and entity.vel
     Call entity.Init(0, fruitType, &scaleVec)
     Set field_0x94 = spawnerInfo[+0x14] (time scale)
     Set gravity = spawnerInfo[+0x18] * scale (overrides default -12)

  5. Fruit::Chuck(delay) — sets launch delay timer
```

### SPAWNER_INFO Fields (used by SpawnFruit)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x14 | float | timeScale | Stored in Fruit.field_0x94 |
| +0x18 | float | gravityScale | Multiplied into gravity vector |
| +0x1c | float | sideSpeed | For side-spawn velocity |
| +0x24 | float | speedMultX | Horizontal speed multiplier |
| +0x28 | float | speedMultY | Vertical speed multiplier |
| +0x2c | float | minAngle | Min spawn angle (-1.0 default) |
| +0x30 | float | maxAngle | Max spawn angle (1.0 default) |
| +0x34 | byte | spawnType | 0=bottom, 1=bottom-slow, 2=left, 3=right, 4=random-side |
| +0x5c | float | zOffset | Z position offset |

---

## See Also

- [Fruit functions](../functions/fruit.md) -- Fruit::Update physics step
- [Wave functions](../functions/wave.md) -- SpawnFruit initial velocity
- [Entity structs](../structs/entities.md) -- MortarEntity base fields
