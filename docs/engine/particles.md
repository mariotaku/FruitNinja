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
<!-- Analysed: 2026-04-13T16:00 -->

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | m_pParticleArray | Flat array of PSPParticle (×0xa4) |
| +0x04 | uint16 | m_FreeListHead | Head index of free-particle linked list; 0 = pool full. AddParticle pops from here; Draw pushes dead particles here. |
| +0x08 | int | m_ActiveCount | Reset to 0 at Draw start; incremented by rendered-particle count per template |
| +0x0c | PSPParticleEmitter* | m_ActiveList | Linked list head |
| +0x10 | int | m_TemplateCount | Count of PSPParticleTemplate entries |
| +0x14 | void* | m_pTemplates | Template array (stride 0xB8 each) |
| +0x18 | int | m_TemplateCount2 | |
| +0x1c | void* | m_pTemplateData | PSPEmitterTemplate array (variable stride) |
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
| +0x04 | uint16 | m_ParticleListHead | — | Live-particle linked-list head index for this template. AddParticle writes new idx here; Draw walks from here. Zero = no active particles. |
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
<!-- Analysed: 2026-04-13T16:00 -->

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

### PSPParticleEmitter::AddParticle (0x00115644, 313 lines)
<!-- Analysed: 2026-04-13T16:00 -->

Signature: `void AddParticle(PSPParticleSet* set, PSPParticleManager* mgr)`

**Slot allocation (free-list pop):**
```
idx = mgr->m_field04          ; head of free list (ushort)
if idx == 0: return            ; pool full
particle = mgr->m_pParticleArray + idx * 0xa4
mgr->m_field04 = *(ushort*)(particle + 0x40)   ; pop: advance free-list head
*(ushort*)(set->m_TemplatePtr + 4) = idx        ; prepend to template's active list
mgr->m_ActiveCount += 1
```
Note: `set->m_TemplatePtr + 4` is `*(ushort*)(PSPParticleTemplate+4)` — the per-template live-list head. This means each PSPParticleTemplate owns a separate linked list of its live particles. The template field at +0x04 is `m_ParticleListHead` (ushort, confirmed from Draw which reads it as `uVar22 = *(ushort*)(pfVar8+1)`).

**PSPParticleTemplate correction — `_pad04` is `m_ParticleListHead` (ushort):** The field previously listed as padding at +0x04 is actually the live-particle-list head index. AddParticle writes the new idx there; Draw walks from it.

**Lifetime and age carry:**
```
particle->m_Lifetime = template->m_StartTime   ; seconds
particle->m_AgeCarry = lifetime - lifetime * emitter->m_field24
                                                ; = 0 when m_field24 == 1.0
```

**Position (CoordSystem branch):**
```
if template->m_CoordSystem == 0:  // Local
    basePos = &emitter->m_Pos_x
else:                              // Global
    basePos = *(float**)(globalPtr)   ; reads a global world-origin pointer
particle->m_Pos = *basePos
```

**Initial gravity (from template velocity min/max range):**
```
// These are the template's gravity fields (+0x08..+0x34), NOT set velocity
_Stack_84 = template->velocityMax(Vec3 at +0x14) - template->velocityMin(Vec3 at +0x08)
r = RandFloat()
_Stack_90 = _Stack_84 * r
particle->m_Gravity = template->velocityMin + _Stack_90
```
(Template velocity fields `m_VelocityMin/Max` at +0x08..+0x1C directly map to initial gravity, confirmed by Ghidra's `pfVar14+8` through `pfVar14+0x14`.)

**Set velocity → particle velocity (shape-independent base):**
```
local_78 = lerp(set->velMin, set->velMax, rand3())   ; 3 separate RandFloat calls
local_78 *= emitter->m_field34
// Apply emitter rotation (field30 is sin of rotation angle):
tmp_x = local_78.x
local_78.x = local_78.y * emitter->m_field30 + tmp_x * emitter->m_ScaleY
local_78.y = local_78.y * emitter->m_ScaleY  - emitter->m_field30 * tmp_x
local_78 *= 0.5                    ; halved before storing
particle->m_Vel = local_78
```
`emitter->m_field30` is a sine value (rotation), `m_ScaleY` is cosine — so this is a 2D rotation matrix applied to the XY velocity plane. `m_field34` is an overall velocity scale.

**Shape-type branching (`template->m_Shape` at +0x38 = `*(char*)(pfVar14+0xe)`):**
- `0 = Point`: no extra branching; pos = basePos, vel = local_78 (already set above).
- `1 = Vertex` (`== '\x01'`): subtracts half-velocity from position, effectively centering the particle on the emitter: `particle->pos -= particle->vel`.
- `2 = Direction` (`== '\x02'`): adds the angle of the velocity vector to the rotation angle: `particle->m_RotAngle += Math::Atan2Idx(vel.x, vel.y)`.
- `3 = Angular`: the `m_field38 != 0` branch — swaps pos.x↔pos.y and vel.x↔vel.y (grid-lock mode), then mirrors the gravity vector based on sign of emitter pos.x, and mirrors vel.x based on sign of particle pos.x. Used for particles that should align to axes.

**Grid-lock (Angular mode only, `m_field38 != 0`):**
```
if m_field38 != 0:
    SWAP(particle->pos.x, particle->pos.y)
    signX = sign(particle->pos.x)   ; +1 or -1 (NaN-safe check)
    particle->gravity.x *= signX
    particle->gravity *= m_field34
    SWAP(local_78.x, local_78.y)
    signPX = sign(emitter->m_Pos_x)
    local_78.x *= signPX
    local_78 *= m_field34
```
This causes particles to fire away from the axis center depending on which quadrant the emitter is in.

**Rotation angle (spin):**
```
spinDeg = lerp(template->m_SpinStart, template->m_SpinStartMax, rand)
particle->m_RotAngle = (short)(spinDeg * 182.044)   ; 65536/360 ≈ 182.044
if m_field38 != 0:
    offset = (pos.x > 0) ? -0x4000 : +0x4000    ; ±90° in ushort units
    particle->m_RotAngle += offset
if shape == Direction:
    particle->m_RotAngle += Atan2Idx(vel.x, vel.y)
```

**Rotation matrix initialisation:**
```
if m_RotAngle == 0:
    m_RotMat_col0 = (1.0, 0.0)     ; identity
    m_RotMat_col1 = (0.0, 1.0)
else:
    col0 = (SinIdx(angle+0x4000), CosIdx(angle+0x4000))
    col1 = (SinIdx(angle),        CosIdx(angle))
    // friction sub-angle via modular division:
    subAngle = (angle + 0xdff2) % 0xfff0
    m_FrictionSinAngle = SinIdx(subAngle) * DAT_00115004   ; ≈ 0.353
    m_FrictionCosAngle = CosIdx(subAngle) * DAT_00115004
```
`DAT_00115004` = 0x3FB47AE1 ≈ 0.4118 (float). This scales the friction direction vector.

**Size:**
```
sizeStart = LERP(template->m_SizeStartMin, template->m_SizeStartMax, rand() & 0xfff)
sizeMid   = LERP(template->m_SizeMidMin,   template->m_SizeMidMax,   rand() & 0xfff)
sizeEnd   = LERP(template->m_SizeEndMin,   template->m_SizeEndMax,   rand() & 0xfff)
particle->m_SizeStart    = (ushort)(sizeStart * emitter->m_ScaleX)
particle->m_SizeDeltaMid = (short)((sizeMid - sizeStart) * emitter->m_ScaleX)
particle->m_SizeDeltaEnd = (short)((sizeEnd - sizeMid)   * emitter->m_ScaleX)
```
`LERP` here is an integer lerp using `rand() & 0xfff` (12-bit fraction).

**Colour (4 channels × 3 stops, packed loop):**
The loop runs 4 times (iVar10 = 0..3), extracting per-channel byte from the float-bitcast BGRA words in the template using bit shifts:
```
for ch in [0,1,2,3]:    // B, G, R, A
    shift = ch * 8
    mask  = 0xff << shift
    startMin_ch = (mask & colourStartMin_word) >> shift
    startMax_ch = (mask & colourStartMax_word) >> shift
    midMin_ch   = (mask & colourMidMin_word)   >> shift
    midMax_ch   = (mask & colourMidMax_word)   >> shift
    r1 = RandFloat(); r2 = RandFloat(); r3 = RandFloat()
    startCh = lerp(startMin_ch, startMax_ch, r1)
    midCh   = lerp(midMin_ch,   midMax_ch,   r2)
    endCh   = lerp(endMin_ch,   endMax_ch,   r3)   ; (endMin/Max used from separate words)
    *(byte*)(particle+0x24+ch) = (byte)startCh
    *(short*)(particle+0x28+ch*2) = (short)(midCh - startCh)    ; m_ColDeltaMidB/G/R/A
    *(short*)(particle+0x30+ch*2) = (short)(endCh - midCh)      ; m_ColDeltaEndB/G/R/A
```
Note: `p_Var15` advances by 2 bytes per iteration (not 12), so the delta shorts are packed tightly at `+0x28..+0x37` as 8 consecutive int16s.

**RotCycle, CycleX, CycleY, Friction, FrictionSpin initialisation:**
All follow the same lerp pattern: `floatLERP(templateMin, templateMax, RandFloat())`. Random phase seeds are from `Math::Random::Rand32(rng, 0)` and stored as ushort. Zero-range fields get zero phase (no cycle).

**Linking into emitter's particle list:**
```
// Already done during slot allocation above:
// The new particle's idx was pushed to template's m_ParticleListHead at +0x04
// The previous head is stored in particle->m_NextIdx (+0x40)
```

### Manager::Draw (0x00114c64, 414 lines)
<!-- Analysed: 2026-04-13T16:00 -->

Signature: `void Draw(float dt, bool paused, int layer)`

**Matrix setup:**
```
MatrixStack::Reset(&matrixManager->m_World)
MatrixManager::UploadCurrentMatrices(matrixManager, true)
```
No perspective or ortho change — the existing world matrix (identity reset) is used. The particle coordinate system is the same centred-ortho as everything else. No `glPushMatrix`/`glPopMatrix` equivalent.

**Layer filtering:**
The outer loop iterates all `m_TemplateCount` templates (stride 0xB8). Filter: `(ushort)(template+4) != 0 AND (float)layer == template->field_0xb4_as_float`.

`template+4` is `m_ParticleListHead` (the live-particle linked list head); a zero head means no active particles for this template. `template->m_UseDepth` (+0xB4, int) is compared directly as a float cast to `param_3` — this means `layer` must equal the template's `m_UseDepth` value for the template to be drawn. The mapping is: `m_UseDepth == 0 → mid layer`, `m_UseDepth == 1 → foreground`. Passing `layer = -1` will never match any valid `m_UseDepth` value (which come from XML `<useDepth>` — always 0 or 1), so `-1` does **not** mean draw-all; it means draw-none. **RE note:** The hint in the signature says -1=all, but the binary condition `(float)param_3 == template->field_0xb4_as_float` means -1 matches nothing. The draw-all path would require the caller to make two calls (layer=0 then layer=1).

**Vertex buffer layout:**
The function writes directly into a global vertex buffer at `DAT_001155cc + iVar15 + 0x28` (offset into static mesh VBO). Each vertex is a `QUADCUSTOMVERTEX` of 0x24 (36) bytes. Six vertices per particle (two triangles). Fields per vertex (confirmed by write offsets):
- `+0x00`: UV? (inferred — not written directly in particle loop but by texture bind)
- `+0x08`: position Z (z-component, written as `uVar2 = local_12c._8_4_` = particle pos.z)
- `+0x18`: colour (packed RGBA from `Colour::PlatformColour`)
- `+0x28`: position X (world X)
- `+0x2C`: position Y (world Y)
- `+0x44`, `+0x48`: U, V texture coordinates (written as 1.0 and DAT_00115018=0.0)

**Per-particle age computation:**
```
age = particle->m_AgeCarry    ; local_f0 in decompile
life = particle->m_Lifetime   ; local_f4
if age >= life: remove particle and continue
t = (life - age) / life       ; normalised remaining fraction (0=dead, 1=fresh)
// Note: t=1 at birth, t=0 at death — reversed from the usual 0→1 convention
```
**Colour interpolation (two-segment piecewise linear):**
```
t2 = t    ; fVar32
if t2 >= 0.5:       // ARM idiom: (int)((uint)(t2 < 0.5) << 0x1f) < 0 → t2 >= 0.5
    frac = 2 * t2                    // first half: start → mid
    R = startR + deltaToMid_R * frac
    G = startG + deltaToMid_G * frac
    B = startB + deltaToMid_B * frac
    A = startA + deltaToMid_A * frac
    size_t = sizeStart + deltaMid * frac
else:
    frac = 2 * t2 - 1                // second half: mid → end
    R = (startR + deltaToMid_R) + deltaFromMid_R * frac
    G = (startG + deltaToMid_G) + deltaFromMid_G * frac
    B = ...
    size_t = (sizeStart + deltaMid) + deltaEnd * frac
```
Byte-clamped: `byte = (0.0 < value) * (byte)(int)value` (zeroes negatives, no max-clamp).

**Size and aspect ratio:**
```
size_x = size_t * template->m_AspectRatio   ; local_44 = local_48 * *(float*)(tmpl+0xb0)
size_y = size_t                              ; local_48
```
The texture aspect ratio scales only the X extent; Y is the raw interpolated size.

**Rotation and CycleXY (active only when not paused, or emitter.m_bUpdateWhenPaused):**
```
// RotCycle: quadratic accumulation
rotAccum = rotCycleBase + (rotCycleStartSpeed + rotAccelHalf * t) * t
if rotAccum != 0:
    sinTheta = SinIdx((ushort)(int)(rotAccum * 65536.0))
    spinAmp  = floatLERP(cycleStartAmp, cycleEndAmp, t2)
    uAngle   = (uint)(sinTheta * spinAmp * 182.044) & 0xffff
    totalAngle = particle->m_RotAngle + uAngle
    // Rebuild rotation matrix from totalAngle
    col0 = (SinIdx(totalAngle+0x4000), CosIdx(totalAngle+0x4000))
    col1 = (SinIdx(totalAngle),        CosIdx(totalAngle))
    // Friction sub-angle:
    subAngle = (totalAngle + 0xdff2) % 0xfff0
    frictionX = SinIdx(subAngle) * DAT_00115004   ; ≈ 0.4118
    frictionY = CosIdx(subAngle) * DAT_00115004

// CycleX: accumulate rotation rate, modulate size_x
rotRate_x = floatLERP(cycleXStart, cycleXEnd, t2)
if rotRate_x != 0:
    particle->m_CycleXPhase += rotRate_x * 182.044 * 360.0 * dt
    particle->m_CycleXPhase = max(0, particle->m_CycleXPhase)  ; clamp negative
if particle->m_CycleXPhase != 0:
    size_x = size_x * CosIdx(m_CycleXPhase)

// CycleY: same for size_y
```
**DAT constants used:**
- `DAT_00115008` = 0x43360000 = 182.044 (≈ 65536/360, degrees→ushort)
- `DAT_0011500c` = 0x43B40000 = 360.0 (degrees full circle)
- `DAT_00114ff8` = 0x47800000 = 65536.0 (float→ushort scale for RotCycle)
- `DAT_00115004` = 0x3FB47AE1 ≈ 0.4118 (friction amplitude)
- `DAT_00115010` = 0x43F00000 = 480.0 (gridLock X offset)
- `DAT_00115014` = 0x43A00000 = 320.0 (gridLock Y offset)
- `DAT_001155c8` = 0x3CCCCCCD ≈ 0.025 (dt threshold for sub-step integration)

**Position snap (gridLock):**
```
if template->m_GridLockStart (at +0x64) > 0:   // offset 100 decimal = 0x64
    pos_x = round((pos_x + 480.0) / gridLockX) * gridLockX - 480.0
if template->m_GridLockEnd   (at +0x68) > 0:
    pos_y = round((pos_y + 320.0) / gridLockY) * gridLockY - 320.0
```
Note: Doc table previously had `m_GridLockStart` at +0x64 and `m_GridLockEnd` at +0x68 — confirmed. Offsets 100 and 0x68 in the Draw source match exactly.

**Sub-step integration (large dt):**
```
if dt > DAT_001155c8 (≈ 0.025):    // more than ~1.5 frames at 60fps
    halfDt = dt * 0.5
    velX = (vel.x + halfDt * gravity.x) * lerp(gravX_min, gravX_max, t2)
    pos.x = savedPos.x + velX * halfDt
    ... (same for Y, Z)
// Then also apply full-dt integration (always):
velX = (vel.x + dt * gravity.x) * lerp(...)
pos.x = savedPos.x + velX * dt
```
Velocity is scaled by an interpolated gravity multiplier from template +0x08..+0x14 (X), +0x0C..+0x18 (Y) ranges. The sub-step only applies the half-step first, then the full-step overwrites — this appears to be a simple 2-point Euler integration for accuracy at large timesteps.

**Quad vertex generation (6 vertices = 2 triangles):**
The rotation matrix columns `(col0, col1)` are multiplied by `(size_x, size_y)`:
```
dx = col0 * size_x     // rotated half-extent along X
dy = col1 * size_y     // rotated half-extent along Y
v0 = (pos_x + dx.x + dy.x,  pos_y + dx.y + dy.y)   // top-left
v1 = (pos_x + dy.x - dx.x,  pos_y + dy.y - dx.y)   // top-right
v2 = (pos_x - dx.x - dy.x,  pos_y - dx.y - dy.y)   // bottom-right (= -v0 offset)
v3 = copy of v2
v4 = copy of v1
v5 = (pos_x - dx.x - dy.x,  ...)                    // bottom-left
```
Six vertices = 2 triangles (tri-list, no index buffer). The z-coordinate is constant per particle = `particle->m_Pos_z`.

**Texture and draw call:**
```
// After processing all particles in a template's list:
if any vertices were written (iVar14 != 0):
    Mortar::Texture::Set(template->m_Texture)
    Mortar::Mesh::DrawTriList(vertexBase, vertexCount, false, NULL)
    Mortar::Texture::UnSet(template->m_Texture)
```
No explicit `glBlendFunc` call visible in Draw itself — blend state is managed by `Mortar::Texture::Set` using the texture's own blend mode. The `template->m_BlendMode` field (+0x58) is read during `LoadFile` and presumably passed to the texture object. There is no texture-cache "last bound" check visible; `Set`/`UnSet` are called per template with live particles.

**Depth test:** No `glDepthTest` enable/disable call is visible in Draw. `m_UseDepth` is used only for layer filtering, not for GL depth state. Depth testing appears to be managed externally (by the caller or the GL state machine).

**Dead particle removal (inline in Draw):**
When `age >= life` (particle is expired), it is removed from the live list and returned to the free list:
```
if prev == 0:
    template->m_ParticleListHead = particle->m_NextIdx   ; update list head
else:
    prevParticle->m_NextIdx = particle->m_NextIdx         ; unlink
// Return to free list:
mgr->m_field04 = particle_idx      ; push to free-list head
particle->m_NextIdx = old_m_field04
```

**PSPParticleManager field correction:** `m_field04` (ushort at +0x04 in PSPParticleManager, currently undocumented) is the free-list head index. It was previously listed only in the emitter struct. The manager struct needs `+0x04 | ushort | m_FreeListHead`.

**`m_ActiveCount` usage in Draw:** `m_ActiveCount` is reset to 0 at Draw start. Each template that produces vertices increments it by `vertexCount / 6` (particle count). This is a rendered-particle counter, not the same as the update count.

## PSPParticle (0xA4 = 164 bytes)
<!-- Analysed: 2026-04-13T16:00 -->

Layout derived from:
- `PSPParticle::PSPParticle(PSPParticle const&)` (0x00117710) — authoritative field enumeration.
- `PSPParticleEmitter::AddParticle` (0x00115644) — write patterns via `(_Vector3<float>*)particle` pointer arithmetic (`this_00[n]`).
- `PSPParticleManager::Draw` (0x00114c64) — read patterns via named copies in `local_12c`.

`_Vector3<float>` stride = 12 bytes; `this_00[n]` = particle_base + n×12.

| Offset | Size | Type | Name | Set by | Notes |
|--------|------|------|------|--------|-------|
| +0x00 | 12 | float[3] | m_Pos (x,y,z) | AddParticle | Initial = emitter.m_Pos (or global origin if CoordSystem=Global) |
| +0x0C | 12 | float[3] | m_Vel (x,y,z) | AddParticle | Half the rotated set velocity (`local_78 * 0.5`); applied each frame during Update |
| +0x18 | 12 | float[3] | m_Gravity (x,y,z) | AddParticle | Lerp(template.velocityMin, template.velocityMax, rand) — Note: Ghidra labels this with template velocity fields at +0x08..+0x14, which are `m_GravityMin/Max` in our table (see PSPParticleTemplate). Confirmed from Draw: lerp uses `template+8..+0x14` for X, `+0xC..+0x18` for Y. |
| +0x24 | 1 | byte | m_ColStartB | AddParticle | Start colour Blue channel (0-255); packed BGRA at +0x24..+0x27 |
| +0x25 | 1 | byte | m_ColStartG | AddParticle | Start colour Green |
| +0x26 | 1 | byte | m_ColStartR | AddParticle | Start colour Red |
| +0x27 | 1 | byte | m_ColStartA | AddParticle | Start colour Alpha |
| +0x28 | 2 | int16 | m_ColDeltaMidB | AddParticle | midColour.B − startColour.B (signed) |
| +0x2A | 2 | int16 | m_ColDeltaMidG | AddParticle | midColour.G − startColour.G |
| +0x2C | 2 | int16 | m_ColDeltaMidR | AddParticle | midColour.R − startColour.R |
| +0x2E | 2 | int16 | m_ColDeltaMidA | AddParticle | midColour.A − startColour.A |
| +0x30 | 2 | int16 | m_ColDeltaEndB | AddParticle | endColour.B − midColour.B (signed) |
| +0x32 | 2 | int16 | m_ColDeltaEndG | AddParticle | endColour.G − midColour.G |
| +0x34 | 2 | int16 | m_ColDeltaEndR | AddParticle | endColour.R − midColour.R |
| +0x36 | 2 | int16 | m_ColDeltaEndA | AddParticle | endColour.A − midColour.A |
| +0x38 | 4 | float | m_Lifetime | AddParticle | Total lifetime in seconds (`template->m_StartTime`); Draw writes remaining life (`m_Lifetime - dt`) |
| +0x3C | 4 | float | m_AgeCarry | AddParticle | `lifetime - lifetime * m_field24` — fractional age carried from previous frame |
| +0x40 | 2 | uint16 | m_NextIdx | AddParticle/Draw | Linked-list next-particle index (0 = end sentinel); overlaps `field_0x40` in copy-ctor |
| +0x42 | 2 | uint16 | _pad42 | — | Padding; completed by copy-ctor `field_0x40` (4 bytes total) |
| +0x44 | 2 | uint16 | m_RotAngle | AddParticle | Current rotation in 16-bit angle units (0..65535 = 0..360°). AddParticle: `(short)(spinDeg * 182.044)`. Draw accumulates: `angle += rotRate * DAT_00115008 * DAT_0011500c * dt`. |
| +0x46 | 2 | — | _pad46 | — | — |
| +0x48 | 4 | float | m_RotCycleStart | AddParticle | Lerp(template.m_RotCycleStart, template.m_RotCycleEnd, rand) stored as float |
| +0x4C | 4 | float | m_RotCycleEnd | AddParticle | Lerp(template.m_RotCycleEndMin, template.m_RotCycleEndMax, rand) stored as float |
| +0x50 | 2 | uint16 | m_RotCyclePhase | AddParticle | Random seed `Math::Random::Rand32(rng,0)` if range is nonzero; else 0 |
| +0x52 | 2 | — | _pad52 | — | — |
| +0x54 | 4 | float | m_CycleXStart | AddParticle | Lerp(template.m_CycleXStart, template.m_CycleXEnd×max, rand) |
| +0x58 | 4 | float | m_CycleXEnd | AddParticle | Second endpoint for X cycle |
| +0x5C | 2 | uint16 | m_CycleXPhase | AddParticle | Random seed if CycleX range nonzero |
| +0x5E | 2 | — | _pad5E | — | — |
| +0x60 | 4 | float | m_CycleYStart | AddParticle | Lerp(template.m_CycleYStart, template.m_CycleYEnd×max, rand) |
| +0x64 | 4 | float | m_CycleYEnd | AddParticle | Second endpoint for Y cycle |
| +0x68 | 2 | uint16 | m_CycleYPhase | AddParticle | Random seed if CycleY range nonzero |
| +0x6A | 2 | — | _pad6A | — | — |
| +0x6C | 4 | float | m_FrictionStart | AddParticle | Lerp(template.m_FrictionSpeedStart, m_FrictionSpeedEnd, rand) |
| +0x70 | 4 | float | m_FrictionEnd | AddParticle | Lerp(template.m_FrictionOffsetMin, m_FrictionOffsetMax, rand) |
| +0x74 | 4 | float | m_FrictionAngle | AddParticle | Lerp(template.m_FrictionAngleMin, m_FrictionAngleMax, rand) |
| +0x78 | 4 | float | m_FrictionSpin | AddParticle | Lerp(template.m_FrictionSpeedStartMin, m_FrictionSpeedStartMax, rand) |
| +0x7C | 2 | uint16 | m_SizeStart | AddParticle | `(ushort)(LERP(sizeStartMin, sizeStartMax, rand) * ScaleX)` |
| +0x7E | 2 | int16 | m_SizeDeltaMid | AddParticle | `(short)((sizeMidStart − sizeStart) * ScaleX)` |
| +0x80 | 2 | int16 | m_SizeDeltaEnd | AddParticle | `(short)((sizeEnd − sizeMid) * ScaleX)` |
| +0x82 | 2 | — | _pad82 | — | — |
| +0x84 | 8 | float[2] | m_RotMat_col0 | AddParticle | Vec2(sin, cos) of rotation angle; identity if angle==0 |
| +0x8C | 8 | float[2] | m_RotMat_col1 | AddParticle | Vec2(sin, cos) of angle; forms 2D rotation matrix with col0 |
| +0x94 | 4 | float | m_FrictionSinAngle | AddParticle | `sin(frictionAngle) * DAT_00115004` (≈0.353) — friction force direction X |
| +0x98 | 4 | float | m_FrictionCosAngle | AddParticle | `cos(frictionAngle) * DAT_00115004` — friction force direction Y |
| +0x9C | 1 | byte | m_FieldFlags | AddParticle | Copy of `emitter.m_field38` |
| +0x9D | 3 | — | _pad9D | — | — |
| +0xA0 | 4 | PSPParticleEmitter* | m_pEmitter | AddParticle | Back-pointer to owning emitter; written as `(float)this` at `this_00[0xd].y` = `+0xA0` |
| Total | **0xA4** | | | | 164 bytes confirmed |

**Colour encoding note:** StartColour is stored as 4 raw bytes (B,G,R,A) at +0x24. The deltas to mid and end are stored as signed int16s (not bytes) to allow negative transitions. Draw reconstructs the colour: if `age/life < 0.5` use `startColour + deltaToMid * (2 * age/life)`; else use `midColour + deltaFromMid * (2 * age/life - 1)`.

**Size encoding note:** All three size values (start, mid, end) are encoded as a start + two int16 deltas at +0x7C..+0x81. The mid size is `sizeStart + m_SizeDeltaMid`, the end size is `midSize + m_SizeDeltaEnd`. Draw computes the same two-segment linear interpolation as colour.

**Rotation matrix note:** `m_RotMat_col0` and `m_RotMat_col1` form a 2×2 rotation matrix (stored column-major as Vec2 pairs). If `m_RotAngle == 0` on spawn, the matrix is set to identity `{(1,0),(0,1)}` — the `(1.0, DAT_00115018)` pattern in AddParticle, where `DAT_00115018 = 0.0`. Draw multiplies the particle half-extents through this matrix for quad vertex positions.

**Linked list at +0x40:** The free-list sentinel uses index 0. `m_field04` of `PSPParticleManager` is the free-list head ushort. AddParticle pops: takes `m_field04`, reads `*(ushort*)(particle+0x40)` as the next free index, stores it back to `m_field04`. Draw removes dead particles by pushing back: writes `m_field04` to `*(uint*)(particle+0x40)` and the previous-particle's `+0x40` link to `m_field04`.

**`+0x3C` (m_AgeCarry):** Computed as `lifetime - lifetime * m_field24`. When `m_field24 == 1.0` this is 0.0 (normal start). This appears to implement a spawn-offset mechanism so newly created particles don't always start at age 0, compensating for fractional emit intervals.

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
| AddEmitter | ✅ ported | Signature matches binary `(hash, ppRef, persistent)`; `unique_ptr`-backed vector keeps emitter pointers stable across growth so caller back-pointers remain valid. |
| ClearEmitter | ✅ ported | Finds by pointer, clears back-ref, erases from list. |
| Emitter::Update (spawn + physics) | ✅ ported | Rate integral, burst, per-frame `pos += vel` quirk all preserved. |
| Manager::Update (emitter walk) | ✅ ported | Wired into `GameUpdate`. Infinite emitters (maxLifetime ≤ 0) kept until explicit `ClearEmitter`. `Ends()` branch not yet RE'd. |
| Manager::Draw | ✅ ported (simplified) | Layer filtering via `template->m_UseDepth`; three-pass call from `GameDraw` (background=1 → 3D → mid=0 → HUD → foreground=-1). Two-segment start→mid→end colour/size lerp. Blend mode applied via `glBlendFunc(SRC_ALPHA, template->m_BlendMode)`. **Not ported:** RotCycle quadratic rotation, CycleXY cosine size modulation, gridLock snap, friction angle, sub-step integration (irrelevant at fixed dt=1/60). |
| AddParticle | ✅ ported (simplified) | Shape-type branching: **Point** (0), **Vertex** (1, `pos -= vel`), **Direction** (2, `rotation += atan2(vel.y, vel.x)`). **Not ported:** Angular (3) — needs `m_field38` state; friction init; CycleX/Y phase seeds. |

## See Also

- [Particle functions](../functions/particles.md) -- emitter/particle pseudocode
- [Resources](../resources.md) -- particles XML files
