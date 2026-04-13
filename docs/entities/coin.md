# Coin : MortarEntity

Bouncing coins spawned on combo rewards. Pool-based via ActorManager (entity type 2).

<!-- Analysed: 2026-04-12T16:45 -->

## Entity Type ID

**2** — confirmed from `ClearCoins` calling `ActorManager::GetEntityFirst(2)` and `MakeCoins` calling `ActorManager::Add(2, true)`.

## Vtable (0x001EA4D8, 10 entries)

| Index | Address | Name | Notes |
|-------|---------|------|-------|
| 0 | 0x00173218 | `~Coin()` [D1] | Non-deleting destructor |
| 1 | 0x00173290 | `~Coin()` [D0] | Deleting destructor |
| 2 | 0x0019D5FC | `Init()` | Stub/empty |
| 3 | 0x001731F4 | `Release()` | Clears fly emitter |
| 4 | 0x0017312C | `Update(float)` | Fixed-timestep wrapper |
| 5 | 0x00173CC4 | `Draw()` | Render coin model |
| 6 | 0x0017318C | `DrawUpdate(float)` | Empty |
| 7 | 0x0019D600 | `PostLoad()` | Empty |
| 8 | 0x0019D800 | `InRect(ColAABB*)` | Inherited |
| 9 | 0x0019D604 | `CollisionResponse()` | Stub |

## Struct Layout (0x94 = 148 bytes)

Inherits from `Entity` (0x3C = 60 bytes).

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 4 | EntityFns* | vtable | |
| 0x04 | 4 | int | field_0x04 | Entity base |
| 0x08 | 2 | ushort | m_TrackerID | Entity tracker |
| 0x0C | 1 | byte | flags | &0xEE clears active+dead bits |
| 0x10 | 12 | Vec3 | pos | Entity position |
| 0x1C | 12 | Vec3 | vel | Entity velocity |
| 0x28 | 12 | Vec3 | scale | Entity scale (default 0.5,0.5,0.5) |
| 0x36 | 2 | ushort | angle | Launch/movement angle (16-bit idx) |
| 0x38 | 4 | void* | m_Col | Entity collision ptr |
| 0x3C | 4 | int | m_CoinValue | Coin value (passed to AddCoins on arrive) |
| 0x40 | 4 | int | m_State | 0=waiting, 1=arrived, 2=flying, 3=decelerating, 4=homing |
| 0x44 | 4 | float | m_Timer | Delay countdown (state 0), elapsed time (states 3,4) |
| 0x48 | 1 | byte | m_Silent | If nonzero, skip SFX on launch |
| 0x4C | 4 | float | m_Speed | Launch speed |
| 0x50 | 2 | ushort | m_SpinAngle | Y-axis spin for rendering |
| 0x54 | 4 | uint | m_FlyFXHash | StringHash of fly particle effect name |
| 0x58 | 4 | uint | m_CollectFXHash | StringHash of collect particle effect name |
| 0x5C | 4 | float | m_TargetX | Homing target X |
| 0x60 | 4 | float | m_TargetY | Homing target Y |
| 0x64 | 4 | float | m_TargetZ | Homing target Z |
| 0x68 | 4 | PSPParticleEmitter* | m_pFlyEmitter | Fly trail emitter |
| 0x6C | 4 | PSPParticleEmitter* | m_pCollectEmitter | Collect/sparkle emitter |
| 0x70 | 24 | Delegate1<void,Coin*> | m_OnArrived | Callback on arrival |

## State Machine

```
State 0 (WAITING)      → timer countdown, plays "achievement" SFX 
                        → State 2 (immediate transition)
                        → immediately State 4

State 1 (ARRIVED)      → calls Arrived(), entity removed

State 2 (FLYING)       → velocity damped ×0.7/frame
                        → when |vel|² < 900 → State 3

State 3 (DECEL)        → waits 0.05s
                        → spawns sparkle
                        → computes homing angle → State 4

State 4 (HOMING)       → steers toward target
                        → accelerates
                        → when dist < 30 → State 1
```

## Key Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| `Coin::Coin` [C1] | 0x00173394 | constructor | Sets defaults, calls LoadContent |
| `Coin::Coin` [C2] | 0x001732D4 | constructor variant | |
| `~Coin` [D1] | 0x00173218 | destructor | Non-deleting |
| `~Coin` [D0] | 0x00173290 | destructor | Deleting |
| `InitCoin` | 0x00173454 | `(pos, gravity, angle, ..., delay, silent)` | Set position, speed, gravity, delay |
| `MakeCoins` | 0x00173568 | `(totalCoins, coinsPerCoin, ...)` | Spawn N coins via ActorManager::Add(2) |
| `Update` | 0x0017312C | `(float)` | Fixed-timestep wrapper, subdivides by 1/60 |
| `_Update` | 0x00173790 | `(float dt)` | 5-state machine + physics (241 lines) |
| `Draw` | 0x00173CC4 | `()` | Scale × RotY(spin) × RotZ(heading) × Translate |
| `Release` | 0x001731F4 | `()` | Clear fly emitter |
| `Arrived` | 0x00173190 | `()` | Invoke delegate, cleanup, mark dead |
| `ClearCoins` | 0x001731B8 | `(bool arrive)` | Remove all coins, optionally collect them |
| `LoadContent` | 0x00173114 | `()` | Set loaded flag |
| `UnLoadContent` | 0x00173CA8 | `()` | Null out model SmartPtr |
| `CoinArrived` | 0x0017320C | `(Coin*)` | Free function: AddCoins(coin->m_CoinValue) |

## Key Constants

| Value | Usage |
|-------|-------|
| Speed = `(500 + rand(524287)/524287 × 550) × 0.66` | Launch speed formula |
| Gravity default = `Vec3(220, -140, 0)` | Default gravity/target |
| Velocity damping = `0.7` | Per-frame in state 2 |
| Vel threshold = `900` (squared) | State 2→3 transition |
| Decel time = `0.05s` | State 3→4 transition |
| Homing close range = `30.0` | Arrival distance |
| Turn rate = `0.85` base + close boost | State 4 steering |
| Spin speed = `32760 × dt × 500` | Visual spin rate |
| Screen bounds = `[-240,240] × [-160,160]` | Spawn clamping |
| SFX = `"achievement"` | Played on launch |
| Scale = `Vec3(0.5, 0.5, 0.5)` | Coin entity scale |

## Pseudocode for `InitCoin` (0x00173454)

```c
InitCoin(Vec3 pos, Vec3 gravity, ushort baseAngle, int playerIdx,
         ushort launchAngle, int coinValue, char* flyFXName,
         float delay, bool silent) {
    flags &= 0xEE;
    angle = launchAngle;
    m_State = 0;
    m_CoinValue = coinValue;
    m_Speed = (500.0f + (Random(524287) / 524287.0f) * 550.0f) * 0.66f;
    this->pos = pos;
    m_Timer = -delay;
    m_TargetX = gravity.x;
    m_TargetY = gravity.y;
    m_TargetZ = gravity.z;
    vel = Vec3(0,0,0);
    scale = Vec3(0.5, 0.5, 0.5);
    m_FlyFXHash = StringHash(flyFXName);
    m_Silent = silent;
    m_CollectFXHash = StringHash(collectFXName);
    m_pFlyEmitter = NULL;
    m_pCollectEmitter = NULL;
    m_OnArrived = onArrived;
    m_SpinAngle = 0;
}
```

## Pseudocode for `MakeCoins` (0x00173568)

```c
MakeCoins(int totalCoins, int coinsPerCoin, float delayRange,
          ushort baseAngle, ushort angleSpread, Vec3* spawnPos,
          char* flyFXName, char* collectFXName,
          Delegate1 onArrived, bool silent) {
    if (totalCoins <= 0) return;
    float delayStep = delayRange / (totalCoins/coinsPerCoin + 1);
    Vec3 gravity = Vec3(220, -140, 0);  // default
    int idx = 0, remaining = totalCoins;
    while (remaining > 0) {
        Coin* coin = (Coin*)ActorManager::Add(2, true);
        ushort randAngle = baseAngle + Random(angleSpread) - angleSpread/2;
        float x = spawnPos->x + SinIdx(randAngle) * 100.0f;
        float y = spawnPos->y + CosIdx(randAngle) * 100.0f;
        // Retry up to 10× if out of screen bounds
        int coinValue = min(remaining, coinsPerCoin);
        coin->InitCoin(pos, gravity, randAngle, ..., coinValue, delay, silent);
        remaining -= coinsPerCoin;
        idx++;
    }
}
```

## Pseudocode for `Draw` (0x00173CC4)

```c
Draw() {
    if (m_Silent == 0) return;
    if (s_coinModel == NULL) return;
    if (m_State <= 1) return;  // don't draw if waiting or arrived
    Matrix44 mat = Scale(scale);
    mat *= RotY(SinIdx(m_SpinAngle), CosIdx(m_SpinAngle));
    mat *= RotZ(SinIdx(angle), CosIdx(angle));
    mat *= Translate(pos);
    Model::Draw(s_coinModel, &mat);
}
```

## See Also

- [Entity base](entity-base.md) — Mortar::Entity base class
- [Scoring functions](../functions/scoring.md) — Coin::MakeCoins in scoring pipeline
- [Particles](../engine/particles.md) — PSPParticleEmitter used for coin trails
- [Actor Manager](../engine/actor-manager.md) — Entity type 2 = Coin
