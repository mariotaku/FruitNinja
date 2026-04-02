# Game State Machine

## Game State Machine

### Architecture

`GameTaskUpdate(float dt)` is a 3-state machine dispatching through function pointer tables. Each state has 4 handlers: Init, Update, Draw, Exit.

```
Table layout (3 entries × 4 bytes each = 0x0c per sub-table):
  +0x00: draw[0],   draw[1],   draw[2]
  +0x0c: exit[0],   exit[1],   exit[2]
  +0x18: update[0], update[1], update[2]
  +0x24: init[0],   init[1],   init[2]
```

State transitions: write new state index to `Game[0]` (the first byte of the Game singleton). `GameTaskUpdate` detects the change, calls old state's Exit, then next iteration calls new state's Init.

### State 0: Splash (SplashTask.cpp)

| Handler | Address | Notes |
|---------|---------|-------|
| SplashInit | 0x0016f648 | Creates MenuBackground, sets up camera (IdleCamera), registers input callbacks |
| SplashUpdate | 0x0016f5d8 | Updates InputManager, SoundManager; increments frame counter; calls SplashInputEvent(NULL) |
| SplashDraw | 0x0016f554 | (stub) |
| SplashExit | 0x0016f59c | Clears input actions, destroys MenuBackground |
| SplashInputEvent | 0x0016f558 | When ready flag is clear: sets `Game[0] = 2` → transitions to Game |

### State 1: Frontend (FrontendTask.cpp)

| Handler | Address | Notes |
|---------|---------|-------|
| FrontendInit | 0x0016ebb4 | Creates MenuBackground, sets camera, waits for DisplayManager light direction |
| FrontendUpdate | 0x0016eb60 | Updates InputManager, SoundManager; when `state[5] != 0`: sets `Game[0] = 2` |
| FrontendDraw | 0x0016eb28 | (stub) |
| FrontendExit | 0x0016eb2c | (stub) |

### State 2: Game (GameTask.cpp)

| Handler | Address | Lines | Notes |
|---------|---------|-------|-------|
| GameInit | 0x0016c644 | 274 | Creates HUD, MissControl pool, SlashEntities, WaveManager init, registers ActorManager factories, sets up entities, loads sounds |
| GameUpdate | 0x0016bed0 | 358 | The main gameplay loop (see sub-states below) |
| GameDraw | 0x0016b888 | 211 | Camera setup, background, ActorManager::Draw, particles, HUD, splats, slices |
| GameExit | 0x0016cf74 | 98 | Saves data, clears coins, releases HUD, destroys pools |

### Normal Flow

```
State 0 (Splash) ──auto──> State 2 (Game)
                            │
                            ├─ Menus (DojoScreen, GameModeScreen, ShopScreen)
                            ├─ Gameplay (WaveManager spawning, slicing, scoring)
                            ├─ GameOver (GameOverScreen overlay)
                            └─ Back to menu (CleanupAndReturnToMainMenu)
```

State 1 (Frontend) is an alternate entry path — possibly for debug or cold-start scenarios. Normal flow is `Splash → Game` directly.

### Game Sub-States (within State 2)

All managed by Game singleton fields, not by the top-level state machine:

| Field | Type | Purpose |
|-------|------|---------|
| Game+0x02 | byte | Game running flag (0=paused) |
| Game+0x04 | byte | gameMode (0=Classic, 1=Arcade, 2=Zen, 3=?) |
| Game+0x05 | byte | Quit/pause flag (1=show menu, set by QuitToMenu) |
| Game+0x06 | byte | Retry flag (triggers RetryUpdate loop) |
| Game+0x08 | float | Retry countdown timer |
| Game+0x0c | float | Transition timer |
| Game+0x10 | float | Bomb hit timer (2.0f on bomb hit; GameOver at 1.5; HitBomb/HitMenuBomb) |
| Game+0x35 | byte | Slow-motion flag |
| Game+0x164 | GameOverScreen* | Non-null = game over overlay active |
| Game+0x1a0 | float | Menu-return countdown (set by QuitToMenu; CleanupAndReturnToMainMenu on zero) |

### Key Transition Functions

| Function | Address | What it does |
|----------|---------|--------------|
| QuitToMenu | 0x00169e50 | Sets Game+0x05=1, Game+0x1a0=countdown, resets speed/score |
| CleanupAndReturnToMainMenu | 0x0016b2dc | Destroys entities, calls QuitToMenu or GameOverScreen::QuitCallback |
| HitBomb | 0x0016b0fc | Sets Game+0x10=timer, camera shake, plays SFX; in Zen: -10 pts |
| HitMenuBomb | 0x0016b234 | Sets Game+0x10=2.0f, task_state+0xf8=1 (menu bomb flag) |
| GameOver | 0x00169ed4 | Creates GameOverScreen at Game+0x164, sets Game+0x05=1 |
| RetryLevel | 0x0016b008 | Sets retry flag |
| EndRetryLevel | 0x0016a208 | Clears retry state |
| PrepareForLevelStart | 0x00169a9c | Resets game entities for new round |

### GameUpdate Internal Flow

```
1. Check deferred HUD control queue
2. Handle notification countdown (upsell/promo URLs)
3. Clear per-frame flags (Game+0x9c, Game+0x9d)
4. Reset acceleration damping per 12-byte slots (Game+0xa8..0xc0)
5. If loading: update texture loading; wait for LoadingJob::CanBoot()
6. InputManager::Update, SoundManager::Update, GameSound::Update
7. If active (param_2=true):
   a. Update slow-time multiplier
   b. SlashEntity::PreUpdate
   c. SplatEntity::UpdateActiveSplats
   d. If Game+0x10 <= 0 (no bomb hit):
      - WaveManager::Update (spawn waves)
      - Physics tick: ActorManager::Update → Fruit/Bomb Update
   e. If Game+0x10 > 0 (bomb hit countdown):
      - Count down timer
      - At 1.5s: GameOver(-1, -1, -1)
      - UpdateBombHit visual effects
   f. BombFlash::UpdateActiveFlashes
   g. PSPParticleManager::Update
   h. FruitCamera::UpdateShake
   i. HUD::Update (combo controls, score, etc.)
8. Handle bomb proximity warning SFX
9. If retry active: RetryUpdate / EndRetryLevel
10. If Game+0x1a0 countdown expires: CleanupAndReturnToMainMenu()
```

---
