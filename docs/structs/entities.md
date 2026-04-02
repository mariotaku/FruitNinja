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

## Bomb : Mortar::Entity

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | |
| +0x1c | Vec3 | pos | Position |
| +0x38 | Col* | m_Col | Collision shape |
| +0x40 | Delegate0\<void\> | hitCallback | |
| +0x64 | int | field38_0x64 | |
| +0x68 | char | activeFlag | Controls update branch |
| +0x7c | int | field58_0x7c | |
| +0x80 | char | movementFlag | |
| +0x84 | int | field63_0x84 | |
| +0x8c | Vec3 | accelForce | |
| +0xa4 | float | countdown | Timer; triggers chain spawn |
| +0xa8 | float | speedMult | |

**Key methods:** Update (0x1729fc), Init (0x172504), Draw (0x171be8), CollisionResponse (0x17280c), Chuck (0x170f68)

Countdown triggers `WaveManager::SpawnBomb(count-1)` for chain bombs.

**Bomb hit behavior:**
- Classic/Arcade: `HitBomb()` → camera shake, Game+0x10 = countdown timer, game-over delay
- Zen mode: `AddToCurrentScore(-10, 0, false, false)`, `WaveManager::ResetSpeed`, `PowerUpManager::ClearTimedPowers`

---

## SlashEntity : Mortar::Entity (blade/swipe, size ≥ 0x184)

Entity base ends at ~0x3c; SlashEntity fields follow.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x3c | PSPParticleEmitter* | m_TrailEmitter | Touch trail particle; null when inactive |
| +0x40 | float | m_Scale | Init = DAT_0017c760 |
| +0x44 | Colour | m_BaseColour | RGBA, 4 bytes |
| +0x48 | Colour | m_HighlightColour | RGBA, 4 bytes |
| +0x4c | char | m_bFlag4c | Init = 0 |
| +0x50 | int | m_SplitPoint | Split index for same-screen multiplayer |
| +0x58 | int | m_PointCount | Blade trail vertex count; 4 = min for collision |
| +0x5c | void* | m_pLeftBuffer | Ptr to left/top vertex strip |
| +0x60 | void* | m_pRightBuffer | Ptr to right/bottom vertex strip |
| +0x64 | Vec3 | m_BladeDir | Blade velocity direction, normalised |
| +0x70 | Vec3 | m_TailPos | Oldest visible trail point |
| +0x7c | Vec3 | m_HeadPos | Newest trail point / current tip |
| +0x94 | float | m_LineLengthSq | = \|head-tail\|²; -1.0 = no active segment |
| +0x98 | float | m_SpeedScale | Blade speed multiplier |
| +0x9c | int | m_SliceCount | Incremented +2 per slice |
| +0xa0 | float | m_SliceTimerA | Zeroed on each slice |
| +0xa4 | float | m_SliceTimerB | Zeroed on each slice |
| +0xa8 | Vec3 | m_BladeVelAtSlice | Blade dir snapshot at moment of slice |
| +0xb4 | Vec3 | m_SlicePos | World position of sliced entity |
| +0xc0 | int | m_SliceEntityType | Byte from entity; score accumulator |
| +0xc4 | float | m_ScoreBonus | Score multiplier field |
| +0xc8 | Vec3[6] | m_GhostPositions | 72 bytes; ends at +0x110 |
| +0x110 | int | m_GhostCount | Init = 0 |
| +0x114 | int | m_GhostFlags | Init = 0 |
| +0x118 | Vec3 | m_GhostDir | Direction snapshot for ghost |
| +0x124 | float | m_ComboTimer | Time window for combo; reset on slice |
| +0x128 | int | m_ComboCount | Fruits sliced in current combo |
| +0x12c | int | m_ComboEntityType | 0=fruit, 1=bomb, 2=special |
| +0x130 | MissControl* | m_pComboCtrl | Null if no active combo |
| +0x134 | float | m_GhostTimer | Counts up while m_bGhostActive |
| +0x138 | char | m_bGhostActive | If true, timer runs → CreateGhost() |
| +0x13c | int | m_ColEntityA | Collision entity handle; -1 = none |
| +0x140 | int | m_ColEntityB | Collision entity handle; -1 = none |
| +0x144 | char | m_bBladeActive | True = blade is a valid collision line |
| +0x148 | float | m_ComboScoreBase | 6.0f; decremented (combo_n × 0.75) per slice |
| +0x14c | int | m_ExtraFieldA | Init = -1 |
| +0x150 | int | m_ExtraFieldB | Init = -1 |
| +0x154 | int[11] | m_ComboFruitIDs | 0x2c bytes; ends at +0x180 |
| +0x180 | ushort | m_AngleCopy | Updated each frame; copied to Entity::angle |

### Collision System

**ColLine** (0x20 bytes): `{vtable, a.x, a.y, a.z, b.x, b.y, b.z, pad}`

**ColCircle** (fruit): `m_Col[0] = {vtable, cx, cy, cz}`, `m_Col[1] = {radius, ?, ?, ?}`. `vtable->GetType()` returns 1 for circle.

**Collision test**: Blade line segment vs fruit circle = segment-circle intersection. Returns 1 if any point on segment within radius of circle center.

### Key Methods

| Address | Name | Notes |
|---------|------|-------|
| 0x0017c65c | Init | Allocates ColLine, calls InitPoints(0xa0) |
| 0x0017c340 | InitPoints | Sets up vertex buffers |
| 0x0017b92c | UpdatePoints | Rebuilds vertex strips from trail, computes angle |
| 0x0017d2e4 | UpdateTouchDown | Manages trail particle emitter on touch |
| 0x0017d664 | Update | Iterates entities, calls CollideWithEntity |
| 0x0017b570 | CollideWithEntity | Line-vs-circle test |
| 0x0017b82c | CreateGhost | Spawns SlashEntityGhost for visual echo |
| 0x0017e424 | DrawSlice | Renders blade geometry |

