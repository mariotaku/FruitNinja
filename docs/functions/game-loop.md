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

Called once from `FruitNinja::OnAppInitializing` (via `ReturnsAnInstanceOfThisMortarGame`).
Sets up ALL engine subsystems and loads all shared data. NOT the same as `GameInit` (0x16c644)
which is the per-session State 2 init handler.

```
GameInitialise(displaySurface, dataPath):
  │  Engine singletons
  ├─ SystemManager::Init
  ├─ MatrixManager::Init
  ├─ FileSystem_Direct → FileManager::AddSystem
  ├─ DisplayManager::SetWindowSize(0, 320, 0, 480) → Init → SetClearColour
  │    SetLightDirection(0, -10, -5)
  ├─ InitialiseData()
  ├─ TextureManager::Initialise (×2)
  ├─ MeshManager::Initialise
  ├─ AnimationManager::Initialise
  ├─ InputManager::Init(0xFFFFFFFE)
  │
  │  Game data
  ├─ PSPParticleManager::LoadFile (particles XML, HD or SD variant)
  ├─ PowerUpManager::Load (poweruplist.xml)
  ├─ LeaderboardManager::Init
  │
  │  Network (skip for port)
  ├─ NetworkManager: SetStatusMessageText ×11, SetGameCenterInitializationCallback
  │   InitializeP2P, SetPreferredNetworkProvider, P2PConnect
  │
  │  Camera + Game singleton
  ├─ FruitCamera(0x16c) → Game+0x48
  ├─ Zero Game fields: +0x50..+0x80 (save data, scores, modes)
  │
  │  Fonts
  ├─ Font::Load → Game+0x54 (main font, HD/SD variant)
  ├─ Font::Load → Game+0x58 (secondary font)
  ├─ Font::Load → Game+0x6c (shared to +0x7c/+0x70/+0x74/+0x78)
  ├─ Font::Load → Game+0x70..+0x78 (optional, if file exists)
  ├─ Font::Load → Game+0x80 (fallback font)
  ├─ Font::Load → Game+0x68 (another fallback)
  │
  │  Shared assets
  ├─ LoadLocalisedTexture → Game+0x17c (fruit atlas)
  ├─ MenuButton::LoadContent
  ├─ Fruit::LoadInfo (FRUIT_INFO from XML)
  ├─ SplatEntity::LoadContent
  ├─ SlashEntity::LoadContent
  ├─ Bomb::LoadContent
  ├─ GameOverScreen::LoadContent
  ├─ PowerUpShop::LoadContent
  └─ PreloadSounds
```

**vs GameInit** (0x16c644, 274 lines): GameInit is the State 2 init handler — creates HUD, entities, screens, pools for each game session. GameInitialise is the one-time boot that creates engine singletons and loads shared data.

| | GameInitialise | GameInit |
|---|---|---|
| **When** | Once at app startup | Each game session |
| **Called by** | OnAppInitializing | GameTaskUpdate state 2 init |
| **Creates** | Engine singletons, loads shared data | Per-session HUD, entities, screens |
| **Port maps to** | `Game::init()` | Entering gameplay |

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

**Note:** `FruitNinja::Draw` (0x001824e0, 98 lines) is the true game loop body despite its name. It runs update, render, buffer swap, FPS tracking, and input/sound polling — all in a single function. The Bada timer fires `OnTimerExpired` every 10ms, which reschedules immediately and delegates everything to `Draw`.

`GlesForm::OnDraw` (0x00183468) also calls `FruitNinja::Draw` as a fallback path when the form is redrawn by the OS.

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

See `docs/structs/game.md` for full 22-step initialization order.

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

