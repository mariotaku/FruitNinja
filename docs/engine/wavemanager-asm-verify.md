# WaveManager ASM-Level Fidelity Audit

Audited 2026-04-30 vs `FruitNinja.exe` (ARM ELF, image base 0x00010000).
Scope: every public method on `WaveManager` listed in the work order.
Sources: Ghidra `decompile_function` + `disassemble_function` + raw `.rodata`
DAT bytes via `inspect_memory_content`.

The user-reported "fruit flying everywhere" symptom traces to **multiple
critical divergences in `SpawnFruit`** (DATs, the `0xb6` multiplier
position, vel scale field mapping, Z position, and the gravity/zoffset
application order). Other methods have many smaller and one severe issue
(GetNextWave wave selector / m_NextWaveDelay).

Status legend:
- **MATCHES** -- byte-for-byte equivalent semantics
- **MINOR DIFF** -- cosmetic / unused-path divergence, no observable runtime effect
- **DIVERGES** -- observable runtime difference
- **CRITICAL** -- root cause of visible bugs (especially "flying everywhere")

---

## 1. SpawnFruit — `0x001225a0`

**Status: CRITICAL (multiple divergences, primary suspect for "flying everywhere")**

Port: `src/game/WaveManager.cpp:933-1007`

### 1.1 The angle-range multiplier is **150**, not 182

Binary disassembly @ 0x001225e2..0x00122610:
```
vldr.32 s15,[pc,#0x260]     ; s15 = DAT_00122844 = -150.0
vldr.32 s17,[pc,#0x25c]     ; s17 = DAT_00122848 = +150.0
vmul.f32 s15,s16,s15        ; s15 = minAngle * -150
vmla.f32 s15,s14,s17        ; s15 = minAngle * -150 + maxAngle * 150
vcvt.u32.f32 s15,s15
blx Random::Rand32          ; r1 = (uint)(150*(maxAngle-minAngle))
vmov s12,r0                 ; r0 = roll
vcvt.f32.u32 s25,s12
vmla.f32 s25,s16,s17        ; s25 = (float)roll + minAngle * -150
                             ; (note: -150 because s17 was reloaded? Disassembly
                             ;  shows it's actually s17=+150 still — see below)
vcvt.s32.f32 s25,s25        ; iVar11 = (int)((float)uVar3 + minAngle * fVar9)
```

Decompile clarifies: `iVar11 = (int)((float)uVar3 + velX * fVar9)` where
`fVar9 = DAT_00122848 = +150.0` was already cached at the top of the loop.
So the compounded baseline angle (in **degrees**, conceptually) is:

```
range = maxAngle*150 - minAngle*150        (note: minAngle * -150 + maxAngle*150 == -150*(minAngle - maxAngle) == 150*(maxAngle - minAngle); both forms equivalent)
roll  = Rand32(range)                      (clamped to 0 if range <= 0)
base  = (int)(roll + minAngle * 150)
```

That `150` constant -- DAT_00122844/8 = -150.0 / +150.0 -- is the
**primary angle multiplier**. It is the number-of-half-degrees-per-radian
or some such (binary chose 150 deg = ~half-circle quantisation), NOT 182.

Port (`WaveManager.cpp:942-944`):
```cpp
float range = minAngle * (-182.0f) + maxAngle * 182.0f;
uint32_t r1 = (range > 0.0f) ? m_Random.Rand32((uint32_t)range) : 0;
uint16_t angle = (uint16_t)(((int)r1 + (int)(minAngle * 182.0f)) * 0xb6);
```

The port is using **182 in two places** instead of the binary's **150 (for the
range/baseline)** and **0xb6=182 (for the second-stage multiplier)**.

The 0xb6 multiplier (binary @ 0x0012267c: `movs r5, #0xb6`) is applied
**later**, AFTER a more elaborate intermediate computation (see 1.2 below).
The port collapsed two stages into one and lost the spread step entirely.

**Port-side action:**
- Replace the two `182.0f` operands in the `range` formula with `-150.0f` and `+150.0f`.
- Move the `0xb6` (i.e. `* 182`) multiplier to the very last step, after the
  spread stage described in 1.2.

### 1.2 Missing "spread" intermediate stage

Binary @ 0x00122614..0x0012267e, decompile excerpt (matching disassembly):
```c
if ((spawner == 0) || ((byte)spawner[0x34] < 2)) { fVar9 = 20.0; }
else                                              { fVar9 = 12.0; }    // spread degrees
fVar10 = RandF(rng, 1.0);                                              // [0,1)
velX = (fVar10 < 0.5) ? fVar10 + 0.5 : 0.5 - fVar10;                   // |0.5 - r|
fVar10 = (fVar10 < 0.5) ? -1.0 : 1.0;                                  // sign
spawnAngle = (short)(int)(
                 (float)(longlong)(int)(((float)iVar11 / DAT_00122844 /*-150*/) * fVar9 * 0.5)
                 + fVar9 * (velX * velX * -2.0 + 0.5) * fVar10
             ) * 0xb6;
```

In words:
1. `iVar11` (the picked degree-baseline from 1.1) is divided by **-150** and
   multiplied by `spread/2` -> centre offset.
2. A parabolic spread `fVar9 * (|0.5 - r|^2 * -2 + 0.5) * sign` is added.
3. The **final** result is converted to short, then multiplied by `0xb6 = 182`
   to produce the uint16 `angle` index used for `SinIdx` / `CosIdx`.

The port has none of this. It just multiplies `(roll + min*182)` by `0xb6`.

This is the **primary cause** of fruit going off in unpredictable
directions. The binary's two-stage angle pipeline produces a tightly
clustered launch arc; the port's single-stage produces a much wider, biased
spread (the `* 0xb6` then truncate-to-uint16 wraps angles past +/-180 to
random other directions on the circle).

**Port-side action:** Reproduce the binary pipeline:
```cpp
// Stage 1: degree baseline
float baseRange = -150.0f * minAngle + 150.0f * maxAngle;
uint32_t roll1 = (baseRange > 0.0f) ? m_Random.Rand32((uint32_t)baseRange) : 0;
int     iBase = (int)((float)roll1 + minAngle * 150.0f);

// Stage 2: spread
float spread = (info && info->m_SpawnType >= 2) ? 12.0f : 20.0f;
float r      = m_Random.RandF(1.0f);
float halfR  = (r < 0.5f) ? (r + 0.5f) : (0.5f - r);  // see binary disasm:
                                                       // velX = (r<0.5)? r+0.5 : 0.5-r  (asymmetric — see asm)
float sign   = (r < 0.5f) ? -1.0f : 1.0f;
int   center = (int)(((float)iBase / -150.0f) * spread * 0.5f);
int   off    = (int)(spread * (halfR*halfR*-2.0f + 0.5f) * sign);
uint16_t angle = (uint16_t)(((short)(center + off)) * 0xb6);
```
(NB: re-check the `halfR` formula against binary asm — the decompile
expression is unusual; specifically asm 0x00122644..0x0012265a uses
`vsub s0,s0,#0.5` then `ite mi/pl` swapping which side adds vs subtracts —
double-verify before final port.)

### 1.3 Velocity / angle field mapping (NAMING ONLY)

Binary semantics (per Init `piVar15` writes):
- `+0x24` = velscale / velXscale  (port `m_MinAngle`, mis-named)
- `+0x28` = velscale / velYscale  (port `m_MaxAngle`, mis-named)
- `+0x2c` = horizmin              (port `m_MinVel`, mis-named)
- `+0x30` = horizmax              (port `m_MaxVel`, mis-named)

Port's `Init` writes `horizmin -> m_MinAngle` (port +0x24) and
`velXscale -> m_MinVel` (port +0x2c). At SpawnFruit time, port reads
`info->m_MinAngle` (port +0x24, which contains horizmin) as minAngle and
`info->m_MinVel` (port +0x2c, which contains velXscale) as velMultX.

So the **XML attribute -> runtime variable wiring is consistent at the
semantic level**, just the struct field NAMES are swapped relative to
binary offsets. Runtime behaviour matches; field naming should be cleaned
up later. **Status: MINOR DIFF.**

### 1.4 chuckDelay base — port reads m_ZOffset (+0x5c) -- MATCHES

Binary @ 0x001225d6 loads `s19 = *(float*)(spawner + 0x5c)` (the runtime
spawn-timer remainder) at the top of each loop. Then @ 0x001229d2-0x001229ee
uses `s19` for chuckDelay: `(s19 >= 0)? s19 + 0.21 : 0.21`.

Port (`WaveManager.cpp:989-990`):
```cpp
float zOffset = info ? info->m_ZOffset : 0.0f;        // +0x5c
float chuckDelay = (zOffset > 0.0f) ? zOffset + 0.21f : 0.21f;
```

Both read +0x5c. Port's predicate is `> 0` vs binary's `>= 0` (the binary's
`-1 < iVar11` test is the IT predicate `pl`, which is `>= 0`). The boundary
at 0.0 case differs: binary takes `0 + 0.21 = 0.21`, port takes the bare
`0.21`. Same numeric result. **MATCHES.**

Note: port's WaveStructs.h labels +0x50 as `m_SpawnTimer` and +0x5c as
`m_ZOffset`, but binary uses +0x5c as the spawn timer and +0x50 as
remaining-count. Port's UpdateWave is **internally consistent** with its
own field names, so no runtime misread. Naming-only inconsistency in the
struct doc. **MINOR DIFF.**

### 1.5 Position formulas — port has 240/-240, binary uses different DATs

Binary @ 0x00122802..0x0012282e (decompile excerpt):
```c
iVar21 = DAT_00122228;        // = ?
fVar14 = fVar18 * -0.75;       // posY hold-over; fVar18 was velY in
fVar18 = fVar20 + fVar15 * *(float *)(spawner + 0x1c) * DAT_00122224;
                               // velY rewritten -- spawner+0x1c is "speed" attr
                               // DAT_00122224 = -0.65
iVar7 = (int)(((float)lVar1 * DAT_00122850) / DAT_00122854);
                               // = (iVar11 * 320 / 480) -- spawn X for sides
```

So the side-placement positions are computed as:
- spawnX (LEFT/RIGHT) = `(iVar11_baseDeg * 320) / 480` -- **NOT 240**
- spawnY (LEFT/RIGHT) = `velY * -0.75 -> truncate-to-int -> * 320/480`

The port (`WaveManager.cpp:976-986`):
```cpp
case PLACEMENT_LEFT:
    posX = -240.0f;
    posY = (float)((int)(m_Random.Rand32(320)) - 160);
    { float tmp = velX; velX = velY; velY = tmp; }
    velX = fabsf(velX);
    break;
case PLACEMENT_RIGHT:
    posX = 240.0f;
    posY = (float)((int)(m_Random.Rand32(320)) - 160);
    ...
```

**Port computes posX/posY for LEFT/RIGHT entirely differently from binary.**
Binary uses:
- `posX = (baseDeg * 320 / 480)` then negated for LEFT
- `posY = (velY * -0.75 * 320 / 480)` (then int-truncated)
- velocity components are mixed by gravity-* speed * -0.65

Port uses fixed `±240` and a fresh `Rand32(320) - 160` roll, plus a
tmp swap of velX/velY which has no analog in the binary side branch.

**This is the second largest divergence and contributes to "flying
everywhere"** — fruit launched from the side has wrong position AND wrong
velocity vector.

### 1.6 BOTTOM placement uses iVar11, not r1 % 320

Binary's BOTTOM/BOTTOM_SLOW path (decompile):
```c
default: case '\\0':
    iVar7 = -0xa0;            // -160 for posY
    // posX is left as the default (computed earlier from iVar11 baseline,
    // not as % 320 -- see below)
```

The decompile assigns `velX = (float)(longlong)iVar11 * local_70.x`
where iVar11 was the degree-baseline computed in stage 1.1. After
truncation that becomes the spawnX for BOTTOM.

Specifically @ 0x001228be..0x001228da:
```
vmov s12,r9                  ; r9 = iVar7 (= -0xa0 in BOTTOM)
vldr.32 s14,[sp,#0xa0]       ; s14 = local_70.x (=1 in BOTTOM, see vec3 ctor)
vcvt.f32.s32 s25,s25         ; iVar21 -> float
vldr.32 s15,[sp,#0xa4]       ; s15 = local_70.y (=1 in BOTTOM)
vcvt.f32.s32 s13,s12         ; iVar7 -> float
vmul.f32 s20,s25,s14         ; s20 = (float)iVar21 * 1 = iVar21
vmul.f32 s16,s13,s15         ; s16 = (float)iVar7  * 1 = -160
```

So `spawnX = iVar21` (the degree-baseline truncated to int), `spawnY = -160`
for BOTTOM. The "rotation vector" `local_70` is `(1,1,1)` for BOTTOM.

Port's BOTTOM:
```cpp
posX = (float)((int)(r1 % 320) - 160);     // r1 was the angle Rand32 result
posY = -240.0f;                             // -- WRONG, should be -160
```

- posY is **-240 in port, should be -160** (binary uses iVar7=-0xa0=-160).
- posX is `(r1 % 320) - 160` in port; binary uses `iBase` directly (no
  modulo, no -160 offset — though by accident `iBase` IS already in the
  right rough range when range is ~150).

**Port-side action:** spawnY for BOTTOM/BOTTOM_SLOW should be `-160`, not
`-240`. spawnX should be `iVar21` (the baseline degree int, after the
spread stage), not `r1 % 320 - 160`.

### 1.7 Z position uses 32, not (i * 32) directly

Binary `(float)(longlong)iVar8 * DAT_00122580` where DAT_00122580 = 32.0
and iVar8 starts at 1 and increments. So z = `(loop_index_starting_1) * 32`.

Port: `f->pos = Vec3(posX, posY, (float)(i * 32));` where i starts at 0
(loop is `for (long i = 0; i < count; ++i)`).

**Port-side action:** Either start `i` at 1 (matching binary's `iVar8 = 1`)
or use `(i+1) * 32` for z. Currently first fruit gets z=0, second z=32,
etc.; binary gives z=32, 64, etc.

Note: SpawnBomb uses iVar8 from 1 to count (matches the binary). So port's
SpawnBomb is right (`for (long i = 1; i <= count; ++i)`) but SpawnFruit is
**off by one** in the z stride.

### 1.8 Gravity / velocity assignment after Init

Binary @ 0x00122954-0x0012299e:
```
vldr.32 s15,[r5,#0xa0]      ; s15 = newFruit->m_Gravity_y (after Init)
vneg.f32 s15,s15            ; -m_Gravity_y
vstr.32 s15,[sp,#0xb0]      ; -- this becomes the operator* arg --
operator* on (spawner+0x18, &neg_gravity_y) -- this builds a Vec3 = spawner.gravity_vec * (-gravity_y)?
   Actually re-read: _Vector3<float>::operator*(&local_dc, (float*)(spawner + 0x18))
   The first arg is the destination, the second is what to multiply by.
   The implicit `this` is the source Vec3 -- but the call site doesn't show it; this is inlined operator*.
   Looking at the asm at 0x00122962-0x00122968:
       add r2,sp,#0xb0               ; r2 = &(-m_Gravity_y)
       blx 0x001028b8                ; Vec3::operator*(Vec3&, float)
   So it multiplies the *spawner's gravity Vec3* (at +0x18) by the scalar
   (-m_Gravity_y) and stores into local_dc.
   
ldm r7,{r0,r1,r2}            ; load local_dc
add.w r3,r5,#0x9c            ; fruit->m_Gravity_x is at r5+0x9c (which is m_Gravity_x)
stm r3,{r0,r1,r2}            ; fruit->m_Gravity = local_dc
                              ; = spawner.gravity_vec3 * (-fruit->m_Gravity_y_after_init)
```

Then for LEFT (`r8 == 2`): `fruit->m_Gravity_x += DAT_00122a2c = 0.01`
For RIGHT (`r8 == 3`): `fruit->m_Gravity_x -= 0.01`

And `newFruit->m_TimeScale = zOffset (= spawner+0x14)`.

Port (`WaveManager.cpp:1004-1005`):
```cpp
// gravity / timescale modifiers from spawner.
// TODO: info->m_Gravity / m_TimeScale application matches binary call pattern.
f->Chuck(f->vel, chuckDelay);
```

**Port has these assignments completely missing.** No m_TimeScale write,
no m_Gravity multiply by spawner gravity Vec3, no LEFT/RIGHT 0.01 nudge.

This means fruit launched by the port use whatever m_Gravity Fruit::Init
left them with -- typically `(0, -3.0, 0)` from FruitInfo defaults. They
do NOT get the per-spawner gravity scaling.

**Critical port-side action:**

After `f->Init(0, fruitType, 0)` and BEFORE `f->Chuck(...)`:
```cpp
f->m_TimeScale = info->m_TimeScale;   // = spawner+0x14 (mis-named in struct -- see 1.9)
if (info) {
    Vec3 gravScale = info->m_Gravity_vec3;        // +0x18, +0x1c, +0x20
    f->m_Gravity = Vec3(gravScale.x * -f->m_Gravity.y,
                        gravScale.y * -f->m_Gravity.y,
                        gravScale.z * -f->m_Gravity.y);
    if (spawnType == PLACEMENT_LEFT)  f->m_Gravity.x += 0.01f;
    else if (spawnType == PLACEMENT_RIGHT) f->m_Gravity.x -= 0.01f;
}
```
NOTE: The above multiply uses `-f->m_Gravity.y` AS A SCALAR -- the binary
loads s15 = m_Gravity_y, negates, then calls Vec3::operator*(spawner.grav,
-m_Gravity_y) producing a Vec3 = (gx*-gy, gy*-gy, gz*-gy). Then stores back
into m_Gravity. This effectively **multiplies the spawner's gravity Vec3
by the magnitude of the post-Init gravity (Y component)**. This is a
quirky but very specific formula; reproduce it exactly.

### 1.9 SPAWNER_INFO gravity is a Vec3 at +0x18, NOT a float at +0x48

Binary `Init` @ 0x001241f8 / `ParseVector(...)`:
```c
piVar15[6] = local_54;        // +0x18 = grav.x
piVar15[7] = iStack_50;       // +0x1c = grav.y
piVar15[8] = iStack_4c;       // +0x20 = grav.z
```

The "gravity" XML attribute is parsed as a **3-component Vec3 starting at
spawner+0x18**.

In the port struct, +0x18..+0x20 are currently `m_Offset_x/_y/_z` ("offset"
attr). And `m_Gravity` (port +0x48) is a single float that doesn't exist
in the binary at that location.

Port `Init` reads `gravity` as a Vec3 (correctly) but only stores `gy`:
```cpp
sscanf(grav, "%f,%f,%f", &gx, &gy, &gz);
s.m_Gravity = gy;  // TODO: store Vec3 when field layout confirmed
```

So:
- Binary: `gravity` Vec3 at +0x18..+0x20 (overlaps with "offset" -- SAME slot).
- Port: `m_Offset` at +0x18..+0x20 (3 floats) and `m_Gravity` at +0x48 (1 float).

The port's m_Offset and m_Gravity are **NEVER both populated** because
the XML `<Spawn>` element has either an "offset" attr OR a "gravity" attr,
not both. (Verifying: `Init` decompile has only one ParseVector call for
"gravity", and... let me re-check whether "offset" is also a Vec3.)

From `Init`:
```c
piVar15[6] = local_54; piVar15[7] = iStack_50; piVar15[8] = iStack_4c;
```
This came from a single `pcVar8 = Attribute(this_02, "gravity")` -> `ParseVector(&local_54, pcVar8)` block. The +0x18..+0x20 slot is reused for either gravity (if specified) or offset (if specified) — same memory. The XML schema either has "gravity" XOR "offset" per spawner.

Looking at the actual XML files (port has `xml/originalwavelist.xml` etc.),
spawners typically have `gravity="..."` not `offset="..."`. So in practice
+0x18 is the gravity Vec3.

**Port-side action (struct):**
- `m_Offset_x/_y/_z` fields at +0x18..+0x20 should be renamed
  `m_Gravity_x/_y/_z` (or kept as a Vec3 union with m_Offset).
- Remove the standalone `m_Gravity` float at +0x48 (or recognise that +0x48
  is something else — see `SPAWNER_INFO::Reset` (binary) for what +0x48 is).
- `Init` should `sscanf` gravity into the Vec3 slots (+0x18..+0x20), NOT
  into the +0x48 float.
- `SpawnFruit/Bomb` should consume `info->m_Gravity_vec3` (via +0x18) for
  the post-Init gravity multiply (1.8 above), AND for the "speed * gravity *
  -0.65" formula in 1.5.

This explains why **fruit gravity is wrong** — the port never even reads the
spawner gravity Vec3 in SpawnFruit (it's stuck at the Fruit::Init default).

---

## 2. SpawnBomb — `0x00121fa8`

**Status: DIVERGES (side branch positions and gravity field mapping)**

Port: `src/game/WaveManager.cpp:1013-1091`

### 2.1 Angle pipeline -- MATCHES

DAT_00122208 = -150.0, DAT_0012220c = 150.0, DAT_00122210 = -300.0. Port
uses these correctly. The center/lo/hi/spread/rng2 stage matches the binary.
The 0xb6 final multiplier matches.

### 2.2 ZOffset for SpawnBomb is +0x5c -- MATCHES

Binary @ 0x001220b0 reads `*(float *)(r6 + 0x5c)` into fVar19 (zOffset).
Port reads `spawner->m_ZOffset` (+0x5c). Same.

### 2.3 Side branch positions and gravity-Y mix -- DIVERGES

Binary @ 0x00122810-0x00122816 (SpawnBomb side branch):
```c
iVar7 = (int)(((float)lVar1 * DAT_0012221c) / DAT_00122220);    // = baseDeg * 320/480
fVar18 = velY_input + speed * *(float *)(spawner + 0x1c) * (-0.65);
                                                  // ^-- spawner+0x1c is gravity.y
                                                  //     (the gravity Vec3 stored at +0x18..+0x20)
spawnX = iVar7;     // NOT raw baseDeg
```

Port (`WaveManager.cpp:1049-1072`):
```cpp
case PLACEMENT_RIGHT:
case PLACEMENT_LEFT: {
    long newVelY = (long)(velX + speed * spawner->m_Gravity * (-0.65f));
    spawnY = (long)(velY * -0.75f);
    spawnY = (long)((float)spawnY * (320.0f / 480.0f));
    velX = (float)(long)velY;
    velY = (float)newVelY;
    if (st == PLACEMENT_LEFT) { spawnX = -spawnX; velX = -velX; }
    break;
}
```

Issues:
1. `spawnX = (float)baseDeg` is missing the `* 320/480` scale.
2. `spawner->m_Gravity` is port's +0x48 (artificial single-float field).
   Binary reads spawner+0x1c (the **Y component** of the gravity Vec3 at
   +0x18..+0x20).

**Port-side action (SpawnBomb):**
```cpp
spawnX = (float)((long)((float)baseDeg * 320.0f / 480.0f));   // not raw baseDeg
long newVelY = (long)(velX + speed * info->m_Gravity_vec3.y * (-0.65f));   // +0x1c not +0x48
```

---

## 3. UpdateWave — `0x00125390`

**Status: DIVERGES (significant blitz/bomb gating logic missing)**

Port: `src/game/WaveManager.cpp:657-749`

### 3.1 Per-spawner spawn-timer offset is +0x5c, not +0x50

Binary `*(float *)(spawner + 0x5c) -= dt * dtMod;` — uses +0x5c (already
covered in 1.4). Port uses `spawner.m_SpawnTimer` at port's +0x50.

This is the offset-naming issue from 1.4. **Behaviourally equivalent**
because the port consistently uses +0x50 for spawn timer, but the docs
don't agree with binary.

### 3.2 The bomb branch — port doesn't check `field_0x68` ("spawnLevel")

Binary @ 0x0012574c (decompile near `iVar11 == -2`):
```c
if (iVar11 == -2) {
    local_50 = local_50 + 1;
    if (0.0 < *(float *)(param_2 + 0x68)) {     // spawnLevel > 0
        SpawnBomb(...);
    }
}
else if (0.0 < *(float *)(param_2 + 0x6c)) {     // field_0x6c > 0
    SpawnFruit(...);
}
```

Port (`WaveManager.cpp:715-727`):
```cpp
if (fruitType == -2) {
    SpawnBomb(1, ...);    // -- no spawnLevel check
} else if (fruitType == -1) {
    int rft = Fruit::RandomFruit(false);
    SpawnFruit(1, rft, &spawner, playerIdx);
} else {
    SpawnFruit(1, fruitType, &spawner, playerIdx);
}
```

**Missing**: the `if (spawnLevel > 0)` gate before SpawnBomb, and the
`if (field_0x6c > 0)` gate before SpawnFruit.

These are powerup multipliers — when zero, the power-up suppresses bomb /
fruit spawning. With the port's stubs leaving them at default 1.0 they
behave correctly **most of the time** (passes the >0 gate), but if any
power-up clears them, fruit/bombs would still spawn.

### 3.3 Massive missing PROBABILITY_OVERIDE / ChooseFrom logic

The binary's UpdateWave has a giant block (~150 lines of decompile) that
handles:
- `param_2 + 0x23d` and `+0x23e` blitz state machine.
- Random selection between PROBABILITY_OVERIDE entries.
- ChooseFrom queue lookup at `*(int *)(param_2 + 0x2c8 + playerIdx * 4)` --
  the per-player fruit queue size.
- `local_50` increment for retried-bomb counts.
- The `iVar14 / 2 <= local_50` cap for repeated bomb selection.
- Special "static SPAWNER_INFO[3]" template construction (`__cxa_guard_acquire`)
  for fruit-power-up spawning at addresses `iVar11 + 4 / +0x68 / +0xcc`.

Port has **none of this**. The port's UpdateWave is a simplified "pick
random type from spawner.types, spawn it" loop with no override or queue
integration.

This is **DIVERGES** for non-arcade waves with no overrides; for Arcade
mode the port will spawn power-up fruit at the wrong rate (or never).

### 3.4 IsWaveProcessing late-call uses no `field_0x470` gate

Binary @ 0x001257c0:
```c
iVar3 = IsWaveProcessing(...);
if ((iVar3 == 0) && (*(char *)(iVar15 + DAT_001259c0 + 0x470) == '\\0')) {
    // wave-end transition
}
```

Port checks only `IsWaveProcessing(playerIdx)`. The `field_0x470` is on
some global (Game?) and acts as a wave-end suppression flag. **MINOR DIFF**
unless Game ever sets it (which the port hasn't ported yet).

---

## 4. GetNextWave — `0x00124f10`

**Status: DIVERGES (m_NextWaveDelay reads wrong WAVE_INFO field)**

Port: `src/game/WaveManager.cpp:759-864`

### 4.1 Wave delay computation reads wrong field

Binary @ 0x00125294-0x001252b6:
```c
if (0.0 < *(float *)(wave + 0x20)) {                 // WAVE_INFO+0x20 = "delay" base
    fVar17 = Max(0.05f, *(wave + 0x20) + *(wave + 0x24) * *(wave + 0x34));
    field_0x234[playerIdx] = fVar17;
}
else field_0x234[playerIdx] = 0.0;

fVar17 = *(float *)(wave + 0x28);                    // WAVE_INFO+0x28 = "wait" base
field_0x238[playerIdx] = fVar17;
fVar16 = *(float *)(wave + 0x30);                    // WAVE_INFO+0x30 = "wait" speed-inc
if (fVar16 != 0.0) {
    fVar17 = fVar17 + fVar16 * m_Speed[playerIdx];
    if (fVar17 <= 0.05) fVar17 = 0.05;
    field_0x238[playerIdx] = fVar17;
}
```

Port (`WaveManager.cpp:853`):
```cpp
m_NextWaveDelay[playerIdx] = wave->m_WaveDelay;     // port +0x44 -- WRONG slot
```

**Port reads +0x44 ("m_WaveDelay") instead of +0x20.** And the +0x238
("wait") field plus its speed-scaled increment from +0x30 are entirely
missing from port. Fix:
- Add a `m_NextDelay_base` field at WAVE_INFO+0x20 (parsed from
  `<NextWaveDelay>` "delay" attr).
- Add `m_NextDelay_inc` at +0x24 (parsed from "inc" attr).
- Add `m_Wait_base` at +0x28 (parsed from "wait" attr).
- Add `m_Wait_speedInc` at +0x30 (parsed from "spinc" or "waitSpinc" attr).
- In GetNextWave: write `field_0x234[p] = max(0.05, +0x20 + +0x24*+0x34)`
  when +0x20 > 0, else 0.0; write `field_0x238[p] = +0x28 + +0x30*speed`.

### 4.2 ChooseFrom queue lacks de-duplication

Binary @ 0x00125052-0x0012516c iterates the ChooseFrom list and, for each
random-typed entry, does a do-while loop to ensure the picked fruit type
isn't already in the player's queue (when `uVar15 < total_fruits - 2`).
Port's loop has no de-dup. **DIVERGES** -- may show repeated fruit in
ChooseFrom waves.

### 4.3 m_WaveCount increment, weighted selection, SPAWNER_INFO::Reset call

These all **MATCH** the binary structure. The `* 10` weighting, the
`m_pCurrentWave[1]++ -> revisit++` post-increment, and the
`SPAWNER_INFO::Reset(wave->field_0x34)` loop are correct. Note that
`SPAWNER_INFO::Reset` itself was not in the audit scope -- the port has
a stub of it in WaveStructs.h:97-110 that may diverge from the binary's
real implementation (flagged for follow-up).

---

## 5. Update — `0x001259d8`

**Status: DIVERGES (combo-mode branch + speed multiplier field)**

Port: `src/game/WaveManager.cpp:603-651`

### 5.1 The "use UpdateComboSpeed instead" branch

Binary @ 0x00125ad8:
```c
if ((field_0x35 == 0) || (m_WaveCount[1] < 1)) {
    // normal Update path (wave pump etc.)
} else {
    UpdateComboSpeed(this, dt);    // arcade-combo only
}
```

If `field_0x35 != 0` AND `m_WaveCount[1] >= 1`, normal update is **skipped**.
Port does **both unconditionally** (calls UpdateComboSpeed inside UpdateWave,
runs wave pump always). **DIVERGES.**

### 5.2 Speed accumulator multiplier reads wrong field

Binary @ 0x00125ac4:
```c
speed = field_0x74 + dt * *(float *)(&this->field_0x7c + gameMode * 4);   // +0x7c[mode]
```

Port (`WaveManager.cpp:621`):
```cpp
float s = field_0x74 + dt * m_SpeedMultPerMode[mode];   // port maps to +0x8c
```

Binary's `+0x7c[4]` is `dtInc` (parsed from `<defaults>` "dtInc" attr).
`+0x8c[4]` is `globalDtStart` (lower clamp bound). **Port is reading the
wrong field** -- currently 1.0 placeholder, so speed grows by `dt` per
frame instead of `dt * dtInc` (typically much smaller).

**Port-side action:** Add `m_DtIncPerMode[4]` at +0x7c (parsed from
`<defaults>` "dtInc" attr in Init), and use that array in Update line 621.

### 5.3 Skipped paths (MINOR DIFF)

- Online-MP dt=0 zero-out (bVar1 = IsOnlineMultiplayer): not ported.
- 10.5s play-time achievement clear: game->field_0x1ac not mapped.
- PowerUpManager::Update conditional: no power-ups ported, field_0x78
  stays 1.0 (which matches the "skip" branch behaviour).

---

## 6. Reset — `0x00125be4`

**Status: MINOR DIFF (camera/HUD/PowerUp not ported, per-mode init MATCHES)**

Port: `src/game/WaveManager.cpp:355-449`

Binary's per-mode field_0x74 init reads +0x8c[mode], port reads
m_SpeedMultPerMode (which is +0x8c) -- MATCHES. Camera vtable calls and
HUD::ResetControls are stubbed in port (camera not ported); LoadTextures
on gameMode==2 skipped (no power-ups). All MINOR DIFF.

---

## 7. Init — `0x0012393c`

**Status: DIVERGES (XML attribute mappings off, multiple fields)**

Port: `src/game/WaveManager.cpp:123-335`

### 7.1 The wave's "wave_dt" attributes go to WAVE_INFO+0x10..+0x18 (not "BombScale1")

Binary `Init` reads `<Wave_dt>` element with attrs:
- "dt" -> WAVE_INFO+0x10
- "inc" -> WAVE_INFO+0x14
- "spinc" -> WAVE_INFO+0x18

These three are then used in `GetWavedt` (binary @ 0x00121908):
```c
fVar4 = *(float *)(puVar1 + 0x10) + *(float *)(puVar1 + 0x34) * *(float *)(puVar1 + 0x14)
        + *(float *)(puVar1 + 0x18) * (&this->m_Speed_P0)[param_1];
```
That's `dt + revisit*inc + spinc*speed`. Port struct has:
- +0x10 -> `m_BombScale1`  (terrible name; should be `m_WaveDtBase`)
- +0x14 -> `wave_dt_inc`   (correct meaning)
- +0x18 -> `delaySpeedScale` (correct meaning)

Naming-only issue. **MINOR DIFF.**

### 7.2 The wave's "NextWaveDelay" attributes go to WAVE_INFO+0x20..+0x30

Binary reads `<NextWaveDelay>`:
- "delay" or "wait" -> +0x20  (the base delay)
- "inc" / similar  -> +0x24  (revisit-scaled inc)
- "spinc" / similar -> +0x28  (speed-scaled inc, or "wait")
- "speedLoss" -> +0x30 (the spinc on the wait)
- And uses `<NextWaveDelay>` "scale" attribute @ 0x00124224 -> +0x1c

Port struct has at +0x20 `m_BombSpeedMax`, at +0x24 `m_BombMinAngle`, at
+0x28 `m_BombMaxAngle`, at +0x30 `m_BombField30`. **All wrongly named.**

GetNextWave (binary) reads +0x20 / +0x24 for the delay computation, NOT
the bomb fields. The port's BombSpeedMax / BombMinAngle / BombMaxAngle
slots in WAVE_INFO are reading the next-wave-delay timing attrs.

Since the port's GetNextWave uses `m_WaveDelay` (port +0x44) instead of
+0x20, the bomb spawn timing **uses zero/garbage values**, but the
**next-wave delay also uses the wrong field**. The port and binary diverge
on how next-wave timing is computed.

### 7.3 BombMin / BombMax storage

Binary @ 0x00123ed4 / 0x00123ee0:
```
Query("bombcount", &local_34[0]->m_BombMin);     // both attrs go to +0x4c
Query("bombmin",   &local_34[0]->m_BombMin);     // re-overwrite from "bombmin"
Query("bombmax",   &local_34[0]->m_BombMax);     // +0x50
if (m_BombMin < 0) m_BombMin = m_BombMax;
if (m_BombMax < 0) m_BombMax = m_BombMin;
```

Port `Init` (`WaveManager.cpp:223`) only queries `overideProbabiltyPool`.
There's NO `bombcount/bombmin/bombmax` query in the port. **Missing**
(port's WAVE_INFO has m_BombMin/m_BombMax fields but they're never
populated from XML). **DIVERGES.**

---

## 8. UpdateComboSpeed — `0x00122f50`

**Status: DIVERGES (port stub is empty)**

Port: `src/game/WaveManager.cpp:751-753` -- TODO stub.

Binary implements a 70-line speed-control routine for arcade mode that:
- Compares `m_Speed_P0` to `m_Speed_P1` (lerp toward each other at ±5/s).
- Manages a `SpeedControl` HUD widget allocation.
- Decrements `field_0x4c` per `m_pCurrentWave_P0->field_0x1c` (arcade fade).
- Calls `ResetSpeed` when `field_0x4c <= 0`.

**Critical for Arcade mode pacing.** Not "flying everywhere" cause.

---

## 9. ResetSpeed — `0x00122e94`

**Status: DIVERGES (stub)**

Port stub. Binary writes 0.0 to several speed fields and clears a save
total. **DIVERGES** (Arcade mode reset broken).

---

## 10. ResetWaveChances — `0x001249d0`

**Status: DIVERGES (heavy)**

Port: simple loop `wi->m_CurrentMax = wi->m_Chance`.

Binary does this for each wave PLUS:
- Writes `wi+0x48 = wi+0x44`, `wi+0x40 = wi+0x3c`, `wi+0x34 = 1.0` (reset
  many WAVE_INFO fields including the revisit counter).
- For waves with a "coinChance" (`wi+0x4c > 0`), interacts with the
  `FruitSaveData` map at `+0x194`, computing a new coin total via two
  Rand32 calls.

Port misses **everything except `m_CurrentMax = m_Chance`**.

---

## 11. AddSpeed — `0x00123510`

**Status: DIVERGES (TODOs are correct, no SFX/score bonus)**

Port `WaveManager.cpp:1161-1169` does the clamp + write. Binary additionally:
- Tracks blitz state at `field_0x4c`/`field_0x60` (sets to 1.0/2.5 on entry).
- Plays SFX via `GameSound::SFXPlay`.
- Adds score via `AddToCurrentScore`.
- Calls `PowerUpManager::ActivateScreenEffect`.

**MINOR DIFF** for game logic; **DIVERGES** for player feedback.

---

## 12. ResetGlobalDt — `0x00121ed8`

**Status: DIVERGES (stub)**

Binary erases all `m_PerWave < 0` PROBABILITY_OVERIDE entries from the
current game-mode list, then sets `field_0x74 = param_1; field_0x2d4 = 0`.
Port stub. **DIVERGES.**

---

## 13. AddToSpeedLossTime — `0x001218ac`

**Status: DIVERGES (stub)**

Binary:
```c
if (0.0 < field_0x4c[playerIdx]) {
    fVar1 = field_0x4c[playerIdx] + amount;
    if (fVar1 < 1.0) fVar1 = 1.0;
    field_0x4c[playerIdx] = fVar1;
}
```

Port stub. **MINOR DIFF** (no power-up using it).

---

## 14. GetCriticalChance — `0x001219c4`

**Status: MATCHES.**

Port uses `(w ? w->m_CriticalChance : 1.0f) * m_CritChanceMult`. Binary
identical (`*(float *)((wave) + 100)` = `wave+0x64` = `m_CriticalChance`).

---

## 15. CriticalMode — `0x001219e4`

**Status: DIVERGES (stub returns false)**

Binary:
```c
critChance = GetCriticalChance(this, playerIdx);
return (uint)((float)(longlong)(**(int **)(GOT + DAT_00121a18) / 2) < critChance);
```

That dereferences a global `int*` (game state), divides by 2, compares
against critChance. Port returns false unconditionally. **DIVERGES** (no
critical slices in port).

---

## 16. GetCurrentOverideList — `0x0012180c`

**Status: DIVERGES (stub returns null)**

Binary returns `&this->probOverrides[gameMode][playerIdx*0x30...]`. Port
returns nullptr. **DIVERGES**, but only used by the override system (also
not ported).

---

## 17. BombScale / BombMultiplyer / FruitMultiplyer / CriticalChanceMod

**Status: MATCHES.**

All four are `field *= mult`. Port `WaveManager.cpp:1179-1182` correct.

---

## 18. GetSpeed — `0x00121834`

**Status: MATCHES.** One-line `return m_Speed[playerIdx]`.

---

## 19. GetWavedt — `0x001218dc`

**Status: MATCHES.** Both compute `dt = base + revisit*inc + spinc*speed`,
multiply by `field_0x74 * field_0x78` (player 0 only), clamp to (0, 100].
Note: port's "delaySpeedScale" naming corresponds to binary's +0x18 (the
wave_dt spinc). Naming OK; behaviour OK.

---

## 20. GetComboBonusProgression — `0x00121840`

**Status: DIVERGES (stub returns 0.0)**

Binary:
```c
fVar2 = field_0x60[playerIdx] / -2.5 + 1.0;
fVar1 = (fVar2 <= 0) ? 0 : (fVar2 > 1) ? 1 : fVar2;
fVar1 = ((float)(longlong)field_0x5c[playerIdx] + fVar1) / 6.0;
return (fVar1 > 1) ? 1.0 : fVar1;
```

Port stub. **DIVERGES** (HUD combo progress meter shows 0).

---

## 21. ClearUnspawned — `0x00122ad8`

**Status: MATCHES.** `Fruit::ClearUnspawned(false); Bomb::ClearUnspawned();`

---

## 22. DrawWaveNumber / Draw — `0x00122ae8`

**Status: DIVERGES (stub)**, but minor (UI overlay only).

---

## 23. SetCurrentWave — `0x00125340`

**Status: MINOR DIFF.**

Port writes `m_WaveCount[playerIdx] = waveNo - 1` and calls `GetNextWave(0)`.
Binary does the same but writes to `m_pCurrentWave_P1[playerIdx]` (which is
the same memory aliased). Behaviourally equivalent.

---

## 24. Resume — `0x00124b1c`

**Status: DIVERGES (most of the body is a stub)**

Port has structure but most action items are TODO. Not relevant to
"fruit flying everywhere" since Resume isn't called in normal play.

---

## 25. IsWaveProcessing — `0x00122a40`

**Status: MINOR DIFF.**

Port matches the binary's branching closely. One subtle difference: binary
calls `Bomb::GetNumActiveForPlayer(-1, true)` only if `IsMultiplayer()` is
true; port unconditionally calls it. Also binary uses
`GetNumActiveForPlayer(-1, false)` for fruit when bombs disallowed; port
matches. Net effect: in single-player, port's extra Bomb check returns
the same answer (active-bomb count). **No observable runtime difference.**

---

## Summary table

| Method | Status |
|---|---|
| SpawnFruit | **CRITICAL** (angle DAT, missing spread, gravity Vec3, side positions, z-stride) |
| SpawnBomb | DIVERGES (side positions, gravity field) |
| UpdateWave | DIVERGES (no spawnLevel/field_0x6c gates, no overrides) |
| GetNextWave | DIVERGES (m_NextWaveDelay wrong field, ChooseFrom no de-dup) |
| Update | DIVERGES (no UpdateComboSpeed branch, wrong dtInc field) |
| Reset | MINOR DIFF (camera/HUD/PowerUp stubs) |
| Init | DIVERGES (BombMin/Max not parsed, NextWaveDelay attrs wrong slot) |
| UpdateComboSpeed | DIVERGES (stub) |
| ResetSpeed | DIVERGES (stub) |
| ResetWaveChances | DIVERGES (only m_CurrentMax handled) |
| AddSpeed | DIVERGES (no SFX/score, only clamp) |
| ResetGlobalDt | DIVERGES (stub) |
| AddToSpeedLossTime | DIVERGES (stub) |
| GetCriticalChance | MATCHES |
| CriticalMode | DIVERGES (stub) |
| GetCurrentOverideList | DIVERGES (stub) |
| BombScale, BombMultiplyer, FruitMultiplyer, CriticalChanceMod | MATCHES |
| GetSpeed | MATCHES |
| GetWavedt | MATCHES |
| GetComboBonusProgression | DIVERGES (stub) |
| ClearUnspawned | MATCHES |
| Draw | DIVERGES (stub, UI) |
| SetCurrentWave | MATCHES (modulo P1 alias) |
| Resume | DIVERGES (mostly stub) |
| IsWaveProcessing | MINOR DIFF |

---

## Top priorities to fix the "flying everywhere" bug

In order of impact:

1. **SpawnFruit angle pipeline (1.1, 1.2)** -- replace 182.0f with 150.0f
   in the range/baseline; add the spread stage with `RandF(1.0)` parabola;
   move `* 0xb6` to the very end. THIS is the single most likely cause of
   wildly-flying fruit.

2. **SpawnFruit position formulas (1.5, 1.6, 1.7)** -- BOTTOM posY = -160
   not -240; BOTTOM posX = baseDeg (after spread) not (r1 % 320 - 160);
   side branches should use (baseDeg * 320/480), not ±240; z-stride should
   start from 1, not 0.

3. **SpawnFruit gravity/timescale assignment (1.8, 1.9)** -- after Init,
   write `f->m_TimeScale = info->m_TimeScale (+0x14)` and multiply
   `f->m_Gravity` by `spawner.gravityVec3 * (-f->m_Gravity.y)`. Apply
   the +/-0.01 nudge for LEFT/RIGHT.

4. **SPAWNER_INFO struct fix** -- gravity is a Vec3 at +0x18..+0x20,
   NOT a float at +0x48. The +0x48 slot is something else (next to be
   determined when porting `SPAWNER_INFO::Reset`).

Other DIVERGES items (UpdateComboSpeed, AddSpeed effects, Init BombMin/Max,
GetNextWave next-wave-delay) affect Arcade pacing and HUD but won't fix
the "flying" symptom directly.
