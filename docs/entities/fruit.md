# Fruit Entity

## Fruit : Mortar::Entity (size = 0x118 / 280 bytes)

### Vtable: 0x001ea528 (13 slots)

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

Entity base (+0x00..+0x3b) — see [Mortar::Entity](entity-base.md). Fruit own fields start at +0x3c.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x3c | byte | m_FruitType | 0..N; x 0x330 for FRUIT_INFO offset |
| +0x3d | byte | m_bNoPowerUp | = 0 in Init; non-zero = skip power-up spawn on slice |
| +0x40 | PSPParticleEmitter* | m_pEmitter1 | Main particle trail; zeroed in Init |
| +0x44 | PSPParticleEmitter* | m_pEmitter2 | Second half particle trail; zeroed in Init |
| +0x48 | float | m_SlicePos_x | Copied from entity pos at slice time |
| +0x4c | float | m_SlicePos_y | |
| +0x50 | float | m_SlicePos_z | |
| +0x54 | | (gap) | 12 bytes (0x54-0x5f) unknown |
| +0x60 | int | m_SlicedFrameAccum | = 0 in Init; increments by DAT x dt when sliced (timer/counter) |
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
| +0x10c | byte | m_bSpecialGravity | = 0 in Init; when set, gravity ramp uses 6.5x instead of 4.5x |
| +0x10d | byte | m_bCriticalEligible | = 1 in Init; = 0 after critical slice |
| +0x110 | float | m_ScaleAnim | Animation scale for sliced halves; starts at DAT, ramps to 1.0 |
| +0x114 | byte | m_bDrawWhole | = 0 in Init; when set, draw whole fruit even if sliced |

---

## All Methods (0x174f00 - 0x17b000 range, Fruit.cpp)

### Vtable methods

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

### Fruit instance methods

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

### Static / free functions

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

### FRUIT_INFO / FRUIT_POWER related

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

### Internal helpers (small, in Fruit.cpp)

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

### Math/engine utilities (inlined in Fruit.cpp)

| Address | Name | CC | Notes |
|---------|------|----|-------|
| 0x17a86c | _Quaternion | __thiscall | Quaternion ctor (identity) |
| 0x17a88c | MultVec33 | __stdcall | Matrix33 x Vec3 |
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

---

## Function Pseudocode

### Fruit::Init (0x00176708, 142 lines)

| Address | Signature |
|---------|-----------|
| 0x00176708 | `void Fruit::Init(void* p1, long fruitType, Vec3* scale)` |

```c
void Fruit::Init(void* p1, long fruitType, Vec3* scale) {
    m_ScaleAnim = INIT_VALUE;     // +0x110
    field_0x114 = 0;              // m_bDrawWhole
    m_TrackerID = 0;
    m_TimeScale = 1.0f;
    
    // Fruit type selection
    if (fruitType < 0 || fruitType >= fruitCount)
        m_FruitType = RandomFruit(true);
    else
        m_FruitType = (byte)fruitType;
    
    // Zen mode: skip special fruit if wave < threshold
    if (gameMode == ZEN && waveProgress < 1.0) {
        while (m_FruitType == specialFruitIdx)
            m_FruitType = RandomFruit(true);
        // Also skip if power-up fruit and conditions not met
        if (hasPowerUp && (waveTime < 8.0 || anyActivePowers))
            { flags |= 0x10; return; }  // kill immediately
    }
    
    // Online multiplayer: avoid specific fruit type
    if (IsOnlineMultiplayer() && m_FruitType == onlineExcludedType)
        m_FruitType--;
    
    // Track special fruit spawn count
    if (fruitInfo[m_FruitType].hasPowerUp)
        specialFruitCount++;
    
    float scaleVal = (scale == NULL) ? 1.0f : *scale;
    SetFruitType(m_FruitType, scaleVal);
    
    m_ChuckDelay = INIT_VALUE;    // +0x80
    m_bSliced = 0;
    flags = (flags & ~0x10) | 0x02;
    m_bNoPowerUp = 0;
    m_PlayerIdx = 0;
    m_SliceTimer = -1.0f;
    m_RotAxis = globalConfigVec3;
    
    // Random rotation velocities (RandF(11.0) - 5.5 per component)
    for (i = 0; i < 2; i++) {
        m_RotVel[i] = Vec3(rand-5.5, rand-5.5, rand-5.5);
        m_pEmitter[i] = NULL;
        m_Rot[i] = RandomStartAngle(false);
    }
    
    field_0x10c = 0;  // m_bSpecialGravity
    m_CollisionSize = 0x4b;
    m_bCriticalEligible = 1;
    field_0x68 = 4;
    field_0x60 = 0;   // m_SlicedFrameAccum
    m_bActive = 1;
    m_SliceState = 0;
    field_0x108 = 0;  // m_pSlashEntity
    m_ZPosition = GetFruitZPosition();
    m_Gravity = Vec3(DAT, -12.0, DAT);
}
```

### Fruit::Update (0x00177680, 412 lines)

```c
// Full fruit physics — see ../systems/physics.md
void Fruit::Update(float dt) {
    float scaledDt = dt * m_TimeScale;
    
    if (!m_bSliced) {
        // === UNSLICED FRUIT ===
        
        // Phase 1: Launch delay
        if (m_ChuckDelay > 0) {
            m_ChuckDelay -= Game.dt;
            if (m_ChuckDelay crosses threshold) playSFX("whoosh");
            if (m_ChuckDelay <= 0) {
                SetTrailParticles(fruitInfo.particleHash);
                // May spawn extra fruits from WaveManager.spawnLevel
            }
            if (m_ChuckDelay > 0) return;
        }
        
        // Phase 2: Ballistic flight
        if (m_bActive) {
            pos += vel * scaledDt + 0.5 * gravity * scaledDt²;
            vel += gravity * scaledDt;
            pos += rotAxis * scaledDt;
        }
        
        // Phase 3: Position backup
        m_HalfB_pos = pos;  // for future split
        m_HalfB_vel = vel;
        
        // Phase 4: Slice timer
        if (m_SliceTimer > 0) {
            m_SliceTimer -= scaledDt;
            if (m_SliceTimer <= 0) { m_SliceTimer = 0; Slice(); }
        }
        
        UpdateBombAvoidance(scaledDt);
        
    } else {
        // === SLICED FRUIT (two halves) ===
        
        // Scale animation
        m_ScaleAnim = min(1.0, m_ScaleAnim + scaledDt * 3.0);
        
        // Gravity ramp-up
        float gravGrowth = DAT * (scaledDt / FRAME_TIME) * 4.5;  // or 6.5 for special
        gravity = normalize(gravity) * (length(gravity) + gravGrowth);
        
        // Two-body physics
        vel  += gravity * scaledDt;   vel2  += gravity * scaledDt;
        pos  += vel * scaledDt;       pos2  += vel2 * scaledDt;
    }
    
    // Rotation update (both halves, loop x2)
    for (i = 0..1) {
        qX = QuatFromAxisAngle(axisX, rotVel[i].x * scaledDt * SCALE);
        qY = QuatFromAxisAngle(axisY, rotVel[i].y * scaledDt * SCALE);
        qZ = QuatFromAxisAngle(axisZ, rotVel[i].z * scaledDt * SCALE);
        rot[i] = normalize(rot[i] * qX * qY * qZ);
    }
    
    if (CheckHasGoneOffscreen()) KillFruit(true);
    
    // Update collision shape
    if (m_Col) { m_Col->center = pos; m_Col->center.z = 0; }
    
    // Update particle emitters position + rotation
    ...
}
```

### Update Flow Summary (0x177680)

**Unsliced branch:**
1. Chuck delay countdown (play whoosh SFX at threshold)
2. At delay=0: SetTrailParticles, spawn extra fruits from WaveManager
3. Physics: `pos += vel*dt + 0.5*gravity*dt^2 + rotAxis*dt`, `vel += gravity*dt`
4. Copy pos/vel to HalfB (backup for future split)
5. Slice timer: if positive, countdown; at 0 -> call Slice()
6. UpdateBombAvoidance

**Sliced branch:**
1. Scale animation ramp: `m_ScaleAnim = min(1.0, m_ScaleAnim + dt*3.0)`
2. Gravity ramp: normalize gravity, increase magnitude by `4.5x` (or `6.5x` if m_bSpecialGravity)
3. Two-body physics: both halves get gravity + velocity integration
4. Frame accumulator: `m_SlicedFrameAccum += DAT * dt`

**Both branches:**
- Quaternion rotation update (loop x2 for both halves)
- CheckHasGoneOffscreen -> KillFruit if true
- Update collision sphere center to pos
- Update particle emitter positions + rotations

### Fruit::CollisionResponse (0x001780b0, 591 lines)

```c
// Triggered by SlashEntity on collision hit
int Fruit::CollisionResponse(Entity* slash, ulong p2, ulong p3, Vec3* bladeVel) {
    if (m_bSliced || m_SliceTimer > -1.0) return 1;  // already sliced
    
    // Critical hit probability
    m_SliceState = 0;
    int score = GetCurrentScore(m_PlayerIdx);
    if (score >= 2 && fruitInfo.comboFlag && !paused && bombTimer <= 0) {
        float critChance = WaveManager::GetCriticalChance(0);
        int threshold = game->m_ScoreThreshold;
        if (threshold < 3) threshold = 2; else threshold--;
        game->m_ScoreThreshold = threshold;
        
        uint roll = Random::Rand32(max(1, threshold/critChance));
        m_bCriticalEligible = (roll == 0);
        if (m_bCriticalEligible)
            game->m_ScoreThreshold += RESET_BONUS;
    }
    
    // Play SFX
    if (m_bCriticalEligible) playSFX("critical_hit");
    else playFruitSFX(fruitInfo.soundList);
    
    // Set slice properties
    m_SliceTimer = BASE_TIMER * (m_bCriticalEligible ? 0.5 : 2.5);
    m_SliceAngle = Atan2Idx(bladeVel->x, bladeVel->y);
    m_SliceImpulse = clamp(magnitude(bladeVel) * SCALE, 4.0, 8.0);
    m_SlicePos = pos;
    
    // Spawn particles
    AddSlice(pos, angle, impulse, isCritical);
    PSPParticleManager::AddEmitter(fruitInfo.particleHashA, ...);
    
    // Score
    int points = fruitInfo.m_BaseScore;
    if (m_bCriticalEligible) points += CRIT_BONUS;
    AddToCurrentScore(points, playerIdx, true, false);
    
    // Achievements
    AchievementManager::UnlockSpecificOrderAchievement(fruitInfo.hash);
    
    // Power-up spawn
    if (fruitInfo.hasPowerUp)
        PowerUpManager::ActivatePower(FRUIT_POWERS::RandomPower(), pos, NULL);
    
    return 1;
}
```

#### CollisionResponse Pipeline

1. Check `m_bSliced` guard
2. Critical hit probability check
3. Play SFX (normal or critical)
4. Capture slice angle/impulse from blade velocity
5. Spawn juice particles (PSPParticleManager)
6. AddSlice() for visual effect
7. UnlockSpecificOrderAchievement
8. Optional: PowerUpManager::RandomPower() if hasPowerUp

### Fruit::Chuck (0x00175a64, 36 lines)

| Address | Signature |
|---------|-----------|
| 0x00175a64 | `void Fruit::Chuck(float delay)` |

```
m_HalfB_pos = pos                  // snapshot position for later split
if (delay < 0) delay = 0.125       // default chuck delay
m_ChuckDelay = delay
hash = StringHash(SPECIAL_FRUIT_NAME)
powers = FruitInfo[m_FruitType].m_pPowers   // at offset +0x32c
if (powers && (gameTime - delay < 8.0) && powers->hash != hash):
    g_FruitCount--              // decrement global fruit counter
    flags |= 0x10               // mark as "skip" (pre-killed)
```

### Fruit::Draw (0x001791f4, 161 lines)

```c
// Renders fruit 3D model with quaternion rotation
void Fruit::Draw() {
    if (m_ChuckDelay > 0) return;  // not visible yet
    
    if (!m_bSliced || m_field114) {
        // Whole fruit
        Model* model = fruitModels[m_FruitType].m_pWholeModel;
        if (!model) return;
        Matrix44 mat = Scale44(entity.scale) * Quaternion::Matrix44Unit(m_Rot1);
        Vec3 drawPos = entity.pos + offset * scale;
        GlobalTranslate44(mat, drawPos);
        Model::Draw(model, mat);
    } else {
        // Two sliced halves
        for (int i = 0; i < 2; i++) {
            Model* half = fruitModels[m_FruitType].halfModels[i];
            if (!half) continue;
            Matrix44 mat = Scale44(entity.scale) * Quaternion::Matrix44Unit(rot[i]);
            Vec3 drawPos = (i == 0) ? entity.pos : m_HalfB_pos;
            drawPos.z += m_ZPosition;
            GlobalTranslate44(mat, drawPos);
            Model::Draw(half, mat);
        }
    }
}
```

#### Draw Pipeline

1. Skip if `m_ChuckDelay > 0` (not visible yet)
2. **Whole fruit** (`!m_bSliced || m_bDrawWhole`):
   - Build Matrix44 from Scale x Quaternion(m_Rot1)
   - Translate to `pos + offset * scale + ZPosition`
   - Online multiplayer: may draw player-tinted model (half model at slot +0x1c)
   - `Mortar::Model::Draw(fruitModels[m_FruitType].wholeModel, matrix)`
3. **Sliced fruit** (two halves, loop x2):
   - Each half: Scale x Quaternion(rot[i]) matrix
   - Half 0 uses `pos`, Half 1 uses `m_HalfB_pos`
   - Z-offset: `drawPos.z += m_ZPosition`
   - `Mortar::Model::Draw(fruitModels[m_FruitType].halfModels[i], matrix)`

### Fruit::Slice (0x00176d58, 355 lines)

Called when `m_SliceTimer` reaches 0 in Fruit::Update. Splits the fruit into two halves.

```c
void Fruit::Slice() {
    m_SliceTimer = 0;
    
    // Random rotation offsets for two halves
    uint16_t randA = Random::Rand32(rng, 0x5550);
    uint16_t randB = Random::Rand32(rng, 0x5550);
    
    // Build rotation matrix from current quaternion
    Matrix44 rotMat = Quaternion::Matrix44Unit(m_Rot1);
    
    // Determine slice direction (which side each half goes)
    Vec3 sliceDir = MultVec44(Vec3(0,0,1), rotMat);
    bool flipSide = (abs(sliceDir.x) + abs(sliceDir.y) > 0) && 
                     angleDelta(sliceDir, m_SliceAngle) < 0;
    
    // Critical hit: extra visual effects
    if (m_bCriticalEligible && m_PlayerIdx < 2) {
        AddSlice(pos, angle + offset, impulse * SCALE1 * SCALE2, 1);  // two slice lines
        AddSlice(pos, angle - offset, impulse * SCALE1 * SCALE2, 1);
        MissControl::MakeCritical(MissControl::GetFree(), pos, m_PlayerIdx);
        impulse *= 1.5;
    }
    
    // Special fruit (score=50): also gets 1.5x impulse
    if (fruitInfo.m_BaseScore == 0x32) impulse *= 1.5;
    
    // Spawn splats (2..4 based on random + critical)
    for (int i = 0; i < splatCount; i++) {
        uint16_t angle = Random::Rand32(0xfff0);
        float speed = (impulse + random(0.5) * impulse) * (i * DECAY + 5.0);
        SplatEntity* splat = SplatEntity::GetFree();
        SplatEntity::MakeSplat(splat, pos, Vec3(sin(angle)*speed, cos(angle)*speed, 0), 
                               false, fruitType + critOffset);
    }
    
    // Play splat SFX (random 1-3)
    sprintf(sfxName, "splat_%d", Random::Rand32(3) + 1);
    GameSound::SFXPlay(sound, sfxName, 1.0, 1.0, delegate);
    
    // Compute half velocities from slice angle + impulse
    float sliceFactor = 1.0 - fruitInfo[0x24c];  // per-fruit slice property
    Vec3 halfVelA = Vec3(sin(angleA), cos(angleA), 0) * impulse * sliceFactor + vel * (1 - sliceFactor);
    Vec3 halfVelB = Vec3(sin(angleB), cos(angleB), 0) * impulse * sliceFactor + vel * (1 - sliceFactor);
    m_HalfB_vel = halfVelA;
    vel = halfVelB;
    
    // Mark as sliced
    m_bSliced = true;
    
    // Boost rotation velocities (ensure minimum spin)
    for (int i = 0; i < 2; i++) {
        rotVel[i] = max(abs(rotVel[i]), MIN_SPIN) * sign(rotVel[i]);
    }
}
```

### Fruit::KillFruit (0x00176abc, 101 lines)

| Address | Signature |
|---------|-----------|
| 0x00176abc | `void Fruit::KillFruit(bool removeFromList)` |

```
// Clear particle emitters
if (m_pEmitter1) { ClearEmitter(m_pEmitter1); m_pEmitter1 = NULL }
if (m_pEmitter2) { ClearEmitter(m_pEmitter2); m_pEmitter2 = NULL }
// Miss penalty logic (only if not sliced, not noPowerUp, and baseScore < 5)
if (!m_bSliced && !m_bNoPowerUp && FruitInfo[m_FruitType].baseScore < 5):
    if (gameMode == ZEN):
        if (doMissPenalty): track "fruit_missed" + per-type miss in FruitSaveData
    else:
        if (FailureEnabled() && doMissPenalty):
            if (!gameState->isFrozen):
                MissControl::MakeDisappear(pos, playerIdx)
                GameSound::SFXPlay("fruit_miss")
                gameState->hadMiss = true
            gameState->missCount++
            if (missCount > 2): GameOver(); clear combo
// Unlink from SlashEntity if backref matches
if (field_0x108 && *(field_0x108+0x134) == this): *(field_0x108+0x134) = 0
// Decrement power fruit counter if applicable
if (!(flags & 0x10) && FruitInfo[m_FruitType].m_pPowers):
    g_PowerFruitCount = max(0, g_PowerFruitCount - 1)
if (m_TrackerID) ET_RemoveEntity(0, m_TrackerID)
flags |= 0x10   // mark killed
```

### Fruit::SetFruitType (0x0017621c, 46 lines)

```c
void Fruit::SetFruitType(uint type, float scale) {
    this->m_FruitType = (byte)type;
    
    // VISUAL SCALE — computed from globals, NOT from FRUIT_INFO
    // Chain: globalVec (BSS, likely (1,1,1)) x GOT_config x 0.01 (DAT_0017633c)
    Vec3 entityScale = globalScaleVec * configFloat * 0.01f;
    this->entity.scale = entityScale;       // +0x28..+0x30
    this->entity.baseScale = entityScale;   // +0xa8..+0xb0
    
    // COLLISION SPHERE — uses FRUIT_INFO fields
    FRUIT_INFO* info = &fruitInfoArray[type];  // type x 0x330 stride
    float radius = info->m_CollisionBase + 0.52f * info->m_CollisionScale;
    //              +0x248 (1.0)          DAT_00176340    +0x244 (25.0)
    
    if (radius <= 0.0) {
        if (this->m_Col) { delete this->m_Col; this->m_Col = NULL; }
    } else {
        if (!this->m_Col) this->m_Col = new ColSphere();
        this->m_Col->pos = Vec3(this->pos_x, this->pos_y, 0);
        this->m_Col->radius = radius * scale;  // scale param from Fruit::Init
    }
}
```

**Scale chain — RESOLVED from `Data/xml/fruitlist.xml` + binary constants:**

The "global config" that was in BSS is the per-fruit `scale` attribute from fruitlist.xml,
loaded by `Fruit::LoadInfo()` which parses the XML into the FRUIT_INFO array.

| Constant | Address/Source | Value | Purpose |
|----------|---------------|-------|---------|
| Per-fruit scale | `fruitlist.xml` `scale=` attr | 50-75 | Visual scale from XML data |
| VISUAL_SCALE_MULT | DAT_0017633c | **0.01** | Multiplied with per-fruit scale |
| COLLISION_FACTOR | DAT_00176340 | **0.52** | collision + 0.52 x collisionScale |
| Menu post-multiply | DAT_0014f194 | **0.2** | MenuButton shrinks entity x 0.2 |

**Visual scale computation:**
```
SetFruitType: entity.scale = Vec3(1,1,1) * FruitInfo[type].scale * 0.01
MenuButton:   entity.scale *= 0.2   (post-Init for menu display only)
```

**Per-fruit scale values (from `Data/xml/fruitlist.xml`):**

| Type | Name | XML scale | Visual (x0.01) | Menu (x0.2) |
|------|------|-----------|----------------|-------------|
| 0 | apple | 60 | 0.60 | 0.12 |
| 1 | banana | 60 | 0.60 | 0.12 |
| 2 | orange | 60 | 0.60 | 0.12 |
| 3 | watermelon | 75 | 0.75 | 0.15 |
| 4 | strawberry | 50 | 0.50 | 0.10 |
| 5 | kiwifruit | 50 | 0.50 | 0.10 |
| 6 | pineapple | 65 | 0.65 | 0.13 |
| 7 | plum | 55 | 0.55 | 0.11 |
| 8 | pear | 60 | 0.60 | 0.12 |
| 9 | mango | 65 | 0.65 | 0.13 |
| 10 | apple_red | 60 | 0.60 | 0.12 |
| 11 | lime | 51 | 0.51 | 0.10 |
| 12 | dragon | 65 | 0.65 | 0.13 |
| 13 | coconut | 70 | 0.70 | 0.14 |

**FRUIT_INFO XML attributes -> struct fields:**

| XML attr | FRUIT_INFO offset | Role |
|----------|-------------------|------|
| `scale` | loaded into scale field | Visual scale (x0.01) |
| `collision` | +0x244 | Collision radius factor |
| `chance` | +0x... | Spawn probability |
| `colour` | +0x... | Juice/splat RGBA |

**Fruit::Init call from MenuButton:**
```c
// MenuButton::Init (0x14ee40):
entity = ActorManager::Add(fruitType >= bombThreshold ? 1 : 0, true);
entity->pos = button.pos;
entity->vel = globalScale;  // written to +0x1c..+0x24 (velocity, not scale!)
entity->Init(0, fruitType, NULL);  // scale param = NULL -> 1.0
// After Init -> SetFruitType computes: scale = FruitInfo[type].scale * 0.01
entity->scale *= 0.2;  // DAT_0014f194 — shrink for menu display
```

### Fruit::EnableCollision (0x00176354, 36 lines)

```c
void Fruit::EnableCollision(bool enable) {
    FRUIT_INFO* info = &fruitInfoArray[m_FruitType];
    float radius = info->m_SpeedMult + CONST_C * info->m_Scale;
    
    if (enable && radius > 0.0) {
        if (!this->m_Col) {
            this->m_Col = new ColSphere();
        }
        this->m_Col->pos = Vec3(this->pos_x, this->pos_y, CONST_Z);
        this->m_Col->radius = radius;  // note: no extra scale factor
    } else {
        // Disable collision
        if (this->m_Col) { delete this->m_Col; this->m_Col = NULL; }
    }
}
```

Same radius formula as `SetFruitType` but without the spawn `scale` multiplier. Used to toggle collision on/off during gameplay (e.g., after slicing, during bomb freeze).

### Fruit::Release (0x001761d8)

```c
void Fruit::Release() {
    if (m_pEmitter1) { PSPParticleManager::ClearEmitter(m_pEmitter1); m_pEmitter1 = NULL; }
    if (m_pEmitter2) { PSPParticleManager::ClearEmitter(m_pEmitter2); m_pEmitter2 = NULL; }
    // Clear backref: if m_pSlashEntity points to a SlashEntity whose +0x134 == this
    if (m_pSlashEntity && *(Fruit**)(m_pSlashEntity + 0x134) == this)
        *(int*)(m_pSlashEntity + 0x134) = 0;
    Entity::Release();
}
```

### Fruit::DrawUpdate (0x0017501c)

```c
void Fruit::DrawUpdate(float dt) {
    m_RotAxis *= DAT_damping;  // dampen rotation axis each frame
    
    if (!m_bSliced && m_ChuckDelay <= 0) {
        if (m_Gravity_x == 0.0) {
            // Vertical gravity: bounce off left/right screen edges
            // Zen mode with flag: hard bounce (reflect vel_x)
            // Normal: soft push (vel_x += dt * 16.0, rotAxis += 20.0)
        } else if (m_Gravity_y == 0.0) {
            // Horizontal gravity: bounce off top/bottom edges
        }
    }
}
```

### Fruit::Fruit() (0x1764dc, 0x176520 — two identical constructors, one thunked)

```
Entity::Entity(this)
this->vtable = &Fruit_vtable + 8
Quaternion::Identity(&this->m_Rot1)
Quaternion::Identity(&this->m_Rot2)
this->m_pEmitter2 = NULL
this->m_Col = NULL
this->field_0x108 = 0      // slash entity backref
this->field_0x10c = 0      // special gravity flag
this->m_pEmitter1 = NULL
```

### Fruit::~Fruit() (0x176424)

```
this->vtable = &Fruit_vtable + 8
Release(this)
Entity::~Entity(this)
```

### Fruit::IsOffscreen (0x175624)

```
// Returns true if fruit position is outside screen bounds (with scale margin)
if (abs(m_Gravity_y) > 0):
    bound = SCREEN_HALF_H + SCREEN_MARGIN * m_Scale_y
    if (pos_y < -bound || pos_y > bound) return true
    check m_HalfB_pos.y against same bounds
else if (abs(m_Gravity_x) > 0):
    bound = SCREEN_HALF_W + SCREEN_MARGIN * m_Scale_y
    if (pos_x < -bound || pos_x > bound) return true
    check m_HalfB_pos.x against same bounds
else: return false
```

### Fruit::CheckHasGoneOffscreen (0x00175218, 128 lines)

```
// Complex offscreen check for sliced fruit halves with bounce-back behavior
// For each gravity axis (X or Y), checks if both halves have gone past screen edge
// Sliced halves that pass the far edge get their position clamped and velocity reversed (-1.0)
// Only calls return (skip kill) if one half is still moving inward
// Also checks perpendicular axis bounds for sliced halves
// If both halves are confirmed offscreen, execution falls through (caller KillFruits)
```

### Fruit::IsActive (0x0017a82c, 10 lines)

| Address | Signature |
|---------|-----------|
| 0x0017a82c | `bool Fruit::IsActive()` — returns true if entity is alive (stub in decompilation) |

```
return (m_ChuckDelay <= 0.0)   // active once chuck delay has expired
```

### Fruit::RotateFacingUp (0x1757f4)

```
// Sets initial rotation for both halves (loop x2)
speed = Random::RandF(2.0)
sign = (Random::Rand32(2) == 0) ? 1.0 : -1.0
rotSpeed = sign * (sign + sign + speed)
for i in [0, 1]:
    RandomStartAngle(&m_Rot[i], true)    // identity + axis rotation
    if (facingUp):
        // Build quaternion from axis, then compose: slice_quat * facing_quat * start_quat
        CreateFromAxisAngle(axis.x, axis.y, axis.z, 0)
        compose with facing direction quaternion
        m_Rot[i] = composed result
    m_RotVel[i] = axis * rotSpeed
```

### Fruit::UpdateBombAvoidance (0x00175988)

```c
void Fruit::UpdateBombAvoidance(float dt) {
    if (m_bSliced) return;
    // Iterate all active bombs
    for (Bomb* bomb : ActorManager::GetEntities(BOMB_TYPE)) {
        if (!bomb->IsActive() || !bomb->m_Col) continue;
        Vec3 delta = pos - bomb->pos;
        float distSq = delta.MagnitudeSqr();
        if (distSq < THRESHOLD_DIST_SQ) {
            float relVelSq = (vel - bomb->vel).MagnitudeSqr();
            if (relVelSq < THRESHOLD_VEL_SQ) {
                float dir = (delta.x < 0) ? -1.0 : 1.0;
                bomb->vel_x += dir * dt * 12.0;  // push bomb away
            }
        }
    }
}
```

### Fruit::SetTrailParticles (0x001756dc)

```c
int Fruit::SetTrailParticles(ulong particleHash) {
    if (!PSPParticleManager::EmitterExists(particleHash)) return 0;
    if (m_pEmitter1) PSPParticleManager::ClearEmitter(m_pEmitter1);
    m_pEmitter1 = PSPParticleManager::AddEmitter(particleHash, NULL, true);
    return 1;
}
```

### Fruit::SetForPlayer (0x00175b78)

```c
void Fruit::SetForPlayer(int playerIdx) {
    if (IsOnlineMultiplayer() && playerIdx == 2)
        m_Col->radius *= ONLINE_RADIUS_SCALE;  // larger hitbox for spectator
    m_PlayerIdx = playerIdx;
}
```

### Fruit::AddShadow (0x175ea0)

```
// Determine shadow offset direction for multiplayer
if (playerIdx >= 1 && IsSameScreenMultiplayer()):
    offsetX = (pos_x < 0) ? -1.0 : 1.0; offsetY = SHADOW_OFFSET
else:
    offsetX = 1.0; offsetY = SHADOW_OFFSET
// Pre-slice shadow (m_ScaleAnim < 1.0):
    alpha = (1.0 - m_ScaleAnim) * SHADOW_ALPHA_SCALE
    size = SHADOW_SIZE_MULT * m_Scale_x
    AddQuad(buf, pos_x + offsetX*size*X_MULT, pos_y + offsetY*size*X_MULT, size, size, rgba)
    count++
// Post-slice shadows (m_ScaleAnim > 0.0): for each half
    alpha = m_ScaleAnim * POST_ALPHA_SCALE
    size = m_Scale_x * POST_SIZE_MULT
    // Transform shadow offset by half's rotation quaternion
    AddQuad for half A at pos + rotated_offset
    AddQuad for half B at m_HalfB_pos + rotated_offset
    count += 2
```

### Fruit::CheckFruitDropped (0x176184)

```
// Check game-mode-specific drop counters to trigger GameOver
lives = g_GameState->lives
if (lives[player1] < 1):
    if (lives[player2] < 1): return     // both dead already
    GameOver(-1, -1.0, 2)               // player 2 survives
elif (lives[player2] < 1):
    GameOver(-1, -1.0, 1)               // player 1 survives
else:
    GameOver(-1, -1.0, 0)               // both alive, normal game over
```

### Fruit::LoadInfo (0x0017987c, 530 lines)

| Address | Signature |
|---------|-----------|
| 0x0017987c | `int Fruit::LoadInfo()` |

See [data.md](../structs/data.md) for full FRUIT_INFO layout and XML schema.

```
if (FruitInfoArray already loaded) return
// Load shadow texture if fast hardware
// Parse "Config/Fruit.xml" with TinyXML
// Read <settings> element: colour, maxFruit, gridSize, combo thresholds, scale factors
// Read <gravity> element: gravityX, gravityY
// Count <fruit> elements -> numFruitTypes
// Allocate FRUIT_INFO[numFruitTypes]
for each <fruit> element:
    name = attribute("name") -> copy to info.name[0x00]
    StringHash(name) -> info.nameHash[0x250], info.altHash[0x254]
    Build hash keys: "%s_sliced", "%s_missed", "%s_title", etc.
    Load textures: "ui/fruit_%s", "ui/fruit_%s_small"
    Read attributes: display_name, shortname, model_name, fact_texture, model_prefix
    Parse colour attribute -> info.m_FruitColour[0x240] (RGBA from comma-separated ints)
    Parse factcolour -> info.m_FactColour[0x2F8]
    Read floats: scale, collision_extra, size_mult
    Read ints: weight, baseScore, unlock_cost, special_cost
    Read bools: has_double_juice, is_scorable, is_visible
    // Parse <fact> child elements -> allocate info.m_pFacts (0x100 bytes each)
    // Parse <sound> child elements -> allocate ImpactSound array with weights
    // Parse <power> child elements -> allocate FRUIT_POWERS with weighted FRUIT_POWER entries
// After parsing: call LoadFruitModels()
```

---

## Fruit::RandomFruit (0x00176564, 113 lines) — FULLY DECOMPILED

Weighted random fruit selection using cumulative weight tables from FRUIT_INFO[].

### Static Data (lazy-initialized once)

```
+0x10: int totalWeight         // sum of all FRUIT_INFO[].chance
+0x14: int totalWeight_avail   // sum for fruits with hitInfluence < 1
+0x18: int totalWeight_onSide  // sum for fruits with onSide == true
+0x1c: int totalWeight_onSide_avail  // sum for onSide && hitInfluence < 1
```

Per FRUIT_INFO (at 0x330-byte stride):
```
+0x308: int   chance              // spawn weight
+0x30c: int   cumulativeChance    // running total (all)
+0x310: int   cumulativeOnSide    // running total (onSide only)
+0x318: bool  onSide              // can appear on side spawns
+0x328: int   hitInfluence        // if < 1: fruit is "available" (not recently hit)
```

### Algorithm

```c
int Fruit::RandomFruit(bool includeOnSideOnly) {
    // 1. Build cumulative weight tables (once, lazy init)
    if (totalWeight < 1) {
        int cumAll = 0, cumAvail = 0, cumOnSide = 0, cumOnSideAvail = 0;
        for (int i = 0; i < fruitCount; i++) {
            FRUIT_INFO* fi = &fruitInfos[i];
            cumAll += fi->chance;
            fi->cumulativeChance = cumAll;        // +0x30c
            
            if (fi->hitInfluence < 1)             // "available" = not recently sliced
                cumAvail += fi->chance;
            
            if (fi->onSide) {                     // +0x318
                cumOnSide += fi->chance;
                if (fi->hitInfluence < 1)
                    cumOnSideAvail += fi->chance;
            }
            fi->cumulativeOnSide = cumOnSide;     // +0x310
        }
        totalWeight = cumAll;
        totalWeight_avail = cumAvail;
        totalWeight_onSide = cumOnSide;
        totalWeight_onSide_avail = cumOnSideAvail;
    }
    
    // 2. Select based on critical mode x includeOnSideOnly (4 paths)
    bool critical = WaveManager::CriticalMode(0);
    Random* rng = &WaveManager::GetInstance()->random;
    
    if (!critical) {
        if (includeOnSideOnly) {
            // Path A: Normal + all fruits (includeOnSide)
            uint roll = Rand32(rng, totalWeight);
            for (int i = 0; i < fruitCount; i++) {
                if (roll < fruitInfos[i].cumulativeChance)  // +0x30c
                    return i;
            }
        } else {
            // Path B: Normal + available only (skip recently-hit)
            uint roll = Rand32(rng, totalWeight_avail);
            int cumulative = 0;
            for (int i = 0; i < fruitCount; i++) {
                if (fruitInfos[i].hitInfluence < 1) {       // +0x328 < 1
                    cumulative += fruitInfos[i].chance;      // +0x308
                    if (roll < cumulative)
                        return i;
                }
            }
        }
    } else {
        if (includeOnSideOnly) {
            // Path C: Critical + onSide only
            uint roll = Rand32(rng, totalWeight_onSide);
            for (int i = 0; i < fruitCount; i++) {
                if (roll < fruitInfos[i].cumulativeOnSide)  // +0x310
                    return i;
            }
        } else {
            // Path D: Critical + onSide + available
            uint roll = Rand32(rng, totalWeight_onSide_avail);
            int cumulative = 0;
            for (int i = 0; i < fruitCount; i++) {
                if (fruitInfos[i].hitInfluence < 1 && fruitInfos[i].onSide) {
                    cumulative += fruitInfos[i].chance;
                    if (roll < cumulative)
                        return i;
                }
            }
        }
    }
    
    // Fallback: random index from [0, fruitCount-1)
    return Rand32(rng, fruitCount - 1);
}
```

### 4 Selection Paths

| Critical | includeOnSideOnly | Weight Pool | Filter |
|----------|------------------|-------------|--------|
| No | Yes | totalWeight (all) | All fruits, use cumulativeChance |
| No | No | totalWeight_avail | Skip fruits with hitInfluence >= 1 |
| Yes | Yes | totalWeight_onSide | Only onSide fruits, use cumulativeOnSide |
| Yes | No | totalWeight_onSide_avail | onSide AND hitInfluence < 1 |

### Key Details

- **Lazy init**: Weight tables are built on first call and cached
- **hitInfluence filter**: `+0x328 < 1` excludes recently-sliced fruits from "available" pools, adding variety
- **Critical mode**: When active, only `onSide` fruits are eligible (typically larger fruits that look good on screen edges)
- **Fallback**: If no fruit matches (shouldn't happen), picks uniformly from `[0, fruitCount-1)`
- **RNG**: Uses `WaveManager`'s embedded `Math::Random` instance
- **16 fruit types**: apple, banana, orange, watermelon, strawberry, kiwifruit, pineapple, plum, pear, mango, apple_red, lime, dragon, coconut, passionfruit, lemon

---

## Static / Free Functions

### Fruit_DrawShadows (0x00178f28, static)

Iterates all fruit entities, calls `AddShadow()` for each with scale > 0.
Renders as a single batched triangle strip using the shadow texture.

```
if (!g_ShadowTexture) return
quadBuf[18432]   // stack buffer for shadow quads
count = 0; ptr = quadBuf
for each Fruit in ActorManager(type=0):
    if (fruit->m_Scale_x > 0): fruit->AddShadow(&ptr, &count)
MatrixManager::Reset(world); UploadMatrices()
Texture::Set(g_ShadowTexture)
Mesh::DrawTriStrip(quadBuf, count*6 - 1)
Texture::UnSet(g_ShadowTexture)
```

### Fruit::ClearUnspawned (0x00176d14, static)

```c
void ClearUnspawned(bool killAll) {
    for (Fruit* f : ActorManager::GetEntities(FRUIT_TYPE)) {
        if (killAll || f->m_ChuckDelay > 0)
            f->KillFruit(false);
    }
}
```

### FruitTypeName (0x174f18)

```
return &FruitInfoArray[type].name   // offset 0x000 in FRUIT_INFO (0x330 stride)
```

### FruitTypeHash (0x174f38)

```
return FruitInfoArray[type].nameHash   // offset 0x250 in FRUIT_INFO
```

### FruitFactTexture (0x174f5c)

```
return &FruitInfoArray[type].factTexturePath   // offset 0x278 in FRUIT_INFO
```

### FruitTypeColour (0x174f80)

```
if (g_SpecialFruitType == type): return g_SpecialFruitColour
return FruitInfoArray[type].m_FruitColour       // offset 0x240 in FRUIT_INFO
```

### FruitFactColour (0x174fc8)

```
return Colour(FruitInfoArray[type].m_FactColour)  // offset 0x2F8 in FRUIT_INFO
```

### FruitInfo (0x174ff8)

```
return &FruitInfoArray[type]   // base + type * 0x330
```

### SetupLighting (0x175018)

```
return param   // no-op stub (lighting not used on Bada)
```

### GetSmallestDelta (0x1751bc)

```
delta = a - b
if (abs(delta) > 180.0):   // DAT_00175210
    if (a <= b): delta = (a + 360.0) - b
    else: delta = delta - 360.0
return delta
```

### AnyActivePowers (0x175714)

```
for i in 0..this->m_Count:
    if (PowerUpManager::GetActiveSingle(powers[i].hash) != 0): return true
return false
```

### RandomStartAngle (0x175740)

```
if (facing):
    Quaternion::Identity(q)
    CreateFromAxisAngle(q, -1, 0, 0, 0xCE2C)   // face camera
else:
    axis = Vec3(RandF(2)-1, RandF(2)-1, RandF(2)-1).Normalise()
    angle = Rand32(0xFF3A)
    Quaternion::Identity(q)
    CreateFromAxisAngle(q, axis, angle)
```

### GetNumActiveForPlayer (0x175928)

```
count = 0
for each Fruit in ActorManager(type=0):
    if (countByPlayer):
        if (fruit->m_PlayerIdx == player): count++
    else:
        if (!Fruit::IsActive(fruit)): count++   // count inactive
return count
```

### FruitType (0x175b10)

```
if (name && *name):
    hash = StringHash(name)
    for i in 0..numFruitTypes:
        if (FruitInfo[i].nameHash == hash || FruitInfo[i].altHash == hash): return i
if (randomFallback):
    return WaveManager::Random::Rand32(numFruitTypes - 1)
return -1
```

### GetFact (0x175ba4)

```
// Returns a fruit fact string for UI display
if (fruitType < 0): fruitType = Random(numFruitTypes)
fruitType = clamp(fruitType, 0, numFruitTypes - 2)
// Lazy-init: resolve "dragonfruit" and alt fruit type indices
if (fruitType == dragonfruitIdx): fruitType = altFruitIdx
if (outType) *outType = fruitType
if (factIdx < 0):
    if (FruitInfo[fruitType].m_FactCount < 1): return GetFact(outType, outFactIdx, -1, -1)  // retry
    increment "fruit_facts_seen" counter in FruitSaveData
    increment per-fruit fact counter
    factIdx = (counter - 1) % factCount
factIdx = clamp(factIdx, 0, factCount - 1)
if (outFactIdx) *outFactIdx = factIdx
// Skip any facts equal to "UNUSED" string, wrap around
return FruitInfo[fruitType].m_pFacts[factIdx]  // 0x100-byte strings
```

### AddQuad (0x175db0)

```
// Build 6-vertex tri-strip quad at (x,y) with size (w,h) and colour c
// Vertices: (x-w,y-h), (x-w,y+h), (x+w,y-h), (x+w,y+h) with UVs and platform colour
// Advances *buf pointer by 0xD8 bytes (6 * 0x24 per vertex)
```

### MakeSFXDelegate_Fruit (0x176a90)

```
// Construct a Delegate1<bool, MortarSound*> for SFX callbacks
BaseDelegate::BaseDelegate(out)
out->funcPtr = FRUIT_SFX_CALLBACK
out->vtable = &FruitSFXDelegate_vtable + 8
```

### CleanupFruit (0x179010)

```
// Full cleanup: null textures, destroy model array, destroy FRUIT_INFO array
SetNull(shadowTex_halved, shadowTex_alt, shadowTex_main, shadowTex_extra)
SetNull(splashTex, splashTex2, bgTex)
if (modelsLoaded):
    for slot in [0..2]: for each type: SmartPtr::SetNull(models[type][slot])
    delete[] FruitModelInfo array
    delete[] FRUIT_INFO array
    modelsLoaded = false
```

### DestroyFruitModels (0x17911c)

```
if (modelsLoaded):
    for slot in [0..3]: for each type: SmartPtr::SetNull(models[type][slot])
    delete[] FruitModelInfo array
    SetNull(shadowTex_halved, shadowTex_alt)
    modelsLoaded = false
```

### LoadFruitModels (0x1794e0)

```
if (modelsLoaded) return
// Allocate FruitModelInfo[numFruitTypes]
for each fruit type:
    for part in [half_A, half_B]:  // 2 iterations
        path = sprintf("models/%c%s_%d", name[0], name+1, part+1)
        model = MeshManager::Load(path)
        models[type][part+4] = model    // slots 4,5 = halfA,halfB
        effectProp[part] = Geometry::GetProperty(model, "diffuse")
        SetupLighting(model)            // no-op
    // Try load whole model and multiplayer model if file exists
    sprintf("models/%s_whole", name) -> models[type][6]
    sprintf("models/%s_mp", name) -> models[type][7]
// Extract base diffuse texture from first model
shadowTex = EffectProperty::TryGetValue(models[0].effectPropA, 0)
shadowTex_alt = shadowTex   // copy
modelsLoaded = true
```

---

## Data / Constructor Functions

### FRUIT_INFO::FRUIT_INFO() (0x17ae20)

```
Colour::init(m_FruitColour)
Colour::init(m_FactColour)
SmartPtr::init(m_pFruitTexture, m_pFruitTexture2)
field_0x310 = 0; m_pPowers = NULL; m_BaseScore = 1
m_SoundCount = 0; m_pSounds = NULL
m_RandBonusBase = 0; m_RandBonusMax = 0
field_0x308 = 0; field_0x30c = 0
m_SizeMult = 0.75
SmartPtr::SetNull(m_pFruitTexture, m_pFruitTexture2)
m_bSpecial = 0; m_bScorable = 1; field_0x2fc = 0; field_0x26c = 0
m_FactCount = 0; m_pFacts = NULL
```

### FRUIT_INFO::~FRUIT_INFO() (0x17ad7c)

```
SetNull(m_pFruitTexture2, m_pFruitTexture)
if (m_pFacts) { delete[] m_pFacts; m_pFacts = NULL }
if (m_pSounds) { foreach ImpactSound: dtor(); delete[] m_pSounds }
if (m_pPowers) { FRUIT_POWERS::dtor(m_pPowers); delete m_pPowers }
~SmartPtr(m_pFruitTexture2, m_pFruitTexture)
```

### FRUIT_POWER::FRUIT_POWER() (0x17a7cc)

```
m_PowerHash = 0; m_Weight = 100; m_CumulativeWeight = 0
```

### FRUIT_POWERS::FRUIT_POWERS() (0x17a824)

```
m_Count = 0
```

### FRUIT_POWERS::~FRUIT_POWERS() (0x17ace0)

```
if (m_pArray) { delete[] m_pArray; m_pArray = NULL }
```

### FRUIT_POWERS::RandomPower() (0x17a7d8)

```
roll = Random::Rand32(powers[count-1].cumulativeWeight)
for i in 0..count:
    if (roll < powers[i].cumulativeWeight): return powers[i].powerHash
```

### ImpactSound::ImpactSound() (0x17a7c0)

```
m_CumulativeWeight = 0; m_Weight = 10; m_SoundName = NULL
```

### ImpactSound::~ImpactSound() (0x17accc)

```
if (m_SoundName) { delete[] m_SoundName; m_SoundName = NULL }
```

### FruitModelInfo::FruitModelInfo() (0x17aeb8)

```
SmartPtr::init(m_pHalfModelA, m_pHalfModelB, m_pWholeModel, m_pMultiplayerModel)
field_0x20 = 0
SetNull all model ptrs; zero all effect property ptrs
```

---

## Internal Helper Functions

### RandFloat_Scaled_Fruit (0x17c8a4)

```
return (Rand32(RAND_MAX) / (float)RAND_MAX) * scale
```

### ZeroInit_Fruit (0x176a7c)

```
*(int*)p = 0; return 0
```

### ZeroInitPassthru_Fruit (0x176a84)

```
ZeroInit_Fruit(p); return p
```

### SmartPtrNull_Tex2D_Fruit (0x178f18)

```
SmartPtr::SetPtr(p, NULL)
```

---

## See Also

- [Mortar::Entity base struct](entity-base.md) — Entity base class layout, CreateEntity factory
- [FRUIT_INFO data struct](../structs/data.md) — FRUIT_INFO (0x330 bytes per type), XML schema
- [Physics system](../systems/physics.md) — Full fruit physics integration details
