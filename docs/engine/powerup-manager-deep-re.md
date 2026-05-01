# PowerUpManager Deep RE Pass

Comprehensive ASM-verified audit of the `PowerUpManager` singleton, its
`PowerUp` template/instance type, and the four `GameModifier` subclasses
(`ScoreModifier`, `TimeModifier`, `SlashModifier`, `WaveModifier`) that
drive the per-frame power-up state. Produced 2026-04-30 by `re-analyst`
against `FruitNinja.exe` (ARM32 LE, image base 0x00010000, GOT base
0x001ec130).

Supersedes / extends the partial info in:
- `docs/systems/power-ups.md` (top-level method index)
- `docs/structs/game-managers.md` §PowerUpManager (offsets only)
- Power-up TODOs in `docs/engine/wavemanager-deep-re.md` §5 (PROBABILITY_OVERIDE)

This builds on `wavemanager-deep-re.md` Section 5 — the WaveManager-side
power-up-fruit selection logic. Neither doc duplicates the other.

---

## 0. Class identity

| Item | Value |
|---|---|
| Mangled name | `_ZN14PowerUpManagerE` (typeinfo: `_ZTI14PowerUpManager`) |
| `_ZTV14PowerUpManager` | (no vtable — concrete singleton with no virtuals) |
| Singleton ctor | `PowerUpManager::PowerUpManager()` @ 0x00117d20 |
| Singleton thunk ctor | 0x00117d60 (alias) and 0x00104004 (init thunk) |
| `GetInstance()` | 0x00118134 — Meyers singleton (`__cxa_guard_acquire` + atexit) |
| Dtor | 0x001187fc (regular), 0x00118880 (deleting) |
| `_GLOBAL__I_PowerUpManager.cpp` | 0x00119df4 (cpp-level static init) |
| Struct size | **144 bytes (0x90)** |

PowerUpManager is **not** virtual. It does not have a vtable. It owns
two `std::map<unsigned long, PowerUp*>`, two `std::list<PowerUp*>`, one
`std::map<unsigned long, ScreenEffect>`, and one
`std::list<ScreenEffect>`, plus a handful of scalar state.

---

## 1. PowerUpManager struct layout — full table (size 0x90)

| Offset | Size | Type | Binary semantic | Refs |
|--------|------|------|-----------------|------|
| +0x00  | 24   | `std::map<ulong, PowerUp*>` | `m_AllPowerUps` — every `<powerup>` parsed from XML, indexed by `StringHash(name)` | Load (insert), Reset+ClearTimedPowers (find), ActivatePower (find), GetActiveSingle (find), LoadTextures (iter) |
| +0x18  | 12   | `std::list<PowerUp*>` | `m_ActivePowerUps` — currently-active, owned (each is a `Clone()` of a template) | Update (iter), Reset, ClearTimedPowers, ActivatePower (push_back), Draw |
| +0x20  | 24   | `std::map<ulong, PowerUp*>` | `m_ActiveByHash` — quick lookup of active by name-hash | ActivatePower (find/insert), Update (erase on expiry), Reset, ClearTimedPowers (erase), GetActiveSingle |
| +0x38  | 24   | `std::map<ulong, ScreenEffect>` | `m_ScreenEffectPool` — every `<screeneffect>` parsed from XML, indexed by hash. Templates stored **by value** | Load (insert), ActivateScreenEffect (find), LoadTextures (iter) |
| +0x50  | 12   | `std::list<ScreenEffect>` | `m_ActiveScreenEffects` — instances pushed by `ActivateScreenEffect`; ticked each frame | Update (iter+erase), ClearScreenEffects, ActivateScreenEffect (push_back) |
| +0x5c  | ?    | (last 4 bytes of the list_base, included in the 12-byte std::list) | — | — |
| +0x58  | 12   | `std::list<PowerUp*>` | `m_PurchasablePowers` — subset of templates with `purchaseable=1` (alias list, not owning) | Load (push_back), Reset (iter for purchase-state) |
| +0x60  | 4    | `int`   | `m_field60` — pointer cache to **active special PowerUp** (`PowerUp*`). Set in Update when `m_field88 < currentTimeProgress` and the candidate is non-purchaseable. Used by `Draw`/HUD-position logic | Update writes, ClearTimedPowers/SetDefaults zero, GetActiveProgression reads via list scan |
| +0x64  | 4    | `float` | **`m_DtMod`** — composite time-scale multiplier. Each frame: `SetDefaults()` resets to 1.0, then every active TimeModifier multiplies it via `ApplyDtMod(scale)`. `WaveManager::Update` mirrors this into `WaveManager+0x78` for `GetWavedt()` | TimeModifier::UpdateSpecific writes via ApplyDtMod, WaveManager mirrors |
| +0x68  | 4    | `float` | **`m_field68`** — "stop clock" / time-bonus accumulator. Each frame `SetDefaults` sets to 0.0; each active TimeModifier with `<stop>` flag adds its `m_Duration` (== remaining time) via `StopClock(d)`. **Read by TimeControl as the +N seconds floating-text overlay** (TODO: confirm via TimeControl `+0x68` xref) | TimeModifier::UpdateSpecific writes, TimeControl reads |
| +0x6c  | 4    | `float` | **`m_field6c`** — "slow clock" / partial-time multiplier. Each frame `SetDefaults` resets to 1.0; each active TimeModifier multiplies it via `SlowClock(s)`. **Read by TimeControl** as "decrement countdown by `dt * m_field6c` instead of `dt * 1.0`** (TODO: confirm via TimeControl xref) | TimeModifier::UpdateSpecific writes |
| +0x70  | 4    | `float` | `m_field70` — composite WaveModifier dt-mod. Reset to 1.0 each frame in `SetDefaults`; each active WaveModifier multiplies via `PowerupDtModMultiply(scale)`. **At end of `Update` it is copied into `+0x74`** so it becomes the value used by ALL active modifiers next frame (one-frame delayed propagation) | WaveModifier::UpdateSpecific writes, Update copies |
| +0x74  | 4    | `float` | scratch — the composite WaveModifier dtMod from the **previous** frame, applied as `dt * +0x74` to each non-purchaseable active power's update. Reset to 1.0 in ctor + Reset + SetDefaults. **At end of `Update`: `+0x74 = +0x70`** | Update reads (multiplies into per-power dt) and writes |
| +0x78  | 4    | `int`   | `m_ScoreGainMult` — multiplicative score-gain (start = 1, applied as `points *= m_ScoreGainMult`). ScoreModifier::UpdateSpecific multiplies via `AddToScoreGainMultiply(factor)` once per `m_ApplyCount` slot | ScoreModifier::UpdateSpecific writes, GetScoreGainMultiplier reads, ClearScoreMultipliers resets to 1 |
| +0x7c  | 4    | `int`   | `m_ScoreGainFactor` — additive score-gain (start = 1). ScoreModifier::UpdateSpecific adds `m_ApplyCount * <gainAdd>` via `AddToScoreGainAdd(n)` | ScoreModifier::UpdateSpecific, GetScoreGainMultiplier, ClearScoreMultipliers |
| +0x80  | 4    | `int`   | `m_ScoreLossMult` — multiplicative score-loss (start = 1). ScoreModifier::UpdateSpecific multiplies via `AddToScoreLossMultiply(factor)` | ScoreModifier::UpdateSpecific, GetScoreLossMultiplier, ClearScoreMultipliers |
| +0x84  | 4    | `int`   | `m_ScoreLossFactor` — additive score-loss (start = 1). ScoreModifier::UpdateSpecific adds `m_ApplyCount * <lossAdd>` via `AddToScoreLossAdd(n)` | ScoreModifier::UpdateSpecific, GetScoreLossMultiplier, ClearScoreMultipliers |
| +0x88  | 4    | `float` | `m_field88` — **highest current-time-progress across all non-purchaseable specials**. Used to pick the "primary" special for HUD spotlighting. Recomputed each frame in Update; clamped to >= `0.001f` (DAT_00118b90) for purchaseable powers | Update writes, ClearTimedPowers/SetDefaults reset to 0.0 |
| +0x8c  | 4    | (pad)   | unused | — |

The port's current `PowerUpManager.h` only models +0x64/+0x68/+0x6c/+0x70 +
the two score Mults/Factors. The maps/lists, +0x60, +0x88, the score-loss
slots (+0x80/+0x84), and the screen-effect machinery are missing.

### Constructor — `PowerUpManager::PowerUpManager()` @ 0x00117d20

```cpp
PowerUpManager::PowerUpManager() {
    // Three std::map ctors
    new (&m_AllPowerUps)        std::map<ulong, PowerUp*>();    // +0x00
    new (&m_ActivePowerUps)     std::list<PowerUp*>();          // +0x18
    new (&m_ActiveByHash)       std::map<ulong, PowerUp*>();    // +0x20
    new (&m_ScreenEffectPool)   std::map<ulong, ScreenEffect>();// +0x38
    new (&m_ActiveScreenEffects)std::list<ScreenEffect>();      // +0x50
    new (&m_PurchasablePowers)  std::list<PowerUp*>();          // +0x58
    // Scalars NOT zeroed by std containers — only field_0x70 and field_0x74
    // are written. Everything from +0x60 to +0x88 is left **uninitialised**
    // until the first call to SetDefaults() / Reset(). This is technically
    // a binary bug (compiler-zero of bss covers it), but the port should
    // explicitly zero everything for safety.
    *(uint32_t*)&this->field_0x74 = 0x3f800000;   // 1.0f
    this->m_field70 = 1.0f;
}
```

No score-mults are written by the ctor — they get initialised by
`ClearScoreMultipliers()` (called via `SetDefaults()`) on first frame.

---

## 2. PowerUp struct (template + instance) — size 0xCC

`PowerUp` is BOTH the XML-parsed template (lives in `m_AllPowerUps`) AND
the active-instance object. `ActivatePower` calls `Clone()` to make a
heap-owned copy that goes into `m_ActivePowerUps`. The template is never
modified during gameplay; only the clone gets its modifier list ticked.

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00  | 4    | (pad / list_base sentinel?) | — | Unused header before m_ModList |
| +0x04  | 4    | `std::list_node*` | `m_ModList_prev` | List of GameModifier* — first byte of std::list<GameModifier*> |
| +0x08  | 4    | `std::list_node*` | `m_ModList_next` | — |
| +0x0c  | 4    | `uint32_t` | `m_NameHash` | `StringHash(m_Name)` |
| +0x10  | 64   | `char[64]` | `m_Name` | XML `name` attribute (lowercase) |
| +0x50  | 64   | `char[64]` | `m_DisplayName` | First letter uppercased copy of m_Name |
| +0x90  | 1    | `bool` | `m_bIsPurchasable` | XML `purchasable="true"`, OR set if `<purchase>` child present |
| +0x91  | 1    | `bool` | `m_bIsSpecial` | XML `special="true"` |
| +0x94  | 4    | `PurchaseInfo*` | `m_pPurchaseInfo` | 0xc4-byte struct; non-null only for purchaseable powers |
| +0x98..+0x9c | 4 | `float` | `field_0x9c` | **CurrentTimeProgress** — max of all GameModifier `m_Duration` across active mods; tracks longest-remaining timer |
| +0xa0  | 4    | `float` | `m_TotalTime` | **MaxTotalTime** — max duration the bar showed for; saturates upward |
| +0xa4  | 4    | `Colour` (BGRA8) | `m_Colour` | XML `colour="r,g,b,a"` — bar tint |
| +0xa8  | 4    | `float` | `field_0xa8` | **OnScreenAmt** — bar reveal/hide animation [0..1]. Increments by `dt*4` while modifiers active, decrements by `dt*12` when none, clamps to 1.0 |
| +0xac  | 4    | `SmartPtr<Texture>` | `m_Texture1` | XML `icon="..."` — bar icon texture |
| +0xb0  | 4    | `SmartPtr<Texture>` | `m_Texture2` | XML `popup="..."` — MissControl popup texture (purchase notification) |
| +0xb4  | 4    | `ScreenEffect*` | `m_pScreenEffect` | Owned 0x50-byte ScreenEffect (parsed from `<screeneffect>` child) |
| +0xb8..+0xc0 | 8 | (pad / spawned-flags) | — | — |
| +0xc4  | 4    | `int` | `field_0xc4` | **DeferedPoints** — score points held back until power deactivates. Negative = "no deferred points pending"; serialised to wave-resume XML |
| +0xc8  | 4    | `float` | `field_0xc8` | **HUD X-position** — interpolated each frame in PowerUpManager::Update with a damping factor 0.2 toward target `(specialIdx*110.0 + (numTimed-1)*-55.0)` |

### XML schema (`xml/powerUpList.xml`)

```xml
<powerInfoFile>
  <powerup name="..." colour="r,g,b,a" purchasable="true|false"
           special="true|false" icon="iconTex" popup="popupTex">
    <purchase cost="..."/>          <!-- 0xC4-byte PurchaseInfo, optional -->
    <screeneffect ...>              <!-- inline or referenced ScreenEffect -->
      ...
    </screeneffect>
    <wave>                          <!-- WaveModifier, repeatable -->
      ... see §3.4
    </wave>
    <slash>                         <!-- SlashModifier, repeatable -->
      ... see §3.3
    </slash>
    <time>                          <!-- TimeModifier, repeatable -->
      ... see §3.2
    </time>
    <score>                         <!-- ScoreModifier, repeatable -->
      ... see §3.1
    </score>
  </powerup>
  <screeneffect hash="..." ...>     <!-- standalone screeneffect templates -->
    ...
  </screeneffect>
</powerInfoFile>
```

The standalone `<screeneffect>` blocks at the top level are pre-baked
templates that can be activated by hash via `ActivateScreenEffect(hash)`
without an associated `<powerup>`. WaveManager uses these for blitz-tier
notifications (`blitz_count`, `blitz_1_count`, …, `blitz_6_count` per
`docs/engine/wavemanager-deep-re.md` §5 force-spawn flow).

---

## 3. GameModifier subclasses — types, layouts, behaviour

### 3.0 GameModifier base — size 0x20

vtable @ 0x001e8cc0. Pointer stored as `vtable + 8` so first virtual
slot is at offset 0 of the stored pointer (skipping the 8-byte ABI
header `[offsetToTop, typeinfo*]`).

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00  | 4    | `void*` | vtable | `(real_vt + 8)` |
| +0x04  | 4    | `float` | `m_Duration` | XML `duration="..."` — initial timer |
| +0x08  | 4    | `int`   | (pad) | — |
| +0x0c  | 4    | `float` | `m_Duration_remaining` | Decremented each frame in Update; expiry when <= 0 |
| +0x10  | 1    | `bool`  | `m_bDeferred` | If true and current timer >= TimeControl threshold, fires Activate after delay |
| +0x14  | 4    | `float` | `m_DeferStart` | -1.0f init; deferred-start timer threshold |
| +0x18  | 1    | `bool`  | `m_bApplied` | True after first ApplyModifier; gates double-application |
| +0x1c  | 4    | `PowerUp*` | `m_pOwner` | Backref to parent PowerUp; set during PowerUp::Parse / Clone |

vtable layout (real bytes — stored pointer + 8 = first virtual at offset 0):

| stored offset | real vt offset | slot | Method |
|---------------|-----------------|------|--------|
| +0x00 | +0x08 | [0] | `~Modifier` (regular dtor) |
| +0x04 | +0x0c | [1] | `~Modifier` (deleting dtor) |
| +0x08 | +0x10 | [2] | `ResetSpecific()` — clears per-modifier state |
| +0x0c | +0x14 | [3] | `Update(dt)` — base dispatcher (returns 0/1=expired) |
| +0x10 | +0x18 | [4] | `UpdateSpecific(dt)` — per-frame override |
| +0x14 | +0x1c | [5] | `ApplyModifier(bool isPurchased, float* extra)` — Activate |
| +0x18 | +0x20 | [6] | `RemoveModifier()` — Deactivate (un-apply side effects) |
| +0x1c | +0x24 | [7] | `GetType()` — returns 0=Time, 1=Wave, 2=Score, 3=Slash |
| +0x20 | +0x28 | [8] | `ParseSpecific(TiXmlElement*)` |
| +0x24 | +0x2c | [9] | `Clone()` — heap-alloc new instance, memcpy state |

#### `GameModifier::Update(dt)` @ 0x001179c4 — the per-frame dispatcher

```cpp
int GameModifier::Update(float dt) {
    // Deferred-activation gate: if marked deferred and TimeControl's
    // current time has passed m_DeferStart, fire Apply once.
    if (m_bApplied) {            // actually checks +0x18 ("not yet applied" flag)
        if (m_DeferStart < TimeControl_GetCurrentTime()) return 0;  // wait
        ApplyModifier(false, nullptr);   // virtual call vtable[5]
        m_bApplied = false;              // clear the deferred flag
    }
    // Decrement m_Duration_remaining.
    if (m_Duration_remaining > 0.0f) {
        m_Duration_remaining -= dt;
        if (m_Duration_remaining <= 0.0f) return 1;   // expired
    }
    // Forward to subclass UpdateSpecific.
    return UpdateSpecific(dt);   // virtual call vtable[4]
}
```

### 3.1 ScoreModifier — size 0x3c, GetType()=2

vtable @ 0x001e8d00.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| (+0x00..+0x1f inherited from GameModifier) | | | |
| +0x20 | `int` | `m_GainAdd` | XML `gainAdd="N"` (int) — added to `m_ScoreGainFactor` per `m_ApplyCount` |
| +0x24 | `int` | `m_GainMultiply` | XML `gainMultiply="N"` (default 1) — multiplied into `m_ScoreGainMult`, `m_ApplyCount` times |
| +0x28 | `int` | `m_LossAdd` | XML `lossAdd="N"` |
| +0x2c | `int` | `m_LossMultiply` | XML `lossMultiply="N"` (default 1) |
| +0x30 | `int` | `m_ApplyCount` | Number of times Apply was called (== combo counter for stacking) |
| +0x34 | `bool` | `m_bDeferPoints` | XML `deferPoints="true"` — defers AddToCurrentScore via SlashModifier delegate |
| +0x38 | 4   | (pad) | — |

#### XML

```xml
<score multiplier="2" gainAdd="0" gainMultiply="2" lossAdd="0" lossMultiply="1" deferPoints="false"/>
```

The `multiplier` attribute is **not** parsed — only the four sub-fields
`gainAdd/gainMultiply/lossAdd/lossMultiply/deferPoints` are read. The
ScoreModifier ctor defaults are `gainMultiply=1, lossMultiply=1, others=0`,
which yields neutral behaviour if the XML omits any.

#### `ScoreModifier::ApplyModifier` @ 0x0011cbe8

```cpp
void ScoreModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_bDeferPoints) {
        // Hold back any pending defered points on the owner power.
        m_pOwner->AddDeferedPoints(0);
        // Install a temporary score-transformation delegate that hooks
        // each AddToCurrentScore call. The delegate calls *this* via
        // operator()(int) (vtable[?] of ScoreModifier).
        Delegate1<int,int>::Callee<ScoreModifier> callee(this, &ScoreModifier::operator());
        Delegate1<int,int> tempDelegate(callee);
        SetScoreDelegate(tempDelegate);   // installs into g_GameData+0xc
    }
    ++m_ApplyCount;       // m_ApplyCount += 1
}
```

#### `ScoreModifier::RemoveModifier` @ 0x0011cd44

```cpp
void ScoreModifier::RemoveModifier() {
    if (m_bDeferPoints) {
        // Restore the default Mortar::Delegate1::Global score transform.
        Delegate1<int,int>::Global g(/* default DefaultScoreDelegate */);
        SetScoreDelegate(g);
    }
}
```

#### `ScoreModifier::UpdateSpecific(dt)` @ 0x0011cb70 — per-frame multiply

```cpp
int ScoreModifier::UpdateSpecific(float dt) {
    if (!m_bDeferPoints) {
        PowerUpManager* m = PowerUpManager::GetInstance();
        m->AddToScoreGainAdd(m_ApplyCount * m_GainAdd);   // +0x7c += ...
        m->AddToScoreLossAdd(m_ApplyCount * m_LossAdd);   // +0x84 += ...
        for (int i = 0; i < m_ApplyCount; ++i) {
            m->AddToScoreGainMultiply(m_GainMultiply);    // +0x78 *= ...
            m->AddToScoreLossMultiply(m_LossMultiply);    // +0x80 *= ...
        }
    }
    return 0;
}
```

The `m_ApplyCount` lets multiple concurrent ScoreModifiers from the
same `<powerup>` stack their contribution. Initial value `0` so first
Apply increments to 1.

#### `ScoreModifier::ResetSpecific()` @ 0x0011cb44

```cpp
void ScoreModifier::ResetSpecific() {
    m_LossAdd      = 0;
    m_LossMultiply = 1;
    m_GainAdd      = 0;
    m_GainMultiply = 1;
}
```

### 3.2 TimeModifier — size 0x3c, GetType()=0

vtable @ 0x001e8d80.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| (+0x00..+0x1f inherited) | | | |
| +0x20 | `float` | `m_DtScale` | XML `<scale rate="..." amount="..."/> amount` (default 1.0). Target dtMod when active |
| +0x24 | `float` | `m_TransitionRate` | XML `<scale rate="..."/>`; dt-per-second to ramp `m_CurrentDtMod` toward `m_DtScale` |
| +0x28 | `float` | `m_CurrentDtMod` | Current applied dt-mod, lerped from 1.0 toward `m_DtScale` over `m_TransitionRate`. Reset to 0.0 in ResetSpecific (default 1.0 in ParseSpecific) |
| +0x2c | `bool`  | `m_bStopClock` | XML `stop="true"` — pause clock by adding `m_Duration_remaining` to PowerUpManager.m_field68 |
| +0x30 | `float` | `m_TimeSlow` | XML `slow="..."` (default 1.0). Multiplied into PowerUpManager.m_field6c each frame (slow-clock multiplier) |
| +0x34 | `float` | `m_AddTime` | XML `addTime="..."` (default 0). After `m_AddTimeDelay` frames, adds this to TimeControl |
| +0x38 | `int`   | `m_AddTimeDelay` | Frames to delay AddTime; 1 if `m_AddTime != 0`. Decrements each frame to 0 then fires |

#### XML

```xml
<time duration="5.0" stop="false" slow="0.5" addTime="0">
  <scale rate="0.4" amount="0.5"/>
</time>
```

#### `TimeModifier::UpdateSpecific(dt)` @ 0x0011ffbc

```cpp
int TimeModifier::UpdateSpecific(float dt) {
    // (1) Frame-delayed AddTime — fires once when counter hits 0.
    if (m_AddTimeDelay > 0) {
        if (--m_AddTimeDelay == 0) {
            TimeControl::AddTime(m_AddTime);
            return 1;   // expire after firing AddTime
        }
    }
    // (2) Stop-clock contribution.
    if (m_bStopClock)
        PowerUpManager::GetInstance()->StopClock(m_Duration_remaining);
    // (3) Slow-clock contribution.
    if (m_TimeSlow != 1.0f)
        PowerUpManager::GetInstance()->SlowClock(m_TimeSlow);
    // (4) Lerp m_CurrentDtMod toward m_DtScale at m_TransitionRate.
    if (m_TransitionRate <= 0.0f) {
        m_CurrentDtMod = m_DtScale;
    } else {
        // First half of duration: lerp toward m_DtScale via fraction
        // of (currentTime / TransitionRate).
        if (m_TransitionRate >= m_Duration_remaining || m_Duration <= 0.0f) {
            // ramping at fixed rate (last branch)
            float target = m_DtScale;
            if (target >= m_CurrentDtMod) {
                m_CurrentDtMod += dt / m_TransitionRate;
                if (m_CurrentDtMod > target) m_CurrentDtMod = target;
            } else {
                m_CurrentDtMod -= dt / m_TransitionRate;
                if (m_CurrentDtMod < target) m_CurrentDtMod = target;
            }
        } else {
            // first-half: t = m_Duration_remaining/m_TransitionRate
            float t = m_Duration_remaining / m_TransitionRate;
            float v = (m_DtScale - 1.0f) * t + 1.0f;
            // clamp toward m_CurrentDtMod at the boundary
            ...
            m_CurrentDtMod = v;
        }
    }
    // (5) Apply this frame's value.
    PowerUpManager::GetInstance()->ApplyDtMod(m_CurrentDtMod);
    return 0;
}
```

(Branch ordering preserved from binary; details around the lerp may need
finer tuning but the high-level behaviour — "smoothly ramp to scale,
then maintain, then on expiry let composite multiply back to 1.0 via
SetDefaults" — is correct.)

### 3.3 SlashModifier — size 0x40, GetType()=3

vtable @ 0x001e8d40.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| (+0x00..+0x1f inherited) | | | |
| +0x20 | `Colour*` | `m_pColours` | Heap array of `Colour` parsed from `<bladecolour>` children. NOT inherited from GameModifier — owned by this modifier |
| +0x24 | `int`   | `m_NumColours` | Element count |
| +0x28 | `int`   | `m_ColourType` | One of `NONE=0, LERP=1, PER_SLASH=2, CONTINUOUS=3` parsed from `colour=` attr via `ParseSlashModColourType` |
| +0x2c | `float` | `m_Width` | XML `width="..."` (default 1.0) — blade width multiplier |
| +0x30 | `char*` | `m_BladeName` | XML `blade="..."` — blade-tex base name (cloned via CloneString) |
| +0x34 | `char*` | `m_Effect`   | XML `effect="..."` formatted via `snprintf("%s", attr, 4)` (max 4 char) — particle effect name |
| +0x38 | `uint32_t` | `m_PowerMask` | Bitmask of `PUSH_FRUIT=1, PULL_FRUIT=2, PUSH_BOMB=4, PULL_BOMB=8, BOMB_HIT=16, FRUIT_BOUNCE=32`. Built by OR-ing each `<power name="..."/>` child via `ParseSlashPowerMask` |
| +0x3c | `bool`  | `m_bApplied` | True after first ApplyModifier (overrides base `m_bApplied`?) |

#### Power-name → bit table (from binary @ 0x001bcf70)

| Bit | Name | Meaning |
|-----|------|---------|
| 0x01 | `PUSH_FRUIT`  | Slash that crosses a fruit accelerates it AWAY |
| 0x02 | `PULL_FRUIT`  | Slash near a fruit pulls it TOWARD the slash midpoint |
| 0x04 | `PUSH_BOMB`   | Slash that crosses a bomb pushes it away (Bomb-Defuse mechanic) |
| 0x08 | `PULL_BOMB`   | Slash pulls bombs toward the blade |
| 0x10 | `BOMB_HIT`    | Slash kills bombs without exploding (Bomb-Defuse "safe slice") |
| 0x20 | `FRUIT_BOUNCE`| Fruits bounce on slash collision |

#### Colour-type table (from binary @ 0x001bcf90)

| Value | Name | Behaviour |
|-------|------|-----------|
| 0 | `NONE`        | No colour modification |
| 1 | `LERP`        | Lerp blade colour over slash duration |
| 2 | `PER_SLASH`   | New random colour per slash |
| 3 | `CONTINUOUS`  | Cycle through colour array continuously |

#### XML

```xml
<slash colour="LERP" width="1.0" blade="rave_blade_glow" effect="...">
  <power name="PUSH_FRUIT"/>
  <power name="PULL_BOMB"/>
  <bladecolour>1.0,0.5,0.2,1.0</bladecolour>
  <bladecolour>0.2,0.5,1.0,1.0</bladecolour>
</slash>
```

#### `SlashModifier::ApplyModifier` @ 0x0011f31c

```cpp
void SlashModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    // Install blade-skin override on SlashEntity.
    if (m_pColours != nullptr && !m_bApplied) {
        m_bApplied = true;
        // Increment a global "active slash mods" counter (global at GOT+0x...).
        ++g_ActiveSlashModCount;
        SlashEntity::SetModColours(
            m_pColours, m_NumColours, m_ColourType,
            m_Width, m_BladeName, m_Effect,
            /*revertable=*/false, nullptr, nullptr);
    }
}
```

#### `SlashModifier::UpdateSpecific(dt)` @ 0x0011f278

```cpp
int SlashModifier::UpdateSpecific(float dt) {
    // OR our PowerMask bits into the global "active slash powers" word.
    *(uint32_t*)(GOT + 0x...) |= m_PowerMask;
    return 0;
}
```

The global is the **same word** that `PowerUpManager::SetDefaults` zeroes
each frame (the per-frame slash power mask read by `Slash::CollisionResponse`
to decide PUSH/PULL/etc. behaviour). See §1's `m_field60` and the
SlashEntity-side `m_PowerMask` field.

### 3.4 WaveModifier — size 0x44, GetType()=1

vtable @ 0x001e8e18.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| (+0x00..+0x1f inherited) | | | |
| +0x20 | 12   | `std::vector<PROBABILITY_OVERIDE>` | Per-modifier blitz/power-up spawn override entries; pushed to per-(mode,player) WaveManager pool on activation |
| +0x2c | `float` | `m_BombMult`     | XML `bombMultiplyer="..."` (default 1.0). Multiplied into `WaveManager.spawnLevel` (+0x68) each frame |
| +0x30 | `float` | `m_BombScale`    | XML `bombScale="..."` (default 1.0). Multiplied into `WaveManager.m_BombScale` (+0x64) |
| +0x34 | `float` | `m_FruitMult`    | XML `fruitMultiplyer="..."` (default 1.0). Multiplied into `WaveManager.m_FruitMult` (+0x6c) |
| +0x38 | `float` | `m_DtMod`        | XML `dtMod="..."` (default 1.0). Multiplied into `PowerUpManager.m_field70` (composite WaveModifier dt-mod) |
| +0x3c | `int`   | `m_OverideProbabilityPool` | XML `overrideProbabilityPool="N"` (default 10000). Random 32-bit pool size for power-up fruit weighting |
| +0x40 | `float` | `m_CritChanceMod` | XML `criticalChance="..."` (default 1.0). Multiplied into `WaveManager.m_CritChanceMult` (+0x70) |

#### XML

```xml
<wave fruitMultiplyer="2.0" bombMultiplyer="0.0" bombScale="1.0"
      criticalChance="1.0" dtMod="0.5" overrideProbabilityPool="10000">
  <override>...</override>      <!-- PROBABILITY_OVERIDE entries; see wavemanager-deep-re §5 -->
  <override>...</override>
</wave>
```

#### `WaveModifier::UpdateSpecific(dt)` @ 0x001280e4

```cpp
int WaveModifier::UpdateSpecific(float dt) {
    WaveManager*    w = WaveManager::GetInstance();
    PowerUpManager* p = PowerUpManager::GetInstance();
    w->FruitMultiplyer(m_FruitMult);    // m_FruitMult *= m_FruitMult
    w->BombMultiplyer(m_BombMult);      // spawnLevel *= m_BombMult
    w->BombScale(m_BombScale);          // m_BombScale *= m_BombScale
    w->CriticalChanceMod(m_CritChanceMod);
    p->PowerupDtModMultiply(m_DtMod);   // m_field70 *= m_DtMod
    return 0;
}
```

WaveModifier's `<override>` children are pushed into the WaveManager's
per-(mode,player) `probOverrides[mode][player]` vector when the
modifier first activates (binary handles this in `WaveModifier::ApplyModifier`
which delegates to `WaveManager::AddProbabilityOverride`). See
`wavemanager-deep-re.md` §5 for the consumer side.

---

## 4. PowerUpManager methods — full pseudocode

### 4.1 `Update(float dt)` @ 0x001189b4 (wrapper @ 0x000f3ccc)

The big one. Called from `WaveManager::Update` once per game-frame.

```cpp
void PowerUpManager::Update(float dt) {
    // (1) Capture the previous-frame's composite WaveModifier dt-mod.
    float prevWaveDtMod = m_field74;        // = last frame's m_field70
    int   specialIdx  = 0;                  // count of seen specials this iter

    // (2) Reset all per-frame composite multipliers (m_DtMod=1, m_field68=0,
    //     m_field6c=1, m_field70=1, m_ScoreGainMult=1, m_ScoreGainFactor=1,
    //     m_ScoreLossMult=1, m_ScoreLossFactor=1, m_field60=0, m_field88=0,
    //     and the global slash-power mask in g_GameData+0x3c).
    SetDefaults();

    // (3) Tick every active PowerUp clone.
    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        // Per-power dt: purchaseable powers don't get the wave-mod scale,
        // others do.
        float perPowerDt = pwr->IsPurchaseable() ? dt : (dt * prevWaveDtMod);
        int   expired   = pwr->Update(perPowerDt);   // virtual: see PowerUp::Update

        if (expired == 0) {
            // Power still alive — bookkeeping for HUD spotlighting.
            float p = pwr->GetCurrentTimeProgress();    // = pwr->field_0x9c
            if (p > m_field88) {
                if (!pwr->IsPurchaseable()) {
                    m_field88 = p;
                    m_field60 = (int)pwr;               // cache for HUD
                } else if (m_field88 < 0.001f /*DAT_00118b90*/) {
                    m_field88 = 0.001f;
                }
            }
            int numTimed = GetNumActiveTimedPowers();
            // Lerp HUD x-position toward target slot (110 px stride per
            // index, centered around player progression).
            float& xpos = pwr->field_0xc8;
            float  target = (specialIdx * 110.0f /*DAT_? */)
                          + ((numTimed - 1) * -55.0f /*DAT_00118b94*/);
            xpos += (target - xpos) * 0.2f /*DAT_00118b98*/;

            if (pwr->IsSpecial()) ++specialIdx;
            ++it;
        } else {
            // Power expired (Update returned 1) — deactivate + free.
            uint32_t hash = pwr->m_NameHash;
            auto byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);    // unwinds modifiers
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        }
    }

    // (4) Tick all active screen-effects.
    auto eit = m_ActiveScreenEffects.begin();
    while (eit != m_ActiveScreenEffects.end()) {
        // ScreenEffect::Update(dt, 0.0, 0.0) — args 3+4 are unused / leftover.
        eit->Update(dt, 0.0f /*DAT_00118b9c*/, 0.0f /*DAT_00118b9c*/);
        if (eit->m_Lifetime <= 0.0f) {       // ScreenEffect+0x50
            eit->Deactivate();
            eit = m_ActiveScreenEffects.erase(eit);
        } else {
            ++eit;
        }
    }

    // (5) Carry composite WaveModifier dt-mod forward one frame.
    m_field74 = m_field70;
}
```

**One-frame latency on `m_field70`/`m_field74`:** the binary stores the
post-SetDefaults value of `m_field70` into `m_field74` at the end of
the frame, then on the next frame uses `prevWaveDtMod = m_field74` as
the per-power dt scale. This means a freshly-activated WaveModifier's
dt-mod doesn't take effect until ONE frame later. The port should
preserve this — the offset latency matters for frame-perfect blitz
trigger timing.

### 4.2 `SetDefaults()` @ 0x00117a80

Called at the top of each `Update`. Resets all per-frame composite state.

```cpp
void PowerUpManager::SetDefaults() {
    m_field88 = 0.0f;
    m_field68 = 0.0f;
    // Clear the **global slash-power mask** at g_GameData + 0x...
    // (the global the SlashModifier::UpdateSpecific OR-writes into).
    *(uint32_t*)(GOT + 0x7740) = 0;       // = g_SlashPowerMaskByCurrentFrame
    m_field60 = 0;
    m_field70 = 1.0f;
    m_DtMod   = 1.0f;
    m_field6c = 1.0f;
    ClearScoreMultipliers();    // m_ScoreGainMult = m_ScoreLossMult = 1
                                // m_ScoreGainFactor = m_ScoreLossFactor = 1
    // Reset the SlashEntity colour/width state.
    SlashEntityState* sx = g_GameData->m_pSlashEntity->m_pState;  // game+0x3c->+...
    sx->m_BladeWidth      = 1.0f;
    sx->m_ColourMod_R     = 1.0f;
    sx->m_ColourMod_G     = 1.0f;
    sx->m_ColourMod_B     = 1.0f;
    sx->m_ColourMod_A     = 1.0f;
    sx->m_TipColourMod    = 1.0f;
}
```

The "g_GameData+0x3c → some struct" reference in `SetDefaults` is the
**SlashEntity's per-frame visual modifier slot** — six floats at
`+0x08/+0x0c/+0x10/+0x14/+0x18/+0x1c` of the slash-state struct.
These are reset to 1.0 each frame and (potentially) overridden by an
active SlashModifier later in the same frame via `SetModColours`.

### 4.3 `ClearScoreMultipliers()` @ 0x0011a218

```cpp
void PowerUpManager::ClearScoreMultipliers() {
    m_ScoreLossMult   = 1;   // +0x80
    m_ScoreGainFactor = 1;   // +0x7c
    m_ScoreLossFactor = 1;   // +0x84
    m_ScoreGainMult   = 1;   // +0x78
}
```

### 4.4 `Reset(bool fullReset)` @ 0x00119b08

```cpp
void PowerUpManager::Reset(bool fullReset) {
    // Reset all composite scalars (same as SetDefaults).
    m_field88 = 0.0f;
    m_field68 = 0.0f;
    *(uint32_t*)(GOT + 0x7740) = 0;     // global slash mask
    m_field60 = 0;
    m_field70 = 1.0f;
    m_field74 = 1.0f;
    m_DtMod   = 1.0f;
    m_field6c = 1.0f;
    ClearScoreMultipliers();
    // Reset the SlashEntity visual state (six 1.0f floats).
    SlashEntityState* sx = g_GameData->m_pSlashEntity->m_pState;
    if (sx) {
        sx->m_BladeWidth = 1.0f; sx->m_ColourMod_R = 1.0f; ...; sx->m_TipColourMod = 1.0f;
    }

    // (1) If fullReset: clear the network/save-state via the game manager (vtable+0x10).
    if (fullReset) {
        (*game->m_pNetMgr->vtable[4])();    // ~ NetworkManager::SyncClear or similar
    }

    // (2) Walk active list, deactivating each.
    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->IsPurchaseable()) {
            // Purchaseable: keep state intact, just call Deactivate(removeAll=true).
            pwr->Deactivate(true);
            if (fullReset) {
                ActivatePurchase(pwr);    // re-apply purchase-active modifier
            } else if (pwr->m_pPurchaseInfo->m_PurchasesRemaining < 1) {
                // skip to deactivate + free path below
                goto deactivate_and_free;
            }
            ++it;
            continue;
        }
        deactivate_and_free:
        // Non-purchaseable: full teardown.
        uint32_t hash = pwr->m_NameHash;
        auto byHash = m_ActiveByHash.find(hash);
        if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
        pwr->Deactivate(false);
        pwr->Release();
        delete pwr;
        it = m_ActivePowerUps.erase(it);
    }

    ClearScreenEffects();

    // (3) Zen mode (game.gameMode == 2): re-activate any "always-on" specials
    //     by walking m_AllPowerUps and calling ActivatePower for each special.
    if (fullReset && g_GameData->gameMode == 2) {
        for (auto& kv : m_AllPowerUps) {
            if (kv.second->m_bIsSpecial) {
                _Vector3<float> spawnPos(*g_DefaultSpawnPos);
                ActivatePower(kv.first, spawnPos, &spawnPos.x);
            }
        }
    }
}
```

`Reset(true)` is the "new game / Continue" path; `Reset(false)` is the
"wave reset" path (drops timed powers but keeps purchaseables).

### 4.5 `ClearTimedPowers()` @ 0x00118904

Called by `Bomb::CollisionResponse` zen-bomb branch.

```cpp
void PowerUpManager::ClearTimedPowers() {
    m_field88 = 0.001f;       // (DAT_001189b0)
    m_field60 = 0;
    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (!pwr->IsPurchaseable() && pwr->IsTimed()) {
            uint32_t hash = pwr->m_NameHash;
            auto byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        } else {
            ++it;
        }
    }
}
```

`PowerUp::IsTimed()` returns true iff the power has a `m_TotalTime > 0`
(i.e. has a duration-based modifier). Purchaseables (Sensei, Peach, etc.)
and instant powers (Banana-Frenzy on collision) survive a zen-bomb hit.

### 4.6 `ActivatePower(hash, position, extraParam)` @ 0x001197c4

```cpp
PowerUp* PowerUpManager::ActivatePower(ulong hash, Vector3 pos, float* extra) {
    auto tmpl = m_AllPowerUps.find(hash);
    if (tmpl == m_AllPowerUps.end()) return nullptr;

    auto active = m_ActiveByHash.find(hash);
    PowerUp* result = nullptr;
    if (active == m_ActiveByHash.end()) {
        // Not yet active — clone the template and push to active list.
        result = tmpl->second->Clone();
        m_ActivePowerUps.push_back(result);

        int  numTimed = GetNumActiveTimedPowers();
        bool isPurchase = (extra && *(int*)extra) || result->IsPurchaseable();
        if (numTimed == 0 || result->m_bIsSpecial || isPurchase) {
            // Either no other timed powers OR this is special-or-purchase
            // → activate immediately.
            result->Activate(/*showPopup=*/true, isPurchase, pos, extra);
        } else {
            // Another timed power is already running. Find the
            // shortest-remaining active **special**; deactivate it; activate
            // the new one.
            float shortest = result->GetLongestMod();
            for (PowerUp* p : m_ActivePowerUps) {
                if (p->IsSpecial()) {
                    float lm = p->GetLongestMod();
                    if (lm < shortest) shortest = lm;
                }
            }
            for (PowerUp* p : m_ActivePowerUps) {
                if (p->IsSpecial() && p != result) {
                    Vector3 defaultPos(*g_DefaultSpawnPos);   // GOT+0x...
                    p->Activate(false, false, defaultPos, &defaultPos.x);
                }
            }
            result->Activate(true, false, pos, extra);
        }
        // Place at HUD slot based on numTimed.
        result->field_0xc8 = (float)numTimed * /*offset*/ 110.0f;
        // If purchaseable, register in m_ActiveByHash.
        if (result->m_bIsPurchasable) m_ActiveByHash[hash] = result;
    } else {
        // Already active — re-Activate to refresh modifier state.
        PowerUp* existing = active->second;
        existing->Activate(false, /*isPurchase=*/(extra && *(int*)extra), pos, extra);
        result = existing;
    }
    return result;
}
```

### 4.7 `ActivateScreenEffect(ulong hash)` @ 0x00119760

```cpp
bool PowerUpManager::ActivateScreenEffect(ulong hash) {
    auto it = m_ScreenEffectPool.find(hash);
    if (it == m_ScreenEffectPool.end()) return false;
    ScreenEffect copy(it->second);   // copy-construct
    copy.Activate();                 // emits particles, adds HUD controls
    m_ActiveScreenEffects.push_back(copy);
    return true;
}
```

This is the API called by WaveManager to fire blitz banners
(`hash == StringHash("blitz_count")` etc — see
`docs/engine/wavemanager-deep-re.md` §5 lines 356/374).

### 4.8 `ClearScreenEffects()` @ 0x00117ed8

```cpp
void PowerUpManager::ClearScreenEffects() {
    for (auto& se : m_ActiveScreenEffects) se.Deactivate();
    m_ActiveScreenEffects.clear();
}
```

### 4.9 `Load()` @ 0x00119cb0

```cpp
void PowerUpManager::Load() {
    TiXmlDocument* doc = new TiXmlDocument("xml/powerUpList.xml");
    m_AllPowerUps.clear();
    m_PurchasablePowers.clear();
    if (!doc->LoadFile(0)) { delete doc; return; }

    TiXmlNode* root = doc->FirstChildElement("powerInfoFile");

    // Parse each <powerup>.
    for (TiXmlNode* pn = root->FirstChildElement("powerup");
         pn; pn = pn->NextSiblingElement("powerup")) {
        PowerUp* pwr = new PowerUp();   // 0xCC bytes
        pwr->Parse(pn);
        m_AllPowerUps[pwr->m_NameHash] = pwr;
        if (pwr->IsPurchaseable()) {
            m_PurchasablePowers.push_back(pwr);
        }
    }

    // Parse each top-level <screeneffect> (templates by hash).
    for (TiXmlNode* sn = root->FirstChildElement("screeneffect");
         sn; sn = sn->NextSiblingElement("screeneffect")) {
        const char* hashAttr = sn->Attribute("hash");
        if (!hashAttr) continue;
        ScreenEffect tmp;                  // 0x50-byte stack-temp
        tmp.Parse(sn);
        ulong h = tmp.m_NameHash;
        m_ScreenEffectPool[h] = tmp;       // copy-assign into map
    }
    delete doc;
}
```

### 4.10 `LoadTextures()` @ 0x0011840c

```cpp
void PowerUpManager::LoadTextures() {
    for (auto& kv : m_AllPowerUps)        kv.second->LoadTextures();
    for (auto& kv : m_ScreenEffectPool)   kv.second.LoadTextures();
}
```

`PowerUp::LoadTextures` calls `ScreenEffect::LoadTextures` and
`PurchaseInfo::LoadTextures` if either is present. ScreenEffect's
`LoadTextures` walks its `vector<EffectImage>` and reloads each
ReloadableTexture. Called from WaveManager's Init when `gameMode == 2`
(Zen) — see `docs/engine/wavemanager-deep-re.md` line 553.

### 4.11 `Draw()` @ 0x00119384

```cpp
void PowerUpManager::Draw() {
    for (PowerUp* pwr : m_ActivePowerUps) pwr->DrawBar();
}
```

`PowerUp::DrawBar` @ 0x001191f8:

```cpp
void PowerUp::DrawBar() {
    if (m_OnScreenAmt <= 0.0f) return;            // animating in/out
    if (!m_Texture1) return;
    PowerUpManager::GetInstance();                // (no-op call?)
    float xpos = field_0xc8;                      // smoothed HUD x-pos
    float bounce = (1.0f - m_OnScreenAmt);        // squashed-in animation
    bounce = bounce * bounce - 0.5f;
    int w = m_Texture1->Width();
    int h = m_Texture1->Height();

    Matrix44 m = identity;
    m[3][0] = xpos + w * 0.0f;                    // x = lerped slot
    m[3][1] = 160.0f                              // top-of-screen Y (DAT_00119374)
            + h * (bounce * bounce - 0.5f) + 1.0f;
    m[3][2] = 0.0f;
    MatrixManager::SetCurrentMatrix(&m);
    MatrixManager::UploadCurrentMatrices(true);
    Texture::Set(m_Texture1);
    DrawQuadUnCached(m_Colour, 0, 1, 0, 1, nullptr);   // 1x1 at world matrix
    Texture::UnSet(m_Texture1);
}
```

So PowerUpManager::Draw renders the row of active-power icons across
the **top of the screen** (Y = 160 in centered-ortho coords; X = +160
on Bada portrait → top edge in landscape display). Each power's
animated `m_OnScreenAmt` controls the slide-in/slide-out effect.

### 4.12 `ApplyDtMod(float)` @ 0x001204dc

```cpp
void PowerUpManager::ApplyDtMod(float scale) {
    m_DtMod *= scale;     // composite into +0x64
}
```

Called by `TimeModifier::UpdateSpecific` once per active TimeModifier per frame.

### 4.13 `SlowClock(float)` @ 0x001204cc

```cpp
void PowerUpManager::SlowClock(float scale) {
    m_field6c *= scale;   // composite into +0x6c
}
```

Called by `TimeModifier::UpdateSpecific` when `m_TimeSlow != 1.0f`.

### 4.14 `StopClock(float)` @ 0x00117a70

```cpp
void PowerUpManager::StopClock(float duration) {
    m_field68 += duration;   // accumulate "freeze time" remaining
}
```

Called by `TimeModifier::UpdateSpecific` when `m_bStopClock == true`,
passing the modifier's `m_Duration_remaining` as the contribution.

### 4.15 `PowerupDtModMultiply(float)` @ 0x001286ec

```cpp
void PowerUpManager::PowerupDtModMultiply(float scale) {
    m_field70 *= scale;   // composite into +0x70 (carried to +0x74 next frame)
}
```

Called by `WaveModifier::UpdateSpecific`.

### 4.16 `AddToScoreGainAdd / GainMultiply / LossAdd / LossMultiply` @ 0x0011d10c..0x0011d128

```cpp
void AddToScoreGainAdd(int n)      { m_ScoreGainFactor   += n; }    // +0x7c
void AddToScoreLossAdd(int n)      { m_ScoreLossFactor   += n; }    // +0x84
void AddToScoreGainMultiply(int n) { m_ScoreGainMult     *= n; }    // +0x78
void AddToScoreLossMultiply(int n) { m_ScoreLossMult     *= n; }    // +0x80
```

Called by `ScoreModifier::UpdateSpecific`.

### 4.17 `GetScoreGainMultiplier() const` @ 0x0010ad34

```cpp
int PowerUpManager::GetScoreGainMultiplier() const {
    return m_ScoreGainMult * m_ScoreGainFactor;
}
```

Read by `DefaultScoreDelegate` (positive points) and `ScoreControl::PreDraw`
Section B (Arcade `x%d` overlay if `> 1`).

### 4.18 `GetScoreLossMultiplier() const` @ 0x0010ad40

```cpp
int PowerUpManager::GetScoreLossMultiplier() const {
    return m_ScoreLossMult * m_ScoreLossFactor;
}
```

Read by `DefaultScoreDelegate` for negative-point branch.

### 4.19 `GetActiveProgression(float t)` @ 0x00117b38

Called by WaveManager's PROBABILITY_OVERIDE branch (see
`wavemanager-deep-re.md` §5 line 960) to clamp blitz-fruit eligibility.

```cpp
float PowerUpManager::GetActiveProgression(float t) {
    PowerUp* active = nullptr;
    for (PowerUp* p : m_ActivePowerUps) {
        if (p->IsSpecial()) active = p;       // last-special wins
    }
    if (!active) return 2.0f;                 // no special → "fully past"
    if (t > 0.0f && active->m_TotalTime > 0.0f) {
        return (active->field_0x9c - t) / active->m_TotalTime;
    }
    return active->GetCurrentTimeProgress();
}
```

### 4.20 `GetActiveSingle(ulong hash)` @ 0x00117cac

```cpp
PowerUp* PowerUpManager::GetActiveSingle(ulong hash) {
    auto it = m_ActiveByHash.find(hash);
    return (it == m_ActiveByHash.end()) ? nullptr : it->second;
}
```

### 4.21 `GetNumActiveTimedPowers()` @ 0x00117bb8

```cpp
int PowerUpManager::GetNumActiveTimedPowers() {
    int n = 0;
    for (PowerUp* p : m_ActivePowerUps) {
        if (p->IsSpecial()) ++n;
    }
    return n;
}
```

### 4.22 `SaveActivePowerUps(TiXmlElement* parent)` @ 0x00117df8 / `LoadActivePowerUps(parent, gameMode)` @ 0x001199d4

Wave-resume serialisation. Each active power becomes a `<powerup>`
child with attributes: `name`, `currentTime` (= field_0x9c),
`totalTime` (= m_TotalTime), `onScreenAmt` (= field_0xa8), and
`deferred` (= field_0xc4 if >= 0).

```cpp
void SaveActivePowerUps(TiXmlElement* parent) {
    for (PowerUp* p : m_ActivePowerUps) {
        TiXmlElement* el = new TiXmlElement("powerup");
        el->SetAttribute("name", p->m_Name);
        el->SetDoubleAttribute("currentTime", (double)p->field_0x9c);
        el->SetDoubleAttribute("totalTime",   (double)p->m_TotalTime);
        el->SetDoubleAttribute("onScreenAmt", (double)p->field_0xa8);
        if (p->field_0xc4 >= 0)
            el->SetDoubleAttribute("deferred", (double)p->field_0xc4);
        parent->LinkEndChild(el);
    }
}

void LoadActivePowerUps(TiXmlElement* parent, int gameMode) {
    for (TiXmlNode* n = parent->FirstChildElement("powerup");
         n; n = n->NextSiblingElement("powerup")) {
        float t; n->QueryFloatAttribute("currentTime", &t);
        ulong hash = StringHash(n->Attribute("name"));
        auto it = m_AllPowerUps.find(hash);
        if (it == m_AllPowerUps.end()) continue;
        PowerUp* tmpl = it->second;
        // Skip non-special, non-purchaseable powers UNLESS gameMode==2.
        if (gameMode == 2 || tmpl->IsSpecial() || tmpl->m_bIsPurchasable) {
            Vector3 pos(*g_DefaultSpawnPos);
            ActivatePower(hash, pos, &pos.x);
            // (above creates a Clone in m_ActivePowerUps, then Activate).
            PowerUp* clone = m_ActiveByHash[hash];   // (or last activated)
            clone->SetCurrentTime(t);
            n->QueryFloatAttribute("totalTime",   &t); clone->SetTotalTime(t);
            n->QueryFloatAttribute("onScreenAmt", &t); clone->SetOnScreenAmt(t);
            int deferred = -1;
            n->QueryIntAttribute("deferred", &deferred);
            if (deferred >= 0) AddToCurrentScore(deferred, 0, false, false);
        }
    }
}
```

---

## 5. Score-transformation delegate — Game.cpp static init flow

### 5.1 `DefaultScoreDelegate(int points)` @ 0x0010a598

```cpp
int DefaultScoreDelegate(int points) {
    if (g_GameData->gameMode == 2 /*Arcade*/) {
        if (points < 1) {
            points *= PowerUpManager::GetInstance()->GetScoreLossMultiplier();
        } else {
            points *= PowerUpManager::GetInstance()->GetScoreGainMultiplier();
        }
    }
    return points;
}
```

Only Arcade mode (`gameMode == 2`) applies the score-multiplier
transformation. Classic / Zen / Survival ignore PowerUpManager's
score-mults entirely. This is why the `x%d` overlay is Arcade-only in
ScoreControl.

### 5.2 `_GLOBAL__I_Game.cpp` @ 0x0010a96c — installs the delegate

```cpp
void _GLOBAL__I_Game_cpp() {
    // ... static-init chain for various Vector3/float globals ...

    // Install the default score-transformation delegate at g_GameData+0xc.
    Delegate1<int,int>::BaseDelegate base;
    base.vtable      = Delegate1<int,int>::Global::vtable;   // GOT lookup
    base.target      = &DefaultScoreDelegate;
    StackAllocatedPointer<...> wrapper;
    wrapper.operator=(base);              // memcpy into g_GameData+0xc
    // Register atexit cleanup.
    __aeabi_atexit(g_GameData+0xc, ~Global, ...);
}
```

The score delegate slot lives at `g_GameData + 0x0c`. `AddToCurrentScore`
(0x0010a7ac) reads it via `Delegate1<int,int>::Call(&g_GameData+0xc, points)`
and stores `prevScore + transformedPoints`. ScoreModifier (when
`deferPoints=true`) replaces this delegate temporarily during its
Activate/Deactivate to install per-power transformations.

### 5.3 `SetScoreDelegate(Delegate1<int,int>)` @ 0x0010a730

```cpp
void SetScoreDelegate(Delegate1<int,int> d) {
    *(Delegate1*)(g_GameData + 0x0c) = d;     // copy-assign
}
```

---

## 6. Integration map — who reads/writes which slot

This table shows EVERY consumer of PowerUpManager state across the port.

| Producer (writes to PowerUpManager state) | Field | Consumer (reads) |
|---|---|---|
| TimeModifier::UpdateSpecific | `m_DtMod` (+0x64) via ApplyDtMod | WaveManager::Update (mirrors to its +0x78), GetWavedt, all entity Updates via WaveManager |
| TimeModifier::UpdateSpecific (stop=true) | `m_field68` (+0x68) via StopClock | TimeControl::Update — reads as "+N seconds" floating overlay (TODO confirm) |
| TimeModifier::UpdateSpecific | `m_field6c` (+0x6c) via SlowClock | TimeControl::Update — reads as countdown speed multiplier |
| WaveModifier::UpdateSpecific | `m_field70` (+0x70) via PowerupDtModMultiply | PowerUpManager::Update next frame (mirrors to +0x74), then per-power dt scaling |
| WaveModifier::UpdateSpecific | `WaveManager.spawnLevel` (+0x68) | UpdateWave bomb-branch gate |
| WaveModifier::UpdateSpecific | `WaveManager.m_BombScale` (+0x64) | SpawnBomb |
| WaveModifier::UpdateSpecific | `WaveManager.m_FruitMult` (+0x6c) | UpdateWave fruit-branch gate |
| WaveModifier::UpdateSpecific | `WaveManager.m_CritChanceMult` (+0x70) | GetCriticalChance |
| ScoreModifier::UpdateSpecific (when !deferPoints) | `m_ScoreGainMult/Factor` (+0x78/+0x7c) | DefaultScoreDelegate (Arcade), ScoreControl::PreDraw §B |
| ScoreModifier::UpdateSpecific (when !deferPoints) | `m_ScoreLossMult/Factor` (+0x80/+0x84) | DefaultScoreDelegate (Arcade) negative-points branch |
| SlashModifier::UpdateSpecific | `g_SlashPowerMaskByCurrentFrame` (global, NOT in struct) | Slash::CollisionResponse — masks PUSH/PULL/etc. behaviour |
| SlashModifier::ApplyModifier | SlashEntityState (`g_GameData+0x3c->m_pState->{m_BladeWidth, m_ColourMod_*}`) | SlashEntity::Draw (visual) |
| `Bomb::CollisionResponse` (zen branch) | calls `ClearTimedPowers()` | — |
| `WaveManager::Init/Reset/Resume` | calls `Reset(bool)` and `Load()`/`LoadTextures()` | — |
| `Game.cpp` static init | installs `DefaultScoreDelegate` at `g_GameData+0xc` | `AddToCurrentScore` reads each call |

---

## 7. Key constants and addresses

### Function addresses (binary)

| Function | Address | Notes |
|----------|---------|-------|
| **PowerUpManager** |   |   |
| `PowerUpManager::PowerUpManager()` | 0x00117d20 | ctor |
| `PowerUpManager::PowerUpManager()` (alias) | 0x00117d60 | |
| `PowerUpManager::~PowerUpManager()` | 0x001187fc | |
| `PowerUpManager::GetInstance()` | 0x00118134 | Meyers singleton |
| `PowerUpManager::SetDefaults()` | 0x00117a80 | |
| `PowerUpManager::ClearScoreMultipliers()` | 0x0011a218 | |
| `PowerUpManager::Update(float)` | 0x001189b4 | (wrapper @ 0x000f3ccc) |
| `PowerUpManager::Reset(bool)` | 0x00119b08 | |
| `PowerUpManager::ClearTimedPowers()` | 0x00118904 | |
| `PowerUpManager::ActivatePower(...)` | 0x001197c4 | |
| `PowerUpManager::ActivateScreenEffect(ulong)` | 0x00119760 | |
| `PowerUpManager::ClearScreenEffects()` | 0x00117ed8 | |
| `PowerUpManager::Load()` | 0x00119cb0 | |
| `PowerUpManager::LoadTextures()` | 0x0011840c | |
| `PowerUpManager::Draw()` | 0x00119384 | |
| `PowerUpManager::ApplyDtMod(float)` | 0x001204dc | |
| `PowerUpManager::SlowClock(float)` | 0x001204cc | |
| `PowerUpManager::StopClock(float)` | 0x00117a70 | |
| `PowerUpManager::PowerupDtModMultiply(float)` | 0x001286ec | |
| `PowerUpManager::AddToScoreGainAdd(int)` | 0x0011d10c | |
| `PowerUpManager::AddToScoreLossAdd(int)` | 0x0011d114 | |
| `PowerUpManager::AddToScoreGainMultiply(int)` | 0x0011d120 | |
| `PowerUpManager::AddToScoreLossMultiply(int)` | 0x0011d128 | |
| `PowerUpManager::GetScoreGainMultiplier() const` | 0x0010ad34 | |
| `PowerUpManager::GetScoreLossMultiplier() const` | 0x0010ad40 | |
| `PowerUpManager::GetActiveProgression(float)` | 0x00117b38 | |
| `PowerUpManager::GetActiveSingle(ulong)` | 0x00117cac | |
| `PowerUpManager::GetNumActiveTimedPowers()` | 0x00117bb8 | |
| `PowerUpManager::SaveActivePowerUps(...)` | 0x00117df8 | |
| `PowerUpManager::LoadActivePowerUps(...)` | 0x001199d4 | |
| `_GLOBAL__I_PowerUpManager.cpp` | 0x00119df4 | |
| **PowerUp** |   |   |
| `PowerUp::PowerUp()` | 0x00118d3c | (and aliases at 0x00118e08, 0x00118ed4, 0x00119004) |
| `PowerUp::~PowerUp()` | 0x001186bc / 0x00118ba0 | |
| `PowerUp::Parse(TiXmlElement*)` | 0x001194f0 | |
| `PowerUp::Activate(bool, bool, Vector3, float*)` | 0x00119134 | |
| `PowerUp::Deactivate(bool)` | 0x00117f18 | |
| `PowerUp::Update(float)` | 0x00117f90 | |
| `PowerUp::Clone()` | (in vtable; addr varies) | |
| `PowerUp::DrawBar()` | 0x001191f8 | |
| `PowerUp::Purchaseable()` | 0x00117a44 | |
| `PowerUp::IsTimed()` | 0x0011a1dc | |
| `PowerUp::IsSpecial()` | (inline / vtable) | |
| `PowerUp::GetCurrentTimeProgress()` | 0x0011a1f0 | |
| `PowerUp::SetCurrentTime(float)` | 0x0011a210 | |
| `PowerUp::SetTotalTime(float)` | 0x001180d4 | |
| `PowerUp::SetOnScreenAmt(float)` | 0x0011a1c4 | |
| `PowerUp::AddDeferedPoints(int)` | 0x000f81f0 (lookup needed) | |
| `PowerUp::GetLongestMod()` | 0x00117aec | |
| `PowerUp::LoadTextures()` | 0x001183f0 | |
| **Modifiers** |   |   |
| `GameModifier::GameModifier()` | 0x0011a160 | |
| `GameModifier::Update(float)` | 0x001179c4 | base dispatcher |
| `GameModifier::ApplyModifier(bool, float*)` | 0x00118178 | base default |
| `ScoreModifier::ScoreModifier()` | 0x0011ca8c | |
| `ScoreModifier::ResetSpecific()` | 0x0011cb44 | |
| `ScoreModifier::UpdateSpecific(float)` | 0x0011cb70 | |
| `ScoreModifier::ApplyModifier(bool, float*)` | 0x0011cbe8 | |
| `ScoreModifier::RemoveModifier()` | 0x0011cd44 | |
| `ScoreModifier::GetType()` | 0x0011d134 | returns 2 |
| `ScoreModifier::ParseSpecific(TiXmlElement*)` | 0x0011ccb0 | |
| `ScoreModifier::Clone()` | 0x0011cc6c | |
| `TimeModifier::TimeModifier()` | 0x0011a228 | |
| `TimeModifier::ResetSpecific()` | 0x0011ff4c | clears m_CurrentDtMod |
| `TimeModifier::UpdateSpecific(float)` | 0x0011ffbc | |
| `TimeModifier::GetType()` | 0x001204ec | returns 0 |
| `TimeModifier::ParseSpecific(TiXmlElement*)` | 0x001200fc | |
| `SlashModifier::SlashModifier()` | 0x0011f1fc | |
| `SlashModifier::ResetSpecific()` | 0x0011f274 | no-op |
| `SlashModifier::UpdateSpecific(float)` | 0x0011f278 | OR mask |
| `SlashModifier::ApplyModifier(bool, float*)` | 0x0011f31c | |
| `SlashModifier::ParseSpecific(TiXmlElement*)` | 0x0011f464 | |
| `SlashModifier::Clone()` | 0x0011f29c | |
| `WaveModifier::WaveModifier()` | 0x00128158 | |
| `WaveModifier::UpdateSpecific(float)` | 0x001280e4 | |
| `WaveModifier::ParseSpecific(TiXmlElement*)` | 0x0012836c | |
| **Score delegate** |   |   |
| `DefaultScoreDelegate(int)` | 0x0010a598 | |
| `SetScoreDelegate(Delegate1<int,int>)` | 0x0010a730 | |
| `AddToCurrentScore(int, int, bool, bool)` | 0x0010a7ac | reads g_GameData+0xc |
| `_GLOBAL__I_Game.cpp` | 0x0010a96c | installs default delegate |
| **WaveManager helpers** |   |   |
| `WaveManager::BombMultiplyer(float)` | 0x0012870c | |
| `WaveManager::BombScale(float)`       | 0x001286fc | |
| `WaveManager::FruitMultiplyer(float)` | 0x0012871c | |
| `WaveManager::CriticalChanceMod(float)` | 0x0012872c | |

### vtables

| Class | vtable address |
|---|---|
| GameModifier  | 0x001e8cc0 |
| ScoreModifier | 0x001e8d00 |
| SlashModifier | 0x001e8d40 |
| TimeModifier  | 0x001e8d80 |
| WaveModifier  | 0x001e8e18 |
| Delegate1<int,int>::Callee<ScoreModifier> | 0x001e8c80 |

### Floating-point and string constants

| Address | Value | Used in | Meaning |
|---------|-------|---------|---------|
| 0x00118b90 | 0.001f       | Update, ClearTimedPowers | min `m_field88` for purchaseable powers |
| 0x00118b94 | -55.0f       | Update | per-special HUD x-offset (centering term) |
| 0x00118b98 | 0.2f         | Update | HUD x-position lerp damping |
| 0x00118b9c | 0.0f         | Update | passed to ScreenEffect::Update args 3+4 |
| 0x00119370 | 0.0f         | DrawBar | matrix zero literal |
| 0x00119374 | 160.0f       | DrawBar | top-of-screen Y in centered ortho |
| 0x001ba37c | "xml/powerUpList.xml" | Load | XML file path |
| 0x001ba390 | "powerInfoFile" | Load | root element name |
| 0x001ba394 | "powerup" | Load | element name |
| 0x001ba39c | "screeneffect" | Load | top-level element name |
| 0x001ba3a0 | "13ScoreModifier" | typeinfo | (class name) |
| 0x001bcf70 | "PUSH_FRUIT\0PULL_FRUIT\0PUSH_BOMB\0PULL_BOMB\0BOMB_HIT\0FRUIT_BOUNCE" | ParseSlashPowerMask | bit table |
| 0x001bcf90 | "NONE\0LERP\0PER_SLASH\0CONTINUOUS" | ParseSlashModColourType | enum table |

---

## 8. Power-up types — list (placeholder; needs `xml/powerUpList.xml` extraction)

The binary code does NOT contain hard-coded power-up names — every
power-up is XML-driven via `xml/powerUpList.xml`. The names are
defined entirely in that file. Common power-up names referenced in
strings / save-data XML elsewhere in the binary include:

| Hash key (likely XML name) | StringHash | Source |
|---|---|---|
| `frenzy` (Banana Frenzy / Fruit Frenzy) | (compute) | implied by `Bonus-Banana-Frenzy` texture string @ 0x001bc6a8 |
| `blitz` / `blitz_1` ... `blitz_6` | (compute) | strings @ 0x001ba76a/0x001ba773/0x001baa-... (combo-blitz-1..6) |
| `blitz_bonus`                  | (compute) | string @ 0x001ba6ff (Wave force-spawn flag) |
| `peach` / `sensei` / etc.      | — | NOT in binary; only XML |

**The binary does not enumerate power-up types.** The full list and
their modifier compositions live in `assets/xml/powerUpList.xml`,
which the port must ship as-is from the original asset bundle. The
port should NOT attempt to hard-code power-up names in source —
instead, port the XML loading and let the asset file define them.

The PowerUpShop screen has a separate `m_pPurchaseInfo` parsed from
`<purchase>` children, which DOES enumerate purchasable powers (Sensei,
Peach, Bonus Banana, etc.). See `docs/structs/shop-list-audit.md` for
the shop-side power list extracted from `purchasable=true` filtering.

---

## 9. Action list for `implementer`

In priority order. Each item is callable as a focused implementer task.

### Tier 1 — Visible / Playable

**T1-1.** **Port PowerUpManager singleton struct and ctor.**
- File: `src/game/PowerUpManager.h` and new `src/game/PowerUpManager.cpp`.
- Add all 144 bytes of fields per §1: `std::map<uint32_t, PowerUp*> m_AllPowerUps`,
  `std::list<PowerUp*> m_ActivePowerUps`, `std::map<uint32_t, PowerUp*> m_ActiveByHash`,
  `std::map<uint32_t, ScreenEffect> m_ScreenEffectPool`, `std::list<ScreenEffect> m_ActiveScreenEffects`,
  `std::list<PowerUp*> m_PurchasablePowers`, plus scalars
  `int m_field60`, `float m_DtMod`, `float m_field68`, `float m_field6c`,
  `float m_field70`, `float m_field74`, `int m_ScoreGainMult`,
  `int m_ScoreGainFactor`, `int m_ScoreLossMult`, `int m_ScoreLossFactor`,
  `float m_field88`.
- Ctor zeroes scalars, default-constructs containers, sets `m_field70 = m_field74 = 1.0f`.
- Replaces the current "fields-only" port at `src/game/PowerUpManager.h`.

**T1-2.** **Port PowerUp class.**
- New `src/game/PowerUp.h` + `src/game/PowerUp.cpp`.
- Layout per §2 (size 0xCC). Methods needed: `Parse(TiXmlElement*)`, `Activate(...)`,
  `Deactivate(bool)`, `Update(float)`, `Clone()`, `DrawBar()`, `IsPurchaseable()`,
  `IsTimed()`, `IsSpecial()`, `GetCurrentTimeProgress()`, `GetLongestMod()`,
  `SetCurrentTime/TotalTime/OnScreenAmt`, `AddDeferedPoints(int)`,
  `LoadTextures()`. Owns a `std::list<GameModifier*>` plus `ScreenEffect*` and
  optional `PurchaseInfo*`.

**T1-3.** **Port four GameModifier subclasses.**
- Existing skeletons in `src/game/GameModifier.h` and `src/game/SlashModifier.{h,cpp}`.
- Add `src/game/ScoreModifier.{h,cpp}`, `src/game/TimeModifier.{h,cpp}`,
  `src/game/WaveModifier.{h,cpp}` per §3.1–§3.4 with full vtable + UpdateSpecific.
- Wire `GameModifier::Update` dispatcher (deferred-activation gate +
  duration tick + UpdateSpecific call) per §3.0.

**T1-4.** **Implement `PowerUpManager::Update(dt)` and `SetDefaults()`.**
- File: `src/game/PowerUpManager.cpp`.
- Full pseudocode in §4.1 and §4.2. Single source of truth for the
  one-frame-latency `m_field70 → m_field74` propagation.
- Once this works, `WaveManager.cpp:747` "Skip PowerUpManager::Update" comment
  can be removed and the port wires:
  ```cpp
  PowerUpManager::GetInstance()->Update(dt);
  field_0x78 = PowerUpManager::GetInstance()->m_DtMod;
  ```

**T1-5.** **Implement `PowerUpManager::Load()` and `LoadTextures()`.**
- Parse `xml/powerUpList.xml`. Produces all entries in `m_AllPowerUps`
  (every power-up the game can ever fire) and `m_ScreenEffectPool`
  (named blitz banners etc.).
- Wire to `GameInitialise.cpp:195` (already calls `Load()` per existing port)
  and a new call to `LoadTextures()` once the asset XML can be loaded.

**T1-6.** **Implement `Bomb::CollisionResponse` zen-bomb branch:**
`PowerUpManager::GetInstance()->ClearTimedPowers()`.
- File: `src/entities/Bomb.cpp:659`. Replace the `// TODO` with the call.

**T1-7.** **Wire `ScoreControl::PreDraw` Section B Arcade overlay.**
- `src/hud/ScoreControl.cpp:422-423`. Replace stub `1.0f` with
  `(float)PowerUpManager::GetInstance()->GetScoreGainMultiplier()`.
- The `x%d` overlay text uses `(int)multiplier` for the integer suffix.

### Tier 2 — Fidelity

**T2-1.** **Implement `Reset(bool)` and `Save/LoadActivePowerUps`.**
- §4.4 / §4.22. Replace the two TODO calls in `WaveManager.cpp:686/694`
  with `PowerUpManager::GetInstance()->Reset(false|true)`.

**T2-2.** **Implement `ActivateScreenEffect(hash)` and `ClearScreenEffects()`.**
- §4.7 / §4.8. Replace `WaveManager.cpp:1468/1481` blitz banner TODOs with
  `PowerUpManager::GetInstance()->ActivateScreenEffect(StringHash("blitz_count"))`
  and `..._blitz_<level>_count`.

**T2-3.** **Wire ScoreModifier delegate hook.**
- New `src/game/ScoreDelegate.{h,cpp}` containing `DefaultScoreDelegate(int)`
  per §5.1, and `SetScoreDelegate(Delegate1<int,int>)` per §5.3.
- Install `DefaultScoreDelegate` at app init (mimics `_GLOBAL__I_Game.cpp`).
  The port's existing `Game.cpp` static-init equivalent should call
  `SetScoreDelegate(Delegate1::Global(&DefaultScoreDelegate))` once at
  startup.
- Wire `AddToCurrentScore` (existing port) to invoke the delegate before
  adding to `g_GameData->score`.

**T2-4.** **Implement `PowerUpManager::Draw()` and `PowerUp::DrawBar()`.**
- §4.11. Replace `WaveManager.cpp:1336-1338` Draw TODO with
  `if (playerIdx == 0) PowerUpManager::GetInstance()->Draw()`.
- DrawBar renders the icon row at top-of-screen. Uses MatrixManager,
  Texture::Set/UnSet, DrawQuadUnCached (already ported).

**T2-5.** **Implement `GetActiveProgression(t)` and finish PROBABILITY_OVERIDE.**
- §4.19. With this in place, the WaveManager PROBABILITY_OVERIDE branch
  per `wavemanager-deep-re.md` §5 can be unblocked. Resulting behaviour:
  power-up fruits actually spawn; blitz mode triggers in Arcade.

### Tier 3 — Polish

**T3-1.** **TimeControl integration of `m_field68`/`m_field6c`.**
- TimeControl reads `m_field68` for the "+N seconds" overlay (text
  showing "+5" floats up when a Time-Bonus power activates) and
  `m_field6c` to scale countdown decrement rate (chrono / freeze powers).
- Inspect `TimeControl::Update` ASM to confirm the exact xrefs to
  `PowerUpManager::GetInstance()->m_field68` and `m_field6c`. Currently
  out-of-scope for this RE pass (TimeControl is its own subsystem; flag
  for a separate RE if the binary's exact formula is needed).

**T3-2.** **PurchaseInfo class.**
- 0xC4 bytes. Only relevant for shop-purchaseable powers (Sensei, Peach,
  Bonus Banana). Out-of-scope until PowerUpShop is ported.

**T3-3.** **ScreenEffect class.**
- 0x50 bytes. Wraps `vector<EffectImage>`, `vector<ScreenTint>`, `vector<Emmiter>`,
  `vector<SoundEffect>`. Used both as map values and active list elements.
- Full RE deferred — the high-level API (`Parse`, `Activate`, `Update`,
  `Deactivate`, `LoadTextures`, copy-constructible by value) is enough
  for PowerUpManager to call. Only blitz banners use the standalone
  pool; PowerUp's own m_pScreenEffect handles per-power effects.

---

## 10. Open RE gaps / flagged for future investigation

1. **TimeControl::Update reads of `m_field68` and `m_field6c`** — confirm
   exact formula by ASM-decompiling TimeControl::Update and finding
   the GOT-relative `PowerUpManager::GetInstance() + 0x68/+0x6c` xrefs.
2. **Global slash-power-mask address** — `SetDefaults` writes to
   `*(uint32_t*)(GOT + 0x7740)` and `SlashModifier::UpdateSpecific`
   ORs into the same address. The exact symbol name (probably
   `g_SlashPowerMask` or similar in `g_GameData+something`) needs naming.
3. **`SlashModifier::SetModColours`'s exact arg semantics** — the
   per-frame visual override path (m_BladeWidth, m_ColourMod_*, m_TipColourMod
   in SlashEntityState) is partially RE'd in `docs/engine/slash-entity-asm-audit.md`
   but the apply mechanic on the new vtable+0x14 is not fully traced. Should
   be doable by inspecting `SlashEntity::SetModColours` body.
4. **TimeModifier ramp formula precision** — the lerp branch in
   `UpdateSpecific` has three arms (first-half, second-half, no-rate)
   that share a complex piecewise formula. The pseudocode in §3.2 captures
   the high-level shape but may need ASM-level verification for frame-perfect
   replay matching.
5. **Power-up names enumeration** — confirmed entirely XML-driven; no
   binary list to extract. Port must ship `xml/powerUpList.xml` from the
   original assets and load by hash. (Per §8.)
