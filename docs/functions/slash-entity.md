# SlashEntity Functions

## SlashEntity (Blade)

### SlashEntity::Update (0x0017d664, 608 lines)

```c
void SlashEntity::Update(float dt) {
    float scaledDt = ...;  // adjusted for power-ups
    
    UpdatePoints(scaledDt);  // rebuild blade trail
    
    // Blade speed visual
    if (m_bBladeActive) {
        float speed = Vec3::Magnitude(m_BladeDir) / 15.0;
        taskState->bladeSpeedGauge = max(taskState->bladeSpeedGauge, clamp(speed, 0, 1) * 0.5 + 0.5);
    }
    
    // Ghost trail
    if (m_bGhostActive) {
        m_GhostTimer += scaledDt;
        if (m_GhostTimer > GHOST_THRESHOLD) { CreateGhost(); m_bGhostActive = false; }
    }
    
    if (m_PointCount < 4 || (flags & 0x40)) { m_SpeedScale = DEFAULT; return; }
    if (game->bombTimer > 0) return;
    
    // === Fruit collision loop ===
    ActorManager::GetEntityFirst(TYPE_FRUIT, &iter);
    while (fruit && !bombHitFlag) {
        if (!Fruit::Sliced(fruit) && Fruit::IsActive(fruit)) {
            int hit = CollideWithEntity(fruit);
            if (hit) {
                // Store slice data
                m_SliceCount += 2;
                m_BladeVelAtSlice = m_BladeDir;
                m_SlicePos = fruit->pos;
                m_ComboFruitIDs[m_ComboCount] = fruit->type;
                m_ComboCount++;
                m_ComboTimer = 0;
                
                // Create/update combo display
                if (m_ComboCount > 2) {
                    MissControl* mc = MissControl::GetFree();
                    MissControl::MakeCombo(mc, m_SlicePos, m_ComboCount, entityType);
                }
                
                // Call fruit's collision response
                fruit->vtable->CollisionResponse(fruit, this, 0, 0, &m_BladeDir);
            }
        }
        ActorManager::GetEntityNext(TYPE_FRUIT, &iter);
    }
    
    // === Bomb collision loop ===
    ActorManager::GetEntityFirst(TYPE_BOMB, &iter);
    while (bomb && !bombHitFlag) {
        if (Bomb::IsActive(bomb)) {
            int hit = CollideWithEntity(bomb);
            if (hit)
                bomb->vtable->CollisionResponse(bomb, this, 0, 0, &m_BladeDir);
        }
        ActorManager::GetEntityNext(TYPE_BOMB, &iter);
    }
    
    // Particle trail management
    if (!m_bBladeActive && m_TrailEmitter)
        PSPParticleManager::ClearEmitter(m_TrailEmitter);
    
    // Blade colour animation
    ...
    
    // Combo timer expiry → score combo
    if (m_ComboTimer < COMBO_WINDOW) {
        m_ComboTimer += scaledDt;
        if (m_ComboTimer >= COMBO_WINDOW && m_ComboCount > 2) {
            AddToCurrentScore(m_ComboCount, m_ComboEntityType, true, ...);
            BonusManager::AddCombo(m_ComboCount);  // zen mode
            Coin::MakeCoins(...);
        }
    }
}
```

### SlashEntity::CollideWithEntity (0x0017b570, 78 lines)

```c
// Line segment vs circle intersection test
int SlashEntity::CollideWithEntity(Entity* entity) {
    if (!m_Col || m_LineLengthSq <= 0 || !entity || !entity->m_Col) return 0;
    
    int colType = entity->m_Col->vtable->GetType();
    
    if (colType != 1) {
        // Non-circle: use generic Collide
        return m_Col->vtable->Collide(m_Col, entity->m_Col, &contactPt);
    }
    
    // Circle collision (fruit/bomb)
    float radius = entity->m_Col[1].field0;  // radius stored in second Col slot
    float radiusSq = radius * radius;
    
    // Check midpoint of blade line to circle center
    Vec3 midToCenter = m_Col->center - entity->m_Col->center;
    float distSq = MagnitudeSqr(midToCenter);
    if (distSq >= radiusSq) {
        // Midpoint outside circle — check closest point on segment
        Vec3 closest = m_Col->center + projectOntoSegment(...);
        if (MagnitudeSqr(closest - entity->m_Col->center) < radiusSq)
            return 1;
        return 0;
    }
    
    // Check both endpoints are within line length
    Vec3 endA = m_Col->a - entity->center;
    Vec3 endB = m_Col->b - entity->center;
    if (MagnitudeSqr(endA) < m_LineLengthSq || MagnitudeSqr(endB) < m_LineLengthSq)
        return 1;
    return 0;
}
```

### SlashEntity::UpdatePoints (0x0017b92c, 470 lines)

| Address | Signature |
|---------|-----------|
| 0x0017b92c | `void SlashEntity::UpdatePoints(float dt)` |

### SlashEntity::UpdateTouchDown (0x0017d2e4, 187 lines)

| Address | Signature |
|---------|-----------|
| 0x0017d2e4 | `void SlashEntity::UpdateTouchDown(InputEvent* event)` |

### SlashEntity::Init (0x0017c65c, 75 lines)

| Address | Signature |
|---------|-----------|
| 0x0017c65c | `void SlashEntity::Init(void*, long, Vec3*)` |

---

