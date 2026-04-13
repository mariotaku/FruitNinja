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

## PSPParticleTemplate (0xB8 = 184 bytes per entry)
<!-- Analysed: 2026-04-13T10:30 -->

Loaded in the **first loop** of `PSPParticleManager::LoadFile` (0x00115f60) from `<particleTemplate>` XML elements. Stride = 0xB8 (confirmed: `__dest = __dest + 0x2e`, 0x2e×4 = 0xB8). Up to 0x400 entries in a 0xA0A0-byte scratch buffer. Hash stored separately in `local_11b0[]`.

XML element: `<particleTemplate name="...">` (DAT_00116208 = "particleTemplate").

Float constants used during parse:
- `DAT_001166c4` = 255.0/31.0 ≈ 8.226 — colour channel scale: XML stores 0-31 values, stored as 0-255 bytes.
- `DAT_001161e8` = 60.0 (double) — lifetime divisor: XML `<life>` in 60ths of a second.

| Offset | Type | Name | XML source | Notes |
|--------|------|------|-----------|-------|
| +0x00 | float | m_StartTime | `<life>` text / 60.0 | Lifetime in seconds |
| +0x04 | uint16 | _pad04 | — | Zero-initialised |
| +0x08 | float | m_VelocityMinX | `<velocity min="x y z"/>` | ParseFloat3 or fallback |
| +0x0C | float | m_VelocityMinY | | |
| +0x10 | float | m_VelocityMinZ | | |
| +0x14 | float | m_VelocityMaxX | `<velocity max="x y z"/>` | |
| +0x18 | float | m_VelocityMaxY | | |
| +0x1C | float | m_VelocityMaxZ | | |
| +0x20 | float | m_GravityMinX | `<gravity>` text ParseInt3 | Stored as float |
| +0x24 | float | m_GravityMinY | | |
| +0x28 | float | m_GravityMinZ | | |
| +0x2C | float | m_GravityMaxX | `<gravity_max>` text ParseInt3 | Falls back to min values |
| +0x30 | float | m_GravityMaxY | | |
| +0x34 | float | m_GravityMaxZ | | |
| +0x38 | byte | m_Shape | `<type>` text | 0=Point,1=Vertex,2=Direction,3=Angular |
| +0x39 | byte | m_CoordSystem | `<system>` text | 0=Local,1=Global |
| +0x3A | byte | m_SizeStartMin | `<size startMin=...>` attr (0-255) | |
| +0x3B | byte | m_SizeStartMax | `<size startMax=...>` | |
| +0x3C | byte | m_SizeMidMin | `<size midMin=...>` or avg(startMin,endMin) | |
| +0x3D | byte | m_SizeMidMax | `<size midMax=...>` or avg(startMax,endMax) | |
| +0x3E | byte | m_SizeEndMin | `<size endMin=...>` | |
| +0x3F | byte | m_SizeEndMax | `<size endMax=...>` | |
| +0x40 | int16 | m_CycleXStart | `<cycleX start=...>` | |
| +0x42 | int16 | m_CycleXEnd | `<cycleX end=...>` | |
| +0x44 | int16 | m_CycleYStart | `<cycleY start=...>` | |
| +0x46 | int16 | m_CycleYEnd | `<cycleY end=...>` | |
| +0x48 | int16 | m_RotCycleStart | `<rotateCycle start=...>` | |
| +0x4A | int16 | m_RotCycleEndMin | `<rotateCycle end=...>` | |
| +0x4C | int16 | m_RotCycleEnd | `<rotateCycle start=...>` (second copy) | |
| +0x4E | int16 | m_RotCycleEndMax | `<rotateCycle end=...>` (second copy) | |
| +0x50 | int16 | m_SpinStart | `<spin start=...>` | |
| +0x52 | int16 | m_SpinEnd | `<spin end=...>` | |
| +0x54 | int16 | m_SpinStartMax | `<spin start=...>` max form | |
| +0x56 | int16 | m_SpinEndMax | `<spin end=...>` max form | |
| +0x58 | uint16 | m_BlendMode | `<DestinationBlend>` text | 0x302=GL_SRC_ALPHA,0x303=GL_ONE_MINUS_SRC_ALPHA,0x1=GL_ONE; SourceBlend="One"→0x302 first, then DestinationBlend overwrites |
| +0x5C | float | m_AngleMin | `<angle start=...>` atoi | |
| +0x60 | float | m_AngleMax | `<angle end=...>` atoi | |
| +0x64 | float | m_GridLockStart | `<gridLock start=...>` QueryFloatAttribute | |
| +0x68 | float | m_GridLockEnd | `<gridLock end=...>` QueryFloatAttribute | |
| +0x6C | float | m_FrictionSpeedStart | `<rotateCycle speedStart=...>` | 9 floats 0x6C..0x8B |
| +0x70 | float | m_FrictionSpeedStartMin | | |
| +0x74 | float | m_FrictionSpeedStartMax | | |
| +0x78 | float | m_FrictionSpeedEnd | | |
| +0x7C | float | m_FrictionSpeedEndMin | | |
| +0x80 | float | m_FrictionSpeedEndMax | | |
| +0x84 | float | m_FrictionOffsetMin | `<rotateCycle offsetMin=...>` | |
| +0x88 | float | m_FrictionOffsetMax | `<rotateCycle offsetMax=...>` | |
| +0x8C | float | m_FrictionAngle | `<rotateCycle angle=...>` | |
| +0x90 | float | m_FrictionSpin | `<rotateCycle spin=...>` (default 1.0) | |
| +0x94 | byte | m_ColourStartMaxB | `<color startMax="R G B A">` B | 0-255 scaled from 0-31 XML |
| +0x95 | byte | m_ColourStartMaxG | | |
| +0x96 | byte | m_ColourStartMaxR | | |
| +0x97 | byte | m_ColourStartMaxA | | |
| +0x98 | byte | m_ColourStartMinB | `<color startMin="R G B A">` B | |
| +0x99 | byte | m_ColourStartMinG | | |
| +0x9A | byte | m_ColourStartMinR | | |
| +0x9B | byte | m_ColourStartMinA | | |
| +0x9C | byte | m_ColourMidMinB | midMin (explicit or avg startMin+endMin) | |
| +0x9D | byte | m_ColourMidMinG | | |
| +0x9E | byte | m_ColourMidMinR | | |
| +0x9F | byte | m_ColourMidMinA | | |
| +0xA0 | byte | m_ColourMidMaxB | midMax (explicit or avg startMax+endMax) | |
| +0xA1 | byte | m_ColourMidMaxG | | |
| +0xA2 | byte | m_ColourMidMaxR | | |
| +0xA3 | byte | m_ColourMidMaxA | | |
| +0xA4 | byte | m_ColourEndMinB | `<color endMin="R G B A">` B | |
| +0xA5 | byte | m_ColourEndMinG | | |
| +0xA6 | byte | m_ColourEndMinR | | |
| +0xA7 | byte | m_ColourEndMinA | | |
| +0xA8 | byte | m_ColourEndMaxB | `<color endMax="R G B A">` B | |
| +0xA9 | byte | m_ColourEndMaxG | | |
| +0xAA | byte | m_ColourEndMaxR | | |
| +0xAB | byte | m_ColourEndMaxA | | |
| +0xAC | SmartPtr* | m_Texture | SmartPtr<Mortar::Texture> (4 bytes) | Loaded from `<texture name="...">` + "%s/%s.tex" path |
| +0xB0 | float | m_AspectRatio | texWidth / texHeight | Computed from texture dimensions |
| +0xB4 | int | m_UseDepth | `<useDepth>` attr (QueryIntAttribute) | |

**Colour layout note:** The four colour ranges (startMin, startMax, endMin, endMax) are always present if any `<color ...>` attrs exist. midMin and midMax are optional — if absent they are computed as `avg(startMin, endMin)` and `avg(startMax, endMax)`. RGBA byte order is B,G,R,A at each group (field order in struct).

**Blend modes:** SourceBlend "One" sets 0x302 first, then DestinationBlend "InverseSourceAlpha" overwrites to 0x303, or "One" → 0x01. Final stored value is the DestinationBlend GL enum.

## PSPEmitterTemplate Header (0x4C bytes) + inline PSPParticleSet[]
<!-- Analysed: 2026-04-13T10:30 -->

Loaded in the **second loop** of `PSPParticleManager::LoadFile` from `<emitter>` XML elements. Each emitter header is 0x4C bytes followed immediately by `numSets × 0x30` bytes of `PSPParticleSet` entries. The emitter-header + sets block is variable-length; the array is traversed using `this_04 = this_04 + (byte)this_04[0x4B] * 0x30 + 0x4C`.

XML element: `<emitter name="...">` (DAT_001170b0 = "emitter").
Divisor: `DAT_001170a0` = 60.0 (double) — `<life>` value divided by 60.0 → maxLifetime in seconds.

### PSPEmitterTemplate Header (0x4C bytes)

| Offset | Type | Name | XML source | Notes |
|--------|------|------|-----------|-------|
| +0x00 | char[0x40] | m_Name | `name` attr via strcpy | 64-byte null-terminated string |
| +0x40 | float | m_Hash | StringHash(name) cast to float | Stored as float but is uint32 hash; used for lookup |
| +0x44 | float | m_MaxLifetime | `<life>` text / 60.0 | In seconds at 60fps |
| +0x48 | byte[3] | _pad48 | — | Zero-initialised (CpuFill8 0x4C bytes) |
| +0x4B | byte | m_NumSets | count of `<particleSet>` children | Counted during inner loop |

**Sets follow at +0x4C.**

### PSPParticleSet (0x30 = 48 bytes, inline after header)
<!-- Analysed: 2026-04-13T10:30 -->

Each `<particleSet name="...">` child of `<emitter>` maps to one entry. `pfVar18` advances by `0xC` floats (0x30 bytes) per set.

| Offset | Type | Name | XML source | Notes |
|--------|------|------|-----------|-------|
| +0x00 | float | m_TemplatePtr | `name` attr → templateIndex as float | Patched post-load: `*word0 = m_pTemplates + index * 0xB8` (becomes PSPParticleTemplate*) |
| +0x04 | float | m_TimeStart | `<time start="...">` atof | Spawn window start (seconds) |
| +0x08 | float | m_TimeStop | `<time stop="...">` atof | Spawn window end (seconds) |
| +0x0C | byte | m_InitCount | `<particleNumber init="...">` atoi | Burst count on first activate |
| +0x0D | byte[3] | _pad0d | — | Zero-initialised |
| +0x10 | float | m_PerSec | `<particleNumber perSec="...">` atof | Continuous spawn rate |
| +0x14 | float | _unused14 | — | Always zero; no XML field maps here |
| +0x18 | float | m_VelocityMinX | `<velocity min="x y z"/>` ParseInt3 | Stored as float (from int) |
| +0x1C | float | m_VelocityMinY | | |
| +0x20 | float | m_VelocityMinZ | | |
| +0x24 | float | m_VelocityMaxX | `<velocity max="x y z"/>` ParseInt3 | |
| +0x28 | float | m_VelocityMaxY | | |
| +0x2C | float | m_VelocityMaxZ | | |

**Post-load patching:** After both loops complete and `m_pTemplates` is allocated, the binary iterates all emitters and for each set calls `PSPEmitterTemplate::sets(emitter, i)` to get the set pointer, then writes `set->m_TemplatePtr = m_pTemplates + (int)set->m_TemplatePtr * 0xB8` — replacing the float-encoded index with an actual pointer to the resolved `PSPParticleTemplate`.

## Key Flows
<!-- Analysed: 2026-04-13T11:00 -->

### AddEmitter (0x001149e0, 56 lines)

Signature: `PSPParticleEmitter* AddEmitter(ulong hash, PSPParticleEmitter** ppRef, bool persistent)`

```
1. If pool (GetUsed + 1 < capacity):
2.   Linear search m_pTemplateData walking variable stride
3.     stride = 0x4C + (byte)tmpl[0x4B] * 0x30
4.   If tmpl[0x40] (hash field) == hash:
5.     Pop PSPParticleEmitter from pool
6.     Zero pos, vel, field30; timer=0
7.     m_Template = tmpl
8.     m_pRefPtr = ppRef         ; caller back-pointer for cleanup
9.     m_ScaleX = m_ScaleY = 1.0
10.    m_TimeScale = 1.0
11.    m_field24 = m_field34 = 1.0
12.    m_ParticleHead = 1         ; NOTE: not 0 — 0 is "has no particles"
13.    m_bUpdateWhenPaused = 0
14.    m_field38 = 0
15.    m_Next = m_ActiveList      ; link to head of intrusive list
16.    m_ActiveList = emitter
17.    return emitter
18. If not found and ppRef: *ppRef = NULL; return NULL
```

The `persistent` (param4) argument is accepted but **unused** in this function.

### Emitter::Update (0x00115d9c, 53 lines)

```
newTime = timer + dt * timeScale
for each ParticleSet in template:
  currentTime = timer            ; snapshot before increments
  start = set.m_TimeStart
  stop  = set.m_TimeStop
  // Continuous rate: spawn while inside window (stop==0 → no stop limit)
  if start <= currentTime AND (stop == 0 OR currentTime <= stop):
    rate = set.m_PerSec
    count = (int)(rate * ((currentTime + dt*timeScale) - start))
          - (int)(rate * (currentTime - start))
    for i in [0, count): AddParticle(this, set, mgr)
  // Burst on first frame crossing start time
  if timer <= start AND start < newTime:
    for i in [0, set.m_InitCount): AddParticle(this, set, mgr)
    if timeScale == 0: timer += dt    ; frozen-time handoff
timer = newTime
pos += vel                             ; NOTE: not `vel * dt` — binary bug or
                                       ; intentional per-frame advance
```

**Quirk:** the per-component `pos += vel` (not `vel * dt`) means emitter velocity is in *units per frame*, not per second. The port matches this.

### Manager::Update (0x00115ed8, 37 lines)

```
walker = &m_ActiveList
emitter = m_ActiveList
while emitter:
  if emitter.m_ParticleHead != 0
     AND emitter.m_TimeScale != 0
     AND (!paused OR emitter.m_bUpdateWhenPaused):
    Emitter::Update(emitter, dt)

  // Keep-alive: keep if timer < maxLifetime
  //             OR (maxLifetime <= 0 AND NOT PSPEmitterTemplate::Ends())
  if emitter.m_Timer < template.m_MaxLifetime
     OR (template.m_MaxLifetime <= 0 AND PSPEmitterTemplate::Ends(template) == 0):
    walker = &emitter.m_Next
  else:
    refPtr = emitter.m_pRefPtr
    *walker = emitter.m_Next           ; unlink
    if refPtr: *refPtr = NULL          ; clear caller back-pointer
    pool.Push(emitter)
  emitter = *walker
```

**`PSPEmitterTemplate::Ends()`** (not yet RE'd) presumably checks whether every `PSPParticleSet` has passed its `m_TimeStop` — if so, an infinite-lifetime emitter (`maxLifetime <= 0`) can still be reclaimed. This is how finite sets on an "infinite" emitter self-terminate.

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

---

## Port Status (2026-04-13)

| Component | State | Notes |
|-----------|-------|-------|
| PSPParticleTemplate struct | ✅ ported | Byte offsets in comments only; host sizeof differs due to 8-byte pointer alignment — the port is a reimplementation, not an ABI copy. |
| PSPEmitterTemplate + PSPParticleSet | ✅ ported | Inline array replaced by `std::vector<PSPParticleSet>`; pointer-patch step preserved via encoded-index resolve at end of LoadFile. |
| LoadFile XML parser | ✅ ported | Parses 136 `<particleTemplate>` + 67 `<emitter>` from `particles_fast.xml`. Texture load via TextureManager. Colour BGRA scale 0-31→0-255. |
| AddEmitter | ✅ ported | Signature matches binary `(hash, ppRef, persistent)`; pool replaced by `std::vector<PSPParticleEmitter>`. |
| Emitter::Update (spawn + physics) | ✅ ported | Rate integral, burst, per-frame `pos += vel` quirk all preserved. Particles stored per-emitter in `std::vector<PSPParticle>`. |
| Manager::Update (emitter walk) | ✅ ported | Wired into `GameUpdate`. Infinite emitters (maxLifetime ≤ 0) kept until explicit `ClearEmitter` (pending). Finite-set termination via `Ends()` — not yet RE'd, treated as stay-alive. |
| Manager::Draw | ❌ stub | 382 lines — not yet RE'd or ported. |
| AddParticle | ⚠ approximated | Binary 313 lines; port uses a simplified spawn pulling template min/max. Colour mid-range, grid-lock, angular shapes, and friction fields not yet replicated. |
| ClearEmitter | ❌ stub | Needed for bomb fuse release. |

## See Also

- [Particle functions](../functions/particles.md) -- emitter/particle pseudocode
- [Resources](../resources.md) -- particles XML files
