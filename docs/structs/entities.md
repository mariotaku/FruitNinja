# Entity Structs

## Mortar::Entity (base class, size = 0x3c)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | |
| +0x04 | int | field_0x04 | |
| +0x08 | ushort | m_TrackerID | Entity network tracker ID; used in FruitSlicedPacket |
| +0x0a | | (padding) | 2 bytes |
| +0x0c | byte | flags | bit 1 = has collision shape, bit 4 = skip entity |
| +0x10 | float | field13_0x10 | pos.x or spawn speed X |
| +0x14 | float | field14_0x14 | pos.y or spawn speed Y |
| +0x18 | float | field15_0x18 | pos.z |
| +0x1c | float | vel.x | |
| +0x20 | float | vel.y | |
| +0x24 | float | vel.z | |
| +0x36 | ushort | angle | Blade/rot angle; used with CosIdx/SinIdx |
| +0x38 | Col* | m_Col | Collision shape pointer |

---

## Fruit : Mortar::Entity

Entity base ends at +0x3c. Fruit own fields follow.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x3c | byte | m_FruitType | 0..N; × 0x330 for FRUIT_INFO offset |
| +0x3d | char | m_bFruitFlag3d | = 0; ≠0 = skip power-up spawn on slice |
| +0x40 | PSPParticleEmitter* | m_pEmitter1 | Particle trail A; zeroed in Init |
| +0x44 | PSPParticleEmitter* | m_pEmitter2 | Particle trail B; zeroed in Init |
| +0x48 | Vec3 | m_SlicePos | Copied from entity pos at slice time |
| +0x60 | int | m_field60 | = 0 in Init |
| +0x64 | int | m_CollisionSize | = 75 (0x4b) in Init; collision circle radius or half-size |
| +0x68 | int | m_field68 | = 4 in Init |
| +0x6c | float | m_SliceSpeedMult | Init=-1.0f; normal slice=×2.5, critical=×0.5 |
| +0x70 | ushort | m_SliceAngle | Atan2Idx(blade.x, blade.y) |
| +0x74 | float | m_SliceImpulse | Clamp 4–8; or 6–8 for special fruit |
| +0x78 | int | m_SliceState | = 0 in Init; set on slice |
| +0x7c | byte | m_bActive | = 1 in Init |
| +0x80 | float | m_field80 | = DAT in Init |
| +0x84 | Vec3 | m_RotAxis | From global config Vec3 |
| +0x90 | int | m_PlayerIdx | = 0 in Init; set for multiplayer routing |
| +0x94 | float | m_field94 | = 1.0f in Init |
| +0x98 | float | m_ZPosition | From GetFruitZPosition() |
| +0x9c | Vec3 | m_HalfA_StartPos | Init = (DAT, -12.0f, DAT); off-screen spawn |
| +0xb4 | char | m_bSliced | = 0 in Init; guard in CollisionResponse |
| +0xb8 | Vec3 | m_SecondPos | Second particle emitter position |
| +0xd0 | Quaternion | m_Rot1 | 16 bytes; both halves init from RandomStartAngle |
| +0xe0 | Quaternion | m_Rot2 | 16 bytes |
| +0xf0 | Vec3 | m_RotVel1 | Random [-5.5, 5.5] per component |
| +0xfc | Vec3 | m_RotVel2 | Random [-5.5, 5.5] per component |
| +0x108 | int | field169_0x108 | = 0 in Init |
| +0x10c | int | m_field10c | = 0 in Init |
| +0x10d | byte | m_bCriticalEligible | = 1 in Init; = 0 after critical slice |
| +0x110 | int | m_field110 | = DAT in Init |
| +0x114 | int | m_field114 | = 0 in Init |

### Key Methods

| Address | Name | Notes |
|---------|------|-------|
| 0x176708 | Init | |
| 0x176770 | Update | Physics integration + Col update |
| 0x176d58 | Slice | Sets up trajectory for sliced halves |
| 0x175a64 | Chuck | Throw/launch the fruit |
| 0x176abc | KillFruit | |
| 0x1780b0 | CollisionResponse | 591 lines; does actual slice logic |

### CollisionResponse Pipeline

1. Check `m_bSliced` guard
2. Critical hit probability check
3. Play SFX (normal or critical)
4. Capture slice angle/impulse from blade velocity
5. Spawn juice particles (PSPParticleManager)
6. AddSlice() for visual effect
7. UnlockSpecificOrderAchievement
8. Optional: PowerUpManager::RandomPower() if hasPowerUp

---

## Bomb : Mortar::Entity (size = 0xac / 172 bytes)

Ghidra struct: `/FruitNinja/Bomb` or `/Demangler/Bomb`

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | Bomb vtable at 0x1EA478 |
| +0x04 | int | field_0x04 | (Entity base) |
| +0x08 | ushort | m_TrackerID | (Entity base) |
| +0x0c | byte | flags | bit 1 = collision, bit 4 = killed |
| +0x10 | float | pos_x | Position X |
| +0x14 | float | pos_y | Position Y |
| +0x18 | float | pos_z | Position Z |
| +0x1c | float | vel_x | Velocity X |
| +0x20 | float | vel_y | Velocity Y |
| +0x24 | float | vel_z | Velocity Z |
| +0x28 | Vec3 | m_Scale | Render scale; set in Init, modified by MakeFat |
| +0x38 | Col* | m_Col | ColSphere pointer (size 0x18) |
| +0x3c | float | m_SpawnTimer | Counts down; spawns BombBlast (type 4) at 0 |
| +0x40 | Delegate0\<void\> | hitCallback | Callback for menu bomb delegate |
| +0x64 | int | field38_0x64 | Bomb variant: 0 = normal, 2 = multiplayer |
| +0x68 | byte | activeFlag | 0 = in-flight, 1 = hit/slashed |
| +0x6c | float | m_ZPosition | From GetBombZPosition(); layer ordering |
| +0x70 | short | m_RotVelX | Rotation velocity X; Random 1..8 |
| +0x72 | short | m_RotVelY | Rotation velocity Y; Random 1..8 |
| +0x74 | short | m_RotX | Current rotation X (accumulated) |
| +0x76 | short | m_RotY | Current rotation Y (accumulated) |
| +0x78 | byte | field_0x78 | Collision guard (prevents double-hit) |
| +0x7c | PSPParticleEmitter* | m_pEmitter | Fuse particle trail; NULL until first draw |
| +0x80 | byte | movementFlag | 1 = physics enabled |
| +0x84 | int | field_0x84 | Backref to Game/TaskState object |
| +0x88 | byte | m_bBombFlag88 | 0 = normal hit, 1 = menu/zen hit |
| +0x8c | float | accelForce_x | Gravity/acceleration X; Init = 0.0 |
| +0x90 | float | accelForce_y | Gravity/acceleration Y; Init = -12.0 |
| +0x94 | float | accelForce_z | Gravity/acceleration Z; Init = 0.0 |
| +0x98 | Vec3 | m_OrigScale | Backup of m_Scale from Init |
| +0xa4 | float | countdown | Chuck delay timer; 0 = ready to spawn |
| +0xa8 | float | speedMult | Speed multiplier; 1.0 normal, higher for fat bombs |

### Key Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x172504 | Init | `void Init(void*, long, Vec3*)` | Vtable[2]; setup after spawn |
| 0x171764 | Release | `void Release()` | Vtable[3]; cleanup emitters |
| 0x1729fc | Update | `void Update(float dt)` | Vtable[4]; 195 lines |
| 0x171be8 | Draw | `void Draw()` | Vtable[5]; mesh + rotation |
| 0x1714e4 | DrawUpdate | `void DrawUpdate(float dt)` | Vtable[6]; fuse particle update |
| 0x17280c | CollisionResponse | `int CollisionResponse(Entity*, ulong, ulong, Vec3*)` | Vtable[9]; blade hit |
| 0x170f68 | Chuck | `void Chuck(float delay)` | Set countdown timer |
| 0x1716e8 | KillBomb | `void KillBomb()` | flags |= 0x10, clear emitter |
| 0x171d78 | MakeFat | `void MakeFat(bool silent)` | Scale up, speed up |

### Bomb::Update Flow

**Normal bomb** (`activeFlag == 0`):
1. Countdown timer decrements by `Game.dt`
2. At threshold: play fuse SFX
3. At zero: chain-spawn via `WaveManager::SpawnBomb(spawnLevel - 1)`
4. Physics: `vel += accelForce * dt`, `pos += vel * dt`
5. Accel grows when vel and accel align (bomb accelerates in flight direction)
6. Rotation: `rotX += rotVelX`, `rotY += rotVelY` (16-bit accumulators)
7. Kill if out of screen bounds

**Hit bomb** (`activeFlag != 0`):
- `m_bBombFlag88 == 0`: spawn BombBlast entity (type 4) after timer
- `m_bBombFlag88 != 0`: physics continues with different accel rate, collision offscreen

**Bomb hit behavior:**
- Classic/Arcade: `HitBomb()` -> camera shake, Game+0x10 = countdown timer, game-over delay
- Zen mode: `AddToCurrentScore(-10, 0, false, false)`, `WaveManager::ResetSpeed`, `PowerUpManager::ClearTimedPowers`
- Menu: delegate callback, `ClearMenuItems()`

---

## SlashEntity : Mortar::Entity (blade/swipe, size = 0x184 / 388 bytes)

Entity base ends at +0x3c. SlashEntity own fields follow.
Full analysis in [slash-entity.md](../functions/slash-entity.md).

| Offset | Type | Name | Init | Notes |
|--------|------|------|------|-------|
| +0x3c | PSPParticleEmitter* | m_TrailEmitter | NULL | Touch trail particle; null when inactive |
| +0x40 | float | m_Scale | 0.0 | Blade glow scale; lerps up in critical, down when idle |
| +0x44 | Colour | m_BaseColour | ctor | RGBA, 4 bytes; blended from highlight each frame |
| +0x48 | Colour | m_HighlightColour | ctor | RGBA, 4 bytes; target colour from mod |
| +0x4c | byte | m_bFlag4c | 0 | Set to 1 on bomb hit (activeFlag && !bombFlag88) |
| +0x50 | int | m_SplitPoint | 0xa0 | Split index for same-screen multiplayer vertex buffers |
| +0x58 | int | m_PointCount | 0 | Blade trail vertex pair count; 4 = min for collision |
| +0x5c | QUADCUSTOMVERTEX* | m_pLeftBuffer | alloc'd | Left/top vertex strip; (splitPoint+2)*0x24 bytes |
| +0x60 | QUADCUSTOMVERTEX* | m_pRightBuffer | alloc'd | Right/bottom vertex strip; (splitPoint+2)*0x24 bytes |
| +0x64 | Vec3 | m_BladeDir | (0,0,0) | Blade velocity direction |
| +0x70 | Vec3 | m_TailPos | -65535 | Oldest visible trail point |
| +0x7c | Vec3 | m_HeadPos | -65535 | Newest trail point / current tip |
| +0x88 | Vec3 | m_PrevHeadPos | -65535 | Previous head position (saved before shift) |
| +0x94 | float | m_LineLengthSq | -1.0 | |head-tail|^2; -1.0 = no active segment |
| +0x98 | float | m_SpeedScale | 0.0 | Set to 1.0 in AddPoint; reset to 0.0 when inactive |
| +0x9c | int | m_SliceCount | -1 | +2 per fruit slice; -1 per splat spawn |
| +0xa0 | float | m_SliceTimerA | 0.0 | Counts down for splat spawn timing |
| +0xa4 | float | m_SliceTimerB | 0.0 | Accumulated random delay between splats |
| +0xa8 | Vec3 | m_BladeVelAtSlice | | Blade dir snapshot at moment of slice |
| +0xb4 | Vec3 | m_SlicePos | | World position of sliced entity |
| +0xc0 | int | m_SliceEntityType | | Fruit type of last sliced fruit (+ critical bonus) |
| +0xc4 | float | m_SwipeSoundTimer | 0.0 | PlaySwipe cooldown; set to 6.0 on swipe |
| +0xc8 | Vec3[6] | m_GhostPositions | (zeroed) | 72 bytes; circular buffer of recent blade directions |
| +0x110 | int | m_GhostIndex | 0 | Current index into m_GhostPositions (mod 6) |
| +0x114 | int | m_GhostCount | 0 | Number of stored ghost positions (max 6) |
| +0x118 | Vec3 | m_GhostDir | (global) | Averaged direction from ghost buffer |
| +0x124 | float | m_ComboTimer | 0.1 | Time window for combo; reset to 0.0 on slice; expires at 0.1 |
| +0x128 | int | m_ComboCount | 0 | Fruits sliced in current combo |
| +0x12c | int | m_ComboEntityType | 0 | 0=fruit, 1=player2, 2=special |
| +0x130 | MissControl* | m_pComboCtrl | NULL | Combo HUD control; null if no active combo |
| +0x134 | float | m_GhostTimer | 0.0 | Counts up while m_bGhostActive; threshold=0.05 |
| +0x138 | byte | m_bGhostActive | 0 | If true, timer runs -> CreateGhost() |
| +0x13c | int | m_ColEntityA | -1 | First collision vertex index |
| +0x140 | int | m_ColEntityB | -1 | Last collision vertex index |
| +0x144 | byte | m_bBladeActive | 0 | 2-bit state: 01=active, 10=deactivating, 00=off |
| +0x148 | float | m_ComboScoreBase | 6.0 | Decremented (combo_n * (rand+0.75)) per slice |
| +0x14c | int | m_ExtraFieldA | -1 | |
| +0x150 | int | m_ExtraFieldB | -1 | |
| +0x154 | int[11] | m_ComboFruitIDs | all -1 | 0x2c bytes; fruit type IDs in current combo |
| +0x180 | ushort | m_AngleCopy | 0 | Updated each frame in AddPoint; copied to Entity::angle |

### Collision System

**ColLine** (0x20 bytes): `{vtable, center.x, center.y, center.z, tail.x, tail.y, tail.z, pad}`
- center = midpoint of blade segment (head+tail)/2
- tail = oldest point of active blade segment

**ColCircle** (fruit): `m_Col[0] = {vtable, cx, cy, cz}`, `m_Col[1] = {radius, ?, ?, ?}`. `vtable->GetType()` returns 1 for circle.

**Collision test** (0x0017b570): Blade line segment vs fruit circle:
1. ColLine::Collide broad-phase check
2. If pass: check midpoint-to-center distance vs radius
3. If outside: project onto perpendicular, check intersection points against line length
4. Returns 1 if any intersection within blade segment

### Key Methods

| Address | Name | Notes |
|---------|------|-------|
| 0x0017c82c | SlashEntity() | Constructor |
| 0x0017c774 | ~SlashEntity() | Destructor |
| 0x0017c65c | Init | Allocates ColLine, calls InitPoints(0xa0) |
| 0x0017c60c | Release | Frees buffers, clears trail emitter |
| 0x0017c340 | InitPoints | Allocs 2 vertex buffers, (split+2)*0x24 bytes each |
| 0x0017b92c | UpdatePoints | Rebuilds vertex strips, fading, collision segment (474 lines) |
| 0x0017d2e4 | UpdateTouchDown | Maps touch to blade position, inserts trail points |
| 0x0017ce0c | AddPoint | Appends vertex pair to trail buffers |
| 0x0017c584 | PreUpdate | Updates ghosts, mod colour, swipe volume |
| 0x0017d664 | Update | Main tick: collision, combos, splats (623 lines) |
| 0x0017b570 | CollideWithEntity | Line-vs-circle intersection test |
| 0x0017b82c | CreateGhost | Spawns SlashEntityGhost for visual echo |
| 0x0017e424 | DrawSlice | Renders blade as two triangle strips |
| 0x0017e504 | PreDraw | Draws 8 ghost trails |
| 0x0017b3b8 | Draw | No-op (blade rendered via DrawSlice) |
| 0x0017b398 | DrawUpdate | Sets frame flags |
| 0x0017b3bc | CollisionResponse | Returns 0 (slash doesn't receive collision) |
| 0x0017b87c | GetHeadThicknessScale | Thickness based on head-vs-tail distance |
| 0x0017ccdc | PlaySwipe | Plays random swipe SFX (1 of 6) |
| 0x0017b0f4 | UpdateModColour | Animates blade modifier colours through palette |
| 0x0017d61c | TouchDown | Resets blade, calls UpdateTouchDown |
| 0x0017c50c | TouchMoveX | Maps input X to pos.x |
| 0x0017c490 | TouchMoveY | Maps input Y to -pos.y |
| 0x0017cbec | CleanupSlash | Free fn; nulls textures, releases ghosts |


---

## See Also

- [Fruit functions](../functions/fruit.md) -- Fruit::Update, LoadInfo, CollisionResponse
- [Bomb functions](../functions/bomb.md) -- Bomb::Update, Explode
- [SlashEntity functions](../functions/slash-entity.md) -- slash collision, combo logic
- [Physics system](../systems/physics.md) -- gravity, spawn velocity, collision
- [Scoring system](../systems/scoring.md) -- combo scoring pipeline
- [FRUIT_INFO data struct](data.md) -- per-fruit-type configuration
- [FruitModelInfo format](../engine/formats/models.md) -- 3D model data for fruit rendering
