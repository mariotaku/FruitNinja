# Bomb Functions

## Bomb Vtable (base: 0x001EA478)

| Index | Address | Method | Notes |
|-------|---------|--------|-------|
| [0] | 0x0017180c | ~Bomb (deleting) | Calls Release, then ~Delegate, ~Entity, operator_delete |
| [1] | 0x001717b4 | ~Bomb (non-deleting) | Calls Release, then ~Delegate, ~Entity |
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

## Bomb::Init (0x00172504)

`void __thiscall Bomb::Init(Bomb *this, void *p1, long type, Vec3 *scale)`

Sets up a newly spawned bomb entity:

```c
void Bomb::Init(void *p1, long type, Vec3 *scale) {
    if (m_Col == NULL)
        m_Col = new ColSphere();  // size 0x18

    float scaleFactor = (scale != NULL) ? scale->x : 1.0f;

    // Position collision sphere at bomb pos, Z = DAT (constant)
    m_Col->center = Vec3(pos_x, pos_y, DAT_z);
    m_Col->radius = FRUIT_INFO->colSize * 0.5f * scaleFactor;

    // Lazy-load bomb texture if not cached
    if (!g_BombTexture)
        g_BombTexture = TextureManager::LoadLocalisedTexture("bomb_tex");

    // Initial state
    field_0x44 = DAT_z;          // z constant (same as collision z)
    m_SpawnTimer = DAT_timer;    // BombBlast spawn delay
    field38_0x64 = 0;            // bomb variant (0 = normal, 2 = multiplayer)
    field_0x78 = 0;
    activeFlag = 0;              // not yet hit
    flags = (flags & ~0x10) | 0x02;  // clear killed, set has-collision
    movementFlag = 1;            // physics enabled
    speedMult = 1.0f;

    // Random rotation for each axis (2 axes)
    for (int i = 0; i < 2; i++) {
        m_RotVel[i] = Random::Rand32(7) + 1;    // 1..8
        m_Rot[i]    = Random::Rand32(0x167);     // 0..359
    }

    field_0x84 = 0;             // game state backref
    m_bBombFlag88 = 0;          // not menu-hit
    m_pEmitter = NULL;           // no fuse particle yet

    // Acceleration / gravity: compute from global config
    Vec3 accel = globalConfig * DAT * scale;
    countdown = 0.0f;            // no delay by default
    field_0x28 = accel;          // scale vector (for rendering)
    field_0x98 = accel;          // original scale backup
    accelForce = Vec3(0.0f, -12.0f, 0.0f);  // gravity
    field_0x6c = GetBombZPosition();  // Z layer cycling
}
```

---

## Bomb::Draw (0x00171be8)

`void __thiscall Bomb::Draw(Bomb *this)`

Renders the bomb mesh:

```c
void Bomb::Draw() {
    float timer = this->countdown;
    g_bombDrawData->flag = 0;  // reset "highest bomb" flag

    if (timer <= 0.0f) {
        // Track highest bomb for UI (BombScale indicator)
        if (this != g_bombDrawData->currentBomb && !m_bBombFlag88) {
            if (pos_y > DAT_threshold)
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

## Bomb::DrawUpdate (0x001714e4)

`void __thiscall Bomb::DrawUpdate(Bomb *this, float dt)`

Updates fuse particle emitter position to follow bomb tip:

```c
void Bomb::DrawUpdate(float dt) {
    if (m_pEmitter != NULL) {
        float sinY = Sin(m_RotY * -0xB6);
        float cosY = Cos(m_RotY * -0xB6);
        float scale = field_0x28;  // bomb scale

        // Fuse tip offset: rotated by Y rotation, scaled
        Vec3 fuseOffset = Vec3(
            sinY * DAT_fuse * scale * DAT_scale,
            cosY * DAT_fuse * scale * DAT_scale,
            5.0f
        );
        Vec3 fusePos = pos + fuseOffset;

        m_pEmitter->pos = fusePos;
        m_pEmitter->dirX = cosY;  // fuse direction
        m_pEmitter->dirY = -sinY;
    }
}
```

---

## Bomb::CollisionResponse (0x0017280c)

`int __thiscall Bomb::CollisionResponse(Bomb *this, Entity *slash, ulong p2, ulong p3, Vec3 *bladeVel)`

Handles what happens when a blade hits the bomb:

```c
int Bomb::CollisionResponse(Entity *slash, ulong p2, ulong p3, Vec3 *bladeVel) {
    if (field_0x78 != 0) return 0;  // already processed

    if (m_bBombFlag88 == 0) {
        // First hit
        if (slash != NULL) {
            if (Game.gameMode == GAME_MODE_ZEN) {
                // Zen mode: no game-over, just penalty
                FruitSaveData::AddToTotal("bomb_sliced", hash, 1, false, false);
                WaveManager::ResetSpeed(0);
                m_bBombFlag88 = 1;

                HitMenuBomb(pos);  // visual effect
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
                    FruitSaveData::AddToTotal("bomb_sliced", hash, 1, true, true);
                    Game.bombTimer = DAT_countdown;  // starts game-over countdown
                    HitBomb(pos);  // camera shake + SFX + flash
                }
            }
        }
    } else {
        // Menu bomb re-hit: clear menu items
        if (field_0x84 == 0 || *(field_0x84 + 0x123) != 0)
            ClearMenuItems();
        hitCallback();  // Delegate0<void> at +0x40
    }

    activeFlag = 1;  // mark as hit
    return 0;
}
```

Key behaviors:
- **Classic/Arcade**: `HitBomb()` triggers camera shake (intensity=DAT, 2.0f), sets `Game.bombTimer`, plays explosion SFX
- **Zen mode**: -10 points, clears power-ups, visual only (no game-over)
- **Menu bombs**: calls delegate callback, clears menu items
- `m_bBombFlag88` distinguishes first-hit (0) from re-hit/menu-hit (1)
- `field_0x78` is a "processed" guard to prevent double-triggering

---

## Bomb::Release (0x00171764)

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

## Bomb::Chuck (0x00170f68)

`void __thiscall Bomb::Chuck(Bomb *this, float delay)`

```c
void Bomb::Chuck(float delay) {
    if (delay <= 0.0f)
        delay = DAT_default_delay;  // ~0.3s default
    countdown = delay;
}
```

---

## Bomb::MakeFat (0x00171d78)

`void __thiscall Bomb::MakeFat(Bomb *this, bool silent)`

Makes a bomb larger (visual upgrade for higher difficulty):

```c
void Bomb::MakeFat(bool silent) {
    speedMult = DAT_fatSpeed;         // increased speed
    field_0x28 *= DAT_fatScale;       // scale up
    field_0x98 = field_0x28;          // backup scale
    m_Col->radius *= DAT_fatScale;    // bigger collision

    if (!silent) {
        // Spawn fat-bomb particle effect
        Vec3 dir = Normalize(accelForce);
        uint hash = (field38_0x64 != 2) ? hash_normal : hash_multi;
        PSPParticleEmitter *emitter = PSPParticleManager::AddEmitter(hash, ...);
        if (emitter) {
            emitter->pos = pos + vel * DAT;
            // Set direction based on which side of screen
            emitter->pos.x = (pos.x < 0) ? DAT_left : DAT_right;
        }
        GameSound::SFXPlay("bomb_fat_sfx", ...);
        Chuck(0.25f);  // short delay before activation
    }
}
```

---

## Bomb::KillBomb (0x001716e8)

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

---

## SpawnBomb (0x00121fa8)

`void WaveManager::SpawnBomb(long count, SPAWNER_INFO *type, int playerIdx)`

Complex spawning function (~200 lines). Key flow:
1. Loop `count` times to spawn bombs
2. Calculate spawn position using SPAWNER_INFO direction/speed fields
3. Position randomized with angle offset (SinIdx/CosIdx)
4. For multiplayer: rotates spawn direction 90 degrees, spawns mirror bomb for player 2
5. Calls `Bomb::Init(0, 0, &scale)` via vtable
6. Calls `Bomb::Chuck(delay)` to set countdown
7. Optional: `Bomb::MakeFat(false)` for fat bombs

---

## HitBomb (0x0016b0fc)

`void HitBomb(Vec3 *pos)`

Called when a bomb is slashed in Classic/Arcade mode:

```c
void HitBomb(Vec3 *pos) {
    if (!Game.gameOverFlag) {
        FruitSaveData::AddToTotal("bomb_sliced", hash, 1, true, true);
        FruitCamera::CreateCameraShake(camera, pos, DAT_intensity, 2.0f);
        Game.bombTimer = DAT_countdown;

        // Store hit position for DrawBombHit flash
        g_bombHitData->visible = false;
        g_bombHitData->pos = *pos;

        // Play explosion SFX
        GameSound::SFXPlay("bomb_explode", 1.0, 1.0, ...);
    }
}
```

---

## DrawBombHit (0x0016b73c)

Renders white flash overlay after bomb explosion:

```c
void DrawBombHit() {
    // Lazy-load flash texture
    if (!g_bombHitTex)
        g_bombHitTex = TextureManager::LoadLocalisedTexture(...);

    float timer = Game.bombTimer;  // read from Game+0x10
    if (timer < 2.0f) {
        // Scale: starts big, shrinks over time
        float t = (timer - DAT_start) / DAT_duration + 1.0;
        float scale = clamp(t * DAT_maxScale, 0.0, DAT_maxScale);

        Texture::Set(g_bombHitTex);
        ResetMatrixStack();
        ScaleMatrix(Vec3(scale, scale, 1.0));
        TranslateMatrix(g_bombHitData->pos);
        UploadMatrices();

        // Alpha fade: proportional to timer
        int alpha = clamp((int)(DAT * timer), 0, 255);
        DrawQuad_Colour(Colour(255, 255, 255, alpha));
        Texture::UnSet(g_bombHitTex);
    }
}
```

---

## Bomb::Update (0x001729fc, 195 lines)

```c
void Bomb::Update(float dt) {
    float scaledDt = dt * speedMult;

    if (activeFlag == 0) {
        // Normal bomb: countdown -> chain spawn
        if (countdown > 0) {
            if (game->paused || game->bombTimer > 0) {
                countdown = 0; pos.y = OFFSCREEN;
                vel = Vec3(0, -1, 0); return;
            }
            countdown -= Game.dt;
            if (countdown crosses SFX_THRESHOLD) playSFX("bomb_fuse");
            if (countdown > 0) return;

            // Chain bomb spawning
            int spawnCount = (int)WaveManager.spawnLevel;
            if (spawnCount > 1)
                WaveManager::SpawnBomb(spawnCount - 1, ...);
        }
        // Physics
        if (movementFlag) {
            vel += accelForce * scaledDt;
            if (vel and accel aligned)
                accelForce = normalize(accelForce) * (length + DAT * dt * 2);
        }
        pos += vel * scaledDt;
        rotX += rotVelX; rotY += rotVelY;  // 16-bit angle
        m_Col->center = pos;
    } else {
        // Hit bomb: spawn BombBlast or continue physics
        if (m_bBombFlag88 == 0) {
            // Timer -> spawn BombBlast entity (type 4)
            m_SpawnTimer -= Game.dt;
            if (m_SpawnTimer < 0) {
                BombBlast *blast = ActorManager::Add(4, true);
                blast->pos = this->pos;
                blast->Init(0, 0, 0);
                m_SpawnTimer = DAT_reset;
            }
        } else {
            // Menu-hit: physics with different drag
            if (movementFlag) {
                vel += accelForce * scaledDt;
                if (aligned) accelForce grows;
            }
            pos += vel * scaledDt;
            rotX += rotVelX; rotY += rotVelY;
        }
        // Move collision offscreen when hit
        m_Col->center = Vec3(OFFSCREEN, OFFSCREEN, DAT);
        m_Col->radius = DAT;
    }

    if (outOfBounds(pos)) KillBomb();
    if (!m_pEmitter) m_pEmitter = PSPParticleManager::AddEmitter(fuseHash[field38_0x64]);
}
```

---

## Helper Functions

| Function | Address | Purpose |
|----------|---------|---------|
| GetBombZPosition | 0x00169080 | Returns decreasing Z for bomb layer ordering |
| GetHeighestBomb | 0x001712c8 | Finds bomb with highest screen Y (for UI) |
| CleanupBomb | 0x001729ac | Releases all bomb models/textures (shutdown) |
| BombScale | 0x001286fc | Bomb scale effect (visual) |
| BombFlashFull | 0x00168f24 | Full-screen flash on bomb hit |
| UpdateBombAvoidance | 0x00175988 | AI avoidance for multiplayer |
| UpdateBombHit | 0x0016a1a8 | Post-hit update logic |
