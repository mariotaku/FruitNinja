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

