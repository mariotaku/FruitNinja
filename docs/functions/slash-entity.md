# SlashEntity Functions

## Overview

SlashEntity is the blade trail — the player's primary interaction. It inherits from
Mortar::Entity (base 0x3c bytes) and adds blade-specific state for collision detection,
combo tracking, ghost trails, and rendering as two symmetric triangle strips.

**Struct size**: 388 bytes (0x184)
**Source file**: Slash.cpp (per `_GLOBAL__I_Slash.cpp` at 0x0017e52c)

---

## SlashEntity Struct Layout (0x184 bytes)

### Mortar::Entity base (0x00 - 0x3b)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | Points to SlashEntity vtable at 0x1ea5a0 |
| +0x04 | int | field_0x04 | |
| +0x08 | ushort | m_TrackerID | Entity network tracker ID |
| +0x0a | | (padding) | 2 bytes |
| +0x0c | byte | flags | bit 1 = has collision shape, bit 6 = skip entity |
| +0x10 | float | pos.x | Current touch position X (landscape top/bottom) |
| +0x14 | float | pos.y | Current touch position Y (landscape left/right) |
| +0x18 | float | pos.z | |
| +0x1c | float | vel.x | |
| +0x20 | float | vel.y | |
| +0x24 | float | vel.z | |
| +0x36 | ushort | angle | Blade angle; from Atan2Idx(-bladeDir.x, bladeDir.y) |
| +0x38 | Col* | m_Col | ColLine* (0x20 bytes): blade collision segment |

### SlashEntity own fields (0x3c - 0x183)

| Offset | Type | Name | Init Value | Notes |
|--------|------|------|------------|-------|
| +0x3c | PSPParticleEmitter* | m_TrailEmitter | NULL | Touch trail particle; null when inactive |
| +0x40 | float | m_Scale | 0.0 | Blade glow scale; lerps up in critical, down when idle |
| +0x44 | Colour | m_BaseColour | (constructed) | RGBA, 4 bytes; blended from highlight each frame |
| +0x48 | Colour | m_HighlightColour | (constructed) | RGBA, 4 bytes; target colour from mod |
| +0x4c | byte | m_bFlag4c | 0 | Set to 1 when bomb is hit (activeFlag && !m_bBombFlag88) |
| +0x50 | int | m_SplitPoint | param (0xa0) | Split index for same-screen multiplayer vertex buffers |
| +0x54 | int | (pad/unknown) | | |
| +0x58 | int | m_PointCount | 0 | Blade trail vertex pair count; 4 = min for collision |
| +0x5c | QUADCUSTOMVERTEX* | m_pLeftBuffer | alloc'd | Left/top vertex strip; (splitPoint+2)*0x24 bytes |
| +0x60 | QUADCUSTOMVERTEX* | m_pRightBuffer | alloc'd | Right/bottom vertex strip; (splitPoint+2)*0x24 bytes |
| +0x64 | float | m_BladeDir.x | 0.0 | Blade velocity direction vector |
| +0x68 | float | m_BladeDir.y | 0.0 | |
| +0x6c | float | m_BladeDir.z | 0.0 | |
| +0x70 | float | m_TailPos.x | -65535.0 | Oldest visible trail point |
| +0x74 | float | m_TailPos.y | -65535.0 | |
| +0x78 | float | m_TailPos.z | -65535.0 | |
| +0x7c | float | m_HeadPos.x | -65535.0 | Newest trail point / current tip |
| +0x80 | float | m_HeadPos.y | -65535.0 | |
| +0x84 | float | m_HeadPos.z | -65535.0 | |
| +0x88 | float | m_PrevHeadPos.x | | Previous head position (saved before update) |
| +0x8c | float | m_PrevHeadPos.y | | |
| +0x90 | float | m_PrevHeadPos.z | | |
| +0x94 | float | m_LineLengthSq | -1.0 | |head-tail|^2; -1.0 = no active segment |
| +0x98 | float | m_SpeedScale | 0.0 | Blade speed multiplier (DAT_0017c760=0.0); set to 1.0 in AddPoint |
| +0x9c | int | m_SliceCount | -1 | Incremented +2 per fruit slice; decremented -1 per splat spawn |
| +0xa0 | float | m_SliceTimerA | 0.0 | Zeroed on each slice; counts down for splat spawn timing |
| +0xa4 | float | m_SliceTimerB | 0.0 | Zeroed on each slice; accumulates random delays |
| +0xa8 | float | m_BladeVelAtSlice.x | | Blade dir snapshot at moment of slice |
| +0xac | float | m_BladeVelAtSlice.y | | |
| +0xb0 | float | m_BladeVelAtSlice.z | | |
| +0xb4 | float | m_SlicePos.x | | World position of sliced entity |
| +0xb8 | float | m_SlicePos.y | | |
| +0xbc | float | m_SlicePos.z | | |
| +0xc0 | int | m_SliceEntityType | | Fruit type of last sliced fruit |
| +0xc4 | float | m_SwipeSoundTimer | 0.0 | PlaySwipe cooldown; set to 6.0 (0x40c00000) on swipe |
| +0xc8 | Vec3[6] | m_GhostPositions | (zeroed) | 72 bytes; circular buffer of recent blade directions |
| +0x110 | int | m_GhostIndex | 0 | Current index into m_GhostPositions (mod 6) |
| +0x114 | int | m_GhostCount | 0 | Number of stored ghost positions (max 6) |
| +0x118 | Vec3 | m_GhostDir | (from global) | Averaged direction from ghost buffer |
| +0x124 | float | m_ComboTimer | 0.1 | Time window for combo; reset to 0.0 on slice; expires at 0.1 |
| +0x128 | int | m_ComboCount | 0 | Fruits sliced in current combo |
| +0x12c | int | m_ComboEntityType | 0 | 0=fruit, 1=player2, 2=special |
| +0x130 | MissControl* | m_pComboCtrl | NULL | Combo HUD control; null if no active combo |
| +0x134 | float | m_GhostTimer | 0.0 | Counts up while m_bGhostActive; threshold=0.05 |
| +0x138 | byte | m_bGhostActive | 0 | If true, timer runs -> CreateGhost() |
| +0x13c | int | m_ColEntityA | -1 | First collision vertex index |
| +0x140 | int | m_ColEntityB | -1 | Last collision vertex index |
| +0x144 | byte | m_bBladeActive | 0 | True = blade is a valid collision line; OR'd with 1 each frame |
| +0x148 | float | m_ComboScoreBase | 6.0 | Decremented (combo_n * (rand+0.75)) per slice |
| +0x14c | int | m_ExtraFieldA | -1 | |
| +0x150 | int | m_ExtraFieldB | -1 | |
| +0x154 | int[11] | m_ComboFruitIDs | all -1 | 0x2c bytes; fruit type IDs in current combo |
| +0x180 | ushort | m_AngleCopy | 0 | Updated each frame in AddPoint; copied to Entity::angle |
| +0x182 | | (padding) | | 2 bytes to size 0x184 |

### QUADCUSTOMVERTEX Layout (0x24 bytes per vertex)

Each vertex in the triangle strip buffers:

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | float | x | Position X |
| +0x04 | float | y | Position Y |
| +0x08 | float | z | Position Z |
| +0x18 | uint | colour | Platform colour (RGBA packed) |
| +0x1c | float | u | Texture U coordinate |
| +0x20 | float | v | Texture V coordinate; 0.0 or 1.0 for strip side |
| +0x24 | | | stride end |

Remaining bytes at +0x0c..+0x17 likely contain normal or other vertex data, initialized to 0.0.
The `u` coordinate is used for fade: 0.98 at trail root, 1.0 at tip (used for distance-based fading).

---

## Vtable (at 0x1ea5a0, 17 slots)

| Slot | Offset | Address | Name | Signature |
|------|--------|---------|------|-----------|
| 0 | +0x00 | 0x0017c774 | ~SlashEntity | destructor |
| 1 | +0x04 | 0x0017c7ec | ~SlashEntity | deleting destructor |
| 2 | +0x08 | 0x0017c65c | Init | `void Init(void* p1, long p2, Vec3* p3)` |
| 3 | +0x0c | 0x0017c60c | Release | `void Release()` |
| 4 | +0x10 | 0x0017d664 | Update | `void Update(float dt)` |
| 5 | +0x14 | 0x0017b3b8 | Draw | `void Draw()` — empty/no-op |
| 6 | +0x18 | 0x0017b398 | DrawUpdate | `void DrawUpdate(float dt)` |
| 7 | +0x1c | 0x0019d600 | PostLoad | `void PostLoad()` — base Entity |
| 8 | +0x20 | 0x0019d800 | InRect | `int InRect(ColAABB*)` — base Entity |
| 9 | +0x24 | 0x0017b3bc | CollisionResponse | `int CollisionResponse(Entity*, ulong, ulong, Vec3*)` — returns 0 |
| 10 | +0x28 | 0x0019d608 | Collide | `void Collide(Entity*, Col*, ulong*, Vec3*)` — base Entity |
| 11 | +0x2c | 0x0019d61c | ReceiveMessage | `void ReceiveMessage(Entity*, Message*)` — base Entity |
| 12 | +0x30 | 0x00172f4c | ListenerCallback | `void ListenerCallback(Entity*, Entity*, Message*)` — empty stub |
| 13 | +0x34 | 0x0017e424 | DrawSlice | `void DrawSlice()` — renders blade geometry |
| 14 | +0x38 | 0x002772d4 | (unknown) | base Entity virtual |
| 15 | +0x3c | 0x001bcf60 | (unknown) | base Entity virtual |
| 16 | +0x40 | 0x001eb42c | (unknown/null) | 0x00000000 follows = vtable end |

**Note**: SlashEntity::Draw (slot 5) is a no-op. The actual blade rendering happens in
**DrawSlice** (slot 13, 0x0017e424), called by the game's render pipeline separately.
Ghost trails are drawn in **PreDraw** (0x0017e504), which iterates 8 SlashEntityGhost objects.

---

## All Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x0017c82c | SlashEntity() | constructor | Sets vtable, constructs colours, nulls ptrs |
| 0x0017c868 | SlashEntity() | copy/variant ctor | |
| 0x0017c774 | ~SlashEntity() | destructor | Calls Release(), ~Entity() |
| 0x0017c7b0 | ~SlashEntity() | variant dtor | |
| 0x0017c7ec | ~SlashEntity() | deleting dtor | |
| 0x0017c65c | Init | `void Init(void*, long, Vec3*)` | Allocs ColLine, calls InitPoints(0xa0) |
| 0x0017c60c | Release | `void Release()` | Frees buffers, clears trail emitter |
| 0x0017c340 | InitPoints | `void InitPoints(int splitPoint)` | Allocs 2 vertex buffers, (split+2)*0x24 bytes each |
| 0x0017d664 | Update | `void Update(float dt)` | Main tick: collision, combos, splats (623 lines) |
| 0x0017b92c | UpdatePoints | `void UpdatePoints(float dt)` | Rebuilds vertex strips, fading, collision segment (474 lines) |
| 0x0017d2e4 | UpdateTouchDown | `void UpdateTouchDown(InputEvent*)` | Maps touch to blade position, inserts trail points |
| 0x0017ce0c | AddPoint | `void AddPoint(Vec3 pos, Vec3 dir, float thickness)` | Appends vertex pair to trail buffers |
| 0x0017c584 | PreUpdate | `void PreUpdate(float dt)` | Updates ghosts, mod colour, swipe volume |
| 0x0017e424 | DrawSlice | `void DrawSlice()` | Renders blade as two triangle strips |
| 0x0017e504 | PreDraw | `void PreDraw()` | Draws 8 ghost trails |
| 0x0017b3b8 | Draw | `void Draw()` | No-op (empty function) |
| 0x0017b398 | DrawUpdate | `void DrawUpdate(float dt)` | Sets frame flags |
| 0x0017b3bc | CollisionResponse | `int CollisionResponse(...)` | Returns 0 (slash doesn't receive collision) |
| 0x0017b570 | CollideWithEntity | `int CollideWithEntity(Entity*)` | Line-vs-circle intersection test |
| 0x0017b82c | CreateGhost | `void CreateGhost()` | Spawns SlashEntityGhost for visual echo |
| 0x0017b87c | GetHeadThicknessScale | `float GetHeadThicknessScale() const` | Returns thickness based on head-vs-tail distance |
| 0x0017ccdc | PlaySwipe | `void PlaySwipe()` | Plays random swipe SFX (1 of 6); sets m_ComboScoreBase=6.0 |
| 0x0017b0f4 | UpdateModColour | `void UpdateModColour(Colour*, float)` | Animates blade modifier colours through palette |
| 0x0017d61c | TouchDown | `uint TouchDown(InputEvent*)` | Resets blade, calls UpdateTouchDown; returns 1 |
| 0x0017c50c | TouchMoveX | `bool TouchMoveX(InputEvent*)` | Maps input X to pos.x (landscape top/bottom) |
| 0x0017c490 | TouchMoveY | `bool TouchMoveY(InputEvent*)` | Maps input Y to -pos.y (landscape left/right) |
| 0x0017cbec | CleanupSlash | `void CleanupSlash()` (free fn) | Nulls textures, releases ghosts |
| 0x0017ca00 | SmartPtrNull_Tex | `void SmartPtrNull_Tex(SmartPtr*)` | Helper to null texture SmartPtr |

---

## Key Function Analysis

### SlashEntity::DrawSlice (0x0017e424) — Blade Rendering

```c
void SlashEntity::DrawSlice() {
    // 1. Handle blade deactivation: create ghost + particle burst
    if (m_bBladeActive != 0) {
        byte val = m_bBladeActive & 1;
        m_bBladeActive = val << 1;    // shift: active->deactivating->off
        if (val == 0) {
            // Blade just became inactive
            if (*g_bGhostEnabled)
                CreateGhost();
            if (*g_burstParticleHash != 0) {
                emitter = PSPParticleManager::AddEmitter(*g_burstParticleHash);
                if (emitter) {
                    emitter->pos = this->pos;  // +0x10,+0x14,+0x18
                }
            }
        }
    }

    // 2. Reset some counter if positive
    if (g_slashState->field_0xbc > 0)
        g_slashState->field_0xbc = 0;

    // 3. Draw blade trail if enough points (>= 4)
    if (m_PointCount >= 4) {
        ResetMatrixStack();
        TranslateMatrix(g_globalOffset);  // HUD offset (480, 320, 0)
        UploadMatrices();

        // Select texture: modded blade texture if active, else default
        Texture* tex;
        if (SmartPtr::IsValid(g_slashState->modTexture))  // +0xd8
            tex = g_slashState->modTexture;
        else
            tex = g_slashState->defaultTexture;  // +0xd0

        Texture::Set(tex);

        // Draw two triangle strips: left and right sides of blade
        Mesh::DrawTriStrip(m_pLeftBuffer,  m_PointCount + 1, false, NULL);
        Mesh::DrawTriStrip(m_pRightBuffer, m_PointCount + 1, false, NULL);

        // Unset same texture
        Texture::UnSet(tex);
    }
}
```

**Key insight**: The blade is rendered as **two mirrored triangle strips** — one for each side
of the swipe centerline. Each strip has `m_PointCount + 1` vertices (the +1 is the tapered tip
added by UpdatePoints). The left buffer vertices have the blade offset negated, the right buffer
has it positive (creating width around the centerline).

The `m_bBladeActive` field uses a 2-bit state machine:
- `0b01` (1) = active blade (drawing, colliding)
- `0b10` (2) = deactivating (ghost created, particles spawned)
- `0b00` (0) = inactive

---

### SlashEntity::UpdateTouchDown (0x0017d2e4) — Touch Input Processing

```c
void SlashEntity::UpdateTouchDown(InputEvent* event) {
    // 1. Manage trail particle emitter
    if (WaveManager::CriticalMode()) {
        // Critical mode: use special particle, update emitter position
        if (!m_TrailEmitter) {
            hash = StringHash("CriticalSlashTrail");  // lazy init
            m_TrailEmitter = PSPParticleManager::AddEmitter(hash);
            if (m_TrailEmitter) m_TrailEmitter->followEntity = true;
        }
        m_TrailEmitter->pos = this->pos;
    } else {
        // Normal mode: create/destroy emitter based on touch state
        if (!*g_bTouchActive) {
            if (m_TrailEmitter) {
                PSPParticleManager::ClearEmitter(m_TrailEmitter);
                m_TrailEmitter = NULL;
            }
        } else if (!m_TrailEmitter) {
            m_TrailEmitter = PSPParticleManager::AddEmitter(*g_trailParticleHash);
            if (m_TrailEmitter) m_TrailEmitter->followEntity = true;
        }
    }

    // 2. Skip if bomb timer active
    if (game->bombTimer > 0.0) return;

    // 3. Skip if touch state inactive
    if (g_slashState->field_0x04 == 0) return;

    // 4. Update trail emitter position
    if (m_TrailEmitter)
        m_TrailEmitter->pos = this->pos;

    // 5. Calculate movement delta from last position
    Vec3 delta = pos - m_TailPos;
    float distSq = delta.x*delta.x + delta.y*delta.y;
    float threshold = (m_bBladeActive == 0) ? DAT_0017d5f8 : 25.0f;

    // 6. If moved enough, or first touch, add trail points
    if (distSq >= threshold || m_TailPos.x <= FAR_SENTINEL) {
        // Reset head/tail to current position if first touch
        if (m_TailPos.x <= FAR_SENTINEL) {
            m_TailPos = m_HeadPos = m_PrevHeadPos = pos;
            // Normalize delta from global reference point
        }

        m_ColEntityA = m_PointCount - 2;

        // Interpolate points along the movement path
        Vec3 normalizedDelta = Normalize(delta);
        float headScale = GetHeadThicknessScale();
        Vec3 interpPos = m_TailPos;
        float step = DAT_0017d5fc;  // point spacing

        for (float t = step; t < |delta|; t += step) {
            Vec3 newPos = interpPos + normalizedDelta * step;
            float thicknessFactor = headScale + (t / |delta|) * (1.0 - headScale);
            AddPoint(thicknessFactor, newPos, ...);
            interpPos = newPos;
        }

        // Update trail particle rotation
        if (m_TrailEmitter && *g_trailMode == 2) {
            short angle = Atan2Idx(delta.x, delta.y);
            m_TrailEmitter->rotY = CosIdx(-angle);
            m_TrailEmitter->rotX = -SinIdx(-angle);
        }

        // Final point at current touch position
        AddPoint(1.0, pos, ...);
        m_ColEntityB = m_PointCount - 2;

        // Shift positions: prev = head, head = tail, tail = current
        m_PrevHeadPos = m_HeadPos;
        m_HeadPos = m_TailPos;
        m_TailPos = pos;
    }

    m_bBladeActive |= 1;  // mark blade as active
}
```

---

### SlashEntity::CollideWithEntity (0x0017b570) — Collision Detection

```c
int SlashEntity::CollideWithEntity(Entity* entity) {
    // Guard checks
    if (!m_Col || m_LineLengthSq <= 0 || !entity || !entity->m_Col) return 0;

    // Additional flags check (two global bytes must be 0)
    if (g_collFlags[2] != 0 || g_collFlags[6] != 0) return 0;

    int colType = entity->m_Col->vtable->GetType();
    Col* bladeLine = m_Col;

    if (colType != 1) {
        // Non-circle: use generic Collide
        return bladeLine->vtable->Collide(bladeLine, entity->m_Col, &contactPt);
    }

    // Circle collision (fruit/bomb)
    // First: quick broad-phase check via ColLine::Collide
    if (!bladeLine->vtable->Collide(bladeLine, entity->m_Col))
        return 0;

    float radiusSq = entity->m_Col[1].x * entity->m_Col[1].x;  // radius at offset +0x14

    // Check midpoint of blade line to circle center
    Vec3 midToCenter = bladeLine->center - entity->m_Col->center;
    float distSq = MagnitudeSqr(midToCenter);

    if (distSq >= radiusSq) {
        // Midpoint outside circle — check projected closest point
        Vec3 projected = ...; // project onto perpendicular
        float projDistSq = MagnitudeSqr(projected);

        if (projDistSq < radiusSq) {
            // Closest point on perpendicular is inside circle
            float penetration = Sqrt(radiusSq - projDistSq);
            Vec3 cross = Cross(dir, (0,0,1));
            // Adjust contact point along blade
        }

        // Check both intersection points against line length
        Vec3 ptA = projected + offset;
        Vec3 ptB = projected - offset;
        Vec3 dA = ptA - bladeLine->center;
        Vec3 dB = ptB - bladeLine->center;
        if (MagnitudeSqr(dA) < m_LineLengthSq)
            return 1;
        if (MagnitudeSqr(dB) < m_LineLengthSq)
            return 1;
        return 0;
    }

    return 1;  // midpoint inside circle = definite hit
}
```

---

### SlashEntity::Update (0x0017d664) — Main Tick (623 lines)

```c
void SlashEntity::Update(float dt) {
    // 1. Calculate scaled delta time
    float scaledDt = 0.0;
    if (dt > 0.0) {
        scaledDt = game->dtScaled;  // game+0x38
        char mode = game->mode;     // game+0x04
        g_slashState->field_0x05 = 1;
        if (mode == 2) {  // Zen mode
            scaledDt *= 0.664f;  // DAT_0017d948
            if (PowerUpManager::m_DtMod < 0.9f)
                scaledDt *= PowerUpManager::m_DtMod;
        }
    }

    // 2. Rebuild blade trail geometry
    UpdatePoints(scaledDt);

    // 3. Blade speed visual gauge
    if (m_bBladeActive) {
        float speed = clamp(Magnitude(m_BladeDir) / 15.0, 0, 1);
        float gauge = speed * 0.5 + 0.5;
        g_slashState->bladeSpeedGauge = max(g_slashState->bladeSpeedGauge, gauge);
    }

    // 4. Ghost trail spawning
    if (m_bGhostActive) {
        m_GhostTimer += scaledDt;
        if (m_GhostTimer > 0.05f) {  // DAT_0017d950
            CreateGhost();
            m_bGhostActive = 0;
        }
    }
    m_GhostTimer = 0.0;

    // 5. Early exit checks
    if (m_PointCount < 4 || (flags & 0x40)) {
        m_SpeedScale = 0.0;  // DAT_0017dcd0
        return;
    }
    if (game->bombTimer > 0.0) return;

    // === 6. FRUIT COLLISION LOOP ===
    ActorManager::GetEntityFirst(TYPE_FRUIT=0, &iter);
    while (fruit && !g_slashState->bombHitFlag) {
        int sliced = Fruit::Sliced(fruit);
        if (!sliced && Fruit::IsActive(fruit)) {
            int hit = CollideWithEntity(fruit);
            if (hit) {
                // --- Slice occurred ---
                if (*g_bGhostEnabled) m_bGhostActive = 1;
                m_SliceTimerA = 0.0;
                m_SliceTimerB = 0.0;
                m_SliceCount += 2;
                m_BladeVelAtSlice = m_BladeDir;
                m_SliceEntityType = fruit->m_FruitType;
                m_SlicePos = fruit->pos;

                if (fruit->field_0x10c == 0 && fruit->m_PlayerIdx != 2) {
                    m_ComboTimer = 0.0;
                    m_ComboFruitIDs[m_ComboCount] = fruit->m_FruitType;
                    // Determine combo entity type from player index
                    m_ComboEntityType = (playerIdx == 0) ? 0 : (playerIdx == 2) ? 2 : 1;

                    float scoreBase = m_ComboScoreBase;
                    m_ComboCount++;
                    if (m_ComboCount > 9) m_ComboTimer = 0.095f;  // force expire

                    float randFactor = RandFloat(0.5) + 0.75;
                    m_ComboScoreBase = scoreBase - m_ComboCount * randFactor;

                    // Show combo HUD if 3+ fruits
                    if (m_ComboCount > 2 && CombosEnabled()) {
                        if (!IsOnlineMultiplayer() || m_ComboEntityType != 2) {
                            if (!m_pComboCtrl) {
                                m_pComboCtrl = MissControl::GetFree();
                                MissControl::MakeCombo(m_pComboCtrl, m_SlicePos,
                                    m_ComboCount, m_ComboEntityType);
                                // Attach delegate for combo callback
                            } else {
                                MissControl::MakeCombo(m_pComboCtrl, m_pComboCtrl->pos,
                                    m_ComboCount, m_ComboEntityType);
                            }
                        }
                    }
                } else if (m_ComboTimer < 0.1f) {
                    m_ComboTimer = 0.095f;  // Force expire on special fruit
                }

                // Call fruit's CollisionResponse
                fruit->vtable->CollisionResponse(fruit, this, 0, 0, &m_BladeDir);

                // Critical hit bonus
                if (fruit->m_bCriticalEligible) {
                    m_SliceEntityType += *g_criticalBonusScore;
                    m_ComboScoreBase += (RandFloat(0.5) + 0.75) * -3.0;
                }

                // Spawn blade-hit particles
                if (fruit->field_0x10c == 0) {
                    if (*g_sliceParticleHash)
                        PSPParticleManager::AddEmitter(*g_sliceParticleHash)->pos = this->pos;
                } else {
                    // Special fruit: set bomb-hit flag
                    g_slashState->bombHitFlag = 1;
                    g_slashState->frameCount = 0;
                }
            } else {
                // Near-miss: attract/repel fruit based on power flags
                // flags & 2: attract (add normalized delta * force to fruit vel)
                // flags & 1: repel (subtract)
                // Clamp fruit speed to max 8.0
            }
        }
        ActorManager::GetEntityNext(TYPE_FRUIT=0, &iter);
    }

    // === 7. BOMB COLLISION LOOP ===
    ActorManager::GetEntityFirst(TYPE_BOMB=1, &iter);
    while (bomb && !g_slashState->bombHitFlag) {
        if (Bomb::IsActive(bomb)) {
            int hit = CollideWithEntity(bomb);
            if (hit) {
                if (!(powerFlags & 0x10)) {
                    // Normal bomb hit
                    bomb->vtable->CollisionResponse(bomb, this, 0, 0, &m_BladeDir);
                    g_slashState->bombHitFlag = 1;
                    g_slashState->frameCount = 0;
                    if (bomb->activeFlag && !bomb->m_bBombFlag88) {
                        m_bFlag4c = 1;  // bomb visual flag
                    }
                } else {
                    // Power: deflect bomb (add blade dir * 10.0 to bomb vel)
                    bomb->vel += m_BladeDir * scaledDt * 10.0;
                }
            } else {
                // Near-miss attract/repel for bombs (flags & 4 or & 8)
            }
        }
        ActorManager::GetEntityNext(TYPE_BOMB=1, &iter);
    }

    // === 8. Trail emitter cleanup ===
    if (!m_bBladeActive && m_TrailEmitter) {
        PSPParticleManager::ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = NULL;
    }

    // === 9. Blade colour animation ===
    // Critical mode: scale up to 1.0 (dt*2 rate)
    // Normal: scale down to 0.0 (dt*-2 rate)
    // Blend m_BaseColour from m_HighlightColour based on m_Scale
    // m_BaseColour.a = 0xFF always
    // RGB = lerp(g_defaultColour, m_HighlightColour, 1.0 - m_Scale)

    // === 10. Combo timer expiry ===
    if (m_ComboTimer < 0.1f) {  // COMBO_WINDOW = 0.1
        m_ComboTimer += scaledDt;
        if (m_ComboTimer >= 0.1f) {
            // Combo expired — process scoring
            if (m_ComboCount > 1 && m_ComboFruitIDs[0] >= 0) {
                // Reduce wave speed: game->waveSpeed -= m_ComboCount (min 2)
                if (m_ComboCount > 2 && m_ComboFruitIDs[1] >= 0) {
                    if (mode == ZEN) {
                        WaveManager::AddSpeed(m_ComboCount / 3.0);
                        AddToCurrentScore(m_ComboCount, m_ComboEntityType, true, true);
                        BonusManager::AddCombo(m_ComboCount);
                    } else {
                        if (!IsOnlineMultiplayer() || m_ComboEntityType != 2)
                            AddToCurrentScore(m_ComboCount, m_ComboEntityType, true, false);
                    }
                    // Save combo stats, spawn coins
                    // Check/update best combo in save data
                    // Unlock combo achievements
                }
            }
            // Reset combo state
            m_ComboCount = 0;
            m_ComboEntityType = 0;
            m_pComboCtrl = NULL;
            memset(m_ComboFruitIDs, 0xFF, 0x2c);
        }
    }

    // === 11. Swipe sound timer ===
    float bladeSpeed = Magnitude(m_BladeDir);
    if ((m_SwipeSoundTimer > 0.0 && m_SwipeSoundTimer < 0.05f) || bladeSpeed < 20.0) {
        m_SwipeSoundTimer -= game->dtScaled;
    } else if (m_SwipeSoundTimer <= 0.0 && bladeSpeed > 35.0f) {
        PlaySwipe();
        m_SwipeSoundTimer = 0.05f;
    }

    // === 12. Splat spawning ===
    if (m_SliceTimerA > -1.0) {
        m_SliceTimerA -= scaledDt;
    }
    while (m_SliceCount >= 0 && m_SliceTimerA <= 0.0) {
        // Validate blade velocity is significant
        if (MagnitudeSqr(m_BladeVelAtSlice) > 1.0 && < 10000.0)
            m_BladeVelAtSlice = m_BladeDir;  // use current if stale

        // Calculate random delay
        float delay = 0.01f + RandFloat(0.05f);  // DAT_0017e3f8
        if (m_SliceTimerB + delay < 0.03f)  // DAT_0017e3fc
            delay = m_SliceTimerB + RandFloat(0.05f) + 0.01f;
        m_SliceTimerB = delay;
        m_SliceTimerA += delay;
        m_SliceCount--;

        // Spawn splat entity
        SplatEntity* splat = SplatEntity::GetFree();
        Vec3 splatDir = {
            m_BladeVelAtSlice.x * (RandFloat(0.75) + 0.75),
            m_BladeVelAtSlice.y * (RandFloat(0.75) + 0.75),
            0.0
        };
        SplatEntity::MakeSplat(splat, pos, splatDir, ...);
    }
}
```

---

### SlashEntity::AddPoint (0x0017ce0c) — Trail Point Insertion

Appends a vertex pair (one for left buffer, one for right buffer) to the blade trail.
Handles the circular ghost position buffer (m_GhostPositions), angle calculation,
vertex buffer scrolling when past split point, and same-screen multiplayer UV flipping.

Key operations:
1. Checks if direction changed from previous (skip if identical)
2. Updates m_GhostPositions circular buffer (6 slots)
3. Averages recent ghost directions for smoothing
4. Calculates angle from blade direction: `Atan2Idx(-bladeDir.x, bladeDir.y)`
5. If m_PointCount >= m_SplitPoint - 2: scrolls all vertices back by 2 pairs (drops oldest)
6. Sets m_SpeedScale = 1.0
7. Writes vertex positions: center +/- perpendicular offset (CosIdx/SinIdx * thickness * 9)
8. Sets UV coords: u = 0.5, v = 0.0 or 1.0 (for left/right strip sides)
9. Increments m_PointCount by 2

---

### SlashEntity::InitPoints (0x0017c340) — Buffer Allocation

```c
void SlashEntity::InitPoints(int splitPoint) {
    m_PointCount = 0;
    m_SplitPoint = splitPoint;  // default = 0xa0 = 160
    m_BladeDir = {0, 0, 0};

    // Init 3 position vectors to far sentinel (-65535.0)
    m_TailPos = m_HeadPos = m_PrevHeadPos = {-65535.0, -65535.0, -65535.0};

    // Allocate two vertex buffers
    for (int i = 0; i < 2; i++) {
        buffer[i] = new[(splitPoint + 2) * 0x24];  // 0x24 = 36 bytes per vertex
        // Initialize all vertices to zero, with u=1.0 at +0x14
        for (int j = 0; j < splitPoint; j++) {
            vertex[j] = {0,0,0,0,0, 1.0f, colour, 0, 0.98f};
        }
    }
}
```

Buffer layout: `(splitPoint + 2) * 0x24` = (160 + 2) * 36 = **5832 bytes per buffer**.
The +2 provides room for the tapered tip vertex added by UpdatePoints.

---

### SlashEntity::TouchDown (0x0017d61c)

```c
uint SlashEntity::TouchDown(InputEvent* event) {
    if (!event->field_0x1b && !event->field_0x34) {
        Reset();  // Clear blade state
        if (*g_gameState == 2)
            UpdateModColour(event+6, 1.0);
    }
    UpdateTouchDown(event);
    return 1;
}
```

### SlashEntity::TouchMoveX (0x0017c50c)

```c
bool SlashEntity::TouchMoveX(InputEvent* event) {
    if (game->bombTimer <= 0.0) {
        Vec2 displaySize = DisplayManager::GetSize();
        pos.x = event->mappedX + displaySize.x * -0.5;
        return true;
    }
    return false;
}
```

### SlashEntity::TouchMoveY (0x0017c490)

```c
bool SlashEntity::TouchMoveY(InputEvent* event) {
    if (game->bombTimer <= 0.0) {
        Vec2 displaySize = DisplayManager::GetSize();
        pos.y = -(event->mappedY + displaySize.y * -0.5);  // note negation
        return true;
    }
    return false;
}
```

---

### SlashEntity::CreateGhost (0x0017b82c)

```c
void SlashEntity::CreateGhost() {
    // Circular buffer of 8 ghosts, index at g_slashState+0x38
    int idx = (g_slashState->ghostIndex + 1) & GHOST_MASK;
    if (idx < 0) idx = ~(~((idx-1) * 0x20000000) >> 0x1d) + 1;  // modulo correction
    g_slashState->ghostIndex = idx;
    SlashEntityGhost::StartEffect(
        &g_slashState->ghosts[idx],
        &m_pLeftBuffer,   // passes both left+right buffer pointers
        m_PointCount
    );
}
```

### SlashEntity::GetHeadThicknessScale (0x0017b87c)

```c
float SlashEntity::GetHeadThicknessScale() const {
    float minScale = 0.0f;  // DAT_0017b91c
    if (m_PointCount > 0) {
        int lastIdx = (m_PointCount - 1) * 0x24;
        float dx = m_pLeftBuffer[lastIdx].x - m_pRightBuffer[lastIdx].x;
        float dy = m_pLeftBuffer[lastIdx].y - m_pRightBuffer[lastIdx].y;
        float distSq = dx*dx + dy*dy;
        if (distSq > DAT_0017b920) {
            minScale = Sqrt(distSq) * 0.5;
        }
        float maxThickness = *g_maxThickness * 9.0;
        minScale = minScale / maxThickness;
        if (minScale >= 1.0) minScale = 1.0;
    }
    return minScale;
}
```

---

## Slash.cpp Global State

The global state for the slash system is referenced by `_GLOBAL__I_Slash.cpp` (0x0017e52c) and
used by all SlashEntity functions via GOT-relative addressing. Key elements:

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | Colour | g_bladeColour | Default blade colour (constructed from global) |
| +0x04 | byte | g_touchActive | Touch input active flag |
| +0x05 | byte | g_updateFlag | Set to 1 when dt > 0 |
| +0x3c | SlashEntityGhost[8] | ghosts | 8 ghost trails, 0x10 bytes each (128 bytes total, ends +0xbc) |
| +0x38 | int | ghostIndex | Current ghost circular buffer index |
| +0xbc | int | frameCount | Counts up to 5 then clears deferFlag |
| +0xc0 | int | frameCount2 | |
| +0xc4 | byte | bombHitFlag | Set when bomb is sliced; blocks further collision |
| +0xc8 | float | bladeSpeedGauge | Max blade speed this frame; fed to ItemManager::SetSwipeLoopVol |
| +0xcc | byte | field_0xcc | Cleared by CleanupSlash |
| +0xd0 | SmartPtr<Texture> | defaultTexture | Default blade texture |
| +0xd4 | SmartPtr<Texture> | texture2 | Alt texture |
| +0xd8 | SmartPtr<Texture> | modTexture | Modifier blade texture (if active) |
| +0xf4 | Colour | bombColour | Init = (0, 0, 0, 0xFF) |

---

## SlashEntityGhost (0x10 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | float | timer | Init = 0.0 (DAT_0017e990) |
| +0x04 | int | pointCount | Init = 0 |
| +0x08 | QUADCUSTOMVERTEX* | leftBuffer | Init = NULL |
| +0x0c | QUADCUSTOMVERTEX* | rightBuffer | Init = NULL |

Constructed by `SlashEntityGhost::SlashEntityGhost()` at 0x0017e97c.
`StartEffect` at 0x0017ec24 copies the current blade vertex buffers into the ghost.
`Update` fades the ghost over time; `Draw` renders with decreasing alpha.

---

## Constants

| Address | Name | Value | Used In |
|---------|------|-------|---------|
| DAT_0017c760 | INIT_SCALE | 0.0f | Init: m_Scale, m_SpeedScale, m_GhostTimer |
| DAT_0017c764 | INIT_COMBO_TIMER | 0.1f | Init: m_ComboTimer |
| DAT_0017c408 | FAR_SENTINEL | -65535.0f | InitPoints: init positions far offscreen |
| DAT_0017c40c | ZERO | 0.0f | InitPoints: vertex init |
| DAT_0017d944 | ZERO_F | 0.0f | Update: default scaledDt |
| DAT_0017d948 | ZEN_DT_SCALE | 0.664f | Update: Zen mode dt multiplier |
| DAT_0017d94c | POWER_DT_THRESH | 0.9f | Update: PowerUp dt mod threshold |
| DAT_0017d950 | GHOST_THRESHOLD | 0.05f | Update: ghost spawn timer threshold |
| DAT_0017d96c | COMBO_FORCE_EXPIRE | 0.095f | Update: force combo timer past window |
| DAT_0017dcd0 | DEFAULT_SPEED | 0.0f | Update: speed scale when inactive |
| DAT_0017e004 | COMBO_WINDOW | 0.1f | Update: combo timer expiry threshold |
| DAT_0017e3ec | SWIPE_COOLDOWN | 0.05f | Update: swipe sound cooldown |
| DAT_0017e3f0 | SWIPE_SPEED_THRESH | 35.0f | Update: min blade speed for swipe sound |
| DAT_0017e3f4 | STALE_VEL_THRESH | 10000.0f | Update: blade velocity staleness check |
| DAT_0017e3f8 | SPLAT_BASE_DELAY | 0.01f | Update: base delay between splat spawns |
| DAT_0017e3fc | SPLAT_MIN_DELAY | 0.03f | Update: minimum accumulated delay |
| DAT_0017e404 | COMBO_TIMER_CAP | 0.1f | Update: special fruit combo timer cap |
| DAT_0017c108 | FADE_BASE_U | 0.98f | UpdatePoints: base U texture coord for fading |
| DAT_0017c320 | ZERO_UV | 0.0f | UpdatePoints: UV coordinate |

---

## See Also

- [Entity Structs](../structs/entities.md) -- Mortar::Entity base, Fruit, Bomb
- [Physics system](../systems/physics.md) -- gravity, collision shapes
- [Scoring system](../systems/scoring.md) -- combo scoring pipeline
- [Touch system](../systems/touch.md) -- input event routing
- [Rendering system](../systems/rendering.md) -- DrawTriStrip, matrix stack
