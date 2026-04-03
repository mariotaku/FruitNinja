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

| Handler | Address | Lines | Notes |
|---------|---------|-------|-------|
| SplashInit | 0x0016f648 | 67 | Creates MenuBackground, camera setup, input config, register callbacks |
| SplashUpdate | 0x0016f5d8 | 22 | InputManager + SoundManager + GameSound update; MenuBackground::Update; SplashInputEvent |
| SplashDraw | 0x0016f554 | 1 | No-op (returns param) |
| SplashExit | 0x0016f59c | 16 | ClearActions, destroy MenuBackground, reset flags |

**SplashInit call tree:**
```
SplashInit(0)
├─ [if not already initialized]
│    ├─ MenuBackground::MenuBackground() → MenuBackground::Init(true)
│    ├─ FruitCamera::SetScale(1.0, 1.0, 1.0)      ← vtable +0x24
│    ├─ FruitCamera::SetPosition(1.0, ?, 10.0)     ← vtable +0x2c
│    ├─ FruitCamera::SetRotation(1.0, 1.0, 1.0)    ← vtable +0x34
│    ├─ FruitCamera::IdleCamera()
│    ├─ Game+0x02 = 0 (paused)
│    ├─ InputManager::LoadConfigFile(...)
│    ├─ InputManager::RegisterInputCallback(StringHash("..."), callback1)
│    ├─ InputManager::RegisterInputCallback(StringHash("..."), callback2)
│    └─ Set initialized flag = 1
```

**SplashUpdate call tree:**
```
SplashUpdate(dt, active)
├─ InputManager::Update(dt)
├─ SoundManager::Update(dt)
├─ GameSound::Update()
├─ frameCounter++
└─ [if active]
     ├─ Clear stateChange flag
     ├─ MenuBackground::Update(dt)
     └─ SplashInputEvent(NULL)
          └─ [if ready flag == 0] → Game[0] = 2  (transition to Game)
```

**SplashInputEvent** (0x0016f558): Checks a flag; when clear, writes 2 to the Game state byte, triggering transition to State 2 (Game). Called every active frame — the flag controls when the splash is "done."

### State 1: Frontend (FrontendTask.cpp)

| Handler | Address | Lines | Notes |
|---------|---------|-------|-------|
| FrontendInit | 0x0016ebb4 | 54 | MenuBackground, camera, busy-wait for DisplayManager ready |
| FrontendUpdate | 0x0016eb60 | 18 | InputManager + SoundManager + GameSound; flag check → transition |
| FrontendDraw | 0x0016eb28 | 1 | No-op (returns param) |
| FrontendExit | 0x0016eb2c | 13 | ClearActions, destroy MenuBackground, reset flags |

**FrontendInit call tree:**
```
FrontendInit(0)
├─ [if not already initialized]
│    ├─ MenuBackground::MenuBackground() → MenuBackground::Init(false)  ← note: false vs Splash's true
│    ├─ FruitCamera::SetScale / SetPosition / SetRotation
│    ├─ while (DisplayManager::SetLightDirection(0, -1, -1) == 0)
│    │    └─ PowerManager::Update()   ← busy-wait for display ready
│    ├─ FruitCamera::IdleCamera()
│    ├─ Game+0x02 = 0 (paused)
│    └─ Set initialized flag = 1
```

**FrontendUpdate call tree:**
```
FrontendUpdate(dt, active)
├─ InputManager::Update(dt)
├─ SoundManager::Update(dt)
├─ GameSound::Update()
└─ [if state+5 != 0] → Game[0] = 2  (transition to Game)
```

Frontend is an alternate entry path. It differs from Splash in:
- `MenuBackground::Init(false)` vs `Init(true)` — different background mode
- Busy-waits for `DisplayManager::SetLightDirection` to succeed (display ready)
- No input callbacks registered
- Transition triggered by external flag (state+5) instead of SplashInputEvent

### State 2: Game (GameTask.cpp)

| Handler | Address | Lines | Notes |
|---------|---------|-------|-------|
| GameInit | 0x0016c644 | 274 | Full game setup — HUD, entities, wave manager, sounds |
| GameUpdate | 0x0016bed0 | 359 | Main gameplay loop — see [game-update.md](../functions/game-update.md) |
| GameDraw | 0x0016b888 | 211 | Full render frame — see [game-loop.md](../functions/game-loop.md) |
| GameExit | 0x0016cf74 | 98 | Save, cleanup all subsystems |

**GameInit call tree:**
```
GameInit(0)
├─ [if not already initialized (task+0x112)]
│    │
│    │  HUD Setup
│    ├─ HUD::HUD() → Game+0x3c
│    ├─ HUD::Release()
│    ├─ [loop 3×] MissControl::MissControl() → position, scale, add to HUD
│    ├─ MissControl::CreatePool(12, hud)
│    ├─ ScoreControl::ScoreControl() → load 3 localised textures, position, add to HUD
│    ├─ CoinCounter::CoinCounter() → Game+0x178, Init, add to HUD
│    ├─ TimeControl::TimeControl() → Game+0x180, Init, CountDown(timer), add to HUD
│    │
│    │  Textures + Models
│    ├─ [if fast HW] Load localised texture → task+0xfc
│    ├─ MeshManager::Load(model1) → task+0xbc
│    ├─ MeshManager::Load(model2) → task+0xc0
│    │
│    │  Slice Effects
│    ├─ List<SliceEffect> → task+100
│    ├─ MemoryPool<SliceEffect::Node>::Create(100) → task+200
│    │
│    │  Screens
│    ├─ MainScreen::MainScreen() → task+0x1c, Init, Game+0x160
│    ├─ PauseScreen::PauseScreen() → task+0x04, Init
│    ├─ TutorialControl::TutorialControl() → Game+0x168, Init
│    ├─ Add MainScreen + PauseScreen + TutorialControl to HUD
│    │
│    │  Entity System
│    ├─ Entity::HeapCreate(0x20000)        ← 128KB entity heap
│    ├─ ActorManager::Initialise(5, 0x2000) ← 5 types, 8192 pool
│    ├─ ActorManager::RegisterFactory(createEntityDelegate)
│    ├─ ActorManager::RegisterHashConverter(hashConverterDelegate)
│    │
│    │  Pre-spawn Entities
│    ├─ [loop 30×]
│    │    ├─ ActorManager::Add(0, true)  ← type 0 (Fruit), flags |= 0x11
│    │    ├─ ActorManager::Add(1, true)  ← type 1 (Bomb), flags |= 0x11
│    │    └─ ActorManager::Add(4, true)  ← type 4 (?), flags |= 0x11
│    │
│    │  Pools + Wave + Sound
│    ├─ SplatEntity::CreatePool(128)
│    ├─ WaveManager::Init() → WaveManager::Resume()
│    ├─ BombFlash::CreatePool(32)
│    ├─ SoundManager::Initialise(soundConfig)
│    ├─ SoundManager::SetSFXVolume(0.5 or custom if Game+0x44)
│    │
│    │  Flags
│    ├─ task+0x112 = 1 (initialized)
│    ├─ Game+0x02 = 0, Game+0x05 = 1
│    └─ Game+0x0c = -1.0 (transition timer)
```

**GameExit call tree:**
```
GameExit()
├─ Coin::ClearCoins(true)
├─ [if not saving] HUD::Save() + SaveCurrentData(true)
├─ Release bomb warning SFX
├─ Reset task flags (0x113, 0x0c, 0x114, 0x118)
├─ SmartPtrNull × 3 (release models task+0xbc/0xc0/0xc4)
├─ Clear 16 slash entity slots (task+0x24..0x64)
├─ List<SliceEffect>::clear + delete
├─ MemoryPool<SliceEffect::Node>::delete
├─ WaveManager::Destroy()
├─ SmartPtrNull texture (task+0x10c)
├─ PSPParticleManager::ClearEmitters()
├─ InputManager::ClearActions(0)
├─ HUD::Release()
├─ Delete MainScreen (task+0x1c)
├─ HUD::~HUD + delete → Game+0x3c = 0
├─ MissControl::CleanPool()
├─ ActorManager::Clear + ClearAllListeners + Destroy
├─ Entity::HeapDestroy()
├─ Game+0x02 = 0, task+0x112 = 0
└─ SmartPtrNull × 3 (textures task+0xfc/0x11c/0xf4)
```

### Normal Flow

```
State 0 (Splash) ──SplashInputEvent──> State 2 (Game)
                                        │
State 1 (Frontend) ──flag+5 set──────> State 2 (Game)
                                        │
                                        ├─ GameInit: HUD, entities, screens, wave, sound
                                        ├─ MainScreen (DojoScreen, GameModeScreen, ShopScreen)
                                        ├─ Gameplay (WaveManager spawning, slicing, scoring)
                                        ├─ GameOver (GameOverScreen overlay)
                                        ├─ Back to menu (CleanupAndReturnToMainMenu)
                                        └─ GameExit: save, destroy all pools/entities
```

Both Splash and Frontend transition directly to State 2 (Game). Splash is the normal boot path; Frontend is an alternate entry (different MenuBackground mode, waits for display ready, no input callbacks). Neither state renders anything — `SplashDraw` and `FrontendDraw` are no-ops; all rendering happens in `FruitNinja::Draw` → `GameTaskDraw`.

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

## See Also

- [Game loop functions](../functions/game-loop.md) -- GameUpdate state dispatch
- [Game struct](../structs/game.md) -- GameTaskState field layout
