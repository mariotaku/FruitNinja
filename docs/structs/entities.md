# Entity Structs

## Mortar::Entity (base class, size = 0x3c)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | |
| +0x04 | int | field_0x04 | |
| +0x08 | ushort | m_TrackerID | Entity network tracker ID; used in FruitSlicedPacket |
| +0x0a | | (padding) | 2 bytes |
| +0x0c | byte | flags | bit 1 = has collision shape, bit 4 = skip entity |
| +0x10 | float | pos_x | Position X |
| +0x14 | float | pos_y | Position Y |
| +0x18 | float | pos_z | Position Z |
| +0x1c | float | vel_x | Velocity X |
| +0x20 | float | vel_y | Velocity Y |
| +0x24 | float | vel_z | Velocity Z |
| +0x28 | float | m_Scale_x | Visual scale X; set in SetFruitType/SetBombScale |
| +0x2c | float | m_Scale_y | Visual scale Y |
| +0x30 | float | m_Scale_z | Visual scale Z |
| +0x34 | | (padding) | 2 bytes |
| +0x36 | ushort | angle | Blade/rot angle; used with CosIdx/SinIdx |
| +0x38 | Col* | m_Col | Collision shape pointer |

---

## Fruit : Mortar::Entity (size = 0x118 / 280 bytes)

### Vtable

Vtable address: **0x001ea528** (typeinfo at 0x001ea520; constructor stores ptr+8)

| Slot | Offset | Address | Name | Signature |
|------|--------|---------|------|-----------|
| 0 | +0x00 | 0x176424 | ~Fruit | `void ~Fruit()` (scalar deleting dtor) |
| 1 | +0x04 | 0x17649c | ~Fruit | `void ~Fruit()` (vector deleting dtor) |
| 2 | +0x08 | 0x176708 | Init | `void Init(void* p1, long fruitType, Vec3* scale)` |
| 3 | +0x0c | 0x1761d8 | Release | `void Release()` |
| 4 | +0x10 | 0x177680 | Update | `void Update(float dt)` |
| 5 | +0x14 | 0x1791f4 | Draw | `void Draw()` |
| 6 | +0x18 | 0x17501c | DrawUpdate | `void DrawUpdate(float dt)` |
| 7 | +0x1c | 0x19d600 | PostLoad | `void PostLoad()` (no-op, inherited) |
| 8 | +0x20 | 0x19d800 | InRect | `bool InRect(ColAABB*)` (inherited) |
| 9 | +0x24 | 0x1780b0 | CollisionResponse | `int CollisionResponse(Entity* slash, ulong, ulong, Vec3* bladeVel)` |
| 10 | +0x28 | 0x19d608 | Collide | `void Collide(Entity*, Col*, ulong*, Vec3*)` (inherited) |
| 11 | +0x2c | 0x19d61c | ReceiveMessage | `void ReceiveMessage(Entity*, Message*)` (inherited) |
| 12 | +0x30 | 0x172f4c | ListenerCallback | `void ListenerCallback(Entity*, Entity*, Message*)` (no-op) |

### Struct Layout

Entity base (+0x00..+0x3b) — see Mortar::Entity above. Fruit own fields start at +0x3c.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x3c | byte | m_FruitType | 0..N; × 0x330 for FRUIT_INFO offset |
| +0x3d | byte | m_bNoPowerUp | = 0 in Init; ≠0 = skip power-up spawn on slice |
| +0x40 | PSPParticleEmitter* | m_pEmitter1 | Main particle trail; zeroed in Init |
| +0x44 | PSPParticleEmitter* | m_pEmitter2 | Second half particle trail; zeroed in Init |
| +0x48 | float | m_SlicePos_x | Copied from entity pos at slice time |
| +0x4c | float | m_SlicePos_y | |
| +0x50 | float | m_SlicePos_z | |
| +0x54 | | (gap) | 12 bytes (0x54-0x5f) unknown |
| +0x60 | int | m_SlicedFrameAccum | = 0 in Init; increments by DAT×dt when sliced (timer/counter) |
| +0x64 | int | m_CollisionSize | = 75 (0x4b) in Init; collision radius |
| +0x68 | int | m_field68 | = 4 in Init; purpose unclear |
| +0x6c | float | m_SliceTimer | Init = -1.0f; set positive by CollisionResponse, countdown triggers Slice() |
| +0x70 | ushort | m_SliceAngle | Atan2Idx(blade.x, blade.y); slice direction |
| +0x74 | float | m_SliceImpulse | Clamp 4-8; or 6-8 for special fruit |
| +0x78 | int | m_SliceState | = 0 in Init; set on slice |
| +0x7c | byte | m_bActive | = 1 in Init; enables physics integration |
| +0x80 | float | m_ChuckDelay | Throw delay timer; counts down to 0, then fruit launches |
| +0x84 | float | m_RotAxis_x | Rotation drift axis X; from global config Vec3 |
| +0x88 | float | m_RotAxis_y | Rotation drift axis Y |
| +0x8c | float | m_RotAxis_z | Rotation drift axis Z |
| +0x90 | int | m_PlayerIdx | = 0 in Init; 0/1 = player, 2 = online spectator, 3 = special |
| +0x94 | float | m_TimeScale | = 1.0f in Init; multiplied with dt for scaled time |
| +0x98 | float | m_ZPosition | From GetFruitZPosition(); depth layer ordering |
| +0x9c | float | m_Gravity_x | Gravity/acceleration X; Init = (DAT, -12.0, DAT) |
| +0xa0 | float | m_Gravity_y | Gravity/acceleration Y; -12.0 = standard downward gravity |
| +0xa4 | float | m_Gravity_z | Gravity/acceleration Z |
| +0xa8 | float | m_BaseScale_x | Backup of m_Scale from SetFruitType (original unmodified) |
| +0xac | float | m_BaseScale_y | |
| +0xb0 | float | m_BaseScale_z | |
| +0xb4 | byte | m_bSliced | = 0 in Init; set to 1 in Slice(); guards CollisionResponse |
| +0xb8 | float | m_HalfB_pos_x | Second half position X (after slice) |
| +0xbc | float | m_HalfB_pos_y | |
| +0xc0 | float | m_HalfB_pos_z | |
| +0xc4 | float | m_HalfB_vel_x | Second half velocity X |
| +0xc8 | float | m_HalfB_vel_y | |
| +0xcc | float | m_HalfB_vel_z | |
| +0xd0 | Quaternion | m_Rot1 | 16 bytes; half A rotation; init from RandomStartAngle |
| +0xe0 | Quaternion | m_Rot2 | 16 bytes; half B rotation |
| +0xf0 | Vec3 | m_RotVel1 | Half A rotation velocity; random [-5.5, 5.5] per component |
| +0xfc | Vec3 | m_RotVel2 | Half B rotation velocity |
| +0x108 | int | m_pSlashEntity | Backref to SlashEntity; Release checks slash+0x134 == this |
| +0x10c | byte | m_bSpecialGravity | = 0 in Init; when set, gravity ramp uses 6.5× instead of 4.5× |
| +0x10d | byte | m_bCriticalEligible | = 1 in Init; = 0 after critical slice |
| +0x110 | float | m_ScaleAnim | Animation scale for sliced halves; starts at DAT, ramps to 1.0 |
| +0x114 | byte | m_bDrawWhole | = 0 in Init; when set, draw whole fruit even if sliced |

### All Methods (0x174f00 - 0x17b000 range, Fruit.cpp)

**Vtable methods:**

| Address | Name | CC | Signature | Lines |
|---------|------|----|-----------|-------|
| 0x176424 | ~Fruit | __thiscall | `void ~Fruit()` (scalar dtor) | - |
| 0x176460 | ~Fruit | __thiscall | `void ~Fruit()` (variant) | - |
| 0x17649c | ~Fruit | __thiscall | `void ~Fruit()` (vector dtor) | - |
| 0x1764dc | Fruit | __thiscall | `Fruit()` (ctor, from thunk) | - |
| 0x176520 | Fruit | __thiscall | `Fruit()` (main ctor) | - |
| 0x176708 | Init | __thiscall | `void Init(void*, long fruitType, Vec3* scale)` | 142 |
| 0x1761d8 | Release | __thiscall | `void Release()` | - |
| 0x177680 | Update | __thiscall | `void Update(float dt)` | 412 |
| 0x1791f4 | Draw | __thiscall | `void Draw()` | 161 |
| 0x17501c | DrawUpdate | __thiscall | `void DrawUpdate(float dt)` | - |
| 0x1780b0 | CollisionResponse | __thiscall | `int CollisionResponse(Entity*, ulong, ulong, Vec3*)` | 591 |

**Fruit instance methods:**

| Address | Name | CC | Signature | Notes |
|---------|------|----|-----------|-------|
| 0x175218 | CheckHasGoneOffscreen | __thiscall | `void CheckHasGoneOffscreen()` | Bounds check for both halves |
| 0x175624 | IsOffscreen | __thiscall | `bool IsOffscreen()` | Simple offscreen test |
| 0x1756dc | SetTrailParticles | __thiscall | `int SetTrailParticles(ulong hash)` | Create/replace particle emitter |
| 0x1757f4 | RotateFacingUp | __thiscall | `void RotateFacingUp(bool, Vec3)` | Orient fruit for display |
| 0x175988 | UpdateBombAvoidance | __thiscall | `void UpdateBombAvoidance(float dt)` | Push bombs away from fruit |
| 0x175a64 | Chuck | __thiscall | `void Chuck(float delay)` | Set throw delay, check special fruit |
| 0x175b78 | SetForPlayer | __thiscall | `void SetForPlayer(int playerIdx)` | Set player + adjust collision for online |
| 0x175ea0 | AddShadow | __thiscall | `void AddShadow(QUADCUSTOMVERTEX**, int*)` | Add shadow quad(s) to batch |
| 0x17621c | SetFruitType | __thiscall | `void SetFruitType(uint type, float scale)` | Set type, scale, collision sphere |
| 0x176354 | EnableCollision | __thiscall | `void EnableCollision(bool)` | Create/destroy ColSphere |
| 0x176abc | KillFruit | __thiscall | `void KillFruit(int doMissPenalty)` | Clear emitters, miss penalty, set killed |
| 0x176d58 | Slice | __thiscall | `void Slice()` | Split into halves, splats, SFX |
| 0x17a82c | IsActive | __thiscall | `bool IsActive()` | Entity alive check |
| 0x17a840 | IsActive | __thiscall | `bool IsActive()` (variant) | - |
| 0x17a854 | SetWaveNumber | __thiscall | `void SetWaveNumber(int)` | Setter |
| 0x17a858 | SetEntityTrackerID | __thiscall | `void SetEntityTrackerID(ushort)` | Setter |
| 0x17a85c | SetRotation | __thiscall | `void SetRotation(...)` | Setter |
| 0x17a860 | SetMagnitude | __thiscall | `void SetMagnitude(float)` | Setter |
| 0x17a868 | SetPoints | __thiscall | `void SetPoints(int)` | Setter |

**Static / free functions:**

| Address | Name | CC | Signature | Notes |
|---------|------|----|-----------|-------|
| 0x174f18 | FruitTypeName | __stdcall | `char* FruitTypeName(int type)` | Get fruit name string |
| 0x174f38 | FruitTypeHash | __stdcall | `uint FruitTypeHash(int type)` | Get fruit name hash |
| 0x174f5c | FruitFactTexture | __thiscall | `Texture* FruitFactTexture(int type)` | Get fruit fact texture |
| 0x174f80 | FruitTypeColour | __stdcall | `Colour FruitTypeColour(int type)` | Get fruit colour |
| 0x174fc8 | FruitFactColour | __thiscall | `Colour FruitFactColour(int type)` | Get fruit fact colour |
| 0x174ff8 | FruitInfo | __stdcall | `FRUIT_INFO* FruitInfo(int type)` | Get FRUIT_INFO by index |
| 0x175018 | SetupLighting | __stdcall | `void SetupLighting(SmartPtr*)` | No-op stub |
| 0x1751bc | GetSmallestDelta | __stdcall | `float GetSmallestDelta(float, float)` | Angle delta utility |
| 0x175714 | AnyActivePowers | __stdcall | `bool AnyActivePowers()` | Check FRUIT_POWERS active |
| 0x175740 | RandomStartAngle | __stdcall | `void RandomStartAngle(Quaternion*, bool)` | Random initial rotation |
| 0x175928 | GetNumActiveForPlayer | __stdcall | `uint GetNumActiveForPlayer(int, bool)` | Count active fruits |
| 0x175b10 | FruitType | __stdcall | `int FruitType(char* name, bool)` | Lookup fruit index by name |
| 0x175ba4 | GetFact | __stdcall | `char* GetFact(int*, int*, int, int)` | Get fruit fact text |
| 0x175db0 | AddQuad | __stdcall | `void AddQuad(QUADCUSTOMVERTEX**, ...)` | Add shadow/splat quad |
| 0x176184 | CheckFruitDropped | __thiscall | `void CheckFruitDropped()` | Check if fruit missed |
| 0x176564 | RandomFruit | __stdcall | `int RandomFruit(bool includeOnSide)` | Weighted random selection |
| 0x176a90 | MakeSFXDelegate_Fruit | __stdcall | `void MakeSFXDelegate_Fruit(void* out)` | Create SFX callback |
| 0x176d14 | ClearUnspawned | __stdcall | `void ClearUnspawned(bool killAll)` | Kill unspawned/all fruits |
| 0x178f28 | Fruit_DrawShadows | __stdcall | `void Fruit_DrawShadows()` | Batch draw all fruit shadows |
| 0x179010 | CleanupFruit | __stdcall | `void CleanupFruit()` | Cleanup all fruit entities |
| 0x17911c | DestroyFruitModels | __stdcall | `void DestroyFruitModels()` | Release model SmartPtrs |
| 0x1794e0 | LoadFruitModels | __stdcall | `void LoadFruitModels()` | Load 3D models per fruit type |
| 0x17987c | LoadInfo | __thiscall | `void LoadInfo()` | Parse XML, create FRUIT_INFO array |
| 0x17a354 | _GLOBAL__I_Fruit.cpp | - | static init | File-level static constructors |

**FRUIT_INFO / FRUIT_POWER related:**

| Address | Name | CC | Notes |
|---------|------|----|-------|
| 0x17a7c0 | ImpactSound | __thiscall | ImpactSound ctor |
| 0x17a7cc | FRUIT_POWER | __thiscall | FRUIT_POWER ctor |
| 0x17a7d8 | RandomPower | __stdcall | FRUIT_POWERS::RandomPower() |
| 0x17a824 | FRUIT_POWERS | __thiscall | FRUIT_POWERS ctor |
| 0x17accc | ~ImpactSound | __thiscall | ImpactSound dtor |
| 0x17ace0 | ~FRUIT_POWERS | __thiscall | FRUIT_POWERS dtor |
| 0x17ad7c | ~FRUIT_INFO | __thiscall | FRUIT_INFO dtor |
| 0x17ae20 | FRUIT_INFO | __thiscall | FRUIT_INFO ctor |
| 0x17aeb8 | FruitModelInfo | __thiscall | FruitModelInfo ctor |
| 0x17ad14 | ~FruitModelInfo | __thiscall | FruitModelInfo dtor |

**Internal helpers (small, in Fruit.cpp):**

| Address | Name | CC | Notes |
|---------|------|----|-------|
| 0x176a20 | __tcf_1 | - | Static destructor registration |
| 0x176a48 | __tcf_0 | - | Static destructor registration |
| 0x176a70 | SetNull | __thiscall | SmartPtr::SetNull wrapper |
| 0x176a7c | ZeroInit_Fruit | __thiscall | Zero-init helper |
| 0x176a84 | ZeroInitPassthru_Fruit | __stdcall | Zero-init passthrough |
| 0x178f18 | SmartPtrNull_Tex2D_Fruit | __stdcall | SmartPtr null check |
| 0x178f24 | SmartPtrNull_Tex2D_Fruit2 | __stdcall | SmartPtr null check |
| 0x179004 | SetNull | __thiscall | SmartPtr::SetNull wrapper |
| 0x1791d8 | SmartPtrNull_Tex_Fruit3 | __stdcall | SmartPtr null check |
| 0x1791e0 | ZeroInit_Fruit2 | __stdcall | Material/colour zero-init |

**Math/engine utilities (inlined in Fruit.cpp):**

| Address | Name | CC | Notes |
|---------|------|----|-------|
| 0x17a86c | _Quaternion | __thiscall | Quaternion ctor (identity) |
| 0x17a88c | MultVec33 | __stdcall | Matrix33 × Vec3 |
| 0x17a8f0 | Identity | __thiscall | Quaternion identity |
| 0x17a910 | _Quaternion | __thiscall | Quaternion copy ctor |
| 0x17a924 | operator* | __thiscall | Quaternion multiply |
| 0x17a990 | _Matrix<float,_Matrix33> | __stdcall | Matrix33 from Quaternion |
| 0x17aa54 | Matrix33 | __thiscall | Matrix33 ctor |
| 0x17aa64 | Matrix33Unit | - | Quaternion to Matrix33 |
| 0x17ab30 | Copy33To44 | __thiscall | Matrix33 to Matrix44 |
| 0x17aba4 | operator.cast.to._Matrix44 | __thiscall | Quaternion cast to Mat44 |
| 0x17abb4 | Matrix44Unit | - | Quaternion to Matrix44 |
| 0x17abf4 | Magnitude | - | Vec3 magnitude |
| 0x17ac1c | Normalise | - | Vec3 normalise |
| 0x17ac68 | CreateFromAxisAngle | __thiscall | Quaternion from axis+angle |
| 0x17acf4 | Clear | - | SmartPtr clear |

### CollisionResponse Pipeline (0x1780b0)

1. Check `m_bSliced` guard
2. Critical hit probability check
3. Play SFX (normal or critical)
4. Capture slice angle/impulse from blade velocity
5. Spawn juice particles (PSPParticleManager)
6. AddSlice() for visual effect
7. UnlockSpecificOrderAchievement
8. Optional: PowerUpManager::RandomPower() if hasPowerUp

### Draw Pipeline (0x1791f4)

1. Skip if `m_ChuckDelay > 0` (not visible yet)
2. **Whole fruit** (`!m_bSliced || m_bDrawWhole`):
   - Build Matrix44 from Scale × Quaternion(m_Rot1)
   - Translate to `pos + offset × scale + ZPosition`
   - Online multiplayer: may draw player-tinted model (half model at slot +0x1c)
   - `Mortar::Model::Draw(fruitModels[m_FruitType].wholeModel, matrix)`
3. **Sliced fruit** (two halves, loop ×2):
   - Each half: Scale × Quaternion(rot[i]) matrix
   - Half 0 uses `pos`, Half 1 uses `m_HalfB_pos`
   - Z-offset: `drawPos.z += m_ZPosition`
   - `Mortar::Model::Draw(fruitModels[m_FruitType].halfModels[i], matrix)`

### Update Flow (0x177680)

**Unsliced branch:**
1. Chuck delay countdown (play whoosh SFX at threshold)
2. At delay=0: SetTrailParticles, spawn extra fruits from WaveManager
3. Physics: `pos += vel×dt + 0.5×gravity×dt² + rotAxis×dt`, `vel += gravity×dt`
4. Copy pos/vel to HalfB (backup for future split)
5. Slice timer: if positive, countdown; at 0 → call Slice()
6. UpdateBombAvoidance

**Sliced branch:**
1. Scale animation ramp: `m_ScaleAnim = min(1.0, m_ScaleAnim + dt×3.0)`
2. Gravity ramp: normalize gravity, increase magnitude by `4.5×` (or `6.5×` if m_bSpecialGravity)
3. Two-body physics: both halves get gravity + velocity integration
4. Frame accumulator: `m_SlicedFrameAccum += DAT × dt`

**Both branches:**
- Quaternion rotation update (loop ×2 for both halves)
- CheckHasGoneOffscreen → KillFruit if true
- Update collision sphere center to pos
- Update particle emitter positions + rotations

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
