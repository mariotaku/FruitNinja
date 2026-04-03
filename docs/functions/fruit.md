# Fruit Functions

## Fruit System

### Fruit::Init (0x00176708, 142 lines)

| Address | Signature |
|---------|-----------|
| 0x00176708 | `void Fruit::Init(void* p1, long fruitType, Vec3* scale)` |

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
    
    // Set entity scale from global fruit scale vector
    Vec3 entityScale = globalScaleVec * CONST_A * CONST_B;
    this->entity.scale = entityScale;       // +0x28..+0x30
    this->entity.baseScale = entityScale;   // +0xa8..+0xb0
    
    // Collision sphere setup from FRUIT_INFO
    FRUIT_INFO* info = &fruitInfoArray[type];  // type × 0x330 stride
    float radius = info->m_SpeedMult + CONST_C * info->m_Scale;  // +0x248 + factor × +0x244
    
    if (radius <= 0.0) {
        // No collision for this fruit type — delete ColSphere
        if (this->m_Col) { delete this->m_Col; this->m_Col = NULL; }
    } else {
        // Create/update collision sphere
        if (!this->m_Col) {
            this->m_Col = new ColSphere();
        }
        this->m_Col->pos = Vec3(this->pos_x, this->pos_y, CONST_Z);
        this->m_Col->radius = radius * scale;
    }
}
```

**FRUIT_INFO fields used:**
| Offset | Name | Default | Role in SetFruitType |
|--------|------|---------|----------------------|
| +0x244 | m_Scale | 25.0 | Collision radius scaling factor |
| +0x248 | m_SpeedMult | 1.0 | Base collision radius |

**Note:** Collision radius = `base + factor × scale` from FRUIT_INFO, then multiplied by the spawn `scale` parameter. If the computed radius is ≤ 0, the fruit has no collision (cannot be sliced).

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

### See Also

- [Entities struct](../structs/entities.md) — Fruit struct layout, m_FruitType at +0x3c
- [Data struct](../structs/data.md) — FRUIT_INFO (0x330 bytes per type)
