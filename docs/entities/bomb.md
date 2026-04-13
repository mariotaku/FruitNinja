# Bomb Entity

Entity type 1. Vtable base: 0x001EA478 (13 entries).

## Bomb : Entity (size = 0xB0 / 176 bytes)

<!-- Analysed: 2026-04-10T11:00 -->

Verified from `CreateEntity`: `operator_new(0xB0)`. Ghidra struct: `/FruitNinja/Bomb`

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00–0x3b | Entity | base | Entity base class (60 bytes): vtable, field_0x04, m_TrackerID, flags, pos, vel, scale, angle, m_Col |
| +0x3c | float | m_SpawnTimer | Counts down; spawns BombBlast (type 4) at 0; Init = 0.6 (DAT_001726ac) |
| +0x40 | Delegate0\<void\> | hitCallback | Callback for menu bomb delegate (36 bytes) |
| +0x64 | int | m_BombVariant | 0 = normal, 2 = multiplayer (selects model variant) |
| +0x68 | byte | m_bHit | 0 = in-flight, 1 = hit/slashed |
| +0x6c | float | m_ZPosition | From GetBombZPosition(); Z layer ordering |
| +0x70 | short | m_RotVelX | Rotation velocity X; Random 1..8 |
| +0x72 | short | m_RotVelY | Rotation velocity Y; Random 1..8 |
| +0x74 | short | m_RotX | Current rotation X (accumulated per frame) |
| +0x76 | short | m_RotY | Current rotation Y (accumulated per frame) |
| +0x78 | byte | m_bCollisionGuard | Prevents double-hit in CollisionResponse |
| +0x7c | PSPParticleEmitter* | m_pEmitter | Fuse particle trail; NULL until first draw |
| +0x80 | byte | m_bMovement | 1 = physics enabled |
| +0x84 | int | field_0x84 | Backref to Game/TaskState object |
| +0x88 | byte | m_bMenuBombHit | 0 = normal hit, 1 = menu/zen hit |
| +0x8c | Vec3 | m_AccelForce | Gravity/acceleration; Init = (0.0, -12.0, 0.0) |
| +0x98 | Vec3 | m_OrigScale | Backup of scale from Init |
| +0xa4 | float | m_Countdown | Chuck delay timer; 0 = ready to spawn |
| +0xa8 | float | m_SpeedMult | Speed multiplier; 1.0 normal, 0.666 for fat bombs |
| +0xac | float | field_0xac | Z constant; Init = 0.0 |

## Bomb Vtable

| Index | Address | Method | Notes |
|-------|---------|--------|-------|
| [0] | 0x001717b4 | ~Bomb (non-deleting) | Calls Release, then ~Delegate, ~Entity |
| [1] | 0x0017180c | ~Bomb (deleting) | Calls Release, then ~Delegate, ~Entity, operator_delete |
| [2] | 0x00172504 | Bomb::Init | `void Init(void* p1, long type, Vec3* scale)` |
| [3] | 0x00171764 | Bomb::Release | Clears emitter, unlinks from Game |
| [4] | 0x001729fc | Bomb::Update | `void Update(float dt)` -- 195 lines |
| [5] | 0x00171be8 | Bomb::Draw | Renders bomb mesh with rotation |
| [6] | 0x001714e4 | Bomb::DrawUpdate | Updates fuse particle emitter position |
| [7] | 0x0019d600 | Entity::OnAdd | (base class) |
| [8] | 0x0019d800 | Entity::OnRemove | (base class) |
| [9] | 0x0017280c | Bomb::CollisionResponse | `int CollisionResponse(Entity*, ulong, ulong, Vec3*)` |
| [10] | 0x0019d608 | Entity::vtable[10] | (base class) |
| [11] | 0x0019d61c | Entity::vtable[11] | (base class) |
| [12] | 0x00172f4c | Entity::ListenerCallback | (base class, empty) |

---

## Bomb Instance Methods

### Bomb::Bomb (0x00171678) -- default constructor

`Bomb * __thiscall Bomb::Bomb(Bomb *this)`

```
Bomb::Bomb() {
    Entity::Entity();           // base class init
    vtable = &Bomb_vtable;      // set vtable to Bomb
    Delegate0<void>::Delegate0(&field_0x40);  // init hit callback delegate
    m_pEmitter = NULL;          // no fuse particle
    field38_0x64 = 0;           // bomb variant (0=normal)
    m_Col = NULL;               // no collision sphere yet
    field_0x84 = 0;             // game state backref
}
```

### Bomb::Bomb (0x001716b0) -- in-place constructor

Identical to 0x00171678. Used for in-place new (different GOT-relative addressing).

### ~Bomb (0x001717b4) -- non-deleting destructor

`Bomb * __thiscall ~Bomb(Bomb *this)`

```
~Bomb() {
    vtable = &Bomb_vtable;       // reset vtable (C++ dtor pattern)
    Bomb_Release();              // free emitter, unlink from game
    Delegate0<void>::~Delegate0(&field_0x40);
    Entity::~Entity();
}
```

### ~Bomb (0x0017180c) -- deleting destructor

Same as non-deleting + calls `operator_delete(this)` at the end.

### ~Bomb (0x0017185c) -- non-deleting destructor (variant)

Same logic as 0x001717b4, different GOT-relative base. Used by different call sites.

---

### Bomb::LoadContent (0x001726c8 thunk → 0x001726e8)

<!-- Analysed: 2026-04-10T12:00 -->

`void Bomb::LoadContent(void)` — static, called once from `GameInitialise` (0x10c41a).

Loads bomb models, textures, and particle hashes into a **global struct** at GOT+0x464A0. Guarded by a loaded flag at +0x28. `Bomb::Draw` reads the pre-loaded models from this global.

```c
void Bomb::LoadContent() {
    if (g_bombData->loadedFlag) return;  // +0x28 guard

    // Model 0 (normal): "models/Fruit/Bomb.mmd" (0x1BCBDB)
    g_bombData->model[0] = MeshManager::Load("models/Fruit/Bomb.mmd");        // +0x0C

    // Fuse particle hash (normal): StringHash("bomb_smoke") (substring at 0x1BCC22)
    g_bombData->fuseHash[0] = StringHash("bomb_smoke");                        // +0x2C

    // Model 1 (purple/multiplayer): "models/Fruit/Bomb_purple.mmd" (0x1BCBF1)
    g_bombData->model[1] = MeshManager::Load("models/Fruit/Bomb_purple.mmd"); // +0x10

    // Texture: "minus_10.tex" (0x1BCC0E) — zen mode -10 score indicator
    g_bombData->tex = LoadLocalisedTexture("minus_10.tex");                    // +0x24

    // Fuse particle hash (purple): StringHash("purple_bomb_smoke") (0x1BCC1B)
    g_bombData->fuseHash[1] = StringHash("purple_bomb_smoke");                 // +0x30

    // Setup lighting on both models
    for (int i = 0; i < 2; i++) {
        if (IsValid(g_bombData->model[i]))
            SetupLighting(g_bombData->model[i]);
    }

    g_bombData->loadedFlag = 1;  // +0x28
}
```

**BombGlobalData** struct (Ghidra: `/FruitNinja/BombGlobalData`, 52 bytes at GOT+0x464A0):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | int | field_0x00 | unknown |
| +0x04 | SmartPtr\<Texture\> | tex_02 | (from CleanupBomb) |
| +0x08 | int | field_0x08 | unknown |
| +0x0C | SmartPtr\<Model\>[3] | m_bombModel | Draw indexes as `m_bombModel[m_BombVariant]`; [0]=Bomb.mmd, [1]=Bomb_purple.mmd, [2]=unused? |
| +0x18 | SmartPtr\<Texture\> | tex_01 | (from CleanupBomb) |
| +0x1C | int | field_0x1c | unknown |
| +0x20 | int | field_0x20 | unknown |
| +0x24 | SmartPtr\<Texture\> | tex_minus10 | `minus_10.tex` (zen mode -10 score indicator) |
| +0x28 | byte | loadedFlag | 0=not loaded, 1=loaded (LoadContent guard) |
| +0x2C | uint | fuseHash_normal | StringHash("bomb_smoke"); particle emitter hash |
| +0x30 | uint | fuseHash_purple | StringHash("purple_bomb_smoke") |

---

### Bomb::Init (0x00172504)

`void __thiscall Bomb::Init(Bomb *this, void *p1, long type, Vec3 *scale)`

Sets up a newly spawned bomb entity:

```c
void Bomb::Init(void *p1, long type, Vec3 *scale) {
    if (m_Col == NULL)
        m_Col = new ColSphere();  // size 0x18

    float scaleFactor = (scale != NULL) ? scale->x : 1.0f;

    // Position collision sphere at bomb pos, Z = 0.0
    m_Col->center = Vec3(pos_x, pos_y, 0.0f);       // DAT_001726a8 = 0.0
    m_Col->radius = FRUIT_INFO->colSize * 0.5f * scaleFactor;

    // Lazy-load bomb texture if not cached
    if (!g_BombTexture)
        g_BombTexture = TextureManager::LoadLocalisedTexture("bomb_explode.tex");

    // Initial state
    field_0xac = 0.0f;          // Z constant
    m_SpawnTimer = 0.6f;        // DAT_001726ac = 0.6 (BombBlast spawn delay)
    field38_0x64 = 0;           // bomb variant (0=normal, 2=multiplayer)
    field_0x78 = 0;             // collision guard
    activeFlag = 0;             // not yet hit
    flags = (flags & ~0x10) | 0x02;  // clear killed, set has-collision
    movementFlag = 1;           // physics enabled
    speedMult = 1.0f;

    // Random rotation for each axis (2 axes: RotX/RotY at +0x70/+0x72, RotVelX/RotVelY at +0x74/+0x76)
    for (int i = 0; i < 2; i++) {
        m_RotVel[i] = Random::Rand32(7) + 1;    // 1..8
        m_Rot[i]    = Random::Rand32(0x167);     // 0..359
    }

    field_0x84 = 0;             // game state backref
    m_bBombFlag88 = 0;          // not menu-hit
    m_pEmitter = NULL;           // no fuse particle yet

    // Scale: globalScaleVec × bombVisualScale × 0.01 × scaleFactor
    // bombVisualScale (55.0) comes from fruitlist.xml: <bomb size="55" collision="35"/>
    // Parsed by Fruit::LoadInfo (0x0017987c) into globalBombConfig at BSS 0x001F43B8
    // Formula: 2.75 × 55 × 0.01 = 1.5125 (pre-menu scale) × scaleFactor
    Vec3 computedScale = globalConfig * scaleFactor;
    countdown = 0.0f;           // no delay by default
    field_0x28 = computedScale; // scale vector (for rendering)
    field_0x98 = computedScale; // original scale backup
    accelForce = Vec3(0.0f, -12.0f, 0.0f);  // gravity
    field_0x6c = GetBombZPosition();  // Z layer cycling
}
```

---

### Bomb::Update (0x001729fc)

`void __thiscall Bomb::Update(Bomb *this, float dt)`

~195 lines. Core bomb update loop.

```c
void Bomb::Update(float dt) {
    float scaledDt = dt * speedMult;
    float dtNorm = scaledDt / 0.01667f;  // DAT_00172c98 = 1/60 normalize

    if (activeFlag == 0) {
        // === ALIVE BOMB ===
        if (countdown > 0) {
            // If game-over or paused: kill bomb immediately
            if (Game.bombTimer > 0 || Game.gameOverFlag) {
                countdown = 0.0f;
                pos_y = -320.0f;  // DAT_00172cb0
                vel = Vec3(0, -1, 0);
                return;
            }
            // Tick down countdown using game dt (not entity dt)
            if (!Game.paused)
                countdown -= Game.dt;

            // Play fuse SFX when crossing 0.2s threshold
            if (countdown <= 0.2f && prev_countdown > 0.2f && !fusePlayedFlag) {
                SoundManager::PreLoadSound("bomb_fuse_preload");
                GameSound::SFXPlay("bomb_fuse", 1.0, 1.0, ...);
                fusePlayedFlag = true;
            }
            if (countdown > 0) return;

            // Chain bomb spawning based on spawnLevel
            float level = WaveManager.spawnLevel;
            int spawnCount = (int)level;
            // Fractional chance for +1
            if ((float)spawnCount + 0.01f < level) {
                if (Random::Rand32(100) < (level - (float)spawnCount) * 100.0f)
                    spawnCount++;
            }
            if (spawnCount < 1) {
                // No spawn: kill bomb
                countdown = 0.0f; pos_y = -320.0f;
                vel = Vec3(0, -1, 0);
            } else if (spawnCount != 1) {
                WaveManager::SpawnBomb(spawnCount - 1, 0, NULL, ...);
            }
        }

        // Physics: velocity += accelForce * scaledDt
        if (movementFlag) {
            vel += accelForce * scaledDt;
            // If vel and accel aligned, grow acceleration
            if (accelAligned(vel, accelForce)) {
                float len = Normalise(accelForce);
                accelForce *= len + 0.2f * dtNorm * 2;  // DAT_00172f30 = 0.2
            }
        }
        pos += vel * scaledDt;

        // Rotation animation
        if (scaledDt > 0) {
            m_RotX += m_RotVelX;
            m_RotY += m_RotVelY;
        }

        // Update collision sphere
        m_Col->center = Vec3(pos_x, pos_y, pos_z);
        m_Col->center.z = 0.0f;  // clamp Z

    } else {
        // === HIT BOMB ===
        if (m_bBombFlag88 == 0) {
            // Non-menu hit: spawn BombBlast entities
            m_SpawnTimer -= Game.dt;
            if (m_SpawnTimer < 0) {
                BombBlast *blast = ActorManager::Add(4, true);
                blast->pos = this->pos;
                blast->Init(0, 0, 0);          // via vtable
                m_SpawnTimer = 0.05f;           // DAT_00172c9c = 0.05
            }
        } else {
            // Menu-hit: continue physics (bomb falls away)
            if (movementFlag) {
                vel += accelForce * scaledDt;
                if (accelAligned(vel, accelForce)) {
                    float len = Normalise(accelForce);
                    accelForce *= len + 0.2f * dtNorm * 2;
                }
            }
            pos += vel * scaledDt;
            if (scaledDt > 0) {
                m_RotX += m_RotVelX;
                m_RotY += m_RotVelY;
            }
        }
        // Move collision offscreen when hit
        m_Col->center = Vec3(1000.0f, 1000.0f, 0.0f);  // DAT_00172ca4 = 1000.0
        m_Col->radius = 0.01f;                           // DAT_00172cac = 0.01
    }

    // Out of bounds check: Y in [-240..240], X in [-360..360]
    if (pos_y <= -240.0f || pos_y >= 240.0f ||
        pos_x <= -360.0f || pos_x >= 360.0f) {
        KillBomb();
    }
    // Lazy-create fuse particle emitter
    else if (m_pEmitter == NULL) {
        uint hash = g_bombData->fuseHash[field38_0x64];
        m_pEmitter = PSPParticleManager::AddEmitter(hash, NULL, Game.timeScale == 0.0f);
        if (m_pEmitter) {
            m_pEmitter->pos = this->pos;
        }
    }
}
```

#### Bomb::Update Flow Summary

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

Key constants:
- Bounds: Y [-240, 240], X [-360, 360] (DAT_00172f34..00172f40)
- Offscreen: Y = -320, collision = (1000, 1000, 0)
- BombBlast spawn interval: 0.05s
- Fuse SFX threshold: 0.2s before launch
- Accel growth rate: 0.2 per normalized dt

---

### Bomb::Draw (0x00171be8)

`void __thiscall Bomb::Draw(Bomb *this)`

Renders the bomb mesh:

```c
void Bomb::Draw() {
    float timer = this->countdown;
    g_bombDrawData->flag = 0;  // reset "highest bomb" flag

    if (timer <= 0.0f) {
        // Track highest bomb for UI (BombScale indicator)
        if (this != g_bombDrawData->currentBomb && !m_bBombFlag88) {
            if (pos_y > -1000.0f)       // DAT_00171d30 = -1000.0 (always true for on-screen bombs)
                g_bombDrawData->currentBomb = this;
        }

        // Get model by variant (field38_0x64 selects bomb model 0 or 1)
        if (SmartPtr::IsValid(g_bombDrawData->model[field38_0x64])) {
            // Build transform: Scale * RotX(fixed) * RotY(m_RotX) * RotZ(m_RotY) * Translate
            Matrix44 scaleMat = Matrix44::Scale(field_0x28);  // scale from Init
            Matrix44 rotMat;
            rotMat.RotX(Sin(0xBFF4), Cos(0xBFF4));           // fixed tilt (~-83 degrees)
            rotMat.RotY(Sin(m_RotX * 0xB6), Cos(m_RotX * 0xB6));
            rotMat.RotZ(Sin(m_RotY * 0xB6), Cos(m_RotY * 0xB6));

            Vec3 offset = globalMeshOffset * DAT;
            Vec3 drawPos = pos + offset;
            Matrix44 transMat = Matrix44::Translate(drawPos);

            Matrix44 finalMat = scaleMat * rotMat * transMat;
            Model::Draw(g_bombDrawData->model[field38_0x64], finalMat);
        }
    }
}
```

Key observations:
- Bomb is NOT drawn while countdown > 0 (still waiting to spawn)
- Two bomb model variants: `model[0]` (normal) and `model[1]` (alternative/multiplayer)
- Fixed RotX tilt of 0xBFF4 (~-83 degrees) makes bomb face camera
- m_RotX/m_RotY animated by rotVel in Update; multiplied by 0xB6 (1 degree in 16-bit angle)
- Tracks "highest bomb" for BombScale UI indicator

---

### Bomb::DrawUpdate (0x001714e4)

`void __thiscall Bomb::DrawUpdate(Bomb *this, float dt)`

Updates fuse particle emitter position to follow bomb tip:

```c
void Bomb::DrawUpdate(float dt) {
    if (m_pEmitter != NULL) {
        float sinY = Sin(m_RotY * -0xB6);
        float cosY = Cos(m_RotY * -0xB6);
        float scale = field_0x28.x;  // bomb scale X

        // Fuse tip offset: rotated by Y rotation, scaled
        // DAT_0017159c = 0.9 (fuse length), DAT_001715a0 = fuse scale factor
        Vec3 fuseOffset = Vec3(
            sinY * 0.9f * scale * DAT_fuse_scale,
            cosY * 0.9f * scale * DAT_fuse_scale,
            5.0f
        );
        Vec3 fusePos = pos + fuseOffset;

        m_pEmitter->pos = fusePos;

        // Fuse direction vectors for particle orientation
        m_pEmitter->dirX = Cos(m_RotY * -0xB6);
        m_pEmitter->dirY = -Sin(m_RotY * -0xB6);
    }
}
```

---

### Bomb::CollisionResponse (0x0017280c)

`int __thiscall Bomb::CollisionResponse(Bomb *this, Entity *slash, ulong p2, ulong p3, Vec3 *bladeVel)`

Handles what happens when a blade hits the bomb:

```c
int Bomb::CollisionResponse(Entity *slash, ulong p2, ulong p3, Vec3 *bladeVel) {
    if (field_0x78 != 0) return 0;  // already processed

    if (m_bBombFlag88 == 0) {
        // First hit (game bomb)
        if (slash != NULL) {
            if (Game.gameMode == GAME_MODE_ZEN) {  // gameMode byte == 2
                // Zen mode: no game-over, just penalty
                FruitSaveData::AddToTotal("bomb_sliced", hash, 1, false, false);
                WaveManager::ResetSpeed(0);
                m_bBombFlag88 = 1;

                HitMenuBomb(pos);  // visual effect + SFX
                FruitCamera::CreateCameraShake(camera, pos, 2.0f, 3.0f);
                AddToCurrentScore(-10, 0, false, false);  // -10 points
                PowerUpManager::ClearTimedPowers();

                // Show "X" miss indicator
                MissControl *miss = MissControl::GetFree();
                miss->MakeDisappear(pos, 0, bombTex);
                miss->field_0x34 = 0x200;  // size
            } else {
                // Classic/Arcade: trigger game-over sequence
                if (!Game.gameOverFlag) {
                    HitBomb(pos);  // camera shake + SFX + flash + sets bombTimer
                }
            }
        }
    } else {
        // Menu bomb re-hit: clear menu items
        if (field_0x84 == 0 || *(field_0x84 + 0x123) != 0)
            ClearMenuItems();
        Delegate0<void>::operator()(&field_0x40);  // fire hit callback
    }

    activeFlag = 1;  // mark as hit
    return 0;
}
```

Key behaviors:
- **Classic/Arcade**: `HitBomb()` triggers camera shake (1.6f intensity, 2.0f duration), sets `Game.bombTimer = 3.2f`, plays explosion SFX
- **Zen mode**: -10 points, clears power-ups, visual only (no game-over)
- **Menu bombs**: calls delegate callback, clears menu items
- `m_bBombFlag88` distinguishes first-hit (0) from re-hit/menu-hit (1)
- `field_0x78` is a "processed" guard to prevent double-triggering

---

### Bomb::Release (0x00171764)

`void __thiscall Bomb::Release(Bomb *this)`

```c
void Bomb::Release() {
    if (m_pEmitter != NULL) {
        PSPParticleManager::ClearEmitter(m_pEmitter);
        m_pEmitter = NULL;
    }
    // Unlink from game state
    if (field_0x84 != 0 && *(field_0x84 + 0x134) == this)
        *(field_0x84 + 0x134) = 0;
    if (g_highestBomb == this)
        g_highestBomb = 0;
    Entity::Release();
}
```

---

### Bomb::KillBomb (0x001716e8)

`void __thiscall Bomb::KillBomb(Bomb *this)`

```c
void Bomb::KillBomb() {
    flags |= 0x10;  // mark for removal
    if (field_0x84 != 0 && *(field_0x84 + 0x134) == this)
        *(field_0x84 + 0x134) = 0;  // unlink from game
    if (m_pEmitter != NULL) {
        PSPParticleManager::ClearEmitter(m_pEmitter);
        m_pEmitter = NULL;
    }
}
```

Pseudocode summary:
- Sets kill flag (0x10) on entity flags
- Unlinks self from game state pointer at +0x134 if it points to this bomb
- Clears fuse particle emitter

---

### Bomb::Chuck (0x00170f68)

`void __thiscall Bomb::Chuck(Bomb *this, float delay)`

```c
void Bomb::Chuck(float delay) {
    if (delay <= 0.0f)
        delay = 0.2f;   // DAT_00170f80 = 0.2 default delay
    countdown = delay;
}
```

Pseudocode summary:
- Sets countdown timer before bomb becomes active
- Default delay is 0.2s if none provided
- Called by SpawnBomb and MakeFat

---

### Bomb::MakeFat (0x00171d78)

`void __thiscall Bomb::MakeFat(Bomb *this, bool silent)`

Makes a bomb larger (visual upgrade for higher difficulty):

```c
void Bomb::MakeFat(bool silent) {
    speedMult = 0.666f;            // DAT_00171eec = 0.666 (slower, not faster!)
    field_0x28 *= 1.33f;           // DAT_00171ef0 = 1.33 scale up
    field_0x98 = field_0x28;       // backup scale
    m_Col->radius *= 1.33f;       // bigger collision

    if (!silent) {
        // Spawn fat-bomb particle effect
        Vec3 dir = Normalize(accelForce);
        int hashIdx = (field38_0x64 != 2) ? DAT_normal : DAT_multi;
        uint hash = StringHash(particleName[hashIdx]);
        PSPParticleEmitter *emitter = PSPParticleManager::AddEmitter(hash, NULL, true);
        if (emitter) {
            emitter->pos = pos;
            emitter->pos += vel * DAT;
            // Set X position based on which side of screen
            if (pos_x < 0.0f)
                emitter->pos.x = DAT_left;     // DAT_00171ef4
            else
                emitter->pos.x = DAT_right;    // DAT_00171ef8
            // Direction from velocity
            Vec2 dir2d = Normalize(Vec2(vel_x, vel_y));
            emitter->dirX = dir2d.y;
            emitter->dirY = dir2d.x;
        }
        GameSound::SFXPlay("bomb_fat_sfx", 1.0, 1.0, ...);
        Chuck(0.25f);  // short delay before activation
    }
}
```

Pseudocode summary:
- Slows bomb (speedMult = 0.666) but scales it up 1.33x
- Enlarges collision radius proportionally
- If not silent: spawns growth particle, plays SFX, sets 0.25s delay

---

## Bomb Static/Free Functions

### CleanupBomb (0x001729ac)

`void CleanupBomb(void)`

```c
void CleanupBomb() {
    SmartPtr<Model>::SetPtr(g_bombData->model[0], NULL);   // +0x0c
    SmartPtr<Model>::SetPtr(g_bombData->model[1], NULL);   // +0x10
    BombFlash::CleanUp();
    SmartPtrNull(g_bombData->tex[0]);   // +0x14
    SmartPtrNull(g_bombData->tex[1]);   // +0x18
    SmartPtrNull(g_bombData->tex[2]);   // +0x04
    SmartPtrNull(g_bombData->tex[3]);   // +0x24
}
```

Pseudocode summary:
- Releases both bomb models (normal + multiplayer variant)
- Cleans up BombFlash system
- Releases 4 textures (bomb tex, flash tex, etc.)
- Called during game shutdown / state cleanup

---

### GetBombZPosition (0x00169080)

`float GetBombZPosition(void)`

```c
float GetBombZPosition() {
    float z = g_bombZCurrent - 50.0f;   // DAT_001690bc = 50.0 (step)
    if (z < -400.0f)                      // DAT_001690c0 = -400.0 (min)
        z = -10.0f;                       // 0xC1200000 = -10.0 (reset)
    g_bombZCurrent = z;
    return z;
}
```

Pseudocode summary:
- Returns a decreasing Z value for bomb layer ordering
- Decrements by 50 each call; wraps to -10 when below -400
- Ensures each bomb renders at a unique Z depth

---

### GetHeighestBomb (0x001712c8)

`float Bomb::GetHeighestBomb(void)`

```c
float GetHeighestBomb() {
    float highest = DAT_init;   // initial low value
    for (int i = 0; ; i++) {
        Entity *bomb = ActorManager::GetEntity(1, i);  // type 1 = Bomb
        if (bomb == NULL) break;

        float screenPos;
        if (IsMultiplayer()) {
            // Multiplayer: use pos_x, mirror if negative
            screenPos = bomb->pos_x;
            if (screenPos < 0)
                screenPos = DAT_mirror - screenPos;
            else
                screenPos += DAT_offset;
        } else {
            // Single-player: use pos_y
            screenPos = bomb->pos_y + DAT_offset;
        }

        // Skip menu bombs, track highest
        if (bomb->m_bBombFlag88 == 0 && screenPos > highest)
            highest = screenPos;
    }
    return highest;
}
```

Pseudocode summary:
- Iterates all Bomb entities (type 1)
- Returns the highest screen position among non-menu bombs
- In multiplayer, uses X axis (rotated screen); single-player uses Y
- Used by BombScale UI indicator to position the "bomb danger" warning

---

### BombScale (0x001286fc)

`void __thiscall WaveManager::BombScale(WaveManager *this, float factor)`

```c
void WaveManager::BombScale(float factor) {
    field_0x64 *= factor;   // multiply bomb scale config
}
```

Pseudocode summary:
- Multiplies the WaveManager bomb scale factor by the given value
- Called by wave configuration / difficulty scaling

---

### BombMultiplyer (0x0012870c)

`void __thiscall WaveManager::BombMultiplyer(WaveManager *this, float factor)`

```c
void WaveManager::BombMultiplyer(float factor) {
    spawnLevel *= factor;   // multiply bomb spawn level
}
```

Pseudocode summary:
- Multiplies the WaveManager spawnLevel by the given factor
- Higher spawnLevel = more chain-spawned bombs per wave

---

### SpawnBomb (0x00121fa8)

`void __thiscall WaveManager::SpawnBomb(WaveManager *this, long count, long type, SPAWNER_INFO *spawner, int playerIdx)`

Complex spawning function (~200 lines). Key flow:

```c
void WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO *spawner, int playerIdx) {
    for (int i = 1; i <= count; i++) {
        // 1. Calculate spawn direction from SPAWNER_INFO
        float dirMin = (type == 0) ? -1.0f : spawner->dirMin;      // +0x2c
        float dirMax = (type == 0) ?  1.0f : spawner->dirMax;      // +0x30
        float range = dirMin * 480.0f + dirMax * 320.0f;
        int angle = Random::Rand32((int)range) + (int)(dirMin * 320.0f);

        // 2. Calculate spread angle
        float spreadDeg = (type == 0 || spawner->spawnType < 2) ? 20.0f : 12.0f;
        float fraction = (float)angle / DAT_divisor;
        int spreadAngle = Random::Rand32(fraction * spreadDeg * 0.5f + spreadDeg * 0.5f
                                       - (fraction * spreadDeg * 0.5f - spreadDeg * 0.5f));
        ushort angle16 = spreadAngle * 0xB6;  // degrees to 16-bit angle

        // 3. Calculate velocity from angle + speed
        float speed = Random::RandF(1.5f) + 9.5f;
        float speedX = (type == 0) ? 1.0f : spawner->speedX;       // +0x24
        float speedY = (type == 0) ? 1.0f : spawner->speedY;       // +0x28
        float velX = Sin(angle16) * speed * speedX;
        float velY = Cos(angle16) * speed * DAT_velScale * speedY;

        // 4. Handle SPAWNER_INFO types (0-4)
        switch (spawner->spawnType) {  // +0x34
            case 1: // half-speed X, mirrored direction
                velX *= 0.5f;
                break;
            case 2: // side spawn (left)
                velY = velY + speed * spawner->sideOffset * DAT;
                velX = velX * -0.75f;  // reverse X
                break;
            case 3: // side spawn (right)
                // same as case 2 but different origin
                break;
            case 4: // random left or right
                // picks case 2 or 3 randomly
                break;
        }

        // 5. Multiplayer: rotate 90 degrees, spawn for both players
        if (IsMultiplayer()) {
            // Rotate vel 90 degrees
            // Spawn bomb for player 1
            Bomb *b1 = ActorManager::Add(1, true);
            b1->pos = Vec3(posY, posX, i * DAT_zSpacing);
            b1->vel = Vec3(velY, velX, DAT_velZ);
            b1->Init(0, 0, &scale);
            b1->accelForce rotated for player 1
            Bomb::SetForPlayer(b1, 1);
            Bomb::Chuck(b1, delay);

            // Mirror bomb for player 2 (if playerIdx allows)
            if (playerIdx == 0 || playerIdx == 2) {
                Bomb *b2 = ActorManager::Add(1, true);
                b2->pos = Vec3(-posY, -posX, i * DAT_zSpacing);
                b2->vel = Vec3(-velY, -velX, DAT_velZ);
                b2->Init(0, 0, &scale);
                Bomb::SetForPlayer(b2, 2);
                Bomb::Chuck(b2, delay);
            }
        } else {
            // Single-player: single bomb
            Bomb *bomb = ActorManager::Add(1, true);
            bomb->pos = Vec3(posX, posY, i * DAT_zSpacing);
            bomb->vel = Vec3(velX, velY, DAT_velZ);
            bomb->Init(0, 0, &scale);
            bomb->pos_y += DAT_posYOffset * bomb->field_0x2c;
            Bomb::Chuck(bomb, delay);
            if (Game.playerCount == 2) Bomb::SetForPlayer(bomb, 1);
        }

        // Optional fat bomb
        if (type == 0 && bomb != NULL && playerIdx > 0)
            Bomb::MakeFat(bomb, false);
    }
}
```

Pseudocode summary:
- Spawns `count` bombs with randomized angle, speed, and position
- SPAWNER_INFO controls direction, speed, and spawn type (0-4)
- Multiplayer mirrors bombs for both players with 90-degree rotation
- Calls Init, Chuck, optionally MakeFat for each bomb

---

### HitBomb (0x0016b0fc)

`void HitBomb(Vec3 pos)`

Called when a bomb is slashed in Classic/Arcade mode:

```c
void HitBomb(Vec3 pos) {
    if (!Game.gameOverFlag) {
        // Hash "bomb_sliced" on first call (lazy-init with guard)
        FruitSaveData::AddToTotal("bomb_sliced", hash, 1, true, true);

        // Set bomb timer to trigger game-over countdown
        Game.bombTimer = 3.2f;              // DAT_0016b218 = 3.2

        // Camera shake
        FruitCamera::CreateCameraShake(camera, pos, 1.6f, 2.0f);
        // DAT_0016b21c = 1.6 (intensity), 2.0 (duration)

        // Store hit position for DrawBombHit flash
        g_bombHitData->visible = false;     // byte at +0xf8
        g_bombHitData->pos = pos;           // Vec3 at +0xcc

        // Play explosion SFX
        GameSound::SFXPlay("bomb_explode", 1.0, 1.0, ...);
    }
}
```

Pseudocode summary:
- Records "bomb_sliced" stat, starts game-over timer (3.2s)
- Triggers camera shake (intensity 1.6, duration 2.0)
- Stores explosion position for flash rendering
- Plays bomb explosion SFX

---

### HitMenuBomb (0x0016b234)

`void HitMenuBomb(Vec3 pos)`

Menu bomb special behavior (also used in Zen mode):

```c
void HitMenuBomb(Vec3 pos) {
    int taskState = g_gameData->taskState + 0x1c;
    // Skip if in specific game state (0x10c == 1)
    if (taskState != 0 && *(taskState + 0x10c) == 1)
        return;

    // Play bomb SFX
    GameSound::SFXPlay("bomb_menu_explode", 1.0, 1.0, ...);

    // Set bomb hit data for visual flash
    g_bombHitData->visible = true;          // byte at +0xf8 = 1
    Game.bombTimer = 2.0f;                  // 0x40000000 = 2.0
    g_bombHitData->pos = pos;               // Vec3 at +0xcc
}
```

Pseudocode summary:
- Lighter version of HitBomb for menu/zen contexts
- Sets bombTimer to 2.0 (shorter than game-over's 3.2)
- Marks flash as visible, stores position
- Plays menu-specific explosion SFX
- Guards against specific game state transitions

---

### UpdateBombHit (0x0016a1a8)

`void UpdateBombHit(float prevBombTimer)`

Updates bomb hit state -- called each frame with previous bomb timer value:

```c
void UpdateBombHit(float prevBombTimer) {
    float currentTimer = Game.bombTimer;  // at Game+0x10

    // At 1.5s threshold: reset game entities (clear fruits off screen)
    if (prevBombTimer > 1.5f && currentTimer <= 1.5f) {
        ResetGameEntities(false);
    }

    // At 1.55s threshold: remove BombBlast flash entities
    if (currentTimer > 0.0f && currentTimer < 1.55f) {   // DAT_0016a1fc = 1.55
        RemoveFlashEntities();
    }
}
```

Pseudocode summary:
- Monitors bombTimer countdown after bomb hit
- At 1.5s: resets all game entities (clears screen)
- Below 1.55s: removes all BombBlast entities (type 4)
- Called by game update loop with previous frame's timer

---

### DrawBombHit (0x0016b73c)

`void DrawBombHit(void)`

Renders white flash overlay after bomb explosion:

```c
void DrawBombHit() {
    // Lazy-load flash texture
    if (!g_bombHitTex)
        g_bombHitTex = TextureManager::LoadLocalisedTexture(...);  // at data+0x10c

    float timer = Game.bombTimer;  // read from Game+0x10
    if (timer < 2.0f) {
        // Scale animation: starts large, based on timer
        float t = (timer - 1.55f) / -0.45f + 1.0f;
        // DAT_0016b864=1.55, DAT_0016b868=-0.45
        float scale;
        if (t <= 0.0f)
            scale = 0.0f;                   // DAT_0016b86c = 0.0
        else if (t < 1.0f)
            scale = t * 20000.0f;           // DAT_0016b870 = 20000.0
        else
            scale = 20000.0f;               // max scale

        Texture::Set(g_bombHitTex);
        ResetMatrixStack();
        ScaleMatrix(Vec3(scale, scale, 1.0));
        TranslateMatrix(g_bombHitData->pos);   // at +0xcc
        UploadMatrices();

        // Alpha fade: proportional to timer
        int alpha = clamp((int)(255.0f * timer), 0, 255);
        // DAT_0016b874 = 255.0
        DrawQuad_Colour(Colour(255, 255, 255, alpha));
        Texture::UnSet(g_bombHitTex);
    }
}
```

Pseudocode summary:
- Draws expanding white flash at bomb explosion position
- Scale grows from 0 to 20000 over 0.45s window (timer 1.55 to 1.1)
- Alpha fades proportionally to remaining timer (255 * timer)
- Only draws when bombTimer < 2.0

---

### BombFlashFull (0x00168f24)

`void BombFlashFull(void)`

```c
void BombFlashFull() {
    // Empty function -- stubbed out
    return;
}
```

Pseudocode summary:
- No-op. Likely was a full-screen white flash that was disabled or moved elsewhere.

---

### RemoveFlashEntities (0x00169ca0)

`void RemoveFlashEntities(void)`

```c
void RemoveFlashEntities() {
    Iterator it;
    Entity *ent = ActorManager::GetEntityFirst(4, &it);  // type 4 = BombBlast
    while (ent != NULL) {
        ent->flags |= 0x11;    // set kill + disable flags
        ent = ActorManager::GetEntityNext(4, &it);
    }
}
```

Pseudocode summary:
- Iterates all BombBlast entities (type 4)
- Marks each for removal (flags |= 0x11)
- Called by UpdateBombHit when bombTimer crosses 1.55s threshold

---

## Summary of Constants

| DAT Address | Value | Used In | Purpose |
|-------------|-------|---------|---------|
| 0x001726a8 | 0.0f | Init | Initial countdown / Z |
| 0x001726ac | 0.6f | Init | m_SpawnTimer (BombBlast spawn delay) |
| 0x00170f80 | 0.2f | Chuck | Default launch delay |
| 0x00172c98 | 0.01667f | Update | Time normalization (1/60) |
| 0x00172c9c | 0.05f | Update | BombBlast spawn interval |
| 0x00172ca0 | 0.2f | Update | Fuse SFX threshold / accel rate (hit) |
| 0x00172ca4 | 1000.0f | Update | Offscreen collision position |
| 0x00172cac | 0.01f | Update | Hit bomb collision radius |
| 0x00172cb0 | -320.0f | Update | Offscreen Y position |
| 0x00172f30 | 0.2f | Update | Acceleration growth rate |
| 0x00172f34 | -240.0f | Update | Bounds min Y |
| 0x00172f38 | 240.0f | Update | Bounds max Y |
| 0x00172f3c | -360.0f | Update | Bounds min X |
| 0x00172f40 | 360.0f | Update | Bounds max X |
| 0x001690bc | 50.0f | GetBombZPosition | Z decrement step |
| 0x001690c0 | -400.0f | GetBombZPosition | Z minimum before wrap |
| 0x0016b218 | 3.2f | HitBomb | Game-over countdown timer |
| 0x0016b21c | 1.6f | HitBomb | Camera shake intensity |
| 0x0016a1fc | 1.55f | UpdateBombHit | Flash entity removal threshold |
| 0x0016b864 | 1.55f | DrawBombHit | Flash start time |
| 0x0016b868 | -0.45f | DrawBombHit | Flash duration divisor |
| 0x0016b870 | 20000.0f | DrawBombHit | Max flash scale |
| 0x0016b874 | 255.0f | DrawBombHit | Alpha multiplier |
| 0x00171eec | 0.666f | MakeFat | Fat bomb speed multiplier |
| 0x00171ef0 | 1.33f | MakeFat | Fat bomb scale multiplier |
| 0x00171d30 | -1000.0f | Draw | Highest bomb Y threshold |
| 0x0017159c | 0.9f | DrawUpdate | Fuse offset length |
| 0x0017120c | 100.0f | BombBlast::Update | Blast radius growth rate |
| 0x00171210 | 2500.0f | BombBlast::Update | Scale growth rate |

---

## See Also

- [BombBlast entity](bomb-blast.md) -- explosion shockwave ring
- [BombFlash system](bomb-flash.md) -- flash overlay effects
- [Entity base struct](entity-base.md) -- Mortar::Entity base class
- [Wave system](../systems/wave.md) -- SpawnBomb integration
- [Camera system](../systems/rendering.md) -- CreateCameraShake
- [Particle system](../systems/particles.md) -- PSPParticleManager
