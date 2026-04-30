# Splat pool & WaveManager::Resume — RE notes

Two `GameInit` tail steps:
- A6: `SplatEntity::CreatePool(0x80)` @ `0x0017ef34`
- A7: `WaveManager::Resume()` @ `0x00124b1c`

Both share GOT base `0x001ec130`. See also
[`entity-heap.md`](entity-heap.md) (unrelated ActorManager pool) and
[`../systems/wave-system-impl.md`](../systems/wave-system-impl.md).

---

## A6. `SplatEntity::CreatePool(int)` @ `0x0017ef34..0x0017eff4`

### What it does

Plain `operator new[]` array allocation of `capacity` SplatEntity
instances with the standard g++ "non-trivial dtor" array-cookie
prefix. There are **no** "15 drops × 8 bytes" sub-records — the prior
re-analyst note misread the formula. SplatEntity itself is a single
0x78-byte (120-byte) instance per slot.

The decompiler shows size as `(capacity * 0xf + 1) * 8` —
arithmetically identical to `capacity * 0x78 + 8` (capacity * sizeof
plus 8-byte cookie). For 0x80 slots: 0x3C08 bytes.

### Pool layout

`[raw+0] uint32 elem_size=0x78` ; `[raw+4] uint32 count` ; `[raw+8]
SplatEntity[0]` (this is `s_PoolHead`) ; subsequent slots stride 0x78.

### Pool globals (resolved via GOT walk)

| Address      | Name suggested        | Type            | Use |
|--------------|------------------------|-----------------|-----|
| `0x0026891c` | `s_SplatPoolHead`      | `SplatEntity*`  | CreatePool/CleanUp set; iterators read |
| `0x00268920` | `s_SplatPoolMax`       | `int`           | capacity |
| `0x00268924` | `s_SplatNextFreeIdx`   | `int`           | GetFree mod-cursor |

`GetFree` does a circular scan from `s_SplatNextFreeIdx` for a slot
with `m_bAlive == 0` (offset `+0x75`), modulo `s_SplatPoolMax`,
bounded to one full sweep. `DrawActiveSplats` /
`UpdateActiveSplats` iterate `for (i=0; i<max; ++i)`. There is no
free-list.

### Pseudocode

```c
void SplatEntity::CreatePool(int capacity) {
    if (s_SplatPoolHead) {                  // inline CleanUp
        int cnt = ((int*)s_SplatPoolHead)[-1];
        for (int i = cnt-1; i >= 0; --i) (s_SplatPoolHead+i)->~SplatEntity();
        operator delete[]((char*)s_SplatPoolHead - 8);
        s_SplatPoolHead = nullptr;
    }
    char* raw = (char*)operator new[](capacity*0x78 + 8);
    ((uint32_t*)raw)[0] = 0x78;             // g++ array cookie
    ((uint32_t*)raw)[1] = capacity;
    SplatEntity* head = (SplatEntity*)(raw + 8);
    for (int i = 0; i < capacity; ++i) new (head + i) SplatEntity();
    s_SplatPoolHead = head;
    s_SplatPoolMax = capacity;
    s_SplatNextFreeIdx = 0;
}
```

### Port-side status: **no edits required**

`src/entities/SplatEntity.cpp:419` already implements this via
`Mortar::MemoryPool<SplatEntity>`, which gives semantically equivalent
behaviour: contiguous capacity-sized slab, `m_bAlive` flag for
liveness, linear iteration in DrawActive/UpdateActive matching the
binary's `for (i=0; i<max; ++i)`. The original's circular
`s_SplatNextFreeIdx` is just a GetFree micro-optimisation (amortised
search cost); MemoryPool's `Pop()` returns the same set of slots in
the same order under steady-state load.

### Cross-references

- Binary: `0x0017ef34` CreatePool, `0x0017eee0` CleanUp, `0x0017ee54`
  GetFree, `0x00180344` DrawActiveSplats, `0x0017fd68`
  UpdateActiveSplats, `0x0017f2f0` MakeSplat. Liveness flag at
  SplatEntity `+0x75` (`m_bAlive`).
- Port: `src/entities/SplatEntity.cpp:419-549`.

---

## A7. `WaveManager::Resume()` @ `0x00124b1c..0x00124eec`

### What it does

Re-applies a saved game's gameplay state on top of a freshly-init'd
`WaveManager` and `ActorManager`. Steps:

1. Restore score + miss count to `GameTaskState`.
2. Restore per-player base speed (floats from `+0x100`/`+0x108` —
   NOT camera shake; see port bug #1).
3. Restore `game->field_0x1c` (was-game-over flag from `m_bWasGameOver`).
4. Re-roll all `PROBABILITY_OVERIDE` entries (`SelectType()` per).
5. Reset transient: `field_0x2c4 = 0`, `field_0x2c8 = 1`,
   `m_FruitQueue[2][32] = -1` (0x80 bytes at `+0x244`).
6. Re-spawn saved entities from `saveData->m_EntityStates` (list at
   `+0x34`) — loop below.
7. `ActorManager::Update(dt=0)` to settle.
8. If Zen mode (`m_GameMode == 2`),
   `PowerUpManager::LoadTextures()`.
9. Pick branch: SkipToGameOver vs SkipToPause+wave-restore.
10. Copy `m_ShakeIntensity`/`m_ShakeDecay` to fade screen; clear
    `m_EntityStates`.

### Cold-boot path

Cold boot does NOT call Resume — only the "resume from save" path
does. New game uses `WaveManager::NewGame()` → `Reset(true)` →
`GetNextWave(0)` to seed wave 0. Resume relies on `Reset` having been
called earlier in `GameInit`, so `m_pCurrentWave_P0` is already
non-null.

### Entity respawn loop

For each `EntityState es` in `saveData->m_EntityStates`:

```c
int actorType;
if (es.type < g_NumFruitTypes)              // *(int*)(GOT+0x7060)
    actorType = (es.type < 0) ? 4 : 0;        // 4=SlashEntity, 0=Fruit
else
    actorType = 1;                            // 1=Bomb

Entity* e = ActorManager::GetInstance()->Add(actorType, true);
e->vtable->Init(e, 0, es.type, &g_OriginVector);  // GOT+0x77cc
e->pos = es.pos;
e->vel = es.vel;

if (e->field_0x35 == 1) {                  // Bomb
    e->m_RotAxis_z = es.field_0x20;
    e->m_PlayerIdx = es.field_0x24;
    e->m_TimeScale = es.field_0x28;
    if (saveData->m_GameMode == 2)
        Bomb::SetForPlayer((Bomb*)e, 1);
} else if (e->field_0x35 == 0) {           // Fruit
    e->m_Gravity = {es.field_0x20, .24, .28};
}

float launchSpeed = es.field_0x34;
if (launchSpeed > 0.0f) {
    if (e->field_0x35 == 0)      Fruit::Chuck((Fruit*)e, launchSpeed);
    else if (e->field_0x35 == 1) {
        if (es.field_0x2c == 0)  Bomb::Chuck((Bomb*)e, launchSpeed);
        else                     Bomb::SetHit((Bomb*)e, launchSpeed);
    } else if (e->field_0x35 == 4)
        e->vtable[4](es.field_0x34, e);    // SlashEntity activator
}
```

`bVar2 = true` after the loop iff at least one entity was respawned.

### EntityState (saved entity, list at FruitSaveData+0x34)

| Offset | Type    | Meaning |
|--------|---------|---------|
| +0x08  | float×3 | `vel.{x,y,z}` |
| +0x14  | float×3 | `pos.{x,y,z}` |
| +0x20  | float×3 | per-type: Bomb {rotAxisZ, playerIdx, timeScale} ; Fruit gravity.{x,y,z} |
| +0x2c  | char    | Bomb hit flag (Chuck vs SetHit) |
| +0x30  | int     | `type` (drives actorType pick) |
| +0x34  | float   | Launch speed (0 = no Chuck/SetHit/activator) |

### Branch selection

```c
bool gameOver = (sd->m_BombHitTimer > 0.0f && sd->m_GameMode != 2)  // +0x130
                || (sd->m_GameOverScreenState >= 0);                 // +0x114

if (gameOver) {
    SkipToGameOver(sd->m_GameOverScreenState,    // +0x114
                   sd->m_GameOverTimer,          // +0x118
                   sd->m_field134,               // +0x134 nextComboBonus
                   sd->m_BombHitTimer,           // +0x130
                   /*field5=*/-1);
} else if ((respawned || sd->m_WaveStates.size() != 0)
           && sd->m_CurrentMissCount < 3) {
    SkipToPause(true);
    // (wave state restore -- see below)
}
```

So `SkipToGameOver` triggers when the save was captured **after** a
fatal bomb hit (Arcade/Classic) or after the GameOver screen was
already showing. `SkipToPause` triggers mid-game (entities or pending
waves still present, miss count < 3).

### SkipToPause wave-state restore

```c
SkipToPause(true);

// Scalar field copies
this->field_0x2c8       = sd->m_FruitQueueCount;            // +0x7c
memcpy(&this->field_0x244, &sd->m_FruitQueue[0], 0x80);     // +0x80..0xff
this->field_0x23d       = (char)sd->m_blitzSpawnedThisGame; // +0x188
this->field_0x23e       = (char)sd->m_blitzForceSpawnedCounter; // +0x18c
this->field_0x240       = sd->m_blitzSpawnTime;             // +0x190
this->field_0x234       = sd->m_WaveDelay;                  // +0x144
this->field_0x238       = sd->m_WaveWait;                   // +0x148
this->field_0x74        = sd->m_field14c;                   // +0x14c (PROBABILITY_OVERIDE flags)
this->m_pCurrentWave_P1 = sd->m_pCurrentWave_P1;            // +0x140 (POINTER, not count!)
this->field_0x4c        = sd->m_Speed_P0;                   // +0x100
this->field_0x60        = sd->m_Speed_P1;                   // +0x108
this->field_0x23c = 1; this->field_0x35 = 1;
this->field_0x36 = 0; this->field_0x37 = 0;
this->m_Speed_P0 = sd->m_Speed_P1;                          // +0x104
this->m_Speed_P1 = sd->m_Speed_P1;
this->field_0x5c = sd->GetTotal(StringHash("blitz_bonus")); // m_BlitzBonusTotal

ResetWaveChances();

// Restore each saved WaveState back into m_WaveTable[0]:
for (WaveState& ws : sd->m_WaveStates) {
    WAVE_INFO* wi = m_WaveTable[playerIdx /* always 0 */][ws.waveIdx];
    wi->field_0x34 = ws.field_0x10;            // unspawned-mob count
    if (ws.spawn_states.size() != 0) {
        m_pCurrentWave_P0 = wi;                // mark active
        for (each SpawnState ss in ws.spawn_states) {
            SPAWNER_INFO* si = (SPAWNER_INFO*)(wi->spawnerArray + i*100);
            si->time_remaining_a = ss.time;     // +0x54
            si->time_remaining_b = ss.time;     // +0x50
            si->offset = 0;                     // +0x58
            si->user_data = ss.user;            // +0x5c
            si->SelectTypes();
        }
    }
}
```

### Constants resolved

| GOT offset | Address      | Symbol |
|------------|--------------|--------|
| `+0x7990`  | (BSS)        | `g_FruitNinjaApp` (Game**) |
| `+0x77cc`  | `0x001f4334` | `g_OriginVector` (Vector3*, zero) |
| `+0x7060`  | `0x0024d754` | `g_NumFruitTypes` (int*) |
| `+0x7478`  | (BSS, ptr)   | global written from save `+0x74` |
| `+0x78f8`  | `0x0024d764` | `g_FruitNinjaApp` alias (different GOT entry) |
| string     | `0x001ba6ff` | `"blitz_bonus"` (StringHash key for `GetTotal`) |

### Port-side bugs found while RE'ing (do NOT fix here — flag for implementer)

**`src/game/FruitSaveData.h` — three field mis-types:**

1. **+0x100 / +0x104 / +0x108** are `m_Speed_P0` / `m_Speed_P0_alias` /
   `m_Speed_P1` (per-player base speed, floats), NOT
   `m_CameraShakeX/Y/Z`. Verified by both `Resume` (`Resume` writes
   WaveManager `field_0x4c` from `+0x100`, `field_0x60` from `+0x108`,
   `m_Speed_P0` from `+0x104`) and `SaveWaveInfo` (writes the inverse:
   `+0x100 = field_0x4c`, `+0x104 = m_Speed_P1`, `+0x108 = field_0x60`).
   There is no camera-shake snapshot in FruitSaveData.

2. **+0x140** is `WAVE_INFO* m_pCurrentWave_P1`, NOT `int m_WaveCount`.
   Verified by `Resume` (`this->m_pCurrentWave_P1 = sd->+0x140`) and
   `SaveWaveInfo` (`sd->+0x140 = (int)this->m_pCurrentWave_P1`).
   Pointer-sized, but stored as raw word.

3. **+0x14c** is the `PROBABILITY_OVERIDE` flag word (`field_0x74` on
   WaveManager), NOT "globalWaveDt". The XML attr name is
   `globalWaveDt`, but the in-code semantic is the override-flag word.

**`src/game/WaveManager.cpp:405` — `Resume` is currently a TODO stub.**

### Port-side implementation plan

**`src/game/WaveManager.cpp:405`** — replace the `Resume()` stub with
the full body. Audit dependencies before implementing:
`GameTaskState::SetScore`/`SetMissCount`, `ActorManager::Add`/`Update`,
`Bomb::{Chuck,SetHit,SetForPlayer}`, `Fruit::Chuck`,
`SkipToPause(bool)`, `SkipToGameOver(int,float,float,float,int)`,
`PowerUpManager::LoadTextures`, `StringHash`,
`FruitSaveData::GetTotal`.

**`src/game/FruitSaveData.h`** — fix the three field mis-types. Audit
any read sites of `m_CameraShake*` / `m_WaveCount` / `m_field14c`
(likely all are TODO stubs).

**`src/game/WaveStructs.h`** — verify `EntityState` and
`WaveState`/`SpawnState` match the offsets above.

### Cross-references

- Binary: `0x00124b1c` Resume, `0x001247f0` SaveWaveInfo (mirror —
  invaluable for offset disambiguation), `0x00125be4` Reset,
  `0x00121f74` GameOver, `0x00121f90` NewGame.
- Binary follow-ups (not RE'd here): `0x001255b8` SkipToPause,
  `0x00125450` SkipToGameOver.
- Port stubs: `src/game/WaveManager.cpp:405` (Resume),
  `src/game/WaveManager.cpp:409` (SaveWaveInfo).
- Existing doc: [`../systems/wave-system-impl.md`](../systems/wave-system-impl.md) §1, §3, §9 (Reset / SetCurrentWave / Reset(true) callers).

---

## Summary

- A6: Port already implements equivalent semantics via `MemoryPool`. No edits required.
- A7: Port stub needs full implementation per pseudocode; three FruitSaveData field mis-types should be corrected first or in the same pass.
