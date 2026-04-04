# Fruit Functions

## Fruit System

### Vtable: 0x001ea528 (13 slots)

See [entities.md](../structs/entities.md) for full vtable and struct layout.

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
// Full fruit physics — see docs/systems/physics.md
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
    
    // Rotation update (both halves, loop ×2)
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

### Fruit::Chuck (0x00175a64, 36 lines)

| Address | Signature |
|---------|-----------|
| 0x00175a64 | `void Fruit::Chuck(float delay)` |

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

### Fruit::LoadInfo (0x0017987c, 530 lines)

| Address | Signature |
|---------|-----------|
| 0x0017987c | `int Fruit::LoadInfo()` |

See `docs/structs/data.md` for full FRUIT_INFO layout and XML schema.

---


---

## Fruit::Slice (0x00176d58, 355 lines)

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
    
    // Special fruit (score=50): also gets 1.5× impulse
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

---

## Fruit::KillFruit (0x00176abc, 101 lines)

| Address | Signature |
|---------|-----------|
| 0x00176abc | `void Fruit::KillFruit(bool removeFromList)` |

Clears particle emitters, handles miss penalty in Zen mode (dropped unsliced fruit → notification + miss SFX), marks entity flags for removal.

## Fruit::IsActive (0x0017a82c, 10 lines)

| Address | Signature |
|---------|-----------|
| 0x0017a82c | `bool Fruit::IsActive()` — returns true if entity is alive (stub in decompilation) |

## Fruit::CheckHasGoneOffscreen (0x00175218, 128 lines)

Checks if both fruit halves (or whole fruit) are outside screen bounds. For sliced fruit, checks both `pos.y` and `m_HalfB_pos.y` against screen-relative thresholds scaled by entity scale.

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
    
    // 2. Select based on critical mode × includeOnSideOnly (4 paths)
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

### Fruit::SetFruitType (0x0017621c, 46 lines)

```c
void Fruit::SetFruitType(uint type, float scale) {
    this->m_FruitType = (byte)type;
    
    // VISUAL SCALE — computed from globals, NOT from FRUIT_INFO
    // Chain: globalVec (BSS, likely (1,1,1)) × GOT_config × 0.01 (DAT_0017633c)
    Vec3 entityScale = globalScaleVec * configFloat * 0.01f;
    this->entity.scale = entityScale;       // +0x28..+0x30
    this->entity.baseScale = entityScale;   // +0xa8..+0xb0
    
    // COLLISION SPHERE — uses FRUIT_INFO fields
    FRUIT_INFO* info = &fruitInfoArray[type];  // type × 0x330 stride
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

**Scale chain (verified from binary constants via read_memory):**

| Constant | Address | Value | Purpose |
|----------|---------|-------|---------|
| Scale multiplier | DAT_0017633c | **0.01** | Visual scale base multiplier |
| Collision factor | DAT_00176340 | **0.52** | CollisionBase + 0.52 × CollisionScale |
| Menu fruit post-multiply | DAT_0014f194 | **0.2** | MenuButton scales entity × 0.2 |
| Global scale vec | BSS (0x1F4334) | runtime | Likely (1,1,1) from static init |

**Visual scale computation:**
- `SetFruitType`: `entity.scale = globalVec × configFloat × 0.01` → small value (~0.5-1.0)
- `MenuButton::Init` additionally: `entity.scale *= 0.2` → very small (~0.1-0.2)
- FRUIT_INFO `m_CollisionScale` (25.0) is for **collision radius only**, NOT visual scale

**Fruit::Init call from MenuButton:**
```c
// MenuButton::Init (0x14ee40):
entity = ActorManager::Add(fruitType >= bombThreshold ? 1 : 0, true);
entity->pos = button.pos;
entity->vel = globalScale;  // written to +0x1c..+0x24 (velocity, not scale!)
entity->Init(0, fruitType, NULL);  // scale param = NULL → 1.0
// After Init:
entity->scale *= 0.2;  // DAT_0014f194 — shrink for menu display
```

**FRUIT_INFO fields (corrected roles):**
| Offset | Name | Default | Role |
|--------|------|---------|------|
| +0x244 | m_CollisionScale | 25.0 | Collision radius scaling ONLY |
| +0x248 | m_CollisionBase | 1.0 | Base collision radius |

The visual entity.scale is computed in SetFruitType from globals — it is NOT 25.0.

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

### Fruit_DrawShadows (0x00178f28, static)

Iterates all fruit entities, calls `AddShadow()` for each with scale > 0.
Renders as a single batched triangle strip using the shadow texture.

### Fruit::ClearUnspawned (0x00176d14, static)

```c
void ClearUnspawned(bool killAll) {
    for (Fruit* f : ActorManager::GetEntities(FRUIT_TYPE)) {
        if (killAll || f->m_ChuckDelay > 0)
            f->KillFruit(false);
    }
}
```

### See Also

- [Entities struct](../structs/entities.md) — Fruit struct layout, vtable, all methods
- [Data struct](../structs/data.md) — FRUIT_INFO (0x330 bytes per type)
