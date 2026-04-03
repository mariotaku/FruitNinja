# GameUpdate — State 2 Update Handler

## GameUpdate (0x0016bed0, 359 lines)

| Address | Signature |
|---------|-----------|
| 0x0016bed0 | `void GameUpdate(float dt, bool active)` |

Called via: `Game::Update` (vtable +0x2c) → `GameTaskUpdate` (0x0010a5d4) → `updateFuncs[2](dt, canUpdate)`

This is the main gameplay update function — runs every frame when the game state machine is in State 2 (playing).

### Call Tree (2 levels)

```
GameUpdate (0x0016bed0)
│
│  HUD Deferred Control
├─ HUD::AddControl(pendingControl)           ← flush queued HUD control from Game+0x100
│
│  Menu Return Timer (Game+0xdc)
├─ [if timer > 0] countdown, on expire:
│    ├─ Jump table by Game+0x220+0x10 byte (states 10–13)
│    └─ NetworkManager::OpenUrl              ← fallback: open leaderboard URL
│
│  Slow-mo Accumulators (Game+0xa8..+0xc0, 4 slots × 0x0c each)
├─ Reset each slot: if > 0 → set 0, if == 0 → set -1
│
│  Loading / Fade-in (fade at Game+0x220+0x1c)
├─ [if fade > 0]
│    ├─ Load localised texture → SmartPtr<Texture> at Game+0xf4
│    ├─ LoadingJob::CanBoot() → wait for assets
│    ├─ Fade countdown: fade -= dt × 2.0
│    └─ [if fade ≤ 0] release texture (SmartPtrNull)
│
│  Input + Sound + Items (always, after fade)
├─ InputManager::Update(dt)
├─ SoundManager::Update(dt)
├─ GameSound::Update()                       ← game-specific sound logic
├─ UpdateMusic(dt)                           ← music fade/crossfade
├─ ItemManager::Update(dt)                   ← power-up item spawning
├─ UpdateUpsideDown(dt)                      ← gyro/orientation effect
│
│  ═══════════════════════════════════════════
│  ACTIVE PATH (active == true)
│  ═══════════════════════════════════════════
│
│  Time Scaling
├─ Compute effectiveDt = dt × slowMoFactor × bombFreezeDecay
│    ├─ slowMoFactor: Game+0x218 (from Frenzy power-up), decays per frame
│    ├─ bombFreezeDecay: Game+0x220+0x24, decays at rate 10.0/s (or 5.0 if flag)
│    └─ Written back to Game+0x38 (dt used by rendering)
│
│  Entity Updates
├─ SlashEntity::PreUpdate(dt)                ← blade physics prep
├─ SplatEntity::UpdateActiveSplats(dt)       ← juice splat alpha decay
│
│  Bomb Hit Branch (Game+0x10 > 0 = bomb timer active)
├─ [if bombHitTimer > 0]
│    ├─ [if bombHitTimer countdown flag] Game+0x35 = 1 (freeze spawning)
│    ├─ bombHitTimer -= effectiveDt
│    ├─ [if mode == Classic && Game+0x0c < 1.0] double drain speed
│    ├─ UpdateBombHit(timer)                 ← screen shake, flash, HUD effects
│    └─ [if timer crosses 1.5 → 0] GameOver(-1, -1.0, -1)  ← classic: bomb = death
│
│  Normal Update Branch (bombHitTimer ≤ 0)
├─ [else]
│    ├─ WaveManager::Update(effectiveDt)     ← spawn fruits/bombs per wave schedule
│    └─ waveDt = WaveManager::GetWavedt(0)   ← current wave speed multiplier
│
│  Common Active Updates
├─ BombFlash::UpdateActiveFlashes(dt)
├─ ActorManager::Update(dt)                  ← all Fruit + Bomb entity physics/collision
├─ [if multiplayer] Fruit::CheckFruitDropped()
│
│  ═══════════════════════════════════════════
│  PAUSED PATH (active == false)
│  ═══════════════════════════════════════════
│
├─ Check Game+0x0c for pause-allowed state
├─ SlashEntity::PreUpdate(0)
├─ [loop 16×] SlashEntity[i]→Update(dt) + Draw(dt)  ← keep blade trails alive
├─ WaveManager::Update(0)                   ← keep wave state consistent
│
│  ═══════════════════════════════════════════
│  POST-UPDATE (always, both paths)
│  ═══════════════════════════════════════════
│
│  Wave Speed → Particle Scaling
├─ waveSpeed = WaveManager::GetWavedt(0)
├─ [if not paused] particleTimeScale = clamp(1.0 / waveSpeed, min=1.0)
│
│  Particles + Camera + HUD
├─ PSPParticleManager::Update(dt / particleTimeScale, paused)
├─ FruitCamera::UpdateShake(camera, dt)
├─ HUD::Update(hud, dt)
│    └─ [if time-scaled] sub-stepped in 0.004s increments to prevent UI jitter
│
│  Wave Speed Touch Acceleration
├─ [if time-scaled gameplay]
│    ├─ IsSingleTouchPressed() → multiply waveSpeed ×2
│    └─ Clamp waveSpeed to max 5.0
│
│  Bomb Warning SFX
├─ [if bombs visible & not game over & not paused]
│    ├─ GameSound::SFXPlay("bomb_warning") if not already playing
│    └─ MortarSound::SetVolume(height-based: higher bomb = louder)
├─ [else] set volume to 0 (silence warning)
│
│  Retry System
├─ [if Game+0x06 retry flag]
│    ├─ RetryUpdate(dt)                      ← retry countdown animation
│    └─ [if timer ≤ 0] EndRetryLevel()       ← reset and restart wave
│
│  Menu Return
└─ [if Game+0x1a0 menuReturnTimer > 0]
     ├─ menuReturnTimer -= dt
     └─ [if ≤ 0] CleanupAndReturnToMainMenu()
```

### Key Design Notes

**Time scaling pipeline:**
The function maintains multiple time scales simultaneously:
1. `dt` — raw frame delta from timer (clamped in GameTaskUpdate)
2. `scaledDt` — after slow-mo factor (Frenzy power-up at Game+0x218)
3. `effectiveDt` — after bomb freeze decay (Game+0x220+0x24)
4. Entities get `effectiveDt`, particles get `dt / waveSpeed`, HUD gets sub-stepped `effectiveDt`

**Bomb hit timer (Game+0x10):**
- Set to ~2.0s by `HitBomb()`
- Drains at `effectiveDt` rate (doubled in Classic if Game+0x0c < 1.0)
- Crosses 1.5s threshold → `GameOver(-1)` in Classic mode
- During countdown: spawning frozen (Game+0x35 = 1), screen effects active

**Pause behavior:**
When `active == false` (from PowerManager or Game+0x02):
- Slash entities still update/draw (blade trails persist)
- WaveManager gets dt=0 (paused but state maintained)
- 16 slash entity slots iterated via vtable Update+Draw calls

**Wave speed acceleration:**
During Frenzy (time-scaled), touching the screen doubles wave speed up to 5.0×. This creates the "swipe faster = more fruit" feedback loop.

### Related Functions

| Function | Address | Called From |
|----------|---------|-------------|
| `UpdateBombHit` | 0x0016af14 | GameUpdate (bomb timer branch) |
| `UpdateMusic` | — | GameUpdate (music fade) |
| `UpdateUpsideDown` | — | GameUpdate (orientation) |
| `GameOver` | 0x00169ed4 | GameUpdate (bomb timer expires) |
| `RetryUpdate` | 0x0016afdc | GameUpdate (retry countdown) |
| `EndRetryLevel` | — | GameUpdate (retry complete) |
| `CleanupAndReturnToMainMenu` | — | GameUpdate (menu return timer) |
| `IsMultiplayer` | — | GameUpdate (fruit drop check) |
| `IsSingleTouchPressed` | — | GameUpdate (wave acceleration) |

### See Also

- [game-loop.md](game-loop.md) — GameTaskUpdate dispatcher, state handler table, entry point chain
- [game-flow.md](game-flow.md) — HitBomb, GameOver, QuitToMenu, SaveCurrentData
- [wave.md](wave.md) — WaveManager::Update, spawn system
- [fruit.md](fruit.md) — Fruit::Update, collision
- [slash-entity.md](slash-entity.md) — SlashEntity::PreUpdate, blade physics
- [power-ups.md](power-ups.md) — Frenzy slow-mo, PowerManager
- [scoring.md](scoring.md) — BonusManager, score pipeline
