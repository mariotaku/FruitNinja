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
