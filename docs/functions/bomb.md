# Bomb Functions

## Bomb System

### Bomb::Update (0x001729fc, 195 lines)

```c
void Bomb::Update(float dt) {
    float scaledDt = dt * speedMult;
    
    if (activeFlag == 0) {
        // Normal bomb: countdown → chain spawn
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
        ...
    }
    
    if (outOfBounds(pos)) KillBomb();
    if (!m_pEmitter) m_pEmitter = PSPParticleManager::AddEmitter(fuseHash);
}
```

### Bomb::CollisionResponse (0x0017280c, 85 lines)

| Address | Signature |
|---------|-----------|
| 0x0017280c | `int Bomb::CollisionResponse(Entity*, ulong, ulong, Vec3*)` |

---

