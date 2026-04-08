# Game Loop & State Machine Functions

## Entry Point Chain

### OspMain (0x000d82a4, 37 instructions)

Anti-tamper wrapper. Hashes 10 bytes from argv[0] using ELF hash (offset 0x12 if byte[1]=='O', else 0x2c). Expected hash = 0x0487BAA3. On match, computes jump target via page-aligned arithmetic: `PC(0xd82ac) - 0xc82a4 → align → + 0x173475 → align → + low12 + 4 = 0x183479` (Thumb). Lands at bootstrap+4, skipping push (tail-call reusing OspMain's register save).

### OspMain_AppBootstrap (0x00183474)

Bada OS boilerplate. Wraps `argv` into an `ArrayList<String>`, then calls `Osp::App::Application::Execute(FruitNinja::CreateInstance, args, AAT_MAIN_APP)`. Returns the framework's result code.

```c
uint OspMain_AppBootstrap(int argc, char** argv) {
    ArrayList* args = new ArrayList();
    args->Construct();
    for (int i = 0; i < argc; i++) {
        args->Add(new String(argv[i]));
    }
    uint ret = Application::Execute(FruitNinja::CreateInstance, args, AAT_MAIN_APP);
    args->RemoveAll(true);
    delete args;
    return (ret != 0) ? (ret & 0xFFFF) : 0;
}
```

### FruitNinja::CreateInstance (0x00182464)

Factory callback passed to `Application::Execute`. GOT pointer at 0x1F37CC resolves to this address.

```c
FruitNinjaApp* FruitNinja::CreateInstance() {
    return new(0x48) FruitNinja();
}
```

### FruitNinja Lifecycle (logical entry point: OnAppInitializing)

The Bada framework drives `FruitNinja` (inherits `Osp::App::Application`) through lifecycle callbacks:

| Callback | Address | Role |
|----------|---------|------|
| `OnAppInitializing` | 0x00182194 | **Logical entry point** — InitEGL, InitGL, GlesForm, Timer(10ms), audio, Game init |
| `OnTimerExpired` | 0x0018269c | Game loop driver → reschedule timer + `FruitNinja::Draw()` (full tick) |
| `OnForeground` | 0x001820b0 | Resume: restart timer, restart audio if not muted, Game::OnResume |
| `OnBackground` | 0x00182060 | Suspend: cancel timer, stop audio, flush audio buffer, Game::OnSuspend |
| `OnAppTerminating` | 0x00182160 | Game::Shutdown, cleanup, `FruitNinja::Cleanup()` |
| `Cleanup` | 0x00182114 | Delete Game singleton, cancel/delete timer, delete GlesForm, `DestroyGL()` |

Other FruitNinja methods:

| Method | Address | Role |
|--------|---------|------|
| `FruitNinja::FruitNinja` | 0x00182488 | Constructor — init Application, IScreenEventListener, ITimerEventListener, set vtables |
| `CreateInstance` | 0x00182470 | Factory — `new FruitNinja()` |
| `Draw` | 0x001824e0 | **Full game tick** (misnamed — does update + render + swap + input + audio) |
| `InitEGL` | 0x00181f80 | EGL setup: display, config, surface, context, makeCurrent |
| `InitGL` | 0x00181e58 | GL setup: viewport, culling, depth, projection matrix |
| `DestroyGL` | 0x00181da0 | Tear down pbuffer, context, surface, display |
| `OnBatteryLevelChanged` | 0x00181d80 | No-op (returns param) |
| `OnScreenOn` | 0x00181d8c | No-op |
| `OnScreenOff` | 0x00181d98 | No-op |
| `~FruitNinja` | 0x001822d8 | Destructor chain |

Non-virtual thunks (adjust `this` for multiple inheritance):

| Thunk | Address | Adjusts | Target |
|-------|---------|---------|--------|
| `OnTimerExpired` thunk | 0x00182694 | `this - 0x10` (ITimerEventListener) | 0x0018269c |
| `OnScreenOff` thunk | 0x00181d90 | `this - 0x0c` (IScreenEventListener) | 0x00181d98 |
| `OnScreenOn` thunk | 0x00181d84 | `this - 0x0c` (IScreenEventListener) | 0x00181d8c |

### FruitNinja Class Layout (0x48 bytes)

```
+0x00: Osp::App::Application      (base, vtable ptr → iVar1 + 0x08)
+0x0c: IScreenEventListener       (vtable ptr → iVar1 + 0x58)
+0x10: ITimerEventListener        (vtable ptr → iVar1 + 0x70)
+0x14: EGLDisplay  m_eglDisplay
+0x18: EGLSurface  m_eglSurface
+0x1c: EGLConfig   m_eglConfig
+0x20: EGLContext   m_eglContext
+0x24: (unused)
+0x28: EGLSurface  m_eglPbuffer   (optional pbuffer surface)
+0x3c: (ptr)        m_pUnk3c      (deleted in Cleanup)
+0x40: Timer*       m_pTimer
+0x44: GlesForm*    m_pGlesForm
```

### Full Call Tree (2 levels) — Entry Point

```
OspMain (0x000d82a4) — anti-tamper hash check
  └─► OspMain_AppBootstrap (0x00183474) — argv wrapping, Application::Execute
        └─► FruitNinja::CreateInstance (0x00182470) — new FruitNinja()
              └─► Bada framework lifecycle:
                    ├─ OnAppInitializing (0x00182194)  ← logical entry point
                    │    ├─ MortarAudioMixerBada()
                    │    ├─ GlesForm::GlesForm(this)
                    │    ├─ Form::Construct()
                    │    ├─ GetAppFrame → AddControl(glesForm)
                    │    ├─ InitEGL (0x00181f80)
                    │    ├─ InitGL (0x00181e58)
                    │    ├─ Timer::Construct → this+0x40
                    │    ├─ MAMAudioController::Init(audioMixer)
                    │    ├─ Check sound mute → StartAudioSubsystem
                    │    ├─ ReturnsAnInstanceOfThisMortarGame()
                    │    │    └─ GameInitialise(displaySurface, dataPath) — see below
                    │    └─ Game::Init(0, 0) via vtable +0x34
                    │
                    ├─ OnTimerExpired (0x0018269c)     ← game loop driver
                    │    ├─ Timer::Start(timer, 10)    ← reschedule 10ms
                    │    └─ FruitNinja::Draw()         ← full game tick (see below)
                    │
                    ├─ OnForeground (0x001820b0) / OnBackground (0x00182060)
                    └─ OnAppTerminating (0x00182160)
```

### GameInitialise (0x0010bdfc, 305 lines) — One-time engine bootstrap

Called once from `FruitNinja::OnAppInitializing` (via `Game_GetInstance`).
Sets up ALL engine subsystems and loads all shared data. NOT the same as `GameInit` (0x16c644)
which is the per-session State 2 init handler.

```
GameInitialise(displaySurface, dataPath):
  │  Engine singletons
  ├─ 1. SystemManager::Init()
  ├─ 2. MatrixManager::Init()
  ├─ 3. FileSystem_Direct = new(0x14), FileManager::AddSystem(fs, 0, 0)
  ├─ 4. DisplayManager::GetInstance()
  │      → ShouldUseHDFonts()
  │      → SetWindowSize(0, 320, 0, 480)   // portrait Bada: 320×480
  │      → Init(displaySurface, dataPath, 0)
  │      → SetClearColour(global)
  │      → SetLightDirection(DAT, -10.0, -5.0)
  ├─ 5. InitialiseData()
  ├─ 6. SetTextureOverloadPrefix("hd/" or "") based on HD/fast hardware
  ├─ 7. TextureManager::Initialise() (×2, possibly legacy)
  ├─ 8. MeshManager::Initialise(0x26C00)   // heap = 158720 bytes
  ├─ 9. AnimationManager::Initialise()
  ├─ 10. InputManager::Init()
  │
  │  Game data
  ├─ 11. PSPParticleManager::LoadFile(xml, fast/slow variant)
  ├─ 12. PowerUpManager::Load()
  ├─ 13. LeaderboardManager::Init()
  │
  │  Network (skip for port)
  ├─ 14. NetworkManager: SetStatusMessageText ×11, GameCenter callback,
  │      InitializeP2P (3 delegates), SetPreferredNetworkProvider, P2PConnect
  │
  │  Camera
  ├─ 15. FruitCamera = new(0x16c)
  │      → FruitCamera::FruitCamera()
  │      → g_GameData+0x48 = camera
  │      → camera->Init(1.0, 10000.0, 16.95, 11.3)  // aspectRatio, farClip, fovX, fovY
  │      → Zero g_GameData: worldPos(+0x90..+0x98), flags(+0x9C..+0x9E), +0x180
  │      → Zero font slots(+0x50..+0x80)
  │
  │  Fonts (g_GameData offsets)
  ├─ 16. Font[0] = new(0x438), Load(main.fnt)          → +0x54
  ├─ 17. Font[1] = new(0x438), Load(secondary.fnt)     → +0x58
  ├─ 18. Font[2] = new(0x438), Load(base.fnt)          → +0x6C
  │      → Copy to +0x7C, +0x70, +0x74, +0x78 (4 region slots default to base)
  ├─ 19. Font[3-5] = optional CJK/Arabic (if file exists) → +0x70, +0x74, +0x78
  ├─ 20. Font[6] = new(0x438), Load(...)               → +0x80
  ├─ 21. Font[7] = new(0x438), Load(...)               → +0x68
  │
  │  Shared assets
  ├─ 22. LoadLocalisedTexture("...") → g_GameData+0x17C  (fruit atlas)
  ├─ 23. MenuButton::LoadContent()
  ├─ 24. Fruit::LoadInfo()             // FRUIT_INFO from Fruit.xml
  ├─ 25. SplatEntity::LoadContent()
  │      SlashEntity::LoadContent()
  │      Bomb::LoadContent()
  │      GameOverScreen::LoadContent()
  │      PowerUpShop::LoadContent()
  └─     PreloadSounds()
```

#### Font Slot Map (g_GameData)

| Offset | Slot | Purpose |
|--------|------|---------|
| +0x54 | Font[0] | Primary game font (HD or SD variant) |
| +0x58 | Font[1] | Secondary font |
| +0x68 | Font[7] | Additional font |
| +0x6C | Font[2] | Base/fallback font |
| +0x70 | Font[3] | CJK override (optional, if file exists) |
| +0x74 | Font[4] | CJK override 2 (optional) |
| +0x78 | Font[5] | Arabic override (optional) |
| +0x7C | (copy) | Defaults to same as Font[2] |
| +0x80 | Font[6] | Additional font |

#### Key Constants

- MeshManager heap: `0x26C00` = 158,720 bytes
- DisplayManager window: `SetWindowSize(0, 320, 0, 480)` — portrait Bada (camera rotation handles landscape)
- Light direction: `(DAT, -10.0, -5.0)`
- FruitCamera size: `0x16C` (364 bytes)
- FruitCamera::Init params: `(1.0, 10000.0, 16.95, 11.3)` — aspect=1.0, farClip=10000.0, fovX=16.95°, fovY=11.3°

### All Game* Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| **Game::Game** | 0x10dab0 | 20 | Constructor: MortarGame base, zero field_0xfc/0xfd/0x100 |
| **GamePreInitialise** | 0x10b588 | 10 | Zeros Game singleton (0x608 bytes via CpuFill8) |
| **GameInitialise** | 0x10bdfc | 305 | One-time engine bootstrap (above) |
| **GameInit** | 0x16c644 | 274 | Per-session init (State 2 handler) |
| **GameTaskUpdate** | 0x10a5d4 | 87 | 3-state dispatcher (Splash/Frontend/Game) |
| **GameUpdate** | 0x16bed0 | 359 | State 2 update — full gameplay loop |
| **GameDraw** | 0x16b888 | 211 | State 2 draw — full render frame |
| **GameTaskDraw** | 0x10a2c4 | 23 | Draw dispatcher (calls state draw handler) |
| **GameTaskExit** | 0x10a320 | 22 | Exit dispatcher (calls state exit handler) |
| **GameTaskInitInput** | 0x169670 | ~100 | Register 16 touch channels + global callbacks |
| **GameExit** | 0x16cf74 | 98 | Per-session cleanup (State 2 exit) |
| **GameOver** | 0x169ed4 | 72 | Create GameOverScreen, set Game+0x05=1 |
| **GameDestroy** | 0x10b7ec | 174 | Full engine teardown (reverse of GameInitialise) |

**Lifecycle order:**

```
App startup:
  Game::Game()          → construct Game singleton
  GamePreInitialise()   → zero 0x608 bytes
  GameInitialise()      → boot all engine singletons + load shared data

Per game session:
  GameInit()            → create HUD, entities, screens, pools
  GameTaskUpdate()      → dispatch to state update handlers
  GameUpdate()          → main gameplay loop (359 lines)
  GameDraw()            → render frame (211 lines)
  GameExit()            → cleanup per-session objects

App shutdown:
  GameDestroy()         → tear down everything (174 lines)
```

**GameDestroy** (0x10b7ec, 174 lines) — reverse of GameInitialise:
1. LeaderboardManager::Destroy, NetworkManager destroy
2. UnLoadContent: FruitFact, About, GameOver, GameMode, MenuButton, Coin, Dojo, Shop, PowerUpShop, Leaderboard
3. AchievementManager::UnLoadAchievementInfo, ItemManager::UnLoadItemData
4. Delete HUD (Game+0x3c), FruitCamera (Game+0x48)
5. Delete all fonts (Game+0x54..0x80)
6. Delete FruitSaveData (Game+0x4c), GameSound (Game+0x188)
7. Clear fruit atlas texture (Game+0x17c)
8. FileManager::ClearSystems, PSPParticleManager::Destroy
9. Cleanup: Bomb, Fruit, Splat, Slash
10. Destroy: InputManager, TextureManager, AnimationManager, MeshManager, DisplayManager, SoundManager, SystemManager

**GamePreInitialise** (0x10b588, 10 lines) — just `CpuFill8(gameSingleton, 0, 0x608)` — zeros the entire Game struct.

---

### Full Call Tree (2 levels) — Game Loop Driver

```
FruitNinja::OnTimerExpired (0x0018269c)
├─ Timer::Start(timer, 10)              ← reschedule 10ms
└─ FruitNinja::Draw (0x001824e0)        ← FULL GAME TICK (misnamed, not just render)
     │
     │  Audio
     ├─ MAMAudioController::GetInstance / Update
     ├─ MAMAudioThread::ThreadMainLoop
     │
     │  GL Context
     ├─ sglMakeCurrent(display, surface, surface, context)
     ├─ glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
     │
     │  Update Phase
     ├─ SystemManager::Update(dt)
     ├─ Game::Update(dt)                ← vtable +0x2c → GameTaskUpdate(dt)
     │    ├─ PowerManager::Update()
     │    ├─ PowerManager::GetState()
     │    ├─ FruitSaveData::Update(saveData, dt, hud)
     │    ├─ initFuncs[state](0)        ← on first frame of new state
     │    ├─ updateFuncs[state](dt, canUpdate)
     │    │    ├─ State 0: SplashUpdate  (0x0016f5d8)
     │    │    ├─ State 1: FrontendUpdate (0x0016eb60)
     │    │    └─ State 2: GameUpdate    (0x0016bed0)
     │    └─ GameTaskExit()             ← on state change
     │
     │  Render Phase
     ├─ DisplayManager::BeginFrame()
     ├─ Game::Draw(dt)                  ← vtable +0x30 → GameTaskDraw(dt)
     │    ├─ drawFuncs[state](1)
     │    │    ├─ State 0: SplashDraw    (0x0016f554)
     │    │    ├─ State 1: FrontendDraw  (0x0016eb28)
     │    │    └─ State 2: GameDraw      (0x0016b888)
     │    └─ clears dt accumulator
     ├─ DisplayManager::EndFrame()
     ├─ DisplayManager::SwapBuffers()
     │
     │  Present
     ├─ glFlush() / glFinish()
     ├─ sglSwapBuffers(display, surface)
     │
     │  Timing
     ├─ FPS calculation (SystemTime::GetTicks, rolling average)
     │
     │  Post-frame
     ├─ Mortar::Touch::Update(0)
     └─ Mortar::SoundManager::Update(0)
```

<!-- Analysed: 2026-04-09T11:00 -->

**Note:** `FruitNinja::Draw` (0x001824e0, 98 lines) is the true game loop body despite its name. It runs update, render, buffer swap, FPS tracking, and input/sound polling — all in a single function. The Bada timer fires `OnTimerExpired` every 10ms, which reschedules immediately and delegates everything to `Draw`.

`GlesForm::OnDraw` (0x00183468) also calls `FruitNinja::Draw` as a fallback path when the form is redrawn by the OS.

### Fixed Timestep (critical for port)

The original `FruitNinja::Draw` does:
```c
float dt = 0.0f;
SystemManager::Update(&dt);  // writes FIXED dt = 1/60
Game::Update(dt);            // passes 1/60 to all game logic
Game::Draw(dt);              // passes 1/60 to all draw logic
```

`SystemManager::Update` (0x0018ade0) writes a **hardcoded constant** `DAT_0018ae84 = 0x3C888889 = 1.0/60.0 ≈ 0.01667` — it never measures actual elapsed time. It also hardcodes `m_LastFrameTime = 59` (0x3b) and all ring buffer entries to 59.

Combined with the 10ms timer (100fps tick rate), the game runs at:
- 100 ticks/sec × 1/60 s/tick = **1.667× game-seconds per real-second**
- All frame-dependent lerps (camera transition `*= 0.125`, bounce physics, state timers) are tuned for this exact rate

Port must match both: fixed dt = 1/60 AND ~10ms frame interval via `SDL_Delay`.

---

## Game Loop & State Machine

### GameTaskUpdate (0x0010a5d4, 87 lines)

```c
// 3-state task dispatcher: Splash(0), Frontend(1), Game(2)
void GameTaskUpdate(float rawDt) {
    while (true) {
        // Clamp dt
        float dt = (rawDt > 0 && rawDt < MAX_DT) ? rawDt * SCALE : DEFAULT_DT;
        float frameMs = dt * MS_SCALE;
        
        Game* game = GetGameSingleton();
        TaskState* task = GetTaskState();
        byte stateIdx = game->state;  // Game[0]
        
        game->dt = dt;
        game->m_FrameTimer += (int)frameMs;
        game->m_bFrameDirty = 0;
        task->totalTime += dt;
        
        if (!task->initialized) {
            // Call init handler: initFuncs[stateIdx](0)
            task->prevState = stateIdx;
            initFuncs[stateIdx](0);
            task->initialized = true;
            if (!stateChangeRequested) return;
            stateChangeRequested = false;
            continue;  // re-enter loop for new state
        }
        
        if (stateIdx == task->prevState) {
            // Same state: run update
            PowerManager::Update();
            int canUpdate = (game->field02 == 0) ? (1 - PowerManager::GetState()) : 0;
            if (game->m_BombHitTimer <= 0)
                FruitSaveData::Update(game->pSaveData, dt, game->pHUD);
            updateFuncs[stateIdx](dt, canUpdate);
        } else {
            // State changed: exit old, loop will init new
            GameTaskExit();
            task->prevState = stateIdx;
            game->state = stateIdx;
        }
        return;
    }
}
```

### GameTaskDraw (0x0010a2c4, 23 lines)

| Address | Signature |
|---------|-----------|
| 0x0010a2c4 | `void GameTaskDraw(float dt)` |

### GameTaskExit (0x0010a320, 22 lines)

| Address | Signature |
|---------|-----------|
| 0x0010a320 | `void GameTaskExit()` |

### State Handlers

| State | Init | Update | Draw | Exit |
|-------|------|--------|------|------|
| 0 Splash | 0x0016f648 | 0x0016f5d8 | 0x0016f554 | 0x0016f59c |
| 1 Frontend | 0x0016ebb4 | 0x0016eb60 | 0x0016eb28 | 0x0016eb2c |
| 2 Game | 0x0016c644 | 0x0016bed0 | 0x0016b888 | 0x0016cf74 |

### GameInit (0x0016c644, 274 lines)

<!-- Analysed: 2026-04-08T12:00 -->

Per-session setup. Called by the state machine when entering State 2. Full call order:

1. **Guard**: check `g_GameData+0x112` (already-inited flag) — skip everything if set
2. **HUD**: `new HUD()` → `Game+0x3c`; call `HUD::Release` first (handles re-entry)
3. **MissControl ×3**: position table at `g_GameData+0x30` (GOT+DAT_0016c9dc+0x30), `playerScale=1.0`. Constructs three `MissControl` at offsets `[0,1,2]`, sets `m_AnimState`, then `MissControl::CreatePool(12, hud)`
4. **ScoreControl**: `new ScoreControl(0x100)`. Loads 3 textures (GOT+DAT_0016c9e0/e4/e8). Sizes from `DAT_0016c9b8`. Positions from `DisplayManager::GetWindowSize()` scaled by constants. Added to HUD.
5. **CoinCounter**: `new CoinCounter(0xd4)` → `Game+0x178`. Calls `LoadContent()` via vtable[2].
6. **TimeControl**: `new TimeControl(0x108)` → `Game+0x180`. Calls `LoadContent()` via vtable[2]. `TimeControl::CountDown(DAT_0016c9cc)`. `Game+0x184 = 0`.
7. **Background texture** (lines 159-170): Guard on `g_TaskState+0xfc` (SmartPtr bool):
   - If already loaded: skip
   - `IsFastHardware()` → fast: `"gb_game.tex"` (DAT_0016c9f0 → 0x001BC923); slow: `"gb_game_sml.tex"` (DAT_0016c9f4 → 0x001BC92F)
   - GOT base = 0x001EC130 (literal at 0x16c9d0 = 0x0007FADC, added to 0x16c654)
   - Loads via `TextureManager::LoadLocalisedTexture` → stores at `g_TaskState+0xfc`
8. **Mesh loads**: `MeshManager::Load(AsciiString(GOT+DAT_0016c9f8))` → `g_TaskState+0xbc`. Second mesh `GOT+DAT_0016cca4` → `g_TaskState+0xc0`.
9. **SliceEffect list**: `new List<SliceEffect>` → `g_TaskState+0x64`; `clear()`. Pool `new MemoryPool(100)` → `g_TaskState+0xc8`.
10. **Flags**: `g_TaskState+0x112 = 1` (inited), `g_TaskState+0x114 = g_TaskState2+0x54`, `g_TaskState+0x111 = 0`, `g_TaskState+0xc = 0`.
11. **MainScreen**: `new MainScreen(0x120)` → `g_TaskState+0x1c`. `LoadContent()`. `g_TaskState+0x1c+0x32 = 1`. `Game+0x160 = mainScreen`.
12. **PauseScreen**: `new PauseScreen(0xd8)` → `g_TaskState+0x04`. `LoadContent()`.
13. **TutorialControl**: `new TutorialControl(0xa0)` → `Game+0x168`. `LoadContent()`. `Game+0x05 = 1`. `Game+0x0c = 0xbf800000` (-1.0f).
14. **HUD::AddControl**: MainScreen, PauseScreen, TutorialControl added to HUD (Game+0x3c).
15. **Entity heap**: `Entity::HeapCreate(0x20000)`.
16. **ActorManager**: `GetInstance()`. `Initialise(5, 0x2000)`. RegisterFactory delegate. RegisterHashConverter delegate.
17. **WaveManager**: `GetInstance()`. `Init()`.
18. **GameTaskInitInput()**.
19. **Pre-spawn loop ×30**: For each iteration: `ActorManager::Add(0, true)` flags|=0x11, `Add(1, true)` flags|=0x11, `Add(4, true)` flags|=0x11.
20. **SplatEntity::CreatePool(0x80)**.
21. **WaveManager::Resume()**.
22. **BombFlash::CreatePool(0x20)**.
23. **SoundManager**: `Initialise(GOT+DAT_0016ccc4)`. `SetSFXVolume(0.5 if soundOn, else DAT_0016cca0)` — reads `Game+0x44` (m_bSoundOn).

### GameUpdate (0x0016bed0, 359 lines)

**Full call tree and analysis in [game-update.md](game-update.md)** — time scaling pipeline, bomb hit logic, wave speed acceleration, pause behavior, retry system.

### GameDraw (0x0016b888, 211 lines)

```c
// Full render frame — see docs/systems/rendering.md for layer diagram
void GameDraw(float dt, bool active) {
    if (!active) { HUD::Draw(hud, 0x400); return; }
    if (!DisplayManager::IsRenderingAllowed()) return;
    
    // Save HUD pos
    Vec3 savedPos = hud->pos;
    
    // Setup
    SetDepthBuffer(false); SetDepthBufferWrite(false);
    SetGlobalAmbience(COLOUR_DARK);
    SetLightDirection(game->worldPos);
    FruitCamera::SetupPerspective(cam, 0, false);
    
    // Background quad
    Texture::Set(bgTex);
    ResetMatrixStack();
    if (cam->shakeX == 0 && cam->shakeY == 0)
        Scale+Translate → DrawQuad(normal);
    else
        Scale+Translate → DrawQuad(shake offset);
    Texture::UnSet(bgTex);
    if (!LoadingJob::CanBoot()) { DrawStartFade(); return; }
    
    // 3D entities
    SetDepthBuffer(true); SetDepthBufferWrite(true);
    SetGlobalAmbience(COLOUR_MEDIUM);
    ActorManager::Draw();  // all Fruit + Bomb meshes
    
    // 2D overlays (layered)
    HUD::BeginDraw(dt);
    HUD::Draw(hud, 0x40);           // combo text
    SplatEntity::DrawActiveSplats(); // juice on background
    Fruit::DrawShadows();
    SlashEntity::PreDraw();          // blade trail setup
    BombBlast::DrawActiveBlasts();
    BombFlash::DrawActiveFlashes();
    HUD::Draw(hud, 0x80);
    PSPParticleManager::Draw(dt, paused, -1);  // bg particles
    for (i = 0..15) slash[i]->Draw();          // blade trails
    PSPParticleManager::Draw(dt, paused, 0);   // mid particles
    DrawSlices(dt);                  // slice line effects
    HUD::Draw(hud, 0x01);           // foreground UI
    PSPParticleManager::Draw(dt, paused, 1);   // fg particles
    WaveManager::Draw(0);
    HUD::Draw(hud, 0x08);
    
    // Post-processing
    MainScreen::DrawPostEffects();
    if (fastHW && critTimer > 0) DrawCritHit();
    HUD::Draw(hud, 0x100);
    if (bombTimer > 0) DrawBombHit();
    HUD::Draw(hud, 0x200);
    
    // Restore + final
    hud->pos = savedPos;
    if (fadeTimer > 0) DrawStartFade();
    HUD::Draw(hud, 0x400);  // topmost: pause, dialogs
}
```

---

