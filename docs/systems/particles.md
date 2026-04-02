# Particle System

## Architecture

```
PSPParticleManager (singleton)
  ├─ m_pParticleArray    — flat array of PSPParticle (164 bytes each)
  ├─ m_ActiveList        — linked list of active PSPParticleEmitters
  ├─ m_pTemplates        — array of PSPEmitterTemplates (loaded from file)
  └─ m_emitters          — MemoryPool<PSPParticleEmitter> (pre-allocated pool)

PSPEmitterTemplate (loaded from data file)
  ├─ hash at +0x40       — ulong identifier (StringHash)
  ├─ maxLifetime at +0x44 — float max emitter duration
  ├─ numSets at +0x4b    — byte count of particle sets
  └─ sets[]              — inline PSPParticleSet array (0x30 bytes each)

PSPParticleEmitter (runtime instance, ~0x4c bytes)
  ├─ timer, position, velocity, scale, template pointer
  └─ linked list (next + back-ref pointer)

PSPParticle (0xa4 = 164 bytes per particle)
  └─ position, velocity, colour, size, rotation, lifetime, etc.
```

## PSPParticleEmitter Struct (~0x4C bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | float | m_Timer | Current time; advances by `dt * m_TimeScale` |
| +0x04 | ushort | m_ParticleHead | First particle index (linked list in array) |
| +0x08 | float | m_Pos_x | Emitter world position |
| +0x0c | float | m_Pos_y | |
| +0x10 | float | m_Pos_z | |
| +0x14 | float | m_Vel_x | Emitter velocity (added to pos each update) |
| +0x18 | float | m_Vel_y | |
| +0x1c | float | m_Vel_z | |
| +0x20 | float | m_TimeScale | Speed multiplier (default 1.0) |
| +0x24 | float | m_field24 | Default 1.0 |
| +0x28 | float | m_ScaleX | Default 1.0 |
| +0x2c | float | m_ScaleY | Default 1.0 |
| +0x30 | float | m_field30 | Default 0.0 |
| +0x34 | float | m_field34 | Default 1.0 |
| +0x38 | byte | m_field38 | Default 0 |
| +0x3c | PSPEmitterTemplate* | m_Template | Pointer to template (searched by hash) |
| +0x40 | PSPParticleEmitter* | m_Next | Linked list next |
| +0x44 | PSPParticleEmitter** | m_pRefPtr | Back-pointer for caller cleanup |
| +0x48 | byte | m_bUpdateWhenPaused | If true, updates even when game paused |

## PSPParticleManager Struct (partial)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | m_pParticleArray | Flat array of PSPParticle (×0xa4) |
| +0x08 | int | m_ActiveCount | |
| +0x0c | PSPParticleEmitter* | m_ActiveList | Linked list head |
| +0x10 | int | m_EmitterTemplateCount | |
| +0x14 | void* | m_pEmitterTemplates | Template array |
| +0x18 | int | m_TemplateCount2 | |
| +0x1c | void* | m_pTemplateData | |
| — | MemoryPool* | m_emitters | Pre-allocated emitter pool |

## PSPEmitterTemplate

Templates are loaded from a data file by `PSPParticleManager::LoadFile`. Each template contains:

| Offset | Type | Name |
|--------|------|------|
| +0x40 | ulong | m_Hash (StringHash identifier) |
| +0x44 | float | m_MaxLifetime |
| +0x4b | byte | m_NumSets |
| +0x4c+ | PSPParticleSet[] | Inline array, 0x30 bytes each |

Template stride: `sizeof_header + numSets * 0x30`

## PSPParticleSet (0x30 bytes, inline in template)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x04 | float | m_StartTime | When to begin spawning |
| +0x08 | float | m_EndTime | When to stop (0 = no end) |
| +0x0c | byte | m_BurstCount | Particles spawned on first frame |
| +0x10 | float | m_SpawnRate | Particles per second (continuous) |

## Key Flows

### AddEmitter (0x1149e0, 56 lines)

```
1. Check pool has capacity
2. Linear search templates by hash: template[i]+0x40 == hash
3. Pop emitter from MemoryPool
4. Initialize all fields to defaults (pos=0, scale=1, timeScale=1)
5. Set m_Template = found template
6. Link to head of active list
7. Return emitter pointer
```

### Emitter::Update (0x115d9c, 53 lines)

```
For each ParticleSet in template:
  if time is within [startTime, endTime]:
    count = (int)(rate * (time + dt*scale - start)) - (int)(rate * (time - start))
    spawn 'count' particles via AddParticle
  if first frame crossing startTime:
    spawn burstCount particles
Advance timer: time += dt * timeScale
Advance position: pos += vel (per component)
```

### Manager::Update (0x115ed8, 37 lines)

```
Walk active emitter linked list:
  if emitter has particles AND timeScale > 0 AND (not paused OR updateWhenPaused):
    Emitter::Update(dt)
  if emitter timer >= template.maxLifetime (or template ended):
    Unlink from active list
    Push back to MemoryPool
```

### Manager::Draw (0x114c64, 382 lines)

```
Reset matrix stack + upload
For each active emitter:
  For each particle in emitter:
    Copy particle data
    Compute position, size, colour, rotation from particle state
    Set texture from template
    Draw textured quad with computed transform
  Layer filtered by param_3 (-1=all, 0=mid, 1=foreground)
```

## PSPParticle (0xA4 = 164 bytes)

Only `field44_0xa0 = 0` set in constructor. Full layout inferred from AddParticle (313 lines):

Key fields (approximate from AddParticle access patterns):
- Position (Vec3) near start
- Velocity components
- Lifetime / age
- Colour (RGBA)
- Size / scale
- Rotation angle
- Parent emitter back-pointer at +0x9C area

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| PSPParticleManager::AddEmitter | 0x001149e0 | 56 | Create emitter from template hash |
| PSPParticleManager::Update | 0x00115ed8 | 37 | Tick all emitters, reclaim dead ones |
| PSPParticleManager::Draw | 0x00114c64 | 382 | Render all particles (layered) |
| PSPParticleManager::ClearEmitter | 0x00114934 | — | Destroy specific emitter |
| PSPParticleManager::LoadFile | 0x00115f60 | — | Load template data from file |
| PSPParticleEmitter::Update | 0x00115d9c | 53 | Spawn particles from sets |
| PSPParticleEmitter::AddParticle | 0x00115644 | 313 | Initialize single particle |
