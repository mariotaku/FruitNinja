# EntityFactory, ComboChecker, TimeKeeper, FruitCamera (Extended)

Analysis from FruitNinja.exe (ARM32 Bada) via GhidraMCP.

---

## 1. EntityFactory

### Overview

`CreateEntity` is a free function at `0x0017421c` that serves as the factory delegate for `ActorManager`. It maps integer entity type IDs to concrete entity classes.

### CreateEntity (0x0017421c)

```c
MortarEntity* CreateEntity(long entityType)
{
    switch (entityType) {
    case 0:  // Fruit
        this = Entity::operator_new(0x118);
        Fruit::Fruit((Fruit*)this);
        break;
    case 1:  // Bomb
        this = Entity::operator_new(0xb0);
        Bomb::Bomb((Bomb*)this);
        break;
    case 2:  // Coin
        this = Entity::operator_new(0x94);
        Coin::Coin((Coin*)this);
        break;
    case 3:  // SlashEntity
        this = Entity::operator_new(0x184);
        SlashEntity::SlashEntity(this);
        break;
    case 4:  // BombBlast
        this = Entity::operator_new(0x70);
        BombBlast::BombBlast((BombBlast*)this);
        break;
    default:
        return NULL;
    }
    return this;
}
```

### Entity Type → Size Map

| Type ID | Class | Alloc Size | Notes |
|---------|-------|-----------|-------|
| 0 | Fruit | 0x118 (280 bytes) | Main sliceable entity |
| 1 | Bomb | 0xb0 (176 bytes) | Explodes on slash |
| 2 | Coin | 0x94 (148 bytes) | Starfruit currency pickup |
| 3 | SlashEntity | 0x184 (388 bytes) | Touch trail / blade |
| 4 | BombBlast | 0x70 (112 bytes) | Bomb explosion visual |

### Registration with ActorManager

In `GameInit` (0x0016c654), after creating the HUD and loading content:

```c
// At ~line 240 of GameInit
ActorManager* actorMgr = ActorManager::GetInstance();
ActorManager::Initialise(actorMgr, 5, 0x2000);  // 5 entity types, 8KB heap

// Register CreateEntity as the factory delegate
// Uses Delegate1<Entity*, long> bound to the global CreateEntity function
ActorManager::RegisterFactory(actorMgr, createEntityDelegate);
```

`RegisterFactory` (0x0016d870) simply assigns the delegate to offset `0x1024` in ActorManager:
```c
void ActorManager::RegisterFactory(ActorManager* this, Delegate1 factoryDelegate) {
    Delegate1::operator=(&this[0x1024], factoryDelegate);
}
```

### Entity Pre-allocation Pool

GameInit pre-creates 30 of each combat entity type (Fruit, Bomb, BombBlast) with `flags |= 0x11` (deactivated):

```c
for (int i = 0; i < 30; i++) {
    entity = ActorManager::Add(actorMgr, 0, true);  // Fruit
    entity->flags |= 0x11;
    entity = ActorManager::Add(actorMgr, 1, true);  // Bomb
    entity->flags |= 0x11;
    entity = ActorManager::Add(actorMgr, 4, true);  // BombBlast
    entity->flags |= 0x11;
}
```

This gives a pool of 30 fruits, 30 bombs, and 30 bomb blasts, all pre-allocated and marked inactive.

### _GLOBAL__I_EntityFactory.cpp (0x00174294)

Static initializer that sets up:
- Identity matrix (4x4, 16 floats)
- Two Vec3 statics: one zero, one (1,1,1)
- One Vec2 static: (0,0)
- ~12 TypeID registrations via `Mortar::__INIT_TYPE_ID()`

### Methods Summary

| Address | Name | Signature |
|---------|------|-----------|
| 0x0017421c | CreateEntity | `MortarEntity* CreateEntity(long entityType)` |
| 0x0016d870 | ActorManager::RegisterFactory | `void RegisterFactory(ActorManager*, Delegate1)` |
| 0x00174294 | _GLOBAL__I_EntityFactory.cpp | Static initializer |

---

## 2. ComboChecker

### Overview

ComboChecker is a stateless free-function system defined in `ComboChecker.cpp`. The main function `CheckCombo` analyzes an array of fruit type IDs that were hit in a single slash and determines the combo type.

### COMBO_TYPE Enum

From the CheckCombo return values and the immediate_values in the function signature:

| Value | Name | Condition |
|-------|------|-----------|
| 0x00 | COMBO_NONE | 1 fruit sliced |
| 0x01 | COMBO_2 | 2 fruits (basic double) |
| 0x02 | COMBO_3 | 3 fruits |
| 0x03 | COMBO_4 | 4 fruits |
| 0x04 | COMBO_ALL_UNIQUE | 5+ fruits, all unique types |
| 0x05 | COMBO_7_PLUS | 7+ fruits (fallback) |
| 0x14 (20) | COMBO_5_2PAIR | 5 fruits, 2 unique types, one appears 2+ times |
| 0x15 (21) | COMBO_5_3UNIQUE | 5 fruits, 3 unique types, one appears 2+ times |
| 0x16 (22) | COMBO_TRIPLE | Any type appears 3 times |
| 0x17 (23) | COMBO_QUAD | Any type appears 4 times |
| 0x18 (24) | COMBO_ALTERNATING | 2 types alternating pattern (e.g., ABAB) |

### Special Single-Fruit Combos

When only 1 unique fruit type is sliced (all fruits in the slash are the same type), CheckCombo checks a table of 14 "special" fruit types with associated combo values. These use `Fruit::FruitType()` to resolve string names to type IDs:

Special fruits checked (from string_constants):
1. orange
2. banana
3. lemon
4. strawberry
5. apple_red
6. mango
7. coconut
8. passionfruit

Plus 6 more (14 total entries, each 8 bytes: type_id + combo_byte).

The table is lazily initialized using `__cxa_guard_acquire` (C++ static local init pattern), storing 14 entries of `{int fruitTypeId, char comboValue}` at 8-byte stride. If the single fruit matches, the associated `comboValue` byte is returned.

### Combo Count → Default Lookup Table

For basic combos without special patterns, a fallback table at `DAT_00110fc0` maps fruit count to combo type:

| Count | COMBO_TYPE |
|-------|------------|
| 0 | (unused) |
| 1 | 0x00 (COMBO_NONE) |
| 2 | 0x01 (COMBO_2) |
| 3 | 0x02 (COMBO_3) |
| 4 | 0x03 (COMBO_4) |
| 5 | 0x03 (COMBO_4) |
| 6 | 0x04 |
| 7+ | 0x05 (COMBO_7_PLUS) |

### CheckCombo (0x00110cb0)

**Signature:** `int CheckCombo(int* fruitTypes, int count, int* outBestType)`

**Algorithm:**
1. Iterate over the `fruitTypes` array (each element is a fruit type hash/ID)
2. Build a histogram: array of `{typeId, count}` pairs (max ~11 entries, 8 bytes each at static offset)
3. Track `numUniqueTypes`, `maxCount` (highest frequency), `bestType` (type with highest frequency)
4. Track `bAllInOrder` (whether types appear in sorted order — used for alternating detection)
5. After building histogram, apply pattern matching:
   - `numUniqueTypes == 1`: Check special single-fruit table (14 entries)
   - `numUniqueTypes == 2` + NOT in order: Check alternating pattern → COMBO_ALTERNATING (0x18)
   - `numUniqueTypes == 2` + `count == 5` + any type appears 2+: → COMBO_5_2PAIR (0x14)
   - `numUniqueTypes == 3` + `count == 5` + any appears 2+: → COMBO_5_3UNIQUE (0x15)
   - `numUniqueTypes == count` + `count > 4`: → COMBO_ALL_UNIQUE (0x04)
   - Otherwise: scan histogram for triple (3) → COMBO_TRIPLE (0x16), quad (4) → COMBO_QUAD (0x17)
   - Final fallback: lookup table by count

6. Output `*outBestType = bestType` (most frequent fruit type in the slash)
7. Return the COMBO_TYPE

### Histogram Structure (static local)

Located at a GOT-relative offset inside CheckCombo. Size = 0x60 bytes:
- Offset 0x00: `{int typeId, int count}[11]` — 88 bytes of type/count pairs
- Offset 0x58: `int numUniqueTypes` — number of unique types found

### Related Functions

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x00110cb0 | CheckCombo | `int CheckCombo(int* types, int count, int* outBest)` | Main combo detection |
| 0x00110c94 | GetComboName | `const char* GetComboName(COMBO_TYPE type)` | Returns name string from lookup table |
| 0x001112e0 | GetComboStarTexture | `SmartPtr<Texture> GetComboStarTexture(COMBO_TYPE)` | Returns combo star texture |
| 0x0010de24 | BonusManager::AddCombo | `void AddCombo(BonusManager*, int count)` | Awards bonus for combo |
| 0x0010a3e4 | CombosEnabled | `bool CombosEnabled(void)` | Checks if game mode allows combos |
| 0x001515a4 | MissControl::MakeCombo | `void MakeCombo(MissControl*, Vec3 pos, int count, int type)` | Shows combo HUD popup |
| 0x0011303c | ItemManager::PlayAlternateComboSound | `void PlayAlternateComboSound(ItemManager*, int)` | Plays combo-specific SFX |
| 0x00122f50 | WaveManager::UpdateComboSpeed | `void UpdateComboSpeed(WaveManager*, float dt)` | Speed control for combo mode |
| 0x00121840 | WaveManager::GetComboBonusProgression | `float GetComboBonusProgression(WaveManager*, int)` | Combo bonus meter progress |

### BonusManager::AddCombo (0x0010de24)

Awards score bonus based on combo size. Uses a `vector<int>` at offset 0x14 as a bonus points table. Index is `max(0, comboCount - 3)`, clamped to vector size. Tracks "combo_bonus" and "best_combo" via `FruitSaveData::AddToTotal()` using StringHash keys.

### Calling Convention Fixes Applied

| Address | Function | Fix |
|---------|----------|-----|
| 0x0010a3e4 | CombosEnabled | Was "Unknown calling convention" → fixed to default (global function) |

---

## 3. TimeKeeper

### Overview

TimeKeeper is a minimal 8-byte struct that stores a time scale factor. It is used as an embedded member in other systems, not as a standalone manager. The TimeControl HUD widget and PowerUpManager interact with TimeModifier to implement time-based power-ups.

### TimeKeeper Struct (8 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | float | m_Time | Initialized to 0.0f |
| 0x04 | float | m_Scale | Initialized to 1.0f (0x3f800000) |

### Constructors

Both constructors at 0x00128d08 and 0x00128d20 are identical:
```c
void TimeKeeper::TimeKeeper(TimeKeeper* this) {
    this->m_Time = 0.0f;
    this->m_Scale = 1.0f;
}
```

### Destructor (0x00128d38)

Empty — no resources to free.

### TimeControl (HUD Widget, 0x108 bytes)

TimeControl is the actual game timer HUD widget, inheriting from HUDControl3d. It manages the countdown display in Classic mode.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | vtable | | HUDControl3d base |
| 0x08 | Vec3 | pos | Position |
| 0x20 | Vec3 | size | Size |
| 0x74 | SmartPtr<Texture> | m_TimerTex | Timer bar texture |
| 0x7C | float | m_TimeRemaining | Current time + added time |
| 0xC0 | float | m_CountDown | Total countdown value; -1.0 = no countdown |
| 0xC8 | byte | field_0xC8 | Unknown |
| 0x32 (50) | byte | field_0x32 | Init to 0 |

**Key methods:**

| Address | Name | Notes |
|---------|------|-------|
| 0x001622e8 | TimeControl::TimeControl() | Constructor, size = 0x108 |
| 0x001620f0 | TimeControl::CountDown(float) | Sets m_CountDown value |
| 0x00162134 | TimeControl::GetCountDown() const | Returns m_CountDown (or 0 in Zen/multiplayer) |
| 0x001204f0 | TimeControl::AddTime(float) | `m_TimeRemaining += addedTime` |

### TimeModifier (Power-Up, ~0x3C bytes)

TimeModifier is a GameModifier subclass that implements time-affecting power-ups (freeze, slow-mo, time bonus).

**Struct layout (extends GameModifier at 0x20 bytes):**

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | vtable | | GameModifier vtable |
| 0x04 | float | m_Duration | From base GameModifier |
| 0x0C | float | m_ActiveTime | Time active |
| 0x20 | float | m_TargetScale | Target dt multiplier; default 1.0 |
| 0x24 | float | m_RampDuration | Time to ramp from current to target scale |
| 0x28 | float | m_CurrentScale | Current dt multiplier; default 1.0 |
| 0x2C | bool | m_bStopClock | If true, calls PowerUpManager::StopClock |
| 0x30 | float | m_SlowFactor | Multiplier for SlowClock; default 1.0 |
| 0x34 | float | m_AddTimeAmount | Bonus time to add (for time bonus power-up) |
| 0x38 | int | m_AddTimeCountdown | If > 0, counts down then adds time |

### TimeModifier::UpdateSpecific (0x0011ffbc)

Called each frame. Logic:

1. **Time bonus check:** If `m_AddTimeCountdown > 0`, decrement. When it reaches 0, call `TimeControl::AddTime(m_AddTimeAmount)` and return 1 (modifier complete).
2. **Stop clock:** If `m_bStopClock`, calls `PowerUpManager::StopClock(m_ActiveTime)` — accumulates time the clock was stopped.
3. **Slow clock:** If `m_SlowFactor != 1.0`, calls `PowerUpManager::SlowClock(m_SlowFactor)` — multiplies the clock speed.
4. **Scale ramping:** Ramps `m_CurrentScale` toward `m_TargetScale` over `m_RampDuration`:
   - If `m_RampDuration <= 0`, snaps instantly
   - If modifier is still in initial activation (`m_ActiveTime > 0` and `m_RampDuration < m_ActiveTime`), uses proportional ramp
   - Otherwise, linear interpolation: `currentScale += dt / rampDuration` (clamped)
5. **Apply:** Calls `PowerUpManager::ApplyDtMod(m_CurrentScale)` — multiplies the global dt modifier

### TimeModifier::ParseSpecific (0x001200fc)

Parses from XML attributes:
- `m_bStopClock` ← `CompareWords(Attribute("type"), "stop")`
- `m_SlowFactor` ← `QueryFloatAttribute("slow")`
- `m_AddTimeAmount` ← `QueryFloatAttribute("addTime")` — if non-zero, sets `m_AddTimeCountdown = 1`
- Child element with `QueryFloatAttribute("rampTime")` → `m_RampDuration`
- Child element with `QueryFloatAttribute("targetScale")` → `m_TargetScale`

### PowerUpManager Time Methods

| Address | Name | Logic |
|---------|------|-------|
| 0x001204dc | ApplyDtMod(float) | `m_DtMod *= scale` |
| 0x001204cc | SlowClock(float) | `m_field6c *= factor` |
| 0x00117a70 | StopClock(float) | `m_field68 += time` |

### Methods Summary

| Address | Name | Signature |
|---------|------|-----------|
| 0x00128d08 | TimeKeeper::TimeKeeper | `void TimeKeeper(TimeKeeper* this)` |
| 0x00128d20 | TimeKeeper::TimeKeeper | `void TimeKeeper(TimeKeeper* this)` (duplicate) |
| 0x00128d38 | TimeKeeper::~TimeKeeper | `TimeKeeper* ~TimeKeeper(TimeKeeper* this)` |
| 0x0011a228 | TimeModifier::TimeModifier | Constructor |
| 0x0011ffbc | TimeModifier::UpdateSpecific | `int UpdateSpecific(TimeModifier*, float dt)` |
| 0x001200fc | TimeModifier::ParseSpecific | `void ParseSpecific(TimeModifier*, TiXmlElement*)` |
| 0x0011ff04 | TimeModifier::~TimeModifier | Destructor |
| 0x001622e8 | TimeControl::TimeControl | Constructor (0x108 bytes) |
| 0x001620f0 | TimeControl::CountDown | `void CountDown(TimeControl*, float)` |
| 0x00162134 | TimeControl::GetCountDown | `float GetCountDown(TimeControl*) const` |
| 0x001204f0 | TimeControl::AddTime | `void AddTime(TimeControl*, float)` |

### Calling Convention Fixes Applied

| Address | Function | Fix |
|---------|----------|-----|
| 0x00162134 | TimeControl::GetCountDown | Was "Unknown calling convention" → fixed to `__thiscall` |
| 0x00129b94 | FruitSaveData::ClearCombo | Was "Unknown calling convention" → fixed to `__thiscall` |

---

## 4. FruitCamera (Extended Analysis)

### Overview

FruitCamera extends MortarCamera (0x12c bytes) to add shake effects and entity following. Total size = 0x16c (364 bytes). The existing docs at `docs/engine/camera.md` have the struct layout; this section adds method analysis.

### Constructor (0x00180e40 / 0x00180de0)

```c
FruitCamera* FruitCamera::FruitCamera(FruitCamera* this) {
    MortarCamera::MortarCamera(this);  // Init base (0x12c bytes)
    this->fns = FruitCamera_vtable + 8;
    this->field_0x12c = 0;            // m_pFollowEntity = NULL
    this->m_CameraMode = 0;           // Idle mode
    this->field_0x134 = 0;
    this->field_0x136 = 0;
    this->m_TargetX = defaultTarget.x; // From static Vec2
    this->m_TargetY = defaultTarget.y;
    this->m_ShakeDir_x = defaultTarget.x;
    this->m_ShakeDir_y = defaultTarget.y;
    this->m_ShakeIntensity = 0.0f;     // DAT_00180e30 = 0.0
    return this;
}
```

### UpdateCamera (0x00180c8c)

Called each frame from the game loop:

```c
float FruitCamera::UpdateCamera(FruitCamera* this, float dt) {
    this->field_0x14c = (float)this->field_0x136;
    this->field_0x150 = (float)this->field_0x134;
    
    // Calculate distance magnitude from position
    Vec3 diff = -this->m_pos;
    this->m_DistanceMag = Vec3::Magnitude(diff);
    
    // Snapshot lookAt
    this->field_0x158 = this->field_0xf0;  // m_lookAt copy
    this->field_0x15c = this->field_0xf4;
    this->field_0x160 = this->field_0xf8;
    
    if (this->m_CameraMode == 0) {
        return UpdateIdle(dt);     // Idle = no-op
    } else if (this->m_CameraMode == 1) {
        UpdateFollow(this, dt);    // Follow entity
    }
}
```

### UpdateIdle (0x00180a08)

Empty function — no camera motion in idle mode. The camera stays at its current position.

### UpdateFollow (0x00180c50)

Follows the entity stored at `field_0x12c`:
```c
void FruitCamera::UpdateFollow(FruitCamera* this, float dt) {
    if (this->field_0x12c == NULL) {
        IdleCamera(this);  // Clear follow target, set mode=0
    } else {
        Vec3 diff = -(this->m_lookAt);
        Entity* target = this->field_0x12c;
        this->m_lookAt = target->pos;  // Copy entity position to lookAt
        this->m_pos -= diff;           // Adjust camera position by delta
    }
}
```

### CreateCameraShake (0x00180d10)

Triggered on bomb explosion:
```c
void FruitCamera::CreateCameraShake(FruitCamera* this, Vec3* impact, float intensity, float scale) {
    this->m_ShakeAngle = Math::Atan2Idx(impact->y, impact->x);
    this->m_ShakeDir_x = Math::CosIdx(m_ShakeAngle) * 9.0;
    this->m_ShakeDir_y = Math::SinIdx(m_ShakeAngle) * 9.0;
    Vec2::operator*=(this->m_ShakeDir, scale);  // Scale shake by bomb distance
    this->field_0x168 = intensity;  // Store initial intensity
    this->m_ShakeIntensity = intensity;
}
```

### UpdateShake (0x00180ea0)

Called each frame after UpdateCamera. Two branches:

**If `m_ShakeIntensity <= 0` (no active shake):**
- Decays `m_TargetX` and `m_TargetY` back to 0.0
- Uses a threshold (DAT constants) to decide whether to decay or snap to zero
- Decay factor is a multiplier (DAT_00181074, ~0.9?)

**If `m_ShakeIntensity > 0` (active shake):**
- `m_ShakeIntensity -= dt` (linear decay)
- If shake direction magnitude^2 < 16.0 (within range):
  - Randomize angle: `m_ShakeAngle += 0x6388 + random(0x38e0)` (pseudo-random jitter using LCG)
  - Scale amplitude: `amplitude = (m_ShakeIntensity / m_field168) * 9.0`
  - Update direction: `m_ShakeDir = (cos(angle), sin(angle)) * amplitude`
- Compute delta from current target to new shake direction
- Apply with interpolation: `factor = m_ShakeIntensity / m_field168 + 1.0`
- `m_Target += delta * dt * factor`
- Set `m_bDirty = 1` (triggers projection recalculation)

### IdleCamera (0x00180a20)

```c
void FruitCamera::IdleCamera(FruitCamera* this) {
    this->field_0x12c = 0;   // Clear follow target
    this->m_CameraMode = 0;  // Back to idle
}
```

### FollowEntity (0x00180b2c)

```c
void FruitCamera::FollowEntity(FruitCamera* this, Entity* entity) {
    if (entity != NULL) {
        this->field_0x12c = entity;
        this->m_CameraMode = 1;
    }
    this->field_0x136 = 0;
    this->field_0x134 = 0;
    this->m_up = Vec3(0.0, 1.0, 0.0);  // Reset up vector
}
```

### SetupPerspective (0x001810ac)

Large function handling 4 perspective types (enum PERSPECTIVE_TYPE):

| Type | Name | LookAt Setup | Ortho Params |
|------|------|-------------|-------------|
| 0 | Default | LookAt(pos, up=(0,1,0), target) | (X_LEFT, X_RIGHT, Y_BOTTOM, Y_TOP, NEAR, FAR) |
| 1 | Rotated | LookAt with (-TargetY, TargetX) | Swapped axes |
| 2 | Mirrored | LookAt with (TargetY, -TargetX) | Swapped axes |
| 3+ | Fixed | LookAt((0,0,1), (0,1,0), staticTarget) | Default ortho |

All types call `MatrixManager::SetupOrtho()` with the named ORTHO constants (160, -160, -240, 240, 2000, -6000 from the project docs). After setting up the projection, copies the result matrices into the camera's `field_0x34` (projection) storage.

### All FruitCamera Methods

| Address | Name | Notes |
|---------|------|-------|
| 0x00180e40 | FruitCamera::FruitCamera() | Constructor (thunk at 0x00180de0) |
| 0x00180c8c | FruitCamera::UpdateCamera(float) | Main per-frame update |
| 0x00180a08 | FruitCamera::UpdateIdle(float) | No-op |
| 0x00180c50 | FruitCamera::UpdateFollow(float) | Follow entity pos |
| 0x00180ea0 | FruitCamera::UpdateShake(float) | Shake decay/randomize |
| 0x00180d10 | FruitCamera::CreateCameraShake(Vec3, float, float) | Start shake from impact |
| 0x00180a20 | FruitCamera::IdleCamera() | Reset to idle mode |
| 0x00180b2c | FruitCamera::FollowEntity(Entity*) | Set follow target |
| 0x001810ac | FruitCamera::SetupPerspective(int, bool) | Ortho/view matrix setup |
| 0x0017ff64 | ZeroInit_FruitCamera | Sets ptr to 0 |
| 0x00180d6c | ~FruitCamera | Destructor (3 variants) |

---

## Calling Convention Fixes Summary

| Address | Function | Before | After |
|---------|----------|--------|-------|
| 0x00129b94 | FruitSaveData::ClearCombo | Unknown calling convention | `__thiscall` with `FruitSaveData*` |
| 0x0010a3e4 | CombosEnabled | Unknown calling convention | `bool CombosEnabled(void)` |
| 0x00162134 | TimeControl::GetCountDown | Unknown calling convention | `__thiscall` returning `float` |
