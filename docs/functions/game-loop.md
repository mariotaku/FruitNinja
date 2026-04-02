# Game Loop & State Machine Functions

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

### GameUpdate (0x0016bed0, 358 lines)

See `docs/systems/state-machine.md` for full internal flow.

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

