# Game & Application Structs

## Game : Mortar::MortarGame (singleton, size ≥ 0x604)

MortarGame base zeroes +0x04..+0x1b3 in ctor.

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
| +0x174 | int | fruitTotal | Last AddToTotal result |
| +0x17c | SmartPtr\<Texture\> | pLocalisedTexture | 12 bytes |
| +0x188 | GameSound* | pGameSound | 0x708 bytes |
| +0x194 | int | m_FrameTimer | = (int)(dt × scale) + prev |
| +0x1a8 | char | flag_0x1a8 | |
| +0x1b1 | char[256×4] | fruitStats1..4 | 4 stat arrays |
| +0x604 | byte | m_bFrameDirty | Cleared each GameTaskUpdate frame |
| +0xf4 | bool | field244_0xf4 | MortarGame field |
| +0xfc | byte | m_bFieldFc | = 0 in Game ctor |
| +0xfd | byte | m_bFieldFd | = 0 in Game ctor |
| +0x100 | int | m_field100 | = 0 in Game ctor |

**Game::Update** delegates entirely to `GameTaskUpdate(dt)`, which uses a task state machine: `*Game` (byte at +0x00) = current task index → indexes function pointer table.

---

## FruitNinja (application, size = 0x48)

Inherits: Osp::App::Application, IScreenEventListener, ITimerEventListener

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void** | vtable | |
| +0x24 | int | field_0x24 | Init=0 |
| +0x28 | int | eglSurface | Int handle |
| +0x38 | int | field_0x38 | this-adj base for ITimerEventListener thunk |
| +0x40 | Timer* | mTimer | 10ms = 100Hz game tick |

**Game loop:** `OnTimerExpired → Timer::Start(10ms) + Draw()`

**Draw() order:** MAMAudioThread::ThreadMainLoop, MAMAudioController::Update, sglMakeCurrent, glClear, SystemManager::Update, game→Update(dt), BeginFrame, game→Draw(dt), EndFrame, SwapBuffers, glFlush/glFinish, sglSwapBuffers, Touch::Update, SoundManager::Update. Terminates if frame stall counter > 90.

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

Screen: 320×480 portrait. `TransformTouchPos: y = 319 - scaled_y`

---
