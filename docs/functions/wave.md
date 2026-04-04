# Wave System Functions

## Wave System

### WaveManager::Init (0x0012393c, 470 lines)

| Address | Signature |
|---------|-----------|
| 0x0012393c | `void WaveManager::Init()` |

See `docs/systems/wave-system.md` for full XML parsing flow.

### WaveManager::SpawnFruit (0x001225a0, 248 lines)

```c
void WaveManager::SpawnFruit(long count, long fruitType, SPAWNER_INFO* info, int playerIdx) {
    for (int i = 0; i < count; i++) {
        // Angle from SPAWNER_INFO
        float minAngle = info ? info->m_MinAngle : -1.0;
        float maxAngle = info ? info->m_MaxAngle : 1.0;
        float range = minAngle * 182 + maxAngle * 182;  // 0xb6 scale
        uint16_t angle = (int)(random(range) + minAngle * 182) * 0xb6;
        
        float speed = random(1.5) + 9.5;  // 9.5..11.0
        float velX = sin(angle) * speed * info->speedMultX;
        float velY = cos(angle) * speed * info->speedMultY;
        
        // Spawn type adjustments
        switch (info->spawnType) {
            case 1: velX *= 0.5; break;     // bottom-slow
            case 2: swap velX/velY, mirror; break;  // left side
            case 3: swap velX/velY; break;          // right side
            case 4: random(2) ? case 2 : case 3; break;
        }
        
        Fruit* fruit = ActorManager::Add(TYPE_FRUIT, true);
        fruit->pos = Vec3(posX, posY, 0);
        fruit->vel = Vec3(velX, velY, 0);
        fruit->Init(0, fruitType, &scaleVec);
        fruit->m_TimeScale = info->timeScale;
        fruit->gravity = info->gravityScale * defaultGravity;
        Fruit::Chuck(fruit, delay);
    }
}
```

### WaveManager::SpawnBomb (0x00121fa8)

| Address | Signature |
|---------|-----------|
| 0x00121fa8 | `void WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO*, int playerIdx)` |

### WaveManager::GetCriticalChance (0x001219c4, 17 lines)

| Address | Signature |
|---------|-----------|
| 0x001219c4 | `float WaveManager::GetCriticalChance(int playerIdx)` |

### WaveManager::CriticalMode (0x001219e4, 14 lines)

| Address | Signature |
|---------|-----------|
| 0x001219e4 | `bool WaveManager::CriticalMode(int playerIdx)` |

---


---

## WaveManager::Update (0x001259d8, 89 lines)

```c
void WaveManager::Update(float dt) {
    // Reset per-frame multipliers
    m_CritChanceMult = 1.0;
    field_0x78 = 1.0;   // dtMod
    field_0x64 = 1.0;
    spawnLevel = 1.0;
    field_0x6c = 1.0;
    
    // Online multiplayer: zero dt if not synced
    if (isOnlineMultiplayer && !game->field_0x199) dt = 0;
    
    // Update power-ups (they modify dtMod, spawnLevel, etc.)
    if (game->transitionTimer < 1.0 && PowersEnabled()) {
        PowerUpManager::Update(dt);
        field_0x78 = PowerUpManager::m_DtMod;
    } else {
        PowerUpManager::SetDefaults();
        field_0x78 = 1.0;
    }
    
    // Wave speed accumulator
    float speed = field_0x74 + dt * speedMultiplier[gameMode];
    speed = clamp(speed, minSpeed[gameMode], maxSpeed[gameMode]);
    field_0x74 = speed;
    
    // Time accumulator for stat tracking
    game->field_0x1ac += dt;
    if (game->field_0x1ac crosses 10.5) clear daily stat;
    
    // Spawn waves on fixed timestep
    float accumDt = field_0x2d4 + dt;
    while (accumDt > WAVE_STEP) {
        if (waveInfos.size() > 0)
            UpdateWave(WAVE_STEP, playerIdx, 0);
        accumDt -= WAVE_STEP;
    }
    field_0x2d4 = accumDt;
    
    // Check for game completion conditions
    ...
}
```

Key insight: waves are updated on a **fixed timestep** (`WAVE_STEP` constant), not per-frame. The accumulator `field_0x2d4` ensures consistent spawning regardless of frame rate.

---

## WaveManager::GetNextWave (0x00124f10, 227 lines)

Selects the next wave for a player. Core wave progression logic.

```c
WAVE_INFO* WaveManager::GetNextWave(int playerIdx) {
    // 1. Increment wave counter
    AchievementManager::GetInstance();
    FruitSaveData::UnlockTotals();
    int score = GetCurrentScore(0);
    AchievementManager::UnlockScoreAchievement(score);
    AchievementManager::UnlockTotalFruitAchievement(game->totalFruitSliced);
    
    m_WaveCount[playerIdx]++;
    if (m_WaveCount[playerIdx] > 1)
        m_CurrentWave[playerIdx]->field_0x34 += 1.0;  // speed ramp
    
    // 2. If wave queue exists (survival/combo modes), pop from queue
    if (m_pWaveQue != NULL) {
        m_CurrentWave[playerIdx] = waveList[m_pWaveQue->currentIndex];
        // (uses queue, not score-based selection)
    }
    // 3. Otherwise: score-based wave selection
    else {
        int totalWeight = 0;
        int matchCount = 0;
        WAVE_INFO* candidates[20];
        
        // Iterate all WAVE_INFOs, find those whose waveNo range contains current wave count
        for (WAVE_INFO* wi : waveInfos) {
            // Update max spawn count with growth factor
            if (wi->waveChanceGrowth > 0) {
                int maxSpawns = wi->maxSpawns;
                if (wi->currentMax < maxSpawns) {
                    float growth = (float)maxSpawns * wi->waveChanceGrowth;
                    if (growth < 1.0) growth = 1.0;
                    wi->currentMax = min(maxSpawns, (int)(wi->currentMax + growth));
                }
            }
            // Check wave range: waveNo <= currentWave <= until (or until == -2 = forever)
            if (wi->waveNo <= m_WaveCount[playerIdx] &&
                (m_WaveCount[playerIdx] <= wi->until || wi->until == -2)) {
                if (matchCount == 0)
                    m_CurrentWave[playerIdx] = wi;
                candidates[matchCount++] = wi;
                totalWeight += wi->currentMax;
            }
        }
        
        // 4. Build ChooseFrom fruit list (if score threshold exceeded)
        if (matchCount > 0) {
            int queueSize = candidates[0]->field_0x18;  // chooseFrom count
            if (queueSize > 0 && m_ScoreThreshold[playerIdx] < candidates[0]->waveNo) {
                if (queueSize > 32) queueSize = 32;
                m_ScoreThreshold[playerIdx] = candidates[0]->waveNo;
                m_FruitQueueSize[playerIdx] = queueSize;
                
                for (int i = 0; i < queueSize; i++) {
                    char* typeName = candidates[0]->chooseFrom[i];
                    int fruitType = Fruit::FruitType(typeName, false);
                    if (fruitType < 0) {
                        // "random" keyword → pick random, avoid duplicates
                        fruitType = Fruit::RandomFruit(false);
                        while (fruitType appears in queue so far && i < fruitCount - 2)
                            fruitType = Fruit::RandomFruit(false);
                    }
                    m_FruitQueue[playerIdx][i] = fruitType;
                }
            }
        }
        
        // 5. Weighted random selection among candidates (if multiple match)
        if (matchCount > 1) {
            uint roll = Rand32(rng, totalWeight * 10);
            int cumulative = 0;
            for (int i = 0; i < matchCount; i++) {
                cumulative += candidates[i]->currentMax * 10;
                if (roll < cumulative) {
                    m_CurrentWave[playerIdx] = candidates[i];
                    break;
                }
            }
        }
    }
    
    // 6. Set wave timing
    WAVE_INFO* wave = m_CurrentWave[playerIdx];
    if (wave->wave_dt > 0) {
        float dt = max(MIN_WAVE_DT, wave->wave_dt + wave->wave_dt_inc * wave->field_0x34);
        m_WaveDt[playerIdx] = dt;
    } else {
        m_WaveDt[playerIdx] = 0.0;
    }
    
    // Next wave delay (with speed ramp)
    float delay = wave->nextWaveDelay;
    if (wave->delaySpeedScale != 0)
        delay = max(MIN_WAVE_DT, delay + wave->delaySpeedScale * m_Speed[playerIdx]);
    m_NextWaveDelay[playerIdx] = delay;
    
    // 7. Reset all spawners in this wave
    for (int i = 0; i < wave->spawnerCount; i++)
        SPAWNER_INFO::Reset(&wave->spawners[i], wave->field_0x34);
    
    // 8. Decrement PROBABILITY_OVERIDE counters, erase expired
    for (auto it = probabilityOverrides[playerIdx].begin(); ...) {
        if (override->counter > 0) {
            override->counter--;
            if (override->counter == 0) { erase(it); continue; }
        }
        override->field_0x08 = 0;
        it++;
    }
    
    // Multiplayer sync
    if (IsMultiplayer()) SendWaveSyncPacket();
    
    return wave->field_0x68;  // wave hash/ID
}
```

### Key Concepts

- **Score-based progression**: Waves are selected by matching `waveNo..until` range against current wave count
- **Weighted random**: When multiple waves match, selection uses `currentMax * 10` as weight
- **ChooseFrom system**: Waves can define a fruit selection pool; "random" entries use `Fruit::RandomFruit` with duplicate avoidance
- **Speed ramp**: `field_0x34` increments each wave, affecting dt and delay calculations
- **Wave queue**: Survival/combo modes use a pre-built queue instead of score-based selection

---

## WaveManager::UpdateWave (0x00125390, 298 lines)

Per-tick wave processing. Spawns fruits/bombs from the current wave's spawners.

```c
void WaveManager::UpdateWave(float dt, int waveManager, int playerIdx) {
    game->field_0x470 = false;
    UpdateComboSpeed(waveManager, dt);
    
    // Accumulate play time
    if (game->field_0x170 != 0) {
        waveManager->field_0x40 += dt;
        waveManager->field_0x44 += dt;
    }
    
    // Skip if networking active
    if (UpdateNetworking(dt, waveManager)) return;
    if (m_CurrentWave[playerIdx] == NULL) return;
    
    // Wave delay countdown
    float waveTimer = m_WaveTimer[playerIdx];
    if (waveTimer > 0) {
        m_WaveTimer[playerIdx] = waveTimer - dt;
        return;  // still waiting
    }
    m_WaveTimer[playerIdx] = 0.0;  // reset
    
    // Process each spawner in current wave
    for (int s = 0; s < wave->spawnerCount; s++) {
        SPAWNER_INFO* spawner = &wave->spawners[s];
        float dtMod = waveManager->field_0x78;  // power-up dt modifier
        if (dtMod <= 1.0) dtMod = 1.0;
        
        // Spawner delay countdown
        spawner->timer -= dt * dtMod;
        
        while (spawner->remainingCount > 0) {
            if (spawner->spawnCount < 1) {
                spawner->timer = 0.0;
                spawner->remainingCount = 0;
                break;
            }
            
            // Pick spawn type from spawner's type list
            uint typeIdx = Rand32(rng, spawner->typeCount);
            int fruitType = spawner->typeList[typeIdx];
            
            // If >50% spawned and type is bomb, re-roll to avoid bomb-heavy end
            if (spawner->typeCount > 1 && spawnedCount >= totalCount / 2) {
                while (fruitType == -2)  // -2 = bomb
                    fruitType = spawner->typeList[Rand32(rng, spawner->typeCount)];
            }
            
            if (fruitType == -1) {
                // -1 = "random" → check PROBABILITY_OVERIDEs for power-up fruit
                // Complex power-up spawning logic:
                //   - Check arcade time remaining
                //   - Check bomb spawn counter
                //   - Weighted random from probability overrides
                //   - If fruit has active powers, spawn as power-up fruit
                //   - Otherwise increment bomb counter and spawn normally
                goto handlePowerUpOrBomb;
            }
            
            // Check if type name is "bomb"
            if (typeName == "bomb") goto handlePowerUpOrBomb;
            
            // Spawn!
            if (fruitType == -2) {
                // Bomb
                bombCount++;
                if (bombChance > 0.0)
                    SpawnBomb(waveManager, 1, spawner, playerIdx);
            } else {
                // Fruit
                if (fruitChance > 0.0)
                    SpawnFruit(waveManager, 1, fruitType, spawner, playerIdx);
            }
            
            game->field_0x23c[playerIdx] = true;  // wave has spawned
            spawner->remainingCount--;
            
            // Refill spawner timer
            float spawnDt = max(0.0, spawner->dt + spawner->dtInc * wave->field_0x34);
            spawner->timer += spawnDt;
        }
    }
    
    // When all spawners done and no wave processing, get next wave
    if (!IsWaveProcessing(waveManager, playerIdx) && !game->field_0x470) {
        if (m_CurrentWave[playerIdx] != NULL) {
            float nextDelay = m_NextWaveDelay[playerIdx];
            if (nextDelay > 0) {
                nextDelay -= dt;
                m_NextWaveDelay[playerIdx] = nextDelay;
                if (nextDelay > 0) return;  // still waiting for delay
            }
        }
        GetNextWave(waveManager, playerIdx);
    }
}
```

### Key Concepts

- **Spawner processing**: Each wave has N spawners, each with a type list, count, and delay timer
- **Type resolution**: -1 = "random" (uses PROBABILITY_OVERIDE), -2 = "bomb"
- **Bomb limiting**: After 50% of a spawner's count, bombs are re-rolled to prevent bomb-heavy endings
- **Power-up fruit**: PROBABILITY_OVERIDEs can override random fruit with power-up-carrying fruit
- **Wave transitions**: When all spawners exhaust and `IsWaveProcessing` returns false, `GetNextWave` is called after `nextWaveDelay`

---

## WaveManager::SetupWaveQue (0x00124564, 142 lines)

Builds the wave queue for survival/combo modes. Used instead of score-based selection.

```c
void WaveManager::SetupWaveQue() {
    // 1. Destroy old queue
    if (m_pWaveQue) { delete m_pWaveQue; m_pWaveQue = NULL; }
    m_pWaveQue = new WaveQue(0xC);  // {count=0, threshold=0.0, index=0}
    WaveQue::WaveQue(m_pWaveQue);
    
    if (m_pWaveQueItem) { delete m_pWaveQueItem; m_pWaveQueItem = NULL; }
    m_pWaveQueItem = new WaveQueItem(0x1C);  // zeroed
    WaveQueItem::WaveQueItem(m_pWaveQueItem);
    
    // 2. Calculate total weight from all waves
    int totalWeight = 0;
    for (WAVE_INFO* wi : waveInfos)
        totalWeight += wi->field_0x3c;  // wave weight/chance
    
    // 3. Fill queue (budget = 27.0 time units)
    float budget = 27.0;
    while (budget > 1.0) {
        // Weighted random wave selection
        uint roll = Rand32(rng, totalWeight);
        WAVE_INFO* picked = waveInfos[0];
        for (WAVE_INFO* wi : waveInfos) {
            picked = wi;
            roll -= wi->field_0x3c;
            if (roll < 1) break;
        }
        
        // Reject if wave duration > remaining budget + 1.0
        while (picked->field_0x28 > budget + 1.0) {
            // Re-roll
            roll = Rand32(rng, totalWeight);
            picked = waveInfos[0];
            for (WAVE_INFO* wi : waveInfos) {
                picked = wi;
                roll -= wi->field_0x3c;
                if (roll < 1) break;
            }
        }
        
        budget -= picked->field_0x28;  // subtract wave duration
        WaveQue::AddWave(m_pWaveQue, picked, false);
    }
    
    // 4. Post-process queue
    m_pWaveQue->threshold = 27.0;  // 0x41d80000
    WaveQue::RandomiseOrder(m_pWaveQue, true);
    
    // 5. Append last 4 waves from wave list (bookend waves) as forced entries
    WAVE_INFO* secondLast = waveInfos[waveInfos.size() - 2];
    WaveQue::AddWave(m_pWaveQue, secondLast, true);  // ×4
    WaveQue::AddWave(m_pWaveQue, secondLast, true);
    WaveQue::AddWave(m_pWaveQue, secondLast, true);
    WaveQue::AddWave(m_pWaveQue, secondLast, true);
    
    // 6. Add specials and final wave
    WaveQue::AddSpecials();
    WAVE_INFO* last = waveInfos[waveInfos.size() - 1];
    WaveQue::AddWave(m_pWaveQue, last, true);
}
```

### Key Concepts

- **Budget system**: Queue is filled until 27 time units are consumed
- **Weight-based selection**: Each wave has a weight at `field_0x3c`; larger weight = more likely picked
- **Duration fitting**: Waves whose duration exceeds remaining budget +1 are rejected (re-rolled)
- **Randomisation**: After filling, `RandomiseOrder(true)` shuffles the queue
- **Bookend waves**: 4 copies of the second-to-last wave are appended as forced entries, plus specials and the final wave
- **Used for**: Survival and combo modes (classic/arcade use score-based GetNextWave instead)

---

## See Also

- [Wave system overview](../systems/wave-system.md) -- WAVE_INFO/SPAWNER_INFO struct layouts, XML parsing
- [RNG system](../engine/rng.md) -- Math::Random used for wave selection
- [Fruit functions](fruit.md) -- RandomFruit, FruitType, FruitInfo
- [Power-ups system](../systems/power-ups.md) -- PROBABILITY_OVERIDE, power-up fruit spawning
