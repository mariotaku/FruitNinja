# Game & Application Structs

## Architecture: Three Distinct Globals

The "Game" system is split into **three separate data structures**, all accessed via
GOT-relative pointers. Understanding this split is critical:

1. **Game class** (MortarGame subclass, 0x104 bytes) -- The singleton C++ object with vtable.
   Holds only MortarGame base fields (version, hardware, language, licensed state) plus
   3 Game-specific bytes. Allocated via `operator_new(0x104)`.

2. **Game Data Global** (g_GameData, 0x608 bytes) -- A flat C-style struct (NOT a class) that
   holds all gameplay state: mode, score, HUD, camera, fonts, save data, screens, sound, etc.
   Zeroed by `GamePreInitialise` via `CpuFill8(ptr, 0, 0x608)`.

3. **GameTask State** (g_TaskState, ~0x120 bytes) -- Per-task state for the game task state
   machine: pause screen, slash entities, slice effects, background texture, etc.

The Game Data Global pointer is loaded from GOT in nearly every game function. Many functions
also load the Game singleton pointer (for vtable calls like IsFastHardware) and the GameTask
State pointer separately.

---

## Game : Mortar::MortarGame (singleton, size = 0x104)

`operator_new(0x104)` -- 260 bytes total.

### Constructor: Game_ctor (0x0010dab0)

```c
void* Game::Game_ctor(Game* this) {
    MortarGame::MortarGame(this);      // init base 0xFC bytes
    this->field_0x100 = 0;
    this->field_0xfc = 0;
    this->field_0xfd = 0;
    *(int*)this = vtable_ptr + 8;       // set vtable to Game override table
    return this;
}
```

### Singleton: ReturnsAnInstanceOfThisMortarGame (0x0010d674)

```c
Game* ReturnsAnInstanceOfThisMortarGame(void) {
    if (g_pGameSingleton == NULL) {
        Game* this = operator_new(0x104);
        Game::Game_ctor(this);
        g_pGameSingleton = this;
    }
    return g_pGameSingleton;
}
```

Thunk at 0x000f4200 calls the above via function pointer.

### MortarGame Struct Layout (0xFC / 252 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | void* | vtable | 16-entry vtable at 0x001eae58 (Game override) |
| +0x04 | 64 | char[64] | m_versionString | Raw version string, e.g. "1.5.1" |
| +0x44 | 64 | char[64] | m_formattedVersion | snprintf'd "%04i.%02i.%02i" version |
| +0x84 | 32 | char[32] | m_languageString | Set by SetLanguage(strcpy) |
| +0xA4 | 4 | int | m_versionCombined | major*10000 + minor*100 + patch |
| +0xA8 | 4 | int | m_versionMajor | Parsed from version string |
| +0xAC | 4 | int | m_versionMinor | Parsed from version string |
| +0xB0 | 4 | int | m_versionPatch | Parsed from version string |
| +0xB4 | 64 | char[64] | m_hardwareString | Set by SetHardware; default "BADA" |
| +0xF4 | 1 | bool | m_bFastHardware | Set by SetHardware second param |
| +0xF5 | 3 | | (padding) | |
| +0xF8 | 4 | int | m_licensedState | 0=unknown, 1=licensed, 2=unlicensed |

### Game-Specific Fields (beyond MortarGame base)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0xFC | 1 | byte | field_0xfc | Init = 0 |
| +0xFD | 1 | byte | field_0xfd | Init = 0 |
| +0xFE | 2 | | (padding) | |
| +0x100 | 4 | int | field_0x100 | Init = 0 |

### MortarGame vtable (at 0x001eae58, 16 entries)

All vtable entries shown are the Game-class overrides. The MortarGame base versions
are all stubs/no-ops (addresses in 0x0018xxxx range). The Game-class override addresses
are in the 0x0010xxxx range where the game logic lives.

| Idx | Base addr | Override addr | Method | Base behavior | Override behavior |
|-----|-----------|---------------|--------|---------------|-------------------|
| 0 | (none) | 0x0010d9d0 | GetHardwareString | -- | Returns this+0x04 (m_versionString ptr) |
| 1 | (none) | 0x0010d9d4 | IsFastHardware | -- | Returns this->m_bFastHardware (+0xF4) |
| 2 | 0x0018aa14 | 0x0018aa14 | RenderAtHalfFrames | No-op stub | Not overridden (base stub) |
| 3 | 0x0018ac80 | 0x0018ac80 | GetHighResolutionScale | Returns 1.0f | Not overridden (base) |
| 4 | 0x0018ac88 | 0x0018ac88 | GetOpenFeintProductKey | No-op stub | Not overridden (defunct) |
| 5 | 0x0018ac8c | 0x0018ac8c | GetOpenFeintSecret | No-op stub | Not overridden (defunct) |
| 6 | 0x0018ac90 | 0x0018ac90 | GetOpenDisplayName | No-op stub | Not overridden (defunct) |
| 7 | 0x0018ac94 | 0x0018ac94 | GetPlayhavenToken | No-op stub | Not overridden (defunct) |
| 8 | (none) | 0x0010d9dc | GetCacheDataArchive | -- | Returns 0 (null) |
| 9 | 0x0018ac98 | 0x0018ac98 | CreateFileSystems | No-op stub | Not overridden (base stub) |
| 10 | 0x0018aa28 | 0x0018aa28 | TellGameToStart | No-op stub | Not overridden (base stub) |
| 11 | 0x0018aa1c | 0x0018aa1c | Update(float dt) | No-op stub | Not overridden (base stub) |
| 12 | 0x0018aa18 | 0x0018aa18 | Draw(float dt) | No-op stub | Not overridden (base stub) |
| 13 | 0x0018aa20 | 0x0018aa20 | Init(int argc, char**) | No-op stub | Not overridden (base stub) |
| 14 | 0x0018aa24 | 0x0018aa24 | End | Returns this | Not overridden (base) |
| 15 | 0x0018aa2c | 0x0018aa2c | Paused | No-op stub | Not overridden (base stub) |

Note: vtable[0] (GetHardwareString) returns `this+4` = m_versionString, NOT m_hardwareString.
This may be a naming error from original symbols, or the Game override intentionally returns
the version string here. The m_hardwareString at +0xB4 is set by SetHardware but has no
vtable getter -- accessed directly.

### MortarGame Non-Virtual Methods

| Address | Signature | Behavior |
|---------|-----------|----------|
| 0x0018ab6c | `MortarGame()` ctor | Zeros all fields +0x04..+0xF8, sets vtable, calls SelfVersion->SetVersion |
| 0x0018abe8 | `MortarGame()` ctor variant | Identical logic, different GOT-relative addressing |
| 0x000f5cdc | `MortarGame()` thunk | GOT thunk -> calls 0x0018abe8 |
| 0x0018ac64 | `void TellGameToQuit()` | Calls SystemManager::QuitGame() |
| 0x0018aa34 | `void SaveOnExit()` | **No-op stub** |
| 0x0018aa38 | `char* SelfVersion()` | Returns pointer to string "1.0.0" (default) |
| 0x0018aa50 | `void SetAppLicensed(bool)` | If true->m_licensedState=1; if false and not already 1->m_licensedState=2 |
| 0x0018aa68 | `int GetAppLicensedState()` | Returns m_licensedState (+0xF8) |
| 0x0018aa70 | `void SetLanguage(char*)` | strcpy to m_languageString (+0x84) |
| 0x0018aa7c | `void SetHardware(char*,bool)` | strcpy to m_hardwareString (+0xB4), sets m_bFastHardware (+0xF4) |
| 0x0018aa90 | `void SetVersion(char*)` | Parses "M.m.p" string, fills version fields, snprintf formatted, sets m_hardwareString default "BADA" |
| 0x0018aa30 | `void UnPaused()` | **No-op stub** |
| 0x0018ac9c | `bool AllowOrientationChange(int)` | Returns false |
| 0x0010d9e0 | `void OrientationDidChange(int)` | **No-op stub** (in Game address range, __thiscall) |

### MortarGame Default Values

- SelfVersion base returns "1.0.0"; Game::SelfVersion override returns "1.5.1"
- SetVersion sets default m_hardwareString to "BADA" via snprintf
- SetVersion format: "%04i.%02i.%02i" -> m_formattedVersion
- m_licensedState: 0=unknown (init), 1=licensed, 2=unlicensed (cannot downgrade from 1)

### Game-Class Override Methods (non-vtable)

These override MortarGame methods with actual game logic. They access the **g_GameData
global**, not the Game singleton's own fields:

| Address | Method | Behavior |
|---------|--------|----------|
| 0x0010d9ec | `char* SelfVersion()` | Returns "1.5.1" string literal (static, not __thiscall) |
| 0x0010da64 | `bool AllowOrientationChange(int)` | Returns 0 (false) |
| 0x0010da68 | `void SetAppLicensed(bool)` | Reads/writes g_GameData+0x18C (NOT Game singleton field) |
| 0x0010da94 | `int GetAppLicensedState()` | Returns g_GameData+0x18C |
| 0x0010dae0 | `void SaveOnExit()` | Calls GameTaskSaveOnExit() |
| 0x0010dae8 | `void UnPaused()` | If LoadingJob::CanBoot: resumes SoundManager, calls GameSound::Unpause, calls UnpauseGame if m_TransitionTimer!=0 |
| 0x0010b140 | `void SetLanguage(char*)` | Writes 0 to byte at g_GameData+0x03 (stub/flag clear) |
| 0x001042d4 | `void SetHardware(char*,bool)` | Thunk -> calls base MortarGame::SetHardware |
| 0x0010dc80 | `void TellGameToStart(int)` | Sets HUD to multiplayer state, resets WaveManager |

### Singleton Getters

| Address | Function | Notes |
|---------|----------|-------|
| 0x000f4200 | ReturnsAnInstanceOfThisMortarGame | Thunk -> calls 0x0010d674 via function pointer |
| 0x0010d674 | ReturnsAnInstanceOfThisMortarGame | Singleton getter: allocates 0x104 if null, caches in GOT global |

---

## Game Data Global (g_GameData, size = 0x608)

This is a **flat C-style struct** (no vtable, no class), holding all gameplay state.
Zeroed to 0 by `GamePreInitialise` via `CpuFill8(ptr, 0, 0x608)`.
Accessed from GOT-relative pointer in nearly every game function.

### g_GameData Struct Layout

| Offset | Size | Type | Name | Evidence |
|--------|------|------|------|----------|
| +0x00 | 1 | byte | taskStateIndex | GameTaskUpdate: `stateIdx = *gameObj` -- indexes function pointer table |
| +0x01 | 1 | byte | field_0x01 | |
| +0x02 | 1 | byte | gameActiveFlag | GameUpdate/GameDraw: checked as bool for active/paused state; 0=paused, !=0=active. EndRetryLevel clears to 0 |
| +0x03 | 1 | byte | languageFlag | SetLanguage writes 0 here |
| +0x04 | 1 | byte | gameMode | 0=Classic, 1=Arcade, 2=Zen/ZenBlitz, 3=AttackMode; ParseGameMode returns 0-4; PowersEnabled checks ==2; IsTimedGame checks (val-2)<2 |
| +0x05 | 1 | byte | pauseFlag | Set to 1 by GameOver, QuitToMenu; checked in GameUpdate/GameDraw |
| +0x06 | 1 | byte | retryFlag | EndRetryLevel clears; QuitToMenu clears; SetupGameWork clears |
| +0x07 | 1 | byte | field_0x07 | |
| +0x08 | 4 | float | retryTimer | EndRetryLevel: checked; GameUpdate: if >0, calls RetryUpdate |
| +0x0C | 4 | float | m_TransitionTimer | SetupGameWork: set to 0.0; EndRetryLevel: set to const; UnpauseGame: flag=1 + sets timer; SaveCurrentData: compared to 1.0 and to <0 |
| +0x10 | 4 | float | bombHitTimer | Set by HitBomb to countdown delay; checked <=0 for wave update; GameUpdate/GameDraw branch on >0; UpdateBombHit called with previous value |
| +0x14 | 1 | byte | missCount | GetCurrentMissCount returns this; AddToCurrentScore decrements; "combo counter" |
| +0x15 | 3 | | (padding) | |
| +0x18 | 4 | int | currentScore | GetCurrentScore returns this; SetScore writes this; AddToCurrentScore modifies |
| +0x1C | 1 | byte | m_bUnsullied | SaveCurrentData copies to FruitSaveData; 0 = no misses |
| +0x1D | 3 | | (gap) | |
| +0x20 | 4 | float | retryPos_x | SetupGameWork: zeroed (part of 12-byte Vec3 block) |
| +0x24 | 4 | float | retryPos_y | EndRetryLevel: copies +0x20 to +0x28 |
| +0x28 | 4 | float | retryPos_z | |
| +0x2C | 4 | float | m_CritTimer | DrawCritHit checks; GameUpdate decrements by scaledDt |
| +0x30 | 4 | int | m_ScoreThreshold | SetupGameWork: set from FruitSaveData+0x110; AddToCurrentScore: score tier SFX trigger |
| +0x34 | 1 | byte | field_0x34 | SetupGameWork: cleared |
| +0x35 | 1 | byte | m_bSlowMotion | GameUpdate: slow-mo flag; if set, fVar9=5.0 else 10.0; cleared each frame |
| +0x36 | 2 | | (gap) | |
| +0x38 | 4 | float | dt | GameTaskUpdate writes rawDt here; GameTaskDraw reads |
| +0x3C | 4 | HUD* | pHUD | GameInit: `operator_new(0x24)`; GameDestroy: delete + null |
| +0x40 | 4 | | (gap) | |
| +0x44 | 1 | bool | isFirstPlay1 | SaveCurrentData: if false, AddToTotal("first_play1") |
| +0x45 | 1 | bool | isFirstPlay2 | SaveCurrentData: if false, AddToTotal("first_play2") |
| +0x46 | 2 | | (padding) | |
| +0x48 | 4 | FruitCamera* | pCamera | GameInitialise: `operator_new(0x16c)`; GameDestroy: vtable dtor + null |
| +0x4C | 4 | FruitSaveData* | pSaveData | GameUpdate: `FruitSaveData::Update(*(+0x4c),rawDt,*(+0x3c))`; GameDestroy: dtor + delete + null |
| +0x50 | 4 | Font* | pFont0 | GameDestroy: dtor + delete + null |
| +0x54 | 4 | Font* | pFont1 | GameInitialise: first font loaded; GameDestroy: dtor + delete |
| +0x58 | 4 | Font* | pFont2 | GameDestroy: dtor + delete + null |
| +0x5C | 4 | Font* | pFont3 | GameDestroy: dtor + delete + null |
| +0x60 | 4 | Font* | pFont4 | |
| +0x64 | 4 | Font* | pFont5 | GameDestroy: dtor + delete (noted as +100 decimal) |
| +0x68 | 4 | Font* | pFontOptional | GameInitialise/GameDestroy: separate font slot |
| +0x6C | 4 | Font* | pFontDefault | GameInitialise: loaded as fallback; +0x70..+0x7C set = +0x6C |
| +0x70 | 4 | Font* | pFontRegion0 | GameInitialise: optional CJK font (File::Exists check) |
| +0x74 | 4 | Font* | pFontRegion1 | GameInitialise: optional CJK font |
| +0x78 | 4 | Font* | pFontRegion2 | GameInitialise: optional CJK font |
| +0x7C | 4 | Font* | pFontRegion3 | |
| +0x80 | 4 | Font* | pFont6 | GameInitialise/GameDestroy: separate slot |
| +0x84 | 4 | | (gap) | |
| +0x88 | 4 | float | field_0x88 | SetupGameWork: set to a float constant |
| +0x8C | 4 | | (gap) | |
| +0x90 | 4 | float | worldPos_x | GameDraw: `*(iVar3 + 0x90)` = light direction X |
| +0x94 | 4 | float | worldPos_y | GameDraw: `*(iVar3 + 0x94)` = light direction Y |
| +0x98 | 4 | float | worldPos_z | GameInitialise: zeroed as part of Vec3 block |
| +0x9C | 1 | byte | field_0x9c | GameUpdate: cleared each frame (2 bytes) |
| +0x9D | 1 | byte | field_0x9d | GameUpdate: cleared each frame |
| +0x9E | 1 | byte | field_0x9e | GameInitialise: cleared |
| +0x9F | 1 | | (gap) | |
| +0xA0 | 8x12 | struct[8] | modifierSlots | GameUpdate: loop `do { ... iVar6 += 0xc } while (iVar6 != gameObj+0xc0)` -- 8 slots of 12 bytes each from +0x9C to +0xBC |
| +0xA8 | 4 | float | modifierTimer(first slot) | GameUpdate: checks <=0, ==0, >0; sets to -1.0 or 0.0 |
| ... | | | (modifier array continues) | 8 entries x 12 bytes = 96 bytes (+0x9C to +0xFB) |
| +0x160 | 4 | MainScreen* | pMainScreen | GameInit: `operator_new(0x120)` |
| +0x164 | 4 | GameOverScreen* | pGameOverScreen | GameOver: `operator_new(0x13c)` |
| +0x168 | 4 | TutorialControl* | pTutorialCtrl | GameInit: `operator_new(0xa0)` |
| +0x16C | 4 | int | multiplayerCtrl | QuitToMenu: if !=0, sets +0x33=1; SetupGameWork: zeroed |
| +0x170 | 1 | byte | m_bOnlineRetry | QuitToMenu: cleared; EndRetryLevel: checked for RetryOnlineMultiplayerGame |
| +0x171 | 3 | | (padding) | |
| +0x174 | 4 | int | fruitTotal | AddToCurrentScore: last AddToTotal result |
| +0x178 | 4 | CoinCounter* | pCoinCounter | GameInit: `operator_new(0xd4)` |
| +0x17C | 12 | SmartPtr\<Texture\> | pLocalisedTexture | _GLOBAL__I_Game.cpp: SmartPtr ctor; GameInitialise: LoadLocalisedTexture; GameDestroy: SetPtr(null) |
| +0x188 | 4 | GameSound* | pGameSound | GameDestroy: `*(+0x188)` dtor + delete; AddToCurrentScore: SFXPlay via this; HitBomb: SFXPlay |
| +0x18C | 4 | int | m_licensedState | Game::SetAppLicensed / Game::GetAppLicensedState access this; SetupGameWork: cleared to 0 |
| +0x190 | 4 | | (gap) | |
| +0x194 | 4 | int | m_FrameTimer | GameTaskUpdate: `(int)(dt * scale) + prev`; accumulates ms |
| +0x198 | 1 | byte | field_0x198 | SetupGameWork: cleared |
| +0x199 | 1 | byte | field_0x199 | SetupGameWork: cleared |
| +0x19A | 1 | byte | field_0x19a | QuitToMenu: cleared |
| +0x19B | 1 | byte | field_0x19b | QuitToMenu: cleared |
| +0x19C | 1 | byte | field_0x19c | QuitToMenu: cleared |
| +0x19D | 1 | byte | field_0x19d | QuitToMenu: cleared |
| +0x19E | 1 | byte | field_0x19e | SetupGameWork: cleared |
| +0x19F | 1 | | (gap) | |
| +0x1A0 | 4 | float | m_MenuReturnTimer | QuitToMenu: set to const; GameUpdate: decremented, when <=0 calls CleanupAndReturnToMainMenu |
| +0x1A4 | 4 | | (gap) | |
| +0x1A8 | 1 | byte | flag_0x1a8 | AddToCurrentScore: checked for bonus stat tracking; SetupGameWork: cleared |
| +0x1A9 | 3 | | (gap) | |
| +0x1AC | 4 | float | field_0x1ac | SetupGameWork: set to float constant (same as bombHitTimer init) |
| +0x1B0 | 1 | byte | field_0x1b0 | SetupGameWork: cleared |
| +0x1B1 | 3 | | (gap) | |
| ... | | | (sparse fields) | |
| +0x5B4 | ~48 | StringTable | m_StringTable | _GLOBAL__I_Game.cpp: `StringTable::StringTable(ptr + 0x5b4)` |
| +0x604 | 1 | byte | m_bFrameDirty | GameTaskUpdate: cleared to 0 each frame |
| +0x605 | 3 | | (padding to 0x608) | |

### Key g_GameData Access Patterns

From `SetupGameWork` (0x0010b4e8), which initializes a new game round:
```
*(byte*)(ptr + 0x00) = 2;              // taskStateIndex = 2 (in-game)
*(float*)(ptr + 0x1AC) = const;        // field_0x1ac
*(float*)(ptr + 0x10) = const;         // bombHitTimer = 0
*(float*)(ptr + 0x2C) = const;         // m_CritTimer = 0
*(float*)(ptr + 0x1A0) = const;        // m_MenuReturnTimer = 0
*(byte*)(ptr + 0x1A8) = 0;
*(byte*)(ptr + 0x199) = 0;
*(float*)(ptr + 0x88) = const;
*(int*)(ptr + 0x30) = saveData->0x110; // m_ScoreThreshold from save
*(byte*)(ptr + 0x34) = 0;
*(byte*)(ptr + 0x1C) = 0;             // m_bUnsullied
*(byte*)(ptr + 0x06) = 0;             // retryFlag
*(byte*)(ptr + 0x170) = 0;
*(byte*)(ptr + 0x198) = 0;
*(byte*)(ptr + 0x19E) = 0;
*(int*)(ptr + 0x16C) = 0;
*(int*)(ptr + 0x164) = 0;             // pGameOverScreen = null
*(int*)(ptr + 0x160) = 0;             // pMainScreen = null
*(float*)(ptr + 0x20) = 0;            // retryPos
*(float*)(ptr + 0x24) = 0;
*(float*)(ptr + 0x28) = 0;
*(byte*)(ptr + 0x1B0) = 0;
*(int*)(ptr + 0x18C) = 0;             // m_licensedState (game-level)
```

---

## GameTask State (g_TaskState, size >= 0x120)

This per-task struct is accessed via a separate GOT offset, not the Game Data Global pointer.

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | float | m_timer | GameTaskUpdate task state machine timer |
| +0x04 | 4 | PauseScreen* | pPauseScreen | 0xD8 bytes; GameInit creates |
| +0x08 | 4 | float | m_pauseTimerUnk | PauseGame: set to 0.25; RetryUpdate reads |
| +0x0C | 1 | byte | m_flag0c | PauseGame: cleared; GameInit: cleared; GameExit: cleared |
| +0x0D | 3 | | (padding) | |
| +0x10 | 4 | float | m_taskTimer | UnpauseGame: set from const |
| +0x14 | 8 | | (gap) | |
| +0x1C | 4 | MainScreen* | pMainScreen | GameInit: same ptr as g_GameData+0x160; GameExit: dtor + delete + null |
| +0x20 | 4 | float | m_globalTimeScale | GameDraw: initially 1.0; modified by wave dt |
| +0x24 | 64 | SlashEntity*[16] | m_SlashEntities | 16 slash entities (0x40 bytes of ptrs) |
| +0x64 | 4 | List\<SliceEffect\>* | pSliceEffectList | GameInit: `operator_new(0x14)` |
| +0xBC | 4 | SmartPtr\<Model\> | pSliceFxModel | slice_fx.mad |
| +0xC0 | 4 | SmartPtr\<Model\> | pSliceFxCritModel | slice_fx_crit.mad |
| +0xC4 | 4 | SmartPtr\<Model\> | pSliceFxModel3 | GameExit: SmartPtrNull_Model |
| +0xC8 | 4 | MemoryPool* | pSliceEffectPool | 100 nodes |
| +0xCC | 12 | Vec3 | bombHitPos | Set by HitBomb/HitMenuBomb; DrawBombHit translates to this |
| +0xD8 | 4 | MortarSound* | pBombWarnSound | GameUpdate: bomb proximity fuse SFX; GameExit: released |
| +0xDC | 4 | float | m_NotifyTimer | Notification countdown; GameUpdate: decremented |
| +0xF0 | 4 | Colour | m_CritColour | DrawCritHit: bytes at +0xF0..+0xF3 = RGBA |
| +0xF4 | 12 | SmartPtr\<Texture\> | pLoadingTexture | Localised loading image; GameUpdate: loaded lazily |
| +0xF8 | 1 | byte | m_bMenuBombHit | Set by HitMenuBomb; HitBomb clears; checked in bomb hit flow |
| +0xFC | 12 | SmartPtr\<Texture\> | pBackgroundTexture | GameInit: loaded; GameDraw: set for background quad |
| +0x100 | 4 | HUDControl* | pDeferredControl | GameUpdate: added to HUD next frame, then nulled |
| +0x104 | 8 | | (guard bytes) | __cxa_guard for StringHash caching |
| +0x10C | 12 | SmartPtr\<Texture\> | pBombHitTexture | DrawBombHit/DrawCritHit: lazily loaded fullscreen effect texture |
| +0x110 | 1 | byte | m_flag110 | GameInit: cleared; GameExit: cleared |
| +0x111 | 1 | byte | m_flag111 | GameInit: cleared; GameDraw: checked, clears InputManager actions |
| +0x112 | 1 | byte | m_bInitialized | Set=1 at end of GameInit; cleared in GameExit |
| +0x113 | 1 | byte | m_flag113 | GameExit: cleared |
| +0x114 | 4 | int | m_savedWaveSpeed | GameInit: from g_GameData+0x54; GameExit: cleared |
| +0x118 | 4 | int | field_0x118 | GameExit: cleared |
| +0x11C | 12 | SmartPtr\<Texture\> | pField11c | GameExit: SmartPtrNull_Tex |

### GameTask Function Pointer Table

The `GameTaskUpdate` state machine uses `taskStateIndex` (g_GameData+0x00) to index into
a function pointer table. Two sub-tables exist at `GOT + DAT + offset`:
- Init table at +0x24 (task init functions, called once on state entry)
- Update table at +0x18 (task update functions, called each frame)
- Draw table indexed similarly
- Exit table at +0x0C (task exit functions)

---

## All Game-Related Methods

### Game Class Methods (on Game singleton)

| Address | Signature | Notes |
|---------|-----------|-------|
| 0x0010dab0 | `Game::Game_ctor(Game*)` | Constructor: calls MortarGame ctor, clears +0xFC/+0xFD/+0x100, sets vtable |
| 0x000f9918 | `Game_ctor(Game*)` | Thunk -> 0x0010dab0 |
| 0x0010d674 | `ReturnsAnInstanceOfThisMortarGame()` | Singleton getter: allocates 0x104 if null |
| 0x000f4200 | `ReturnsAnInstanceOfThisMortarGame()` | Thunk -> 0x0010d674 via function pointer |
| 0x0010d9ec | `Game::SelfVersion()` | Returns "1.5.1" (static) |
| 0x0010d9e0 | `Game::OrientationDidChange(int)` | No-op (__thiscall) |
| 0x0010da64 | `Game::AllowOrientationChange(int)` | Returns false |
| 0x0010da68 | `Game::SetAppLicensed(bool)` | Writes g_GameData+0x18C |
| 0x0010da94 | `Game::GetAppLicensedState()` | Returns g_GameData+0x18C |
| 0x0010dae0 | `Game::SaveOnExit()` | Calls GameTaskSaveOnExit() |
| 0x0010dae8 | `Game::UnPaused()` | Resume sound + unpause if transitioning |
| 0x0010b140 | `Game::SetLanguage(char*)` | Clears g_GameData+0x03 |
| 0x001042d4 | `Game::SetHardware(char*,bool)` | Thunk to base MortarGame::SetHardware |
| 0x0010dc80 | `Game::TellGameToStart(int)` | HUD multiplayer + WaveManager reset |

### Game Lifecycle Functions (free functions operating on g_GameData)

| Address | Function | Lines | Notes |
|---------|----------|-------|-------|
| 0x0010b588 | `GamePreInitialise()` | 3 | CpuFill8(g_GameData, 0, 0x608) |
| 0x0010bdfc | `GameInitialise(void*,char*)` | ~200 | Inits all engine subsystems, loads fonts, creates camera, loads content |
| 0x0010b4e8 | `SetupGameWork()` | ~35 | Inits g_GameData fields for new game round |
| 0x0016c644 | `GameInit(ulong)` | 283 | Creates HUD, MissControl, ScoreControl, entities, WaveManager |
| 0x0016bed0 | `GameUpdate(float,bool)` | ~200 | Main update: wave, physics, HUD, bomb, score, music |
| 0x0016b888 | `GameDraw(float,bool)` | ~200 | Main render: background, actors, HUD, particles, effects |
| 0x0010b7ec | `GameDestroy()` | ~100 | Destroys all subsystems, fonts, camera, HUD, save data, sound |
| 0x0016cf74 | `GameExit()` | ~60 | Cleans up task state, wave, entities, HUD, textures |

### Game State Functions

| Address | Function | Notes |
|---------|----------|-------|
| 0x0010a5d4 | `GameTaskUpdate(float)` | State machine: reads taskStateIndex, calls init/update/exit |
| 0x0010a2c4 | `GameTaskDraw(float)` | Calls current state draw function |
| 0x0010a320 | `GameTaskExit()` | Calls exit func for current state |
| 0x0016cf40 | `GameTaskSaveOnExit()` | Save on exit handler |
| 0x00169670 | `GameTaskInitInput()` | Registers input actions |

### Gameplay Functions

| Address | Function | Notes |
|---------|----------|-------|
| 0x0010a7ac | `AddToCurrentScore(int,int,bool,bool)` | Adds points, triggers SFX, tracks fruit stats |
| 0x0010a4a0 | `GetCurrentScore(int)` | Returns g_GameData+0x18 |
| 0x0010a4b8 | `SetScore(int,int)` | Writes g_GameData+0x18 |
| 0x0010a4d0 | `GetCurrentMissCount(int)` | Returns byte at g_GameData+0x14 |
| 0x0010a48c | `GetScoreMultiplyer(int)` | Returns 1 (stub) |
| 0x0010a44c | `IsTimedGame()` | Returns (gameMode - 2) < 2 |
| 0x0010a470 | `IsMultiplayer()` | Returns false (stub) |
| 0x0010a478 | `IsSameScreenMultiplayer()` | IsMultiplayer && !IsOnlineMultiplayer |
| 0x0010a42c | `PowersEnabled()` | Returns gameMode == 2 |
| 0x0010a730 | `SetScoreDelegate(Delegate1)` | Sets score modifier delegate |
| 0x0010a750 | `ZeroInit_Game(int*)` | Writes 0 to *param, returns 0 |
| 0x0010ad34 | `PowerUpManager::GetScoreGainMultiplier()` | +0x78 * +0x7C |
| 0x0010ad40 | `PowerUpManager::GetScoreLossMultiplier()` | +0x80 * +0x84 |

### Game Flow Functions

| Address | Function | Notes |
|---------|----------|-------|
| 0x00169ed4 | `GameOver(int,float,int)` | Creates GameOverScreen, adds to HUD, tracks stats |
| 0x00169e50 | `QuitToMenu()` | Resets wave dt, sets pause, clears fields, starts menu return timer |
| 0x0016a058 | `ResetGameEntities(bool)` | Resets all slash entities, chucks bombs/fruit offscreen |
| 0x0016ada0 | `SkipToGameOver(int,float,float,float,int)` | Skips directly to game over state |
| 0x0016b0fc | `HitBomb(Vec3)` | Sets bombHitTimer, camera shake, bomb SFX |
| 0x0016a1a8 | `UpdateBombHit(float)` | Checks timer thresholds, resets entities, removes flashes |
| 0x00169cd4 | `RetryUpdate(float)` | Lerps entities toward retry position |
| 0x0016a208 | `EndRetryLevel()` | Resets score, entities, wave; restores MainScreen |
| 0x00168f80 | `PauseGame()` | Sets gameActiveFlag=1, clears task flag, sets timer |
| 0x00168fb0 | `UnpauseGame()` | Sets timer, sets task flag=1 |
| 0x0016b5b4 | `DrawCritHit()` | Renders fullscreen critical hit overlay |
| 0x0016b73c | `DrawBombHit()` | Renders fullscreen bomb hit flash overlay |

### Save/Load Functions

| Address | Function | Notes |
|---------|----------|-------|
| 0x0016ccc8 | `SaveCurrentData(bool)` | Copies game state to FruitSaveData, calls SaveGame |
| 0x0012be74 | `LoadGame(FruitSaveData*)` | Loads XML save file, parses into FruitSaveData |
| 0x0012a2fc | `SaveGame(FruitSaveData*)` | Serializes FruitSaveData to XML |
| 0x00129ca8 | `SaveGameState()` | |
| 0x0012a034 | `FruitSaveData::FinishedGame()` | Decrements modifier map values |

### Mode/Config Functions

| Address | Function | Notes |
|---------|----------|-------|
| 0x0010b444 | `ParseGameMode(ulong)` | Hash-compares to 4 mode name hashes, returns 0-4 |
| 0x0010b15c | `GetModeName(GAME_MODE)` | Returns string from mode name table |
| 0x0010d544 | `IsFastHardware()` | Calls vtable[1] on Game singleton |

### Static Initializer

`_GLOBAL__I_Game.cpp` at 0x0010a96c initializes:
1. A global 4x4 identity matrix
2. Two global Vec3s (zero and unit)
3. A global Vec2 (zero)
4. A global Colour (0x00, 0x00, 0x00, 0xFF = black)
5. SmartPtr<Texture> at g_GameData+0x17C (constructor)
6. StringTable at g_GameData+0x5B4 (constructor)
7. Score modifier Delegate1<int,int> at g_GameData+0x0C offset area
8. Registers atexit destructors for all above
9. Initializes 13 Mortar TYPE_IDs (for RTTI-like type system)

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

**Game loop:** `OnTimerExpired (0x0018269c) -> Timer::Start(10ms) + FruitNinja::Draw (0x001824e0)`

**FruitNinja::Draw** is the full game tick (misnamed -- does update + render + swap):
Audio -> sglMakeCurrent -> glClear -> SystemManager::Update -> Game::Update(dt) -> BeginFrame -> Game::Draw(dt) -> EndFrame -> SwapBuffers -> glFlush/glFinish -> sglSwapBuffers -> FPS calc -> Touch::Update -> SoundManager::Update. Terminates if stall counter > 90.

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

Screen: 480x320 landscape (game coords). Physical portrait device; `TransformTouchPos` swaps axes: phys.Y->game.X, phys.X->game.Y(319-scaled)

---

## See Also

- [Game loop functions](../functions/game-loop.md) -- GameUpdate, GameDraw
- [Game flow functions](../functions/game-flow.md) -- state transitions, SaveCurrentData
- [State machine system](../systems/state-machine.md) -- GameTaskState transitions
- [Touch input system](../engine/touch-input.md) -- GlesForm touch handling
