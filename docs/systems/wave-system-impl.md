# Wave System — Implementation Notes

This document fills implementation gaps in `docs/systems/wave-system.md` and
`docs/functions/wave.md` so the C++ port can replace the stubs in
`src/game/WaveManager.cpp`.

All addresses are inside `FruitNinja.exe` (ARM32 Bada ELF). Pseudocode is
post-processed Ghidra output, not raw decompiler text.

The existing docs already cover `Init`, `Update`, `UpdateWave`, `GetNextWave`,
`SetupWaveQue`, `SpawnFruit`. The items below are the parts that were missing
or wrong.

---

## 1. `WaveManager::Reset(bool fullReset)` @ `0x00125be4`

Resets per-game state on `STATE_GAME_START`. `fullReset == true` is the only
public path used (see item 9). The flag controls whether `NewGame` is called
at the end (which in turn calls `PowerUpManager::Reset(true)` and
`WaveManager::ResetGlobalDt(1.0f)`).

```c
void WaveManager::Reset(bool fullReset) {
    Game* game = g_Game;                        // *(GOT + 0x125ef4)
    if (game->m_GameMode == 2)                  // mode 2 = arcade
        PowerUpManager::GetInstance()->LoadTextures();

    // 1. Drop wave queue + queue item (used by survival/combo modes)
    if (m_pWaveQue)     { delete m_pWaveQue;     m_pWaveQue = nullptr; }
    if (m_pWaveQueItem) { delete m_pWaveQueItem; m_pWaveQueItem = nullptr; }

    // 2. Per-frame multipliers + state flags
    field_0x36 = 0;                             // (?)
    field_0x35 = 1;                             // wave-active flag
    field_0x37 = 0;
    *(int*)&field_0x38 = -1;                    // last selected waveIdx
    *(int*)&field_0x28 = 0;                     //
    field_0x2d0 = 0;                            //
    field_0x2cc = 0;                            //
    field_0x2d4 = 0.0f;                         // wave-step accumulator
    field_0x40 = 0.0f; field_0x44 = 0.0f;       // play-time accums
    field_0x48 = 0;
    field_0x238 = 0.0f;
    field_0x4c = 0.0f; field_0x60 = 0.0f;       // combo timers / level
    m_Speed[0] = 0.0f; m_Speed[1] = 0.0f;       // +0x54, +0x58

    // 3. Game-side flags / score
    game->field_0x1c   = 0;                     // some "in-game" toggle
    field_0x23d = 0;                            // PROBABILITY_OVERIDE flags
    field_0x23e = 0;
    *(float*)&field_0x240 = Math::Random::RandF(&random, 10.0f) + 10.0f;
    SetScore(0, -1);                            // 0 points, all players
    SetMissCount(0, -1);
    ET_ClearKnownEntities(-1);

    // 4. Reset HUD/Camera (vtable calls on game->display.camera)
    Camera* cam = game->display->camera;        // game->field_0x48 → cam
    *(int*)cam->some_ptr = 0;                   // cam->field_0x00 = 0 (vtable)
    *(int*)other_ptr   = 1;                     // (HUD)
    field_0x23c = 1;                            // wave-was-spawned flag
    *(int*)&field_0x230 = -1;                   // m_WaveCount[0] = -1 (preincr later)
    cam->field_0x1a8 = 1;                       // tutorial-active
    cam->field_0x1ac = 0.0f;                    // play-time elapsed
    field_0x5c = 0;

    // Camera reset: SetPos / SetTarget / SetUp via vtable
    Vec3 camDefault = *(Vec3*)(GOT + DAT_00125f04);   // default cam pos
    cam->SetPos(camDefault);
    Vec3 zVec; zVec.z = 160.0f / TanIdx((short)(cam->fov * 182.0f));
    cam->SetTarget(camDefault + zVec);
    cam->SetUp(Vec3(0, 1, 0));

    // HUD::ResetControls only if game->field_0x3c != 0 (HUD exists)
    if (game->display->field_0x3c)
        HUD::ResetControls();

    // 5. Clear unspawned fruits + bombs, disable any active ones
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
    ActorManager* am = ActorManager::GetInstance();
    for (int i = 0; ; i++) {
        Fruit* f = (Fruit*)am->GetEntity(0, i);
        if (!f) break;
        Fruit::Disable(f);
    }
    for (int i = 0; ; i++) {
        Bomb* b = (Bomb*)am->GetEntity(1, i);
        if (!b) break;
        Bomb::Disable(b);
    }

    // 6. Reset per-wave chance counters + PROBABILITY_OVERIDE state
    ResetWaveChances();
    field_0x2c = 0;
    for (PROBABILITY_OVERIDE& po : m_ProbOverrides[m_GameMode])
        po.SelectType();                        // re-roll which fruit it overrides

    field_0x2c4 = 0;
    field_0x2c8 = 1;
    // Clear m_FruitQueue[2][16]: 32 ints from +0x244 to +0x2c4
    for (int i = 0; i < 0x80; i += 4)
        *(int*)(&field_0x244 + i) = -1;

    // 7. Kick first wave if there are any waves
    if (waveInfos.size() != 0) {
        if (!IsOnlineMultiplayer())
            GetNextWave(0);
        if (IsSameScreenMultiplayer())
            field_0x234 += 1.0f;                // bump P1 wave delay
    }

    // 8. Final per-mode speed-multiplier defaults
    game->field_0x199 = 0;                      // multiplayer-sync flag
    field_0x78 = 1.0f;                          // dtMod
    field_0x74 = m_SpeedMultPerMode[m_GameMode]; // +0x8c..+0x98 array

    if (fullReset)
        NewGame();
}
```

### Constants resolved

| DAT addr     | Value      | Meaning |
|--------------|------------|---------|
| `0x00125ee4` | `0.0f`     | zero seed for several float fields |
| `0x00125ee8` | `182.0f`   | angle scale (deg → fixed-point) |
| `0x00125eec` | `160.0f`   | half-screen width for camera math |

### Key observations

- `fullReset == false` is **never used by the game itself** — only by save/load
  paths (Resume calls `Reset(false)` indirectly). The user-facing "new game"
  button always calls `Reset(true)`.
- The whole "first wave" call (`GetNextWave(0)`) happens inside Reset, so the
  port should NOT pre-call GetNextWave from MainScreen — Reset already does it.

---

## 2. `WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO*, int playerIdx)` @ `0x00121fa8`

Mirrors `SpawnFruit` but creates `Bomb` entities. The `type` parameter is
either a `SPAWNER_INFO*` or one of two magic values — the binary tests
`type == 0` and `(int)type < 2` to branch.

The split-screen MP path also spawns a *mirrored* bomb for player 2.

```c
void WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO* spawner, int playerIdx) {
    Random* rng = &this->random;

    for (int i = 1; i <= count; i++) {
        // ---- 1. Angle from spawner (or default ±1.0) ----
        float minAngle, maxAngle;
        if (type == 0)              { minAngle = -1.0f; maxAngle = 1.0f; }
        else                        { minAngle = spawner->m_MinAngle;  // +0x2c
                                      maxAngle = spawner->m_MaxAngle; }// +0x30

        // angle range expressed in 182-units (deg * 182)
        float range = minAngle * (-150.0f) + maxAngle * 150.0f;   // DAT_00122208 = -150, DAT_0012220c = +150
        uint32_t r1  = rng->Rand32((uint)((range > 0) ? range : 0));
        int   baseDeg = (int)((float)r1 + minAngle * 150.0f);

        // ---- 2. Spread depends on spawner side flag ----
        // spawnType < 2 (= bottom) → ±10° (fVar15=20.0)
        // spawnType >= 2 (= side)  → ±6°  (fVar15=12.0)
        float spread = (type == 0 || spawner->m_SpawnType < 2) ? 20.0f : 12.0f;
        long center  = (long)(((float)baseDeg / -300.0f) * spread * 0.5f);  // DAT_00122210 = -300
        long lo      = (long)((float)center + spread * -0.5f);
        long hi      = (long)((float)center + spread *  0.5f);
        uint32_t r2  = rng->Rand32(hi - lo);
        uint16_t angle = (uint16_t)((short)(lo + r2) * 0xb6);   // 0xb6 = 182

        // ---- 3. Speed + per-axis multipliers ----
        float speed = rng->RandF(1.5f) + 9.5f;        // 9.5 .. 11.0
        float sin_a = Math::SinIdx(angle);
        float cos_a = Math::CosIdx(angle);
        float velMultX  = (type == 0) ? 1.0f : spawner->m_MinVel;        // +0x24
        float velMultY  = (type == 0) ? 1.0f : spawner->m_MaxVel;        // +0x28
        float zOffset   = (type == 0) ? 0.0f : spawner->m_ZOffset;       // +0x5c

        velX = sin_a * speed * velMultX;
        velY = cos_a * speed * velMultY * 1.075f;     // DAT_00122218 = 1.075

        // Spawn position uses default `(SCREEN_BOTTOM)` vector
        Vec3 posSign(*(Vec3*)(GOT + DAT_00122234));   // (1, -1, 1)?
        Vec3 velSign(*(Vec3*)(GOT + DAT_00122238));   // (1, -1, ...)?
        // (operator* with -1.0 flips Y on bottom side -- standard)

        // ---- 4. Side-spawn rewrites (apply same way as SpawnFruit) ----
        if (type != 0) {
            char st = spawner->m_SpawnType;
            switch (st) {
            case 1:                                 // bottom-slow
                posSign = (Vec3){ 1, -1, 1 };
                velSign = (Vec3){ 1,  1, 1 };       // ??? from DAT_00122238
                velY *= 0.5f;
                break;
            case 4:                                 // random side
                st = (rng->Rand32(2) == 0) ? 2 : 3;
                /* fall through */
            case 3:                                 // right side
                posSign = (Vec3){ 1, 1, 1 };
                velSign = (Vec3){ 1, 1, 1 };        // from DAT_0012223c (sideSign)
                /* fall through */
            case 2:                                 // left side -- SWAP X/Y
                center = (long)(velY * -0.75f);
                int newVelY = (long)(velX + speed * spawner->m_Gravity * (-0.65f));
                                                    // DAT_00122224 = -0.65
                lo = (long)(((float)center) * (320.0f / 480.0f));   // baseDeg→Y screen
                                                    // DAT_0012221c=320, DAT_00122220=480
                velX = (float)center;
                velY = (float)newVelY;
                if (st == 2) {                      // mirror X
                    posSign = (Vec3){ -1, 1, 1 };
                    velSign = (Vec3){ -1, 1, 1 };
                }
                break;
            }
        }

        // Apply sign vectors to spawn pos/vel
        float spawnX = (float)(long)baseDeg * posSign.x;
        float spawnY = (float)(long)lo      * posSign.y;
        float spawnZ = (float)(long)i      * 32.0f;             // DAT_00122580 = 32 (Z stagger)
        velX *= posSign.x; velY *= posSign.y;

        Vec3 scaleVec; scaleVec = *(Vec3*)(GOT + DAT_00122598);  // bomb scale
        scaleVec *= ...;                                          // velSign

        // ---- 5. SP vs MP branches ----
        if (!IsMultiplayer()) {
            // -------- Single-player path --------
            Bomb* b = (Bomb*)ActorManager::GetInstance()->Add(1, true);
            b->base.pos = Vec3(spawnX, spawnY, spawnZ);
            b->base.vel = Vec3(velX, velY, 0.0f);                 // DAT_00122584 = 0
            b->Init(0, 0, &scaleVec);                             // vtable[+8]
            b->base.pos.y += -100.0f * b->base.scale.y;           // DAT_00122588 = -100
            float chuckDelay = (zOffset >= 0) ? zOffset + 0.21f   // DAT_0012258c
                                              : 0.21f;
            Bomb::Chuck(b, chuckDelay);
            if (game->m_GameMode == 2) Bomb::SetForPlayer(b, 1);  // arcade single-player
        }
        else {
            // -------- Same-screen multiplayer: mirrored pair --------
            float mpFlipScalar = *(float*)(GOT + DAT_00122594);   // multiplayer flip (sin?)
            // Apply rotation: spawn rotated 90° in screen space
            float newY = spawnX * mpFlipScalar;
            spawnY = (float)(long)((float)spawnY * mpFlipScalar - 120.0f); // DAT_0012257c = 120
            spawnX = -newY;
            // Apply same to velocities
            float newVelY = velX * mpFlipScalar;
            velY = velY * mpFlipScalar;
            velX = -newVelY;

            // First (player-1) bomb (pos as-computed), only if `(int)spawner < 2`
            // (the binary uses `spawner` parameter as a side-selector flag in MP)
            if ((int)spawner < 2) {
                Bomb* b1 = (Bomb*)ActorManager::GetInstance()->Add(1, true);
                b1->base.pos = Vec3(spawnX, spawnY, spawnZ);
                b1->base.vel = Vec3(velY, velX, 0.0f);
                b1->Init(0, 0, &scaleVec);
                b1->base.pos.x += 100.0f * b1->base.scale.y;       // DAT_00122588
                b1->accelForce_x = b1->accelForce_y * mpFlipScalar; // y-flip already done
                b1->accelForce_y = 0.0f;
                Bomb::SetForPlayer(b1, 1);
                Bomb::Chuck(b1, (zOffset >= 0) ? zOffset + 0.21f : 0.21f);
            }
            // Second (player-2) bomb: mirrored coordinates
            if ((int)spawner == 0 || (int)spawner == 2) {
                Bomb* b2 = (Bomb*)ActorManager::GetInstance()->Add(1, true);
                b2->base.pos = Vec3(-spawnX, -spawnY, spawnZ);
                b2->base.vel = Vec3(-velY, -velX, 0.0f);
                b2->Init(0, 0, &scaleVec);
                b2->base.pos.x += 100.0f * b2->base.scale.y;       // DAT_00122590
                b2->accelForce_x = -b2->accelForce_y * mpFlipScalar;
                b2->accelForce_y = 0.0f;
                Bomb::SetForPlayer(b2, 2);
                Bomb::Chuck(b2, (zOffset >= 0) ? zOffset + 0.21f : 0.21f);
            }
            // Big-bomb upgrade: when type == 0 and count > 1
            if (type == 0 && b2 && (int)spawner > 0)
                Bomb::MakeFat(b2, false);
        }
    }
}
```

### Constants resolved (SpawnBomb)

| DAT addr     | Hex bytes      | Value     | Meaning |
|--------------|----------------|-----------|---------|
| `0x00122208` | `00 00 16 C3` | -150.0f   | min angle scale (negate component) |
| `0x0012220c` | `00 00 16 43` | +150.0f   | max angle scale |
| `0x00122210` | `00 00 96 C3` | -300.0f   | base→spread divisor |
| `0x00122214` | `00 00 00 00` | 0.0f      | default zOffset when type==0 |
| `0x00122218` | `9A 99 89 3F` | 1.075f    | Y-velocity multiplier |
| `0x0012221c` | `00 00 A0 43` | 320.0f    | screen height (used in side-spawn) |
| `0x00122220` | `00 00 F0 43` | 480.0f    | screen width |
| `0x00122224` | `66 66 26 BF` | -0.65f    | side-bomb gravity multiplier |
| `0x0012257c` | `00 00 F0 42` | 120.0f    | MP Y offset (half of player 1 zone) |
| `0x00122580` | `00 00 00 42` | 32.0f     | Z stagger per bomb in chain |
| `0x00122584` | `00 00 00 00` | 0.0f      | default vel.z |
| `0x00122588` | `00 00 C8 C2` | -100.0f   | SP/MP-P1 Y-offset relative to bomb scale |
| `0x0012258c` | `3D 0A 57 3E` | 0.21f     | default Chuck delay when zOffset < 0 |
| `0x00122590` | `00 00 C8 42` | +100.0f   | MP-P2 X-offset |

### Key observations

- The `type` parameter is **dual-typed**: when 0 it means "no spawner template,
  use defaults"; when non-zero it's a `SPAWNER_INFO*`. This matches the
  existing `SpawnFruit` convention.
- The `spawner` parameter (4th arg) is **also overloaded** in MP: in single
  player it's the same SPAWNER_INFO\*; in MP the binary treats it as an
  integer flag (`< 2`, `== 0`, `== 2`) that selects which player(s) get the
  bomb. The chain-bomb path in `Bomb::Update` calls `SpawnBomb(count, 0, 0, p)`.
- Bomb `accelForce_y` is always set to `0.0` (`DAT_00122584`).
- `Bomb::SetForPlayer(b, n)` is called *after* Chuck in MP — order matters
  because SetForPlayer flips physics for player 2.

---

## 3. `WaveManager::SetCurrentWave(int waveNo, float delay, int playerIdx)` @ `0x00125340`

Used by `Resume` to restore a wave after pause/resume:

```c
void WaveManager::SetCurrentWave(int waveNo, float delay, int playerIdx) {
    ClearUnspawned();
    // Write `waveNo - 1` so the next GetNextWave will land on `waveNo`
    *((int*)&this->m_pCurrentWave_P1)[playerIdx] = waveNo - 1;
                                          // ^ +0x230 + playerIdx*4
                                          // (overloaded with pointer field; see note)
    GetNextWave(0);

    // Add `delay` to the per-player wave delay accumulator (clamp >= 0)
    float* delayField = (float*)(&this->field_0x234 + playerIdx * 4);  // +0x234, +0x238
    *delayField = max(0.0f, *delayField + delay);                       // DAT_0012538c = 0.0
}
```

### Note on `m_pCurrentWave_P1` overload

The struct field at `+0x230` is labelled `m_pCurrentWave_P1` (a pointer) in
Ghidra, but `SetCurrentWave` writes an `int` (`waveNo-1`) here for player 0.
This appears to be a binary bug or compiler-driven aliasing — the field is
re-purposed as `m_WaveCount[0]` during single-player. `GetCriticalChance` /
`GetWavedt` read offset `+0x22c + playerIdx*4` (so they treat 0x230 as the
P1 wave-pointer). For port fidelity: write to `+0x230 + playerIdx*4` as
`waveNo-1` regardless of the type confusion.

---

## 4. `WaveManager::IsWaveProcessing(int playerIdx)` @ `0x00122a40`

Returns true while the current wave still has pending spawns OR active fruits/bombs.

```c
bool WaveManager::IsWaveProcessing(int playerIdx) {
    bool flag = (&this->field_0x23c)[playerIdx];   // per-player "wave-spawned" flag
    if (!flag)
        return flag;                                // (= false)

    if (playerIdx == 0) {
        // -------- Player 0 (or single-player) --------
        WAVE_INFO* w = m_pCurrentWave[0];
        if (w) {
            // m_bAllowBombsFrenzy at +0x39 -- if FALSE, treat wave as done
            // (frenzy waves never block)
            if (w->m_bAllowBombsFrenzy == 0)
                goto done;

            // m_bAllowBombs at +0x38 -- if FALSE, only check fruit count globally
            if (w->m_bAllowBombs == 0) {
                if (Fruit::GetNumActiveForPlayer(-1, false) >= 1)
                    return true;
                if (Bomb::GetNumActiveForPlayer(-1, true) >= 1)
                    return true;
                goto done;
            }
        }
        // Default check: any fruit *spawning* in the actor pool
        if (ActorManager::GetInstance()->GetNumEntities(0) != 0)
            return true;
        // MP edge case: any bomb-for-any-player still active
        if (IsMultiplayer() && Bomb::GetNumActiveForPlayer(-1, true) >= 1)
            return true;
        if (!IsMultiplayer() &&
            ActorManager::GetInstance()->GetNumEntities(1) != 0)
            return true;
done:
        (&this->field_0x23c)[0] = 0;
        return false;
    }
    else {
        // -------- Player 1 (split-screen MP) --------
        if (Fruit::GetNumActiveForPlayer(playerIdx, true) >= 1)
            return true;
        if (Bomb::GetNumActiveForPlayer(playerIdx, true) >= 1)
            return true;
        (&this->field_0x23c)[playerIdx] = 0;
        return false;
    }
}
```

### Key observations

- **Not** a simple `remainingCount > 0` check — it's based on the per-player
  flag `field_0x23c[p]` AND on whether actor pools still have active entities.
- The `m_bAllowBombsFrenzy` (+0x39) and `m_bAllowBombs` (+0x38) on the current
  WAVE_INFO control whether the cheap "any fruit/bomb in actor pool" path is
  taken, vs the per-player active-count path.
- The flag is *cleared* by IsWaveProcessing itself when the answer is false —
  this is what triggers the wave-end transition in `UpdateWave`.

---

## 5. `WaveManager::ClearUnspawned()` @ `0x00122ad8`

Trivial:

```c
void WaveManager::ClearUnspawned() {
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
}
```

These are static methods on Fruit and Bomb that walk the actor pool and
disable any entity in pre-spawn (chuck-delayed) state. They are NOT
WaveManager spawner-counter resets — those happen in `GetNextWave` via
`SPAWNER_INFO::Reset` per-spawner.

---

## 6. Accessors

### `WaveManager::GetSpeed(int playerIdx)` @ `0x00121834`

```c
float WaveManager::GetSpeed(int playerIdx) {
    return (&this->m_Speed_P0)[playerIdx];   // +0x54 + playerIdx*4
}
```

### `WaveManager::GetWavedt(int playerIdx)` @ `0x001218dc`

Returns the per-frame wave dt, clamped to `[0.0, 100.0]`:

```c
float WaveManager::GetWavedt(int playerIdx) {
    WAVE_INFO* w = m_pCurrentWave[playerIdx];
    float waveDt = (w == nullptr)
        ? 1.0f
        : w->m_BombScale1               // +0x10 (semantic: wave_dt base)
        + w->wave_dt_inc                // +0x14
            * w->field_0x34             // +0x34 (wave-revisit counter)
        + w->delaySpeedScale            // +0x18
            * m_Speed[playerIdx];

    // dtMod only applies to player 0; player 1 uses 1.0
    float dtMod = (playerIdx == 0)
                ? this->field_0x74 * this->field_0x78  // global speed * powerup dtMod
                : 1.0f;

    float result = waveDt * dtMod;
    if (result <= 0.0f) return 0.0f;             // DAT_00121974
    if (result < 100.0f) return result;          // DAT_00121978
    return 100.0f;                                // clamped max
}
```

### `WaveManager::AddSpeed(float amount, int playerIdx)` @ `0x00123510`

Larger function — applies a speed boost, plays the "speed-up" SFX,
updates the in-save total. The hot-path arithmetic is:

```c
void WaveManager::AddSpeed(float amount, int playerIdx) {
    // IMPORTANT: AddSpeed touches the slot at `+0x58 + playerIdx*4`
    //   (NOT `+0x54 + playerIdx*4`). Disassembly:
    //     add.w r3, r1, #0x16    ; r3 = playerIdx + 22
    //     add.w r3, r0, r3, lsl #2 ; r3 = this + (playerIdx+22)*4
    //                              ;    = this + 0x58 + playerIdx*4
    //
    // GetSpeed reads `m_Speed[2]` at +0x54..+0x5b. AddSpeed writes
    // `m_BoostedSpeed[2]` at +0x58..+0x5f. The two arrays OVERLAP at
    // +0x58 (= m_Speed[1] = m_BoostedSpeed[0]). The binary uses this
    // overlap deliberately: in single-player only m_Speed[0] (+0x54) is
    // ever read for player 0's wave dt, and m_BoostedSpeed[0] (+0x58)
    // doubles as player-1's m_Speed (unused in SP). This matches what the
    // game does on AddSpeed: it boosts the slot that GetSpeed will later
    // read for the OPPOSITE player — but in SP that's a no-op.
    //
    // For port fidelity: replicate the binary's write target verbatim.
    float* slot = &(&this->field_0x58)[playerIdx];      // +0x58 + playerIdx*4
    float v = amount + *slot;
    if (v <= 0.0f)        v = 0.0f;
    else if (v >= 14.0f)  v = 14.0f;                    // hard cap = 14.0f
    *slot = v;

    if (amount <= 0.0f) return;                          // negative = no-op

    // Reset combo timers (+0x4c, +0x60 base, stride 4 per-player)
    *(float*)(&this->field_0x4c + playerIdx * 4) = 1.0f;
    if (*(float*)(&this->field_0x60 + playerIdx * 4) <= 0.0f) {
        if (*slot > 2.9f) {                              // DAT_00123828 = 2.9
            // First combo step: clear save-total for "speed_kill" tag,
            // add 1 to it, +5 score, screen effect "kombo1", play SFX.
            ulong hash_speedkill = StringHash("speed_kill");
            FruitSaveData* save = g_Game->save;
            save->ClearTotal(hash_speedkill);
            int nTotal = save->AddToTotal("speed_kill", hash_speedkill, 1, false, false);
            this->field_0x5c = nTotal;
            *(float*)(&this->field_0x60 + playerIdx*4) = 2.5f;   // 0x40200000 = 2.5
            AddToCurrentScore(5, playerIdx, false, false);
            PowerUpManager::ActivateScreenEffect("kombo1");
            GameSound::SFXPlay(g_Game->sound, "sfx_combo_1", 0.0f, 1.0f, ...);
        }
    }
    else {
        // Combo timer was already running — decrement; if expires, escalate
        float t = *(float*)(&this->field_0x60 + playerIdx*4) - amount;
        *(float*)(&this->field_0x60 + playerIdx*4) = t;
        if (t <= 0.0f) {
            int n = save->AddToTotal("speed_kill", hash, 1, false, false);
            this->field_0x5c = n;
            byte clamped = (n < 6) ? (byte)n : 6;
            char effectName[16]; snprintf(effectName, 16, "kombo%d", clamped);
            PowerUpManager::ActivateScreenEffect(StringHash(effectName));
            const char* sfxList[] = { /* DAT_00123840 array */ };
            int sfxIdx = (clamped > 1) ? clamped - 1 : 0;
            GameSound::SFXPlay(g_Game->sound, sfxList[sfxIdx], 0.0f, 1.0f, ...);
            int comboLevel = min(this->field_0x5c, 6);
            AddToCurrentScore(comboLevel * 5, playerIdx, false, false);
            *(float*)(&this->field_0x60 + playerIdx*4) = 2.5f;
        }
    }

    // Track high-water-mark in save (best speed-combo this game)
    int prevBest = save->GetTotal(hash_max_speed);
    int delta = this->field_0x5c - prevBest;
    if (delta > 0)
        save->AddToTotal("max_speed_kill", hash_max_speed, delta, false, false);
}
```

### Constants (AddSpeed)

| DAT addr     | Value      | Meaning |
|--------------|------------|---------|
| `0x00123824` | `0.0f`     | speed-clamp floor |
| `0x00123828` | `2.9f`     | combo trigger threshold |
| (literal)    | `14.0f`    | speed cap (hard-coded as `vmov.f32 s14, 0x41600000`) |
| (literal)    | `2.5f`     | combo timer reset value (`0x40200000`) |

---

## 7. Game-mode → XML file mapping

`WaveManager::Init` indexes a 4-pointer table at **`0x001f3d34`** (named
`gameModeWaveListXMLs` in the binary's symbol table at `0x00066cda`):

| Mode | String addr  | Path |
|------|--------------|------|
| 0    | `0x001ba9a0` | `xml/originalWaveList.xml`  (Classic) |
| 1    | `0x001ba9b9` | `xml/comboWaveList.xml`     (combo / **NOT** survival) |
| 2    | `0x001ba9cf` | `xml/arcadeWaveList.xml`    (Arcade) |
| 3    | `0x001ba9e6` | `xml/zenWaveList.xml`       (Zen) |

The other XML files in `data/xml/` (`survivalwavelist.xml`,
`intensewavelist.xml`, `wavelist.xml`, `zenvswavelist.xml`) are referenced
elsewhere — likely loaded conditionally for multiplayer or for a build flag.
Search `data/xml/intensewavelist.xml` etc. via `get_xrefs_to` if their loaders
are needed (out of scope here).

### Mode → screen mapping (verified)

The `m_GameMode` byte (Game struct +0x4) is read by Init via
`pWVar16->field_0x114 * 0x30` indexing into the per-mode `vector<WAVE_INFO*>`
banks (one per player). `Game::m_GameMode` is set by `GameModeScreen` based on
which gamepad button is pressed. The 4 modes in port code should map to:

```
0 → MODE_CLASSIC   (originalWaveList.xml)
1 → MODE_COMBO     (comboWaveList.xml)    [a.k.a. survival in some docs]
2 → MODE_ARCADE    (arcadeWaveList.xml)
3 → MODE_ZEN       (zenWaveList.xml)
```

This contradicts an earlier guess that mode 1 = "Arcade". **Mode 2 = Arcade**
is correct (Reset() at item 1 special-cases mode 2 to load PowerUp textures —
Arcade is the only mode with power-ups).

---

## 8. WAVE_STEP

`Update`'s accumulator runs at fixed timestep:

```c
for (dtMod = dt + this->field_0x2d4; DAT_00125bd4 < dtMod;
     dtMod = dtMod - DAT_00125bd4) {
    if (waveInfos.size() != 0)
        UpdateWave(DAT_00125bd4, this, 0);
}
this->field_0x2d4 = dtMod;
```

- **DAT addr**: `0x00125bd4`
- **Bytes (LE)**: `89 88 88 3C`
- **Float value**: `0x3c888889` = **0.016666...** = **1/60**

So `WAVE_STEP = 1.0f / 60.0f`. Use exactly that constant in the port to keep
spawn cadence frame-rate-independent.

---

## 9. Who calls `WaveManager::Reset(true)`?

Tracing from `MainScreen::Update` @ `0x0014b278`, the **case 2** branch:

```c
case 2:                                      // STATE_GAME_START
    if (transitionTimer > DAT_0014bb70) {   // ≈ 0.999
        game = g_Game;
        game->field_0x28 = game->field_0x20;  // copy savedScore→displayScore
        WaveManager::GetInstance()->Reset(true);
        game->field_0x5 = 1;                  // "in-game" flag
    }
    iVar11 = g_Game;
    fVar14 = game->field_0xc * 0.75f;        // damp the screen-y vel
    game->field_0xc = fVar14;
    if (fabsf(fVar14) < DAT_0014bb74) {       // ≈ 0.001
        game->field_0x5 = 0;
        game->field_0xc = 0.0f;
        this->m_State = 0x11;                // STATE_HUD_HIDE → 0x11 (POST_RESET wait)
    }
    /* slide screen offscreen with transitionTimer */
    break;
```

So **state 2 in MainScreen IS the binary's `STATE_GAME_START`**, and
`Reset(true)` is called ONCE at the top of state 2 (when `game.transitionTimer
> 0.999`, i.e. nearly fully transitioned in). After Reset, state advances to
0x11 once the screen-Y velocity damps below ~0.001.

### How state 2 is entered

`MainScreen_NewGameCallback` @ `0x0014c384`:

```c
void MainScreen::NewGameCallback() {
    CancelNews();
    this->m_State = 2;                                  // STATE_GAME_START
    GameSound::SFXPlay(..., "sfx_gamestart", ...);
    InitVec3_MissControl(...);                          // reset miss banner
}
```

`NewGameCallback` is wired as the click delegate for the `pPlayButton`
created in MainScreen state 1. So the user flow is:

1. Game starts → MainScreen state 0 (logo)
2. After delay → state 1 (menu shown, pPlayButton created)
3. User taps Play → NewGameCallback → state 2
4. State 2 reaches transitionTimer > 0.999 → **`Reset(true)`**
5. State 2 damps screen velocity → state 0x11

### So the handover doc claim is correct

> "STATE_GAME_START in MainScreen calls Reset(true)"

— is exactly right. `Reset(true)` is NOT called from GameModeScreen,
FrontendTask, or NewGameCallback directly. It's called from
**`MainScreen::Update` case 2** when the transition is mostly complete.

### Implementation note for the port

The current `MainScreen` port should already be in state 2 when this happens
(per `m_State = 2` in NewGameCallback). The port stub for
`WaveManager::Reset(true)` is the function the implementer needs to fill in
per item 1 above. Until then, state 2 is effectively a no-op on game state.

---

## 10. `m_CurrentWave` indexing — per-player array, NOT `vector`

From the struct dump (`get_struct_layout WaveManager`):

```
556 (0x22c) | 4 | pointer | m_pCurrentWave_P0
560 (0x230) | 4 | pointer | m_pCurrentWave_P1
```

So **`m_pCurrentWave[2]`** is a fixed-size array of two `WAVE_INFO*` (one per
player), at offsets `0x22c` and `0x230`. Per-player accesses use
`(&m_pCurrentWave_P0)[playerIdx]`.

`waveInfos` (the *list* of WAVE_INFOs) IS a vector — actually 4 vectors, one
per game mode, at base offset `+0xac` (stride 0xc per mode). MP doubles this
to 2 banks per mode at `+0xac + 0x30 * field_0x114` (where `field_0x114` is
0 in singleplayer and 1 in MP-versus-mode).

### Caveat (from item 3)

Ghidra's struct dump labels +0x230 as `m_pCurrentWave_P1` (pointer), but
`SetCurrentWave(playerIdx=0)` writes an integer (`waveNo - 1`) there. This
is the binary's quirk — likely a same-storage-as-counter overload during
single-player. The implementer should:

- **Treat `m_pCurrentWave[2]` as `WAVE_INFO*[2]` per the doc** for normal
  flow (GetNextWave assigns pointers; GetCriticalChance/GetWavedt read
  pointers).
- **In `SetCurrentWave`**, write `waveNo - 1` to the same memory regardless,
  matching the binary. The downstream `GetNextWave(0)` call inside
  `SetCurrentWave` will overwrite it with a real pointer immediately.

In other words: SetCurrentWave's int-write is a *transient* state that is
clobbered before any read. The port can match by writing
`reinterpret_cast<WAVE_INFO*>((intptr_t)(waveNo - 1))` then calling
GetNextWave(0), or the port can simplify by passing waveNo to GetNextWave
directly. Behavior should be identical because GetNextWave only reads the
old wave-counter via `m_WaveCount[]++` arithmetic that ends up writing a
fresh pointer.

---

## Cross-reference summary (binary addresses)

| Function | Address | Status |
|----------|---------|--------|
| Reset(bool)            | `0x00125be4` | item 1 ✓ |
| SpawnBomb              | `0x00121fa8` | item 2 ✓ |
| SetCurrentWave         | `0x00125340` | item 3 ✓ |
| IsWaveProcessing       | `0x00122a40` | item 4 ✓ |
| ClearUnspawned         | `0x00122ad8` | item 5 ✓ |
| GetSpeed               | `0x00121834` | item 6 ✓ |
| GetWavedt              | `0x001218dc` | item 6 ✓ |
| AddSpeed               | `0x00123510` | item 6 ✓ |
| Init (XML loader)      | `0x0012393c` | item 7 ✓ (modes mapped) |
| Update                 | `0x001259d8` | item 8 ✓ (WAVE_STEP=1/60) |
| MainScreen::Update     | `0x0014b278` | item 9 ✓ (state 2 → Reset(true)) |
| MainScreen::NewGameCb  | `0x0014c384` | item 9 ✓ (sets state 2) |

| Data table | Address | Contents |
|------------|---------|----------|
| `gameModeWaveListXMLs` | `0x001f3d34` | 4 × char* per game-mode XML |
| `WAVE_STEP` (1/60)     | `0x00125bd4` | float `0x3c888889` |

## Gaps / TODOs (out of scope here, but flagged for future RE)

- The exact semantics of `WaveManager.field_0x114` (used as 0 or 1 to pick a
  per-player wave bank) — controlled by `<defaults quesize="...">` in XML.
  Suspect: 0=normal, 1=MP-versus.
- `SPAWNER_INFO::Reset(spawner, field_0x34)` — parameter list and what it
  resets. Used by GetNextWave per-wave. Already partially documented but
  needs confirming the role of `field_0x34` (wave-revisit counter).
- `Bomb::SetForPlayer(b, n)` — not yet RE'd in detail. SpawnBomb calls it
  with `n=1` for player-1 bombs, `n=2` for player-2 bombs.
- Y-spawn position for SpawnBomb — the variable `lo` (= screen Y) reuses
  the angle-spread `lo` integer, which is suspicious. May need a closer
  look at the disassembly for the side-spawn case to be sure.
- The actual `accelForce_y` / `accelForce_x` semantics on Bomb — used by
  SpawnBomb's MP path. The port's Bomb struct may need these fields named.
