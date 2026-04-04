# Game & Application Structs

## Game : Mortar::MortarGame (singleton, size ≥ 0x604)

MortarGame base zeroes +0x04..+0x1b3 in ctor.

### MortarGame vtable (at 0x001eae58, 16 entries)

| Index | Address | Method | Notes |
|-------|---------|--------|-------|
| 0 | 0x0010d9d0 | GetHardwareString | Returns device string |
| 1 | 0x0010d9d4 | IsFastHardware | Returns bool |
| 2 | 0x0018aa14 | RenderAtHalfFrames | Stub |
| 3 | 0x0018ac80 | GetHighResolutionScale | Returns 1.0f |
| 4 | 0x0018ac88 | GetOpenFeintProductKey | Online service (defunct) |
| 5 | 0x0018ac8c | GetOpenFeintSecret | Online service (defunct) |
| 6 | 0x0018ac90 | GetOpenDisplayName | Online service (defunct) |
| 7 | 0x0018ac94 | GetPlayhavenToken | Online service (defunct) |
| 8 | 0x0010d9dc | GetCacheDataArchive | Data path |
| 9 | 0x0018ac98 | CreateFileSystems | Mounts data archives |
| 10 | 0x0018aa28 | TellGameToStart | Called after engine init |
| 11 | 0x0018aa1c | Update(float dt) | Per-frame update |
| 12 | 0x0018aa18 | Draw(float dt) | Per-frame render |
| 13 | 0x0018aa20 | Init(int argc, char** argv) | One-time init |
| 14 | 0x0018aa24 | End | Shutdown |
| 15 | 0x0018aa2c | Paused | Pause event handler |

Other MortarGame functions:

| Address | Function | Notes |
|---------|----------|-------|
| 0x0018ab6c | MortarGame::MortarGame() | Constructor (zeroes fields, sets version) |
| 0x0018abe8 | MortarGame::MortarGame() | Constructor variant (with operator_delete) |
| 0x0018ac64 | TellGameToQuit | Calls SystemManager::QuitGame() |
| 0x000f4200 | ReturnsAnInstanceOfThisMortarGame | Singleton getter (returns Game*) |
| 0x0010d674 | ReturnsAnInstanceOfThisMortarGame | Singleton getter (variant) |

### Game Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | vtable | |
| +0x04 | char | gameMode | 0=Classic, 1=Arcade, 2=Zen/ZenBlitz, 3=TBD |
| +0x05 | char | pauseFlag | |
| +0x06 | char | gameStateFlag | =0 checks for offscreen bonus logic |
| +0x10 | float | bombHitTimer | Set to countdown delay on bomb hit |
| +0x14 | char | comboCounter | Decrements per score threshold crossing |
| +0x18 | int | currentScore | |
| +0x1c | char | m_bUnsullied | = 0 = no misses yet; checked for UnsulliedAchievement |
| +0x30 | int | m_ScoreThreshold | Decremented on fruit slice; used for score-tier SFX |
| +0x3c | HUD* | pHUD | |
| +0x44 | bool | isFirstPlay1 | |
| +0x45 | bool | isFirstPlay2 | |
| +0x48 | FruitCamera* | pCamera | 0x16c bytes |
| +0x4c | FruitSaveData* | pSaveData | 0x238 bytes |
| +0x50 | Font*[4] | pFont0..3 | |
| +0x68 | Font* | pFontOptional | |
| +0x6c | Font* | pFontDefault | Fallback/base |
| +0x70 | Font*[4] | pFontRegion | CJK/Arabic overrides |
| +0x80 | Font* | pFont6 | |
| +0x90 | float[3] | worldPos | x/y/z |
| +0x0c | float | m_TransitionTimer | |
| +0x2c | float | m_CritTimer | Critical hit visual timer |
| +0x35 | byte | m_bSlowMotion | Slow-mo flag |
| +0x38 | float | dt | Current frame delta time |
| +0x54 | int | m_WaveSpeed | Copied to GameTask state on init |
| +0x160 | MainScreen* | pMainScreen | 0x120 bytes |
| +0x164 | GameOverScreen* | pGameOverScreen | 0x13C bytes |
| +0x168 | TutorialControl* | pTutorialCtrl | 0xA0 bytes |
| +0x174 | int | fruitTotal | Last AddToTotal result |
| +0x178 | CoinCounter* | pCoinCounter | 0xD4 bytes |
| +0x17c | SmartPtr\<Texture\> | pLocalisedTexture | 12 bytes |
| +0x180 | TimeControl* | pTimeCtrl | 0x108 bytes |
| +0x184 | int | m_field184 | = 0 |
| +0x188 | GameSound* | pGameSound | 0x708 bytes |
| +0x194 | int | m_FrameTimer | = (int)(dt × scale) + prev |
| +0x1a0 | float | m_MenuReturnTimer | Set by QuitToMenu, counted in GameUpdate |
| +0x1a8 | char | flag_0x1a8 | |
| +0x1b1 | char[256×4] | fruitStats1..4 | 4 stat arrays |
| +0x604 | byte | m_bFrameDirty | Cleared each GameTaskUpdate frame |
| +0xf4 | bool | field244_0xf4 | MortarGame field |

### GameTask State Struct (separate from Game singleton)

This per-task struct is accessed via GOT offset, not the Game singleton pointer.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x04 | PauseScreen* | pPauseScreen | 0xD8 bytes |
| +0x0c | byte | m_flag0c | Init=0 |
| +0x1c | MainScreen* | pMainScreen | Same ptr as Game+0x160 |
| +0x24 | SlashEntity*[16] | m_SlashEntities | 16 slash entities (0x40 bytes of ptrs) |
| +0x64 | List\<SliceEffect\>* | pSliceEffectList | |
| +0xbc | SmartPtr\<Model\> | pSliceFxModel | slice_fx.mad |
| +0xc0 | SmartPtr\<Model\> | pSliceFxCritModel | slice_fx_crit.mad |
| +0xc8 | MemoryPool* | pSliceEffectPool | 100 nodes |
| +0xcc | Vec3 | bombHitPos | Set by HitBomb/HitMenuBomb |
| +0xdc | float | m_NotifyTimer | Notification countdown |
| +0xf4 | SmartPtr\<Texture\> | pLoadingTexture | Localised loading image |
| +0xf8 | byte | m_bMenuBombHit | Set by HitMenuBomb |
| +0xfc | SmartPtr\<Texture\> | pBackgroundTexture | |
| +0x100 | HUDControl* | pDeferredControl | Added to HUD next frame |
| +0x110 | byte | m_flag110 | |
| +0x111 | byte | m_flag111 | |
| +0x112 | byte | m_bInitialized | Set=1 at end of GameInit |
| +0x113 | byte | m_flag113 | |
| +0x114 | int | m_savedWaveSpeed | From Game+0x54 |

**Game::Update** delegates entirely to `GameTaskUpdate(dt)`, which uses a task state machine: `*Game` (byte at +0x00) = current task index → indexes function pointer table.

### GameInit Flow (0x16c644, 274 lines)

```
1.  Create HUD (0x24 bytes) → Game+0x3c
2.  Create 3 MissControl (0x94 each) for combo text → add to HUD
3.  MissControl::CreatePool(12, hud) — 12 pooled combo sprites
4.  Create ScoreControl (0x100 bytes) + load 3 number textures → add to HUD
5.  Create CoinCounter (0xD4 bytes) → Game+0x178 → add to HUD
6.  Create TimeControl (0x108 bytes) → Game+0x180 → start countdown → add to HUD
7.  Load background texture → task state +0xfc
8.  Load slice_fx.mad → task state +0xbc
9.  Load slice_fx_crit.mad → task state +0xc0
10. Create SliceEffect list + MemoryPool(100 nodes) → task state +0x64/+0xc8
11. Create MainScreen (0x120 bytes) → task state +0x1c, Game+0x160
12. Create PauseScreen (0xD8 bytes) → task state +0x04
13. Create TutorialControl (0xA0 bytes) → Game+0x168
14. Add to HUD: MainScreen, PauseScreen, TutorialControl
15. Entity::HeapCreate(0x20000)
16. ActorManager::Initialise(5 types, 0x2000 max entities)
17. Register Fruit factory (type 0), hash converter
18. Pre-create 30 of each: Fruit(type 0) + Bomb(type 1) + BombBlast(type 4), all disabled
19. SplatEntity::CreatePool(128)
20. WaveManager::Init() + Resume()
21. BombFlash::CreatePool(32)
22. SoundManager::Initialise + SetSFXVolume(0.5 or first-play volume)
```

Entity types in ActorManager:
- Type 0 = Fruit
- Type 1 = Bomb
- Type 4 = BombBlast

---

## FruitNinja (application, size = 0x48)

Inherits: Osp::App::Application (+0x00), IScreenEventListener (+0x0c), ITimerEventListener (+0x10)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void** | vtable_App | Application vtable (iVar1 + 0x08) |
| +0x0c | void** | vtable_Screen | IScreenEventListener vtable (iVar1 + 0x58) |
| +0x10 | void** | vtable_Timer | ITimerEventListener vtable (iVar1 + 0x70) |
| +0x14 | EGLDisplay | m_eglDisplay | |
| +0x18 | EGLSurface | m_eglSurface | |
| +0x1c | EGLConfig | m_eglConfig | |
| +0x20 | EGLContext | m_eglContext | |
| +0x24 | int | field_0x24 | Init=0 |
| +0x28 | EGLSurface | m_eglPbuffer | Optional pbuffer surface |
| +0x3c | void* | m_pUnk3c | Deleted in Cleanup |
| +0x40 | Timer* | m_pTimer | 10ms game tick |
| +0x44 | GlesForm* | m_pGlesForm | GL surface + touch input |

**Non-virtual thunks** (multiple inheritance this-adjustment):

| Thunk | Address | Adjusts | For Interface |
|-------|---------|---------|---------------|
| OnTimerExpired | 0x00182694 | this - 0x10 | ITimerEventListener |
| OnScreenOff | 0x00181d90 | this - 0x0c | IScreenEventListener |
| OnScreenOn | 0x00181d84 | this - 0x0c | IScreenEventListener |

**Methods** (all addresses are real implementations, not GOT thunks):

| Method | Address | Lines |
|--------|---------|-------|
| FruitNinja() | 0x00182488 | 34 |
| CreateInstance | 0x00182470 | 13 |
| OnAppInitializing | 0x00182194 | 65 |
| OnAppTerminating | 0x00182160 | 18 |
| OnForeground | 0x001820b0 | 25 |
| OnBackground | 0x00182060 | 20 |
| OnTimerExpired | 0x0018269c | 10 |
| Draw (GameTick) | 0x001824e0 | 98 |
| Cleanup | 0x00182114 | 22 |
| InitEGL | 0x00181f80 | 60 |
| InitGL | 0x00181e58 | 35 |
| DestroyGL | 0x00181da0 | 28 |

**Game loop:** `OnTimerExpired (0x0018269c) → Timer::Start(10ms) + FruitNinja::Draw (0x001824e0)`

**FruitNinja::Draw** is the full game tick (misnamed — does update + render + swap):
Audio → sglMakeCurrent → glClear → SystemManager::Update → Game::Update(dt) → BeginFrame → Game::Draw(dt) → EndFrame → SwapBuffers → glFlush/glFinish → sglSwapBuffers → FPS calc → Touch::Update → SoundManager::Update. Terminates if stall counter > 90.

See [functions/game-loop.md](../functions/game-loop.md) for full call tree.

---

## GlesForm (size = 0x1f8)

Inherits: Osp::Ui::Controls::Form

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x000 | Form | super | 0x1c8 = 456 bytes of Bada Form |
| +0x1c8 | ITouchEventListener | touchListener | vtable ptr |
| +0x1cc | IKeyEventListener | keyListener | vtable ptr |
| +0x1d0 | FruitNinja* | mpApp | Back-pointer to app |
| +0x1d4 | uint[8] | touchIds | Bada touch point IDs, max 8 fingers |
| +0x1f4 | int | isReady | =1 after ctor |

Screen: 480×320 landscape (game coords). Physical portrait device; `TransformTouchPos` swaps axes: phys.Y→game.X, phys.X→game.Y(319-scaled)

---

## See Also

- [Game loop functions](../functions/game-loop.md) -- GameUpdate, GameDraw
- [Game flow functions](../functions/game-flow.md) -- state transitions, SaveCurrentData
- [State machine system](../systems/state-machine.md) -- GameTaskState transitions
- [Touch input system](../engine/touch-input.md) -- GlesForm touch handling
