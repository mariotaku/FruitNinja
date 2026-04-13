# Fruit Slicing RE Notes

<!-- Analysed: 2026-04-13T23:30 -->

Ground-truth RE for the fruit slice pipeline, extracted from
`Fruit::CollisionResponse` (0x001780b0, 591 lines) and `Fruit::Slice`
(0x00176d58, 355 lines). Use this to implement `Fruit::OnSliced` in the
port once the `Fruit` struct has been expanded to match.

This is the fruit-side analogue of `particle-refine-notes.md`: written as
a short, actionable spec for implementation, not a full function dump.

## Entry point — `Fruit::CollisionResponse` (0x001780b0)

### Guard
```c
if (this->m_bSliced != 0 || this->m_SliceTimer > -1.0f) return 1;
```
`m_SliceTimer` starts at `-1.0f` in `Init`. CollisionResponse sets it to a
positive value; `Update` counts it down; `Slice` fires at 0.

### Timer + impulse constants (from DATs near 0x001784dc)

| DAT | Value | Meaning |
|---|---|---|
| 0x001784dc | **0.03f** | Base `m_SliceTimer` |
| 0x001784e0 | **0.1f**  | Blade-speed scale (`magnitude(bladeVel) * 0.1`) |
| 0x001784e4 | **-182.0f** | Slice-angle / degrees conversion divisor |
| 0x001784e8 | **0.4f**  | `AddSlice` y multiplier (impulse display scale) |
| 0x001784ec | **90.0f** | `AddSlice` x offset in degrees |

### Pipeline

```c
// 1. Critical eligibility (skip for now — needs WaveManager + RESET_BONUS)
m_bCriticalEligible = <rng test>;

// 2. Play SFX
if (m_bCriticalEligible) {
    GameSound::SFXPlay("critical_hit", 0.25, 1.0);   // string at DAT_00178508
    ItemManager::PlayAlternateImpactSound(1.0, 0.5);
} else {
    // Iterate FRUIT_INFO[type].m_pImpactSounds (array at +0x31c,
    // count at +0x320). Each 12 bytes wide; element[0] is a char*
    // sfx name. Play them all at (0.5, 1.0).
    int n = fruitInfo.m_ImpactSoundCount;  // +0x320
    for (int i = 0; i < n; i++) {
        GameSound::SFXPlay(fruitInfo.m_pImpactSounds[i].name, 0.5, 1.0);
    }
}

// 3. Impulse clamp
m_SliceTimer = 0.03f;
m_SlicePos   = pos;
float bladeSpeed = magnitude(bladeVel) * 0.1f;

// Three branches for the clamp range + timer scale:
if (m_bCriticalEligible) {
    bladeSpeed  = clamp(bladeSpeed, 6.0, 8.0);
    m_SliceTimer *= 2.5;                    // → 0.075
    CriticalFlash(pos, g_CritColour);       // coloured flash
} else if (fruitInfo.m_BaseScore == 0x32 /* 50, special fruit */
           && !game->gameOverFlag) {
    bladeSpeed  = clamp(bladeSpeed, 6.0, 8.0);
    m_SliceTimer *= 0.5;                    // → 0.015
    CriticalFlash(pos, Colour(255,255,255,128));
    MissControl::GetFree()->MakeRare(pos);
} else {
    bladeSpeed = clamp(bladeSpeed, 4.0, 8.0);
    // m_SliceTimer stays at 0.03
}

m_SliceAngle   = Atan2Idx(bladeVel.x, bladeVel.y);  // 16-bit angle
m_SliceImpulse = bladeSpeed;

// 4. Clear the fruit's trail emitters (m_pEmitter1/2)
ClearEmitter(m_pEmitter1); m_pEmitter1 = NULL;
ClearEmitter(m_pEmitter2); m_pEmitter2 = NULL;

// 5. Visual AddSlice line (single line normal, two lines critical in Slice())
Vec3 sliceInfo = {
    x: m_SliceAngle / -182.0f + 90.0f,   // angle as degrees-offset
    y: bladeSpeed * 0.4f,                 // length
    z: unused
};
AddSlice(sliceInfo, ..., ..., &pos);

// 6. Spawn blade-hit particles (FRUIT_INFO[type] + 0x250 = m_NameHash,
//    or "fruit_critical"-ish hash if critical). Rotate emitter to blade dir.
uint32_t hitHash = m_bCriticalEligible
    ? StringHash("<crit template>")       // at DAT_001788f0
    : fruitInfo.m_NameHash;               // +0x250
if (PSPParticleManager::EmitterExists(hitHash)) {
    auto* e = PSPParticleManager::AddEmitter(hitHash, NULL, persistent);
    if (e) {
        e->m_CosAngle =  CosIdx(-m_SliceAngle);   // +0x2c
        e->m_SinAngle = -SinIdx(-m_SliceAngle);   // +0x30
        e->m_Pos      = pos;
    }
}

// 7. Spawn juice particles (FRUIT_INFO[type] + 0x25c = m_SlicedHash).
//    Two emitters — one per future half. Both created immediately.
uint32_t juiceHash = m_bCriticalEligible
    ? StringHash("<crit juice>")          // at DAT_001788f8
    : fruitInfo.m_SlicedHash;             // +0x25c
if (PSPParticleManager::EmitterExists(juiceHash)) {
    m_pEmitter1 = PSPParticleManager::AddEmitter(juiceHash, NULL, true);
    m_pEmitter2 = PSPParticleManager::AddEmitter(juiceHash, NULL, true);
    m_pEmitter1->m_Pos = pos;
    m_pEmitter2->m_Pos = m_HalfB_pos;     // second half starts at same pos,
                                          // splits after Slice() fires
}

// 8. Achievements + power-up spawn (skip for port v1)
AchievementManager::UnlockSpecificOrderAchievement(fruitInfo.m_NameHash);
if (fruitInfo.m_pPowers != NULL) {   // +0x32c
    PowerUpManager::ActivatePower(FRUIT_POWERS::RandomPower(), pos, NULL);
}

// 9. Score (needs scoring subsystem)
int base = fruitInfo.m_BaseScore;      // +0x314
if (m_bCriticalEligible)
    base += g_CriticalBonus;           // +0x3c8 (FRUIT_INFO singleton)
// ... combo-streak bookkeeping, WaveManager::AddToSpeedLossTime, etc.
AddToCurrentScore(base, m_PlayerIdx, true, false);

// 10. FruitSaveData::AddToTotal for "<name>" and "<name>_point_total"

// 11. Coin::MakeCoins if (iVar27 > 0) — amount scales with score threshold

return 0;
```

### Port-side bridge

The port's slice-test pass in `SlashEntity::Update` already fires
`Entity::OnSliced(bladeVel)` on hit. To wire fruit up, add
`Fruit::OnSliced` that replicates the three blocks that matter for v1:

1. **Guard** — `if (m_bSliced || m_SliceTimer > -1.0f) return;`
2. **Timer / impulse** — no critical, no special, just the base 0.03f +
   clamp to `[4, 8]`.
3. **Juice emitter** — `fruitInfo.m_SlicedHash` resolves to the "%s_sliced"
   template string which `FruitInfo_Load` already computes at +0x25c.
   `PSPParticleManager::AddEmitter(hash, NULL, true)` is already ported.

Everything else (SFX, AddSlice, achievement, score, coin) is a TODO that
doesn't block the visual from working.

## Split function — `Fruit::Slice` (0x00176d58)

Called from `Fruit::Update` when `m_SliceTimer` countdown hits zero.

### Constants

| DAT | Value | Meaning |
|---|---|---|
| 0x00177034 | **0.0f**  | z-plane constant |
| 0x00177038 | **-182.0f** | slice-angle → degrees divisor |
| 0x0017703c | **360.0f** | angle offset (GetSmallestDelta base) |
| 0x00177040 | **182.0f** | positive angle divisor |
| 0x00177044 | **0.4f**  | `AddSlice` critical impulse scale A |
| 0x00177048 | **0.7f**  | `AddSlice` critical impulse scale B |
| 0x0017704c | **60.0f** | critical dual-line angle offset (degrees) |
| 0x00177050 | **0.2f**  | per-splat velocity decay |
| 0x0017706c | **0.3f**  | splat-z-velocity damping threshold |
| 0x001774a8 | **10920.0f** | `0x2aa8` fallback for rand |
| 0x001774ac | **0.0f**  | z-velocity for halves |

### Pipeline

```c
void Fruit::Slice() {
    m_SliceTimer = 0.0f;

    // 1. Build a rotation matrix from m_Rot1, multiply by (0,0,1) to get
    //    the slice plane normal in world space.
    Matrix44 rotMat = Quaternion::Matrix44Unit(m_Rot1);
    Vec3 up = MultVec44({0, 0, 1}, rotMat);   // slice direction

    // 2. Force non-critical in post-game-over states
    if (game->gameOverFlag) m_bCriticalEligible = 0;

    // 3. Compute whether to flip the two halves' angles: if the up
    //    direction disagrees with m_SliceAngle (via GetSmallestDelta on
    //    the +0xC004/+0x8008 offset pair), bVar8 = true.
    bool flipSide = false;
    if (|up.x| + |up.y| > 0) {
        short sliceDirAngle = Atan2Idx(up.y, up.x);
        float delta = GetSmallestDelta(
            (sliceDirAngle + 0xC004) / -182.0f + 360.0f,
            (m_SliceAngle   + 0x8008) /  182.0f);
        if (delta < 0) flipSide = true;
    }

    // 4. Splat count: 2..3 (rand(0..1) + 2)
    float impulse = m_SliceImpulse;
    int splatCount = Rand32(2) + 2;

    // 5. Critical-only: draw two slice lines (+/- 60° offset), impulse *= 1.5
    if (m_bCriticalEligible && m_PlayerIdx < 2) {
        Vec3 infoA = { m_SliceAngle/-182.0 + 60.0,
                       impulse * 0.4 * 0.7, 0 };
        Vec3 infoB = { m_SliceAngle/-182.0 - 60.0,
                       impulse * 0.4 * 0.7, 0 };
        AddSlice(infoA, ..., ..., &pos);
        AddSlice(infoB, ..., ..., &pos);
        splatCount = <some extra count from GOT>;
        impulse *= 1.5;
        MissControl::GetFree()->MakeCritical(pos, playerIdx);
    }

    // 6. Special-fruit (baseScore == 0x32): impulse *= 1.5
    if (fruitInfo.m_BaseScore == 0x32) {
        splatCount = <some extra count from GOT>;
        impulse *= 1.5;
    }

    // 7. Online game-over edge: splatCount = 0 if this is a
    //    "went offscreen during game-over" case
    if (game->gameOverFlag && IsOffscreen(this) && m_PlayerIdx > 1)
        splatCount = 0;

    // 8. Spawn splat entities
    for (int i = 0; i < splatCount; i++) {
        uint16_t angle = Rand32(0xfff0);
        float r      = RandF(0.5f);
        float speed  = (impulse + r * impulse) * (i * 0.2f + 5.0f);

        Vec3 vel(SinIdx(angle) * speed, CosIdx(angle) * speed, 0.0f);
        SplatEntity* s = SplatEntity::GetFree();
        SplatEntity::MakeSplat(s, pos, vel, <some bool>, fruitType + critOffset);

        // Z-velocity damping: 1 - (i - 2) / splatCount, clamped >= 0.3
        float zFade = 1.0f - (float)(i - 2) / (float)splatCount;
        if (zFade < 0.3f) zFade = 0.3f;
        if (zFade > 1.0f) zFade = 1.0f;
        s->m_Vel.z *= zFade;

        // i >= 3: further damp all three components by globals
        if (i > 2) {
            s->m_Vel *= g_SplatDampScalarA;   // from GOT
            // And scale (m_PosA?) by vec3 multiplier
        }
    }

    // 9. Splat SFX: pick one of "splat_1", "splat_2", "splat_3"
    if (splatCount > 0) {
        char name[128];
        sprintf(name, "splat_%d", Rand32(3) + 1);
        GameSound::SFXPlay(GameSound, name, 1.0, 1.0);
    }

    // 10. Two-body velocity split.
    // sliceFactor = 1 - fruitInfo[+0x24c]  (per-fruit slice softness)
    // sliceFactor blends: new_vel = sliceDir * impulse * sliceFactor
    //                               + old_vel * (1 - sliceFactor)
    float sliceSoftness = fruitInfo[+0x24c];
    float sliceFactor   = 1.0f - sliceSoftness;

    // Pick two angular offsets for the two halves. Each offset is
    //   randA/B * (1 - softness) * 4
    // where randA/B comes from a resampled rand32(0x5550) > 0x2aa8
    // pattern (takes two tries for good distribution).
    short offA = (short)(rand_biased() * (1 - softness) * 4);  // m_HalfB
    short offB = (short)(rand_biased() * (1 - softness) * 4);  // this (vel)

    if (flipSide) {
        // Rotate the base angle by 180° (+0x7ff8 = +180° in 16-bit)
        // before applying offsets.
        m_SliceAngle += 0x7ff8;
    }
    short halfAngleA = m_SliceAngle + offA;   // first half
    short halfAngleB = m_SliceAngle - offB;   // second half
    if (flipSide) {
        halfAngleA += 0x7ff8;
        halfAngleB += 0x7ff8;
    }

    Vec3 halfVelA(SinIdx(halfAngleA) * impulse,
                  CosIdx(halfAngleA) * impulse, 0.0f);
    Vec3 halfVelB(SinIdx(halfAngleB) * impulse,
                  CosIdx(halfAngleB) * impulse, 0.0f);

    m_HalfB_vel = halfVelA * sliceFactor + vel * sliceSoftness;
    vel         = halfVelB * sliceFactor + vel * sliceSoftness;

    // 11. Critical / special override: use pure sliceDir angle (no
    //     blending with old vel). SliceAngle + 0x3ffc = +90°.
    if (m_bCriticalEligible || fruitInfo.m_BaseScore == 0x32) {
        short a1 = m_SliceAngle + 0x3ffc;    // +90°
        short a2 = m_SliceAngle + 0xc004;    // -90°
        m_HalfB_vel = Vec3(SinIdx(a1)*impulse, CosIdx(a1)*impulse, 0) * 0.5;
        vel         = Vec3(SinIdx(a2)*impulse, CosIdx(a2)*impulse, 0) * 0.5;
    } else if (field_0x10c == 0) {
        MoveFruitZPositionToBack(&m_ZPosition);   // push to back of Z queue
    }

    m_bSliced = 1;

    // 12. Spin boost — loop x2 for both halves. Each half gets random
    //     rotation velocity based on the sum of abs(m_RotVel1) components.
    //     Critical: spin *= 2 then x1.5. Normal: spin *= 0.5 then x1.5.
    //     One component gets +/- 50% random, one gets * 1.5 sign-flipped.
    for (int h = 0; h < 2; h++) {
        float mag = |rv.x| + |rv.y| + |rv.z|;
        mag = m_bCriticalEligible ? mag * 2 : mag * 0.5;

        float compA = mag * (RandF(0.5) + 0.75);
        float compB = mag * (RandF(0.5) + 0.75);
        if (Rand32(3) == 0) compB = fabs(compA) * 1.5;  // 1/4 chance
        else                compB = mag * (RandF(-0.75..0.25)); // blend

        // Sign-flip rules based on flipSide + half index
        // (too messy to transcribe cleanly — see raw decompile)

        newRotVel = Vec3(compB, compA, -compC);
        m_RotVel[h] = newRotVel;

        // Build a new m_Rot[h] quaternion from three axis-angle rotations:
        //   RotX(1,0,0, compB)  — but binary actually passes (0,0,1,0x3ffc)
        //   RotY(0,1,0, 0x3ffc)
        //   RotZ(0,0,1, m_SliceAngle)
        // then m_Rot[h] *= quatX * quatY * quatZ
    }
}
```

### The "resampled rand" idiom

Seen twice at the start of Slice and twice in the velocity offset calc:

```c
randVal = Rand32(0x5550);           // 0x5550 = 21840
if (randVal > 0x2aa8 /* 10920 */) {
    Rand32(0x5550);                 // re-roll, discard first
}
```

This draws a biased random number — low-half of the range triggers a
re-roll. Looks like an early "avoid small values" heuristic. In the port
it's fine to just call `Rand32(0x5550)` once.

## Supporting functions

### `SplatEntity::MakeSplat` (0x0017f2f0, 131 lines)

Signature: `void MakeSplat(Vec3 pos, Vec3 vel, bool p3, long splatTypeIdx)`

Takes a free splat from the pool, sets its position to `pos` **with
z forced to 0**, then applies a velocity transform:
```c
this->m_Vel = vel;
float speed = magnitude(vel);
this->m_Vel.y *= 1.5f;
this->m_Vel.z  = speed * -0.5f - DAT_0017f568 - RandF(10.0f);
this->m_Vel   *= 6.0f;
```
Then picks a random rotation (0..360°), computes a colour from
`Fruit::FruitTypeColour(splatTypeIdx)`, and picks one of N sprite
variants from a modulo on `splatTypeIdx % splatVariantCount`.

Pool access: `SplatEntity::GetFree` at 0x0017f?? (unknown; search for it).
Pool size and stride: **0x78 bytes per splat** (docs/entities/splat-entity.md).
Field offsets:
- +0x38: position (`m_Pos`)
- +0x5c: velocity (`m_Vel`)
- +0x70: fruit type index
- +0x75: active flag (1 = alive)

Rendering: `SplatEntity::DrawActiveSplats` (0x00180344) iterates the pool,
calls each splat's `Draw()` vtable to build vertex data, then issues one
batched `Mesh::DrawTriList` call with all vertices. Port this as a single
`Renderer::DrawTriList` call after the splat loop.

### `AddSlice` (0x0016b480, 62 lines)

Pops a `SliceEffect` node from `MemoryPool<List<SliceEffect>::Node>`,
stores `(x=angle_deg_offset, y=impulse, pos, flag)`, adds to a global list.

1-in-9 chance of playing a random whoosh/slice SFX if `impulse > 2.5`:
- `DAT_0016b590`, `DAT_0016b594`, `DAT_0016b598` each hold a sfx name
  string offset. The exact strings are unresolved here but live in
  the `.rodata` area near 0x0016b480 — look them up if SFX is needed.

The actual rendering of the slice lines is in a global
`SlashEntity::DrawSlices` pass (not `DrawSlice` blade renderer); see
GameDraw 0x16b888 call at layer 0x40.

## Port `Fruit` struct — what's missing

The port's `Fruit.h` currently has only:
- `m_FruitType`, `m_bSliced`, `m_Rot1/2`, `m_RotVel1/2`,
  `m_SecondPos/Vel`, `m_Gravity`, `m_ScaleAnim`, `m_ChuckDelay`,
  `m_RotAxis`, `m_ZPosition`, `m_Model`

For OnSliced to work, the following fields need to be added to match
the binary layout (docs/entities/fruit.md offset table):

| Binary offset | Port field | Type | Purpose |
|---|---|---|---|
| +0x6c | `m_SliceTimer` | `float` | Countdown to Slice(); init -1 |
| +0x70 | `m_SliceAngle` | `uint16_t` | Atan2Idx of bladeVel |
| +0x74 | `m_SliceImpulse` | `float` | Clamped `bladeSpeed` |
| +0x78 | `m_SlicePos` | `Vec3` | Snapshot of pos at slice time |
| +0xb4 | `m_bSliced` | `byte` | (already present) |
| +0xc4..+0xcc | `m_HalfB_vel` | `Vec3` | Second-half velocity |
| +0x7c | `m_pEmitter1` | `PSPParticleEmitter*` | Trail / juice emitter A |
| +0x80 | `m_pEmitter2` | `PSPParticleEmitter*` | Trail / juice emitter B |
| +0x108 | `m_PlayerIdx` | `int` | For split-screen 2P |
| +0x110 | `m_bCriticalEligible` | `byte` | Crit flag |
| +0x114 | `m_bNoPowerUp` | `byte` | Power-up suppression |
| +0x10c | `field_0x10c` | `byte` | Special game-state flag |
| +0x4c | `m_SliceState` | `byte` | 0=normal, 2=local crit hit |

## Minimal v1 implementation plan

For a first pass that just makes fruit visually slice:

1. **Expand `Fruit` struct** with `m_SliceTimer`, `m_SliceAngle`,
   `m_SliceImpulse`, `m_SlicePos`, `m_pEmitter1/2`. Init `m_SliceTimer = -1`.
2. **Populate `m_Col`** in `Fruit::Init` — already computed from
   `fruitInfo.m_CollisionScale * 0.52` via `SetFruitType` (docs).
3. **Update `m_Col.center` each frame** in `Fruit::Update` so the slice
   test hits the right place.
4. **Add `Fruit::OnSliced(bladeVel)`** with the guard + base-case path
   (no critical, no special, no SFX, no score, no achievement). Just:
   - set `m_SliceTimer = 0.03f`
   - set `m_SliceAngle = Atan2Idx(bladeVel.x, bladeVel.y)`
   - set `m_SliceImpulse = clamp(magnitude(bladeVel) * 0.1, 4, 8)`
   - spawn juice emitter using `fruitInfo.m_SlicedHash`
5. **Extend `Fruit::Update`** slice-timer countdown branch: when it
   reaches 0, call a minimal `Slice()` that:
   - sets `m_bSliced = 1`
   - computes `halfVelA` / `halfVelB` from `m_SliceAngle` +/- some offset
     (skip the flipSide logic)
   - skips splat spawning entirely in v1 (SplatEntity not ported)
6. **Update `Fruit::Draw`** to pick half models when `m_bSliced == 1`.
   Requires loading the `_half_a` / `_half_b` models for each fruit
   type — `LoadFruitModels` (0x1794e0) handles this in the binary.
   Docs say each `FRUIT_INFO` has model slots at some offset; currently
   the port loads whole-fruit models inline in `Fruit::Init`.

Step 6 is the biggest open question: the port doesn't yet have half-fruit
models loaded, so slicing would show the whole fruit splitting into two
copies of itself. Acceptable as v1 visual stub until LoadFruitModels is
ported.

## See also

- `docs/entities/fruit.md` — struct layout + full vtable
- `docs/entities/splat-entity.md` — splat pool (thin)
- `src/entities/Fruit.{h,cpp}` — port impl (needs expansion)
- Binary: `CollisionResponse @ 0x1780b0`, `Slice @ 0x176d58`,
  `MakeSplat @ 0x17f2f0`, `AddSlice @ 0x16b480`,
  `MoveFruitZPositionToBack @ ?`, `SetFruitType @ 0x17621c`
