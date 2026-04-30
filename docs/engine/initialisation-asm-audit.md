# Initialisation chain ASM audit

ASM-level audit of the FruitNinja boot pipeline against `FruitNinja.exe`.
All addresses come from Ghidra (`disassemble_function`, `get_function_by_address`,
`get_xrefs_to`); call ordering is taken from the actual `blx` stream of the
binary, not from the decompile cosmetic line order. Per-call PLT thunks are
listed by their thunk address (what GameInit/GameInitialise actually `blx`s);
the real callee is shown after the `->` arrow.

## Section 1 -- Boot chain

```
[Bada AppFw]
  Osp::App::Application::Execute(...)
       |
       v
OspMain (0x000d82a4)              -- unpack handle, branch into AppBootstrap
  -> OspMain_AppBootstrap (0x00183474)
       new ArrayList(args), Application::Execute(..., AAT_MAIN_APP)
       v
FruitNinja::OnAppInitializing (0x00182194)
  1. new MortarAudioMixerBada(0x160) + ctor                          (sound)
  2. new GlesForm(0x1f8) + ctor + Form::Construct                    (window)
  3. AppFrame->vtable[9](frame) -> SetClientArea(form)               (window)
  4. FruitNinja_InitEGL                                              (GL ctx)
  5. FruitNinja_InitGL                                               (GL state)
  6. new Osp::Base::Runtime::Timer + Construct                       (frame timer)
  7. MAMAudioController::Init(&audioMixer->m_AudioThread)            (audio thread)
  8. SettingInfo::GetValue("SoundMute" key) -> if !mute, StartAudioSubsystem
  9. game = Game_GetInstance_Thunk()  -- lazy ctor (first call)
       Game::Game_Constructor (0x0010dab0)
         MortarGame::MortarGame()
         m_appState=0, m_bInitialized=0, m_bLanguageSet=0
         vtable = &Game_vtable + 8   (i.e. Game_vtable[2..])
 10. (game->vtable[0x34/4])(game, 0, 0)  -- this is GameInitialise
       *** GameInitialise (0x0010bdfc) runs here ***
 11. PowerManager::KeepScreenOnState(true, true)
       v
return true (Bada keeps app alive; subsequent input/timer callbacks
drive GameTaskUpdate -> dispatch table -> GameInit/GameUpdate/GameExit)

[On termination]
FruitNinja::OnAppTerminating (0x00182160)
  -> game->vtable[0x38/4](game, registry, forced)  -- one of GameDestroy/...
  -> operator_delete(game), zero slot
  -> FruitNinja_Cleanup (0x00182114)
       - delete tableHandle
       - Timer::Cancel + delete
       - delete GlesForm
       - FruitNinja_DestroyGL
```

`GamePreInitialise` (0x0010b588) is `CpuFill8(g_GameData, 0, 0x608)` -- a flat
zero-fill of the 1544-byte Game singleton. It is invoked from the static
`_GLOBAL__I_*` ctor chain (loader), NOT from `OnAppInitializing` itself, but
the effect is identical: by the time `GameInitialise` runs, every Game field
is zero. The port re-implements this as field-by-field clears in
`GamePreInitialise()`.

The state-2 task body (`GameInit @ 0x0016c644`) is invoked indirectly via the
task table walked by `GameTaskUpdate (0x0010a5d4)`:

```
GameTaskUpdate(rawDt) {
    state = g_GameData[0]                       // current task state
    if (!g_TaskState->initComplete) {           // +0x112 == 0
        initFunc = TaskTable[state*4 + 0x24]    // 4-entry dispatch
        initFunc(0)                             // -- GameInit runs HERE
        g_TaskState->initComplete = 1
    }
    ...                                         // per-frame Update
}
```

So the lifecycle is: `OspMain -> AppBootstrap -> Execute -> OnAppInitializing
-> Game::vtable[13] = GameInitialise (one-shot engine boot) -> [Bada main loop
fires GameTaskUpdate per frame] -> dispatch[state]=GameInit (first time only)
-> per-frame GameUpdate/GameDraw -> GameTaskExit/GameDestroy at shutdown`.

## Section 2 -- GameInitialise (0x0010bdfc) per-call audit

Binary call order, in `blx` stream order. "Port" column: `OK` = present in
src/game/GameInitialise.cpp at the same logical step; `MISSING` = no
equivalent call; `STUB` = port has a placeholder; `DIFFERS` = present but
materially different from the binary.

| # | Bin addr | Call (-> resolved) | Purpose | Port |
|---|----------|--------------------|---------|------|
| 1 | 0x10be10 | `SystemManager::Init` (0x000fb070) | Set m_field50=0, m_bRunning=1, clock base | OK |
| 2 | 0x10be18 | `MatrixManager::Init` (0x0010773c) | ResetAllStacks | OK |
| 3 | 0x10be1e | `operator new(0x14)` | alloc IFileSystem | MISSING |
| 4 | 0x10be24 | `FileSystem_Direct::FileSystem_Direct` (0x001082d0) | ctor on disk-backed VFS | MISSING |
| 5 | 0x10be3c | `fsDir->vtable[7]` (Construct? path config) | Bind to dataPath | MISSING |
| 6 | 0x10be3e | `FileManager::GetInstance` (0x000fa524) | -- | MISSING |
| 7 | 0x10be48 | `FileManager::AddSystem(fileMgr, fsDir, 0, 0)` (0x00101064) | mount fs at root | MISSING |
| 8 | 0x10be4c..0x10be54 | `DisplayManager::GetInstance` + `vtable[18]` (`ShouldUseHDFonts`) | branch flag | DIFFERS (port hardcodes false) |
| 9 | 0x10be58..0x10be70 | `DisplayManager::SetWindowSize(0,320,0,480)` via vtable[10] | configure window dims | DIFFERS (port passes 0,0,FN_SCREEN_W,FN_SCREEN_H) |
|10 | 0x10be72..0x10be82 | `DisplayManager::Init(displaySurface, dataPath, 0)` via vtable[2] | wire EGL surface | MISSING (port has no displaySurface plumbing) |
|11 | 0x10be84..0x10be9a | `DisplayManager::SetClearColour(black)` (0x000f3c54) | clear color | OK |
|12 | 0x10be9e..0x10beb2 | `DisplayManager::SetLightDirection(0?, -10, -5)` (0x0010350c) | global light | OK |
|13 | 0x10beba | (no thunk -- ARM imm) | -- | -- |
|14 | 0x10bebe | `InitialiseData` (0x001071cc -> 0x0010b66c) | save load + 15 sub-steps | DIFFERS (only items 7,9,10,14 ported) |
|15 | 0x10bec8..0x10bede | `IsFastHardware` + `DisplayManager::SetTextureOverloadPrefix` | HD/SD asset path prefix | MISSING |
|16 | 0x10bee2..0x10beea | `TextureManager::GetInstance` + `Initialise` (0x000f7284) | texture cache | MISSING |
|17 | 0x10beee..0x10bef6 | `TextureManager::Initialise` (called twice) | cache slot 2 | MISSING |
|18 | 0x10befa..0x10bf02 | `MeshManager::Initialise(0x26c00)` (0x000fbdfc) | 158 KB mesh arena | DIFFERS (port: `meshMgr.Initialise(32)` -- WRONG SIZE: 32 vs 0x26c00) |
|19 | 0x10bf0e | `AnimationManager::Initialise(arg)` (0x00105468) | animation cache | MISSING |
|20 | 0x10bf12..0x10bf1a | `InputManager::GetInstance` + `Init` (0x00101b44) | input subsystem | OK (with Touch::Update wiring) |
|21 | 0x10bf1e..0x10bf3c | `PSPParticleManager::LoadFile(slow.xml or fast.xml, idx?)` | particle XML | OK (port hardcodes `particles_fast.xml`) |
|22 | 0x10bf40..0x10bf48 | `PowerUpManager::Load` (0x00107430) | parse power-up XML | MISSING |
|23 | 0x10bf4c..0x10bf50 | `LeaderboardManager::Init` (0x000fd4ac) | OpenFeint init | SKIP (defunct) |
|24 | 0x10bf5a..0x10c00a | `NetworkManager::SetStatusMessageText` x11 (id 0..0xa) | localised network strings | SKIP (defunct) |
|25 | 0x10c020..0x10c060 | `NetworkManager::SetGameCenterInitializationCallback` + delegate | GC init callback | SKIP (defunct) |
|26 | 0x10c064..0x10c0a0 | `(*netMgr->vtable[0])(netMgr, 0)` -- enable Game Center | Game Center toggle | SKIP (defunct) |
|27 | 0x10c0b6..0x10c0ee | 3x delegate ctors + `NetworkManager::InitializeP2P` (0x000f91b0) | P2P multiplayer init | SKIP (defunct) |
|28 | 0x10c10c..0x10c1de | StringHash("p2p_total") + `FruitSaveData::GetTotal` x3 + `NetworkManager::SetPreferredNetworkProvider` | restore last-used network provider | SKIP (defunct) |
|29 | 0x10c1e6 | `P2PConnect(true)` (0x000f44d0) | start P2P advertise | SKIP (defunct) |
|30 | 0x10c1ec..0x10c23e | `new FruitCamera(0x16c) + ctor + Init(1.0f, 10000.0f, 16.95f, 11.3f)` | game camera | OK |
|31 | 0x10c240..0x10c25e | zero g_GameData fields (`worldPos`, +0x50..+0x80, +0x180=0) | clear stale per-camera fields | OK (worldPos zeroed; SmartPtrs default-null) |
|32 | 0x10c260..0x10c286 | `pFontMain` (+0x54): SD or HD `font_fruit_ninja.fnt` | Font #1 | OK |
|33 | 0x10c290..0x10c2bc | `pFontNumbers` (+0x58): null-guarded SD/HD `fruit_ninja_numbers.fnt` | Font #2 | OK |
|34 | 0x10c2c6..0x10c2fa | `pFontArcade` (+0x6C): null-guarded `arcade_results_numbers.fnt`, then alias into +0x70/74/78/7C | Font #3 + 4 aliases | OK |
|35 | 0x10c2fe..0x10c326 | `File::Exists` + (if) `pFontGold` (+0x70): `gold_numbers.fnt` | Font #4 | OK |
|36 | 0x10c32a..0x10c354 | `File::Exists` + (if) `pFontSilver` (+0x74) | Font #5 | OK |
|37 | 0x10c358..0x10c382 | `File::Exists` + (if) `pFontBronze` (+0x78) | Font #6 | OK |
|38 | 0x10c386..0x10c3ac | `pFontBlue2` (+0x80): null-guarded `fruit_ninja_numbers_blue2.fnt` | Font #7 | OK |
|39 | 0x10c3b0..0x10c3da | `pFontGreen` (+0x68): null-guarded `fruit_ninja_numbers_green.fnt` | Font #8 | OK |
|40 | 0x10c3de..0x10c406 | `LoadLocalisedTexture(...)` -> `g_GameData+0x17c` (`SmartPtr<Texture>`) | localised "fruit atlas" | MISSING |
|41 | 0x10c40a | `MenuButton::LoadContent` (0x00103890) | menu sprites | OK |
|42 | 0x10c40e | `Fruit::LoadInfo` (0x0010812c) | parse fruitlist.xml | OK |
|43 | 0x10c412 | `SplatEntity::LoadContent` (0x00106a28) | splat textures | OK |
|44 | 0x10c416 | `SlashEntity::LoadContent` (0x000fb6b8) | blade trail textures | OK |
|45 | 0x10c41a | `Bomb::LoadContent` (0x00106950) | bomb model + textures | OK |
|46 | 0x10c41e | `GameOverScreen::LoadContent` (0x00106dd0) | game-over UI textures | STUB (port calls method, but body is `// TODO`) |
|47 | 0x10c422 | `PowerUpShop::LoadContent` (0x000fd7e8) | power-up shop textures | STUB |
|48 | 0x10c426 | `PreloadSounds` (0x00101cac) | preload 25 named WAVs + per-fruit + arcade*N + 7-* tone variants | MISSING |

`InitialiseData` (call #14) sub-steps, per `0x0010b66c`:

| Sub | Call | Port |
|-----|------|------|
| 1a  | `StringTableUtilInit` | MISSING |
| 1b  | `StringTableUtilLoadStrings` -> `LoadStringsTable(0)` | OK (port: `Localisation::Load`) |
| 2   | `memset(g_GameData+0x1b1, 0, 0x100)` x4 -- 1024-byte item/achievement state | MISSING |
| 3a  | `g_GameData+0x85 = 0` | MISSING |
| 3b  | `g_GameData+4 = 0` (gameMode pre-clear) | OK (GamePreInitialise) |
| 4   | `new GameSound(0x708) + ctor` -> `g_GameData+0x188` | OK (different new size, port struct may diverge) |
| 5   | `new FruitSaveData(0x238) + ctor` -> `g_GameData+0x4c` | OK |
| 6   | `LoadGame(saveData)` | OK (`FruitNinja_LoadGame`) |
| 7   | `g_GameData+4 = saveData->m_GameMode (+0x6c)` | MISSING (port leaves gameMode at 0) |
| 8   | `SetupGameWork` (0x0010b4e8) -- writes 16 fields including `gameMode=2`, `+0x88 = const`, `+0x10/2c/1a0 = const`, AddToTotal("plays_total", +1) | MISSING |
| 9   | `m_bSoundOn = (GetTotal("sound_off") == 0)` | MISSING |
| 10  | `m_bMusicOn = (GetTotal("music_off") == 0)` | MISSING |
| 11  | Reset both totals to 0 (consume the muted-flag) | MISSING |
| 12  | `SlashEntity::InitModColours` | MISSING |
| 13  | `AchievementManager::LoadAchievementInfo` | SKIP (defunct) |
| 14  | `ItemManager::LoadItemData` | OK |
| 15  | `BonusManager::Init` | MISSING |

Notes:

- The port replaces step 4 (Bada `FileSystem_Direct` + `FileManager::AddSystem`)
  with direct `fopen`/`FileManager::OpenCI`. That is intentional and the
  comment in `GameInitialise.cpp` already flags this as "skipped, port uses
  direct filesystem". MISSING in the table reflects the binary call sequence
  but is **not** an action item.
- Step 14 (NetworkManager / Game Center / OpenFeint / P2P -- calls #23 to
  #29) is whole-chunk SKIP per project policy (defunct online services).
- Calls flagged DIFFERS for window-size / HD-fonts are port-required
  divergences (resolution, hardware probe). Not action items.
- **Real bug**: call #18 -- `MeshManager::Initialise` is invoked with `32`
  in the port but `0x26c00` (158720) in the binary. This is the mesh
  arena size in bytes, not a slot count. Port likely runs OK because it
  uses `std::map`-backed cache, but the constant should at least carry a
  matching `// DIFFERS` comment.

## Section 3 -- GameTaskInitInput (0x00169670) per-call audit

| Bin region | Call | Purpose | Port |
|------------|------|---------|------|
| 0x16967e..0x169688 | `InputManager::GetInstance` + `InputManager_LoadConfigFile(path)` (0x001022d0) | parse `input.xml`-style config (key map) | MISSING |
| 0x169690..0x1696a8 | iter=0..15: `g_TaskState[playerSlot+0xa0..0xa8]` = vec3 init from DAT, `ActorManager::Add(3, true)` (entity type 3 = TouchListener), 12-byte stride | per-region touch entity alloc | MISSING (full loop body) |
| 0x1696ac..0x16976e | iter loop body: `Entity::vtable[2](0,0,&Stack_7c)` (Init), `OS_SPrintf` x3 to build `"touch%d"`/`"touchUp%d"`/`"touchMove%d"` strings | per-region callback names | MISSING |
| 0x169770..0x169a30 | iter loop body: 3x `InputManager::RegisterInputCallback(hash, delegate)` -- touch down, touch up (slot=0x59), touch move | per-region callback registration | MISSING |
| 0x169a32..end | 7x global callbacks: `RegisterInputCallback(hash(name), delegate)` -- KEY_FIRE / KEY_PAUSE / KEY_BACK / accel / etc. | global key + accel handlers | MISSING |

The port's `GameTaskInitInput()` is a 16-line stub with `// TODO`. The body
is **not optional** -- without the per-region touch callbacks, every touch
slot dispatch through `InputManager::DispatchEvent` fails to land on
`Fruit::Slice` or any other gameplay handler. The port currently bypasses
this through `Mortar::Touch::Update()` polling directly from `GameUpdate`,
so single-touch slicing works, but multi-touch and key/back/pause dispatch
do not flow through the binary's intended `InputManager` pipeline.

## Section 4 -- Missing-stub list

For each unimplemented binary call, recommended port-side action.

### High priority -- correctness-affecting

| Binary call | Addr | Action |
|-------------|------|--------|
| `InitialiseData` step 2: zero `+0x1b1..+0x5b0` (1024 bytes of item/achievement state) | inside `Game` struct | Add `memset(&game->itemFlags[0], 0, 1024)` to `GamePreInitialise` (or replace per-field clears with full `memset(game, 0, sizeof(Game))` to mirror the binary's `CpuFill8`) |
| `InitialiseData` step 7: `gameMode = saveData->m_GameMode` | -- | After `FruitNinja_LoadGame`, set `game->gameMode = saveData->m_GameMode` |
| `InitialiseData` step 8: `SetupGameWork` (0x0010b4e8) | new file | Port as `src/game/SetupGameWork.cpp`. Sets `gameMode=2` (Classic default), 16 field stores, increments `plays_total` save counter, copies `saveData[+0x110]` into Game `+0x30`. Body in `re-analyst` decompile already RE'd. |
| `InitialiseData` steps 9-11: `m_bSoundOn / m_bMusicOn` from save totals + reset | -- | After `LoadGame`, hash `"sound_off"`/`"music_off"`, read totals, set `game->m_bSoundOn = (totalSoundOff==0)`, then `AddToTotal(soundOff, -total)`. Mirror for music. |
| `InitialiseData` step 12: `SlashEntity::InitModColours` | `src/entities/SlashEntity.{h,cpp}` | Method exists conceptually; add static `InitModColours()` that fills the per-power-up colour table read by `SlashEntity::SetMod`. Body in binary: `0x...` (RE not pulled in this audit; tag as RE-gap). |
| `InitialiseData` step 15: `BonusManager::Init` | new `src/game/BonusManager.{h,cpp}` | RE-gap: BonusManager class (combo/streak bonus tracker) not yet ported. Stub `BonusManager::GetInstance()->Init()` no-op until RE'd. |
| `MeshManager::Initialise(0x26c00)` (call #18) | `src/game/GameInitialise.cpp:99` | Change `meshMgr.Initialise(32)` -> `meshMgr.Initialise(0x26C00)` and add `// DIFFERS: port uses std::map cache, size unused but matches binary literal`. |
| `Localised fruit atlas` -> `Game+0x17c` (call #40) | `src/game/Game.h` (existing `field_0x17c` SmartPtr) | After font block: `game->field_0x17c = TextureManager::LoadLocalisedTexture("...")`. Need the texture name (DAT_0010c46c -- string not pulled). Currently the SmartPtr stays null; consumers of +0x17c (TBD) silently get nothing. RE-gap: which texture name? |
| `PreloadSounds` (call #48) | `src/audio/PreloadSounds.cpp` (new) | Port `0x0010b204` body: 25 hard-coded sound names + iterate FRUIT_INFO->m_pSounds + 7 `arcade%d.wav` + 3 `<other>%d.wav` patterns. Without this, sounds load on-demand which causes audible hitches on first slice/explosion. Stub: `void PreloadSounds() {}` until ported. |
| `GameTaskInitInput` (357 lines) | `src/game/GameTaskInput.cpp` | Port the full body. See Section 3. |
| `PowerUpManager::Load` (call #22) | new `src/game/PowerUpManager.{h,cpp}` | RE-gap: PowerUpManager class not yet ported. Stub no-op. |
| `g_TaskState->initComplete (+0x112)` gate semantics | `src/game/GameTaskState.h` | Port already exposes `initComplete` but skips the **early-return** at the top of GameInit when it's already 1. Wrap the entire `GameInit()` body in `if (ts->initComplete) return;`. Otherwise re-entering State 2 (e.g. via `GameTaskUpdate` state-change path) re-runs the entire 274-line setup, leaking heaps and re-allocating MainScreen/PauseScreen. |

### Medium priority -- hygiene / fidelity

| Binary call | Action |
|-------------|--------|
| `TextureManager::GetInstance + Initialise` x2 (calls #16-17) | Add `TextureManager::Init()` stub on existing class, call twice to mirror the binary (the second call is idempotent). Currently port assumes lazy-init. |
| `AnimationManager::Initialise` (call #19) | Stub `AnimationManager::Init()` -- port has no animation system today; safe no-op. |
| `DisplayManager::SetTextureOverloadPrefix(prefix)` (call #15) | Stub on `DisplayManager`. The prefix is the HD/SD path prefix appended to texture loads (e.g. `"hd/"` vs `""`). Port can set `""` and add `// DIFFERS: HD assets not shipped`. |
| `StringTableUtilInit` (InitialiseData step 1a) | One-shot init for the `gPaketDecorator` string table machinery. Port's `Localisation::Load` already replaces `LoadStringsTable`, so this is folded in. Add a comment, no code. |
| `g_GameData+0x85 = 0` (InitialiseData step 3a) | Add `game->field_0x85 = 0;` -- semantics TBD (RE-gap). Currently the field is implicitly zero from `CpuFill8`-equivalent, but explicit clear documents intent. |

### Low priority -- defunct / SKIP

- All NetworkManager / GameCenter / OpenFeint / P2P calls (table rows
  #23..#29). These remain SKIP per project policy.
- `LeaderboardManager::Init` (call #23). Defunct.
- `AchievementManager::LoadAchievementInfo` (InitialiseData step 13). Defunct.

## Section 5 -- Reorder / fidelity action list

File-by-file edits, in priority order. (Spec only -- implementer applies.)

1. **`src/game/GameInitialise.cpp:99`**
   `meshMgr.Initialise(32)` -> `meshMgr.Initialise(0x26C00); // DIFFERS: port cache is std::map, size advisory`

2. **`src/game/GameInitialise.cpp` -- after `FruitNinja_LoadGame(...)` at line 126**, INSERT (in order, mirroring `InitialiseData @ 0x0010b66c` steps 7..12,15):
   ```cpp
   // InitialiseData step 7: restore last-used game mode
   game->gameMode = game->pSaveData->m_GameMode;
   // InitialiseData step 8: SetupGameWork (0x0010b4e8)
   SetupGameWork();
   // InitialiseData steps 9-11: sound/music on/off from save totals
   const unsigned int hSoundOff = StringHash("sound_off");
   const unsigned int hMusicOff = StringHash("music_off");
   game->m_bSoundOn = (game->pSaveData->GetTotal(hSoundOff) == 0);
   game->m_bMusicOn = (game->pSaveData->GetTotal(hMusicOff) == 0);
   game->pSaveData->AddToTotal("sound_off", hSoundOff,
       -game->pSaveData->GetTotal(hSoundOff), false, true);
   game->pSaveData->AddToTotal("music_off", hMusicOff,
       -game->pSaveData->GetTotal(hMusicOff), false, true);
   // InitialiseData step 12: per-power-up colour table
   SlashEntity::InitModColours();
   // InitialiseData step 15: BonusManager (combo/streak)
   BonusManager::GetInstance()->Init();      // STUB until ported
   ```

3. **`src/game/GameInitialise.cpp` -- before MenuButton::LoadContent** (i.e. between current font block and `MenuButton::LoadContent()` at line 259), INSERT:
   ```cpp
   // Binary call #40: localised fruit atlas -> Game+0x17c
   // RE-gap: confirm the texture name (DAT_0010c46c). Suspected
   // "fruit_atlas.tex" or similar; check via Ghidra string xref.
   // game->field_0x17c = Mortar::TextureManager::LoadLocalisedTexture("...");
   ```

4. **`src/game/GameInitialise.cpp:283` -- replace `// TODO: PreloadSounds` with**:
   ```cpp
   PreloadSounds();   // STUB until ported -- 25 named WAVs + per-fruit
   ```

5. **`src/game/GameInit.cpp:53` -- top of `GameInit()`, INSERT guard**:
   ```cpp
   GameTaskState* ts = GetTaskState();
   if (ts->initComplete) return;           // matches binary 0x0016c660 guard
   ```
   ...and remove the existing scattered `ts->initComplete = true` / `firstFrame = false` writes from step 10 -- the binary sets `+0x112=1` near the top of the function (after step 10) but the gate must be at function entry. Fix-up: keep the writes in step 10, also keep the early-return.

6. **`src/game/GameTaskInput.cpp` -- replace the stub with the full
   16-region + 7-global body**. Spec is in Section 3 of this doc and in
   `docs/systems/gameinit-todos.md` step 18.

7. **New file `src/game/SetupGameWork.cpp`** -- port of `0x0010b4e8`. Body
   already RE'd in the decompile in this audit (15 field stores +
   `AddToTotal("plays_total", +1, true, true)` + read of
   `saveData->+0x110` into `game->+0x30`). Add header in
   `src/game/SetupGameWork.h`.

8. **New stub class `src/game/PowerUpManager.{h,cpp}`** -- declare
   `Load()` and `GetInstance()`; both no-op; called from
   `GameInitialise` to satisfy call #22.

9. **New stub class `src/game/BonusManager.{h,cpp}`** -- declare `Init()`
   and `GetInstance()`; both no-op; called from `GameInitialise` step 15.

10. **`src/game/GameDestroy.cpp` (or `GameInitialise.cpp` tail)** -- the
    current `GameDestroy()` already covers most of the binary's 174-line
    teardown. Outstanding TODOs already flagged in code; re-confirm the
    teardown order against the binary `0x0010b7ec`:
    - LeaderboardManager::Destroy + NetworkManager::vtable[1]
      (defunct, SKIP).
    - AchievementManager::UnLoadAchievementInfo +
      ItemManager::UnLoadItemData (one is defunct, the other should be
      added).
    - All current TODOs (PSPParticleManager::Destroy,
      StringTableUtilUnload, Cleanup* x4, InputManager/TextureManager x2/
      AnimationManager/MeshManager/DisplayManager/SoundManager/
      SystemManager) -- bulk-stub on the relevant managers. Each is a
      simple `Destroy()` no-op stub.

## Verdict

The port's `GameInitialise` matches the binary on the **content-load**
tail (fonts, MenuButton/Fruit/SplatEntity/SlashEntity/Bomb LoadContent)
and on the FruitCamera + FruitSaveData blocks. The early **subsystem-init**
section is materially incomplete: file system, texture manager,
animation manager, power-up manager, preload-sounds, and most of
`InitialiseData` (steps 2,3a,7-12,15) are all MISSING. The
`MeshManager::Initialise` size constant is wrong (32 vs `0x26C00`) but
unlikely to break behaviour given the std::map-backed port cache.

`GameInit` (state-2 setup) is well-aligned with the binary at the call
level -- the 23 documented steps match -- but the **`initComplete` early
return at function entry is missing**, which means re-entering State 2
re-runs the whole sequence (heap leak + dup MainScreen/PauseScreen).

`GameTaskInitInput` is a stub; the binary's 16-region touch + 7 global
callback registration is entirely absent. Single-touch slicing works
today only because the port polls `Mortar::Touch::Update()` from the
update loop, bypassing the InputManager dispatch the binary relies on.

`GamePreInitialise` matches the binary's intent (zero the singleton)
but per-field instead of `CpuFill8`; functionally equivalent.

`GameDestroy` is largely stubbed but covers the high-impact teardown
(HUD, Camera, Fonts, FruitSaveData, GameSound, ActorManager). Engine
manager `Destroy()` calls remain TODO.
