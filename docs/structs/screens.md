# Screen Classes

Full-screen UI panels used in Fruit Ninja (Bada/Mortar Engine). All inherit from
`HUDControl3d` (or `BaseScreen -> HUDControl3d`). Sizes measured from the highest
field offset written in constructor + field width.

---

## AboutScreen

**Constructor**: `0x0012ecb8` -- `AboutScreen::AboutScreen(DojoScreen*)`
**Update**: `0x0012f020` -- `AboutScreen::Update(float)`
**Draw**: `0x0012f394` -- `AboutScreen::Draw(float*)`

**Base class**: `HUDControl3d` (size 0x74 for base fields)

**Struct size**: ~0xA0 (highest field = `field_0x9c` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00 | vtable* | vtable | Set to AboutScreen vtable |
| 0x00-0x73 | | (HUDControl3d base) | Includes pos, size, layer flags at 0x34 |
| 0x74 | SmartPtr\<Texture\> | m_BackgroundTex | Texture assigned from content; dimensions queried for sizing |
| 0x7C | float | m_TransitionAlpha | Lerped 0->1 in state 0, multiplied by 0.75 in state 2 |
| 0x8C | MenuButton* | m_BackButton | Created lazily in Update when state==0 completes (QCallee\<AboutScreen\>) |
| 0x90 | DojoScreen* | m_ParentDojo | Pointer to parent DojoScreen passed in constructor |
| 0x94 | MenuButton* | m_CreditsButton | Created first in Update; text/credits button |
| 0x98 | SmartPtr\<Texture\> | m_Texture2 | Additional texture SmartPtr |
| 0x9C | int | m_State | 0 = transition-in, 1 = idle, 2 = transition-out |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Lerps `m_TransitionAlpha` toward 1.0 (step 0.125). On first call, creates m_CreditsButton (MenuButton at 0x94). When alpha reaches threshold, creates m_BackButton (0x8C), sets state=1. |
| 1 | Idle -- buttons are interactive |
| 2 | Fades out: `m_TransitionAlpha *= 0.75`. When below threshold, marks self for removal (`m_bNoDestructor = 1`). |

---

## ShopScreen

**Constructor**: `0x0015cdac` -- `ShopScreen::ShopScreen(DojoScreen*)`
**Update**: `0x0015e1f4` -- `ShopScreen::Update(float)` (381 lines)
**Draw**: `0x0015dd50` -- `ShopScreen::Draw(float*)`

**Base class**: `HUDControl3d`

**Struct size**: ~0xBC (highest field = `field_0xb8` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x73 | | (HUDControl3d base) | |
| 0x74 | SmartPtr\<Texture\> | m_Texture | SetNull in constructor |
| 0x7C | float | m_TransitionAlpha | Initialized to 0; lerped toward 1.0 |
| 0x80 | int | m_LayerFlagsAlt | Set to 0x40 when no active splats |
| 0x84 | MenuButton* | m_BuyButton | Created lazily; has OnPress + OnHighlight delegates |
| 0x88 | float | m_BuyDelay | Timer decremented by dt; controls when buy button appears |
| 0x8C | MenuButton* | m_EquipButton | Created lazily for equip action |
| 0x90 | DojoScreen* | m_ParentDojo | Parent screen |
| 0x94 | ShopListControl* | m_ShopList | Scrollable list; selection tracked |
| 0x98 | int | m_SelectedIndex | Compared against ShopList current index |
| 0xAC | float | m_ScrollOffset | Computed from list item count + 0.5 |
| 0xB4 | int | m_AnimFrame | Animation counter for scroll |
| 0xB8 | int | m_State | State machine index |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Transition in: lerps alpha to 1.0. On completion, creates m_BuyButton (0x84) with QCallee\<ShopScreen\>, removes all splats, sets state=1. |
| 1 | Active: manages buy delay timer, creates equip button (0x8C) if needed. Calls `SetSelected()` on list changes. Manages `ItemManager::IsEquipped` checks. |
| 2 | Transition out (to dojo): `alpha *= 0.75`. When below threshold and parent exists, marks parent `m_bNoDestructor=1`, self pending removal, changes GameState to 8. |
| 3 | Buy animation: `alpha *= 0.75`. On completion, creates a new buy button with fruit animation. Flings old button fruit offscreen with random velocity. |
| 4 | Resets layer flags to 0x80. |
| 5-6 | Similar to 3 -- fruit fling animation for purchased item. |
| 7 | Same as 2 -- transition out variant (goes back to dojo). |

---

## DojoScreen

**Constructor**: `0x00137b90` -- `DojoScreen::DojoScreen()`
**Update**: `0x00138414` -- `DojoScreen::Update(float)` (247 lines)
**Draw**: `0x0013822c` -- `DojoScreen::Draw(float*)`

**Base class**: `BaseScreen -> HUDControl3d`

**Struct size**: ~0xA4 (highest field = `field4_0xa0` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x93 | | (BaseScreen base) | BaseScreen adds fields at 0x8C (transition alpha) and 0x90 (state) |
| 0x8C | float | m_TransitionAlpha | (in BaseScreen) Transition interpolation factor |
| 0x90 | int | m_State | (in BaseScreen) State machine |
| 0x94 | MenuButton* | m_PlayButton | Main play/dojo button; created lazily with QCallee\<DojoScreen\> |
| 0x98 | MenuButton* | m_ShopButton | Shop button; checks `ItemManager::AreNewItems()` for "new" badge |
| 0x9C | MenuButton* | m_AboutButton | About/credits button |
| 0xA0 | int | m_Unknown | Set to 0 in constructor |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Transition in: calls `BaseScreen::UpdateButtons`. Lerps `m_TransitionAlpha` toward 1.0 (step 0.25). Lazily creates all three buttons: m_PlayButton (0x94), m_ShopButton (0x98 -- with `ItemManager::AreNewItems` new-symbol check), m_AboutButton (0x9C). When alpha threshold reached, sets state=1. |
| 1 | Idle: checks `ItemManager::AreNewItems()` each frame to update shop button new-symbol. |
| 2-3 | Transition out: `alpha *= 0.75`. When below threshold, nulls button pointers. State 3 creates `AboutScreen` child. State 2 creates `ShopScreen` child. |
| 4 | Network/dashboard: waits for no active entities, then calls `NetworkManager::LaunchDashboard`, resets to state 0. |
| 6 | Fade out for game start: `alpha *= 0.75`. When below threshold, marks pending removal and sets GameState = 8. |

---

## PauseScreen

**Constructor**: `0x00155460` -- `PauseScreen::PauseScreen()` (101 lines)
**Update**: `0x00154468` -- `PauseScreen::Update(float)` (569 lines)

**Base class**: `HUDControl3d` (with extra texture SmartPtrs)

**Struct size**: ~0xD8 (highest field = `field35_0xd4` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x73 | | (HUDControl3d base) | |
| 0x74 | SmartPtr\<Texture\> | m_PauseTitleTex | (inherited) Title texture |
| 0x7C | HUDControlFns* / float | m_TransitionAlpha | Used as float; controls fade in/out |
| 0x84-0x94 | float[3]+float | m_ButtonOriginPos | Stored position for button layout. field5_0x8C-0x94 = saved button pos |
| 0x98 | MenuButton* | m_ResumeButton | Resume/play button (uses m_PauseButtonTex) |
| 0x9C | MenuButton* | m_Player2ResumeBtn | Only created in same-screen multiplayer |
| 0xA0 | MenuButton* | m_QuitButton | Uses m_QuitTitleTex |
| 0xA4 | MenuButton* | m_P2QuitButton | Only for same-screen multiplayer |
| 0xA8 | SmartPtr\<Texture\> | m_PauseButtonTex | Loaded in constructor |
| 0xAC | MenuButton* | m_RetryButton | Uses m_RetryButtonTex |
| 0xB0 | MenuButton* | m_P2RetryButton | Multiplayer retry |
| 0xB4 | float | m_ButtonFadeAlpha | Separate alpha for button fading |
| 0xB8 | SmartPtr\<Texture\> | m_PlayButtonTex | Loaded in constructor |
| 0xBC | SmartPtr\<Texture\> | m_QuitTitleTex | Loaded in constructor |
| 0xC0 | SmartPtr\<Texture\> | m_RetryButtonTex | Loaded in constructor |
| 0xC4 | undefined4 | m_Unknown_C4 | |
| 0xC8 | int | m_LastHitButton | Init to -1 (0xFFFFFFFF); index of last-hit menu button |
| 0xCC | int | m_PlayerIndex | 0 or 1 for same-screen multiplayer button layout |
| 0xD0 | float | m_ButtonTimer | Timer for button reveal delay |
| 0xD4 | int | m_State | State machine |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Initializing: Creates buttons lazily: m_ResumeButton (0x98), m_QuitButton (0xA0), m_RetryButton (0xAC). In multiplayer, also creates P2 variants (0x9C, 0xA4, 0xB0). Fade: `alpha *= 0.75`, clamp. Timer-based button reveal. |
| 1 | Bomb flash state: sets alpha=1, button fade=1. Calls `BombFlashFull()`. Resets `PowerUpManager`. Returns to state 0. |
| 2 | Transition from paused to unpaused: lerps alpha toward 1.0 (step 0.25). Marks `game.paused=true` if not online. When threshold reached, goes to state 3. |
| 3 | Active/paused: buttons enabled. Resume (0x98) and Retry (0xAC) are interactable. Marks game paused for non-online. |
| 4-5 | Button press fadeout: `alpha *= 0.75`. State 5 = retry (calls `RetryLevel()`, `SaveCurrentData`, `UnpauseGame`). State 4 = resume (`UnpauseGame`). |
| 6 | Quit confirmation: `alpha *= 0.5` then `*= 0.75`. Calls `QuitToMenu()`, `HitMenuBomb` at button position, `SaveCurrentData`. Returns to state 1. |

### Notes
- Swaps texture on resume button between `m_PauseButtonTex` and `m_PlayButtonTex` based on alpha (pause/unpause visual).
- Buttons use `SetSingular()` for exclusive hit detection.
- In online multiplayer, quit button is hidden and layout adjusted.

---

## GameOverScreen

**Constructor**: `0x0014297c` -- `GameOverScreen::GameOverScreen(char const*, int, float, int, int, int, int)`
**Initialise**: `0x00142674` -- `GameOverScreen::Initialise(char const*, int, float, int, int, int, int)` (133 lines)
**Update**: `0x00141b34` -- `GameOverScreen::Update(float)` (529 lines)

**Base class**: `HUDControl3d`

**Struct size**: ~0x13C (highest field = `field111_0x138` + 4)

### Constructor Parameters

```c
GameOverScreen(const char* modeName, int startState, float startTimer,
               int expressionIdx, int bgPatternIdx, int pomCount, int starCount)
```

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x73 | | (HUDControl3d base) | |
| 0x74 | SmartPtr\<Texture\> | m_PauseTitleTex | (inherited) Title texture, set per game mode |
| 0x7C | float | m_InitialFadeVal | Set to DAT constant in Initialise |
| 0x80 | int | m_State | Main state machine variable |
| 0x84 | float | m_Timer | Accumulates `dt`; used for entry animation |
| 0x88-0x8C | float[2] | m_TitleSize | Computed from title texture dimensions |
| 0x90 | float | m_TitleSizeZ | |
| 0x94 | int | m_Unknown_94 | Set to 0 |
| 0x98 | MenuButton* | m_RetryButton | |
| 0x9C | MenuButton* | m_QuitButton | |
| 0xA0 | MenuButton* | m_Button3 | Third button |
| 0xA4 | MenuButton* | m_Button4 | Fourth button |
| 0xA8 | MenuButton* | m_Button5 | |
| 0xAC | int | m_AnimCounter | Accumulates `dt * speed`; mod 1000 counter |
| 0xB0-0xB8 | Vector3 | m_OffsetPos | Position offset for content layout |
| 0xBC | FruitFactControl* | m_FruitFactCtrl | Created in state 6 for fruit-fact display |
| 0xC0 | MenuButton* | m_ExtraButton | |
| 0xC4 | BonusScreen* | m_BonusScreen | Created in state 1 via `BonusScreen::BonusScreen()` |
| 0xC8 | int | m_Unknown_C8 | Set to 0 in Initialise |
| 0xCC | char | m_Flag_CC | |
| 0xCD | char | m_Flag_CD | |
| 0xCE | char[64] | m_ScoreLabel | `OS_SPrintf` with score difference string |
| 0x10E | -- | -- | (end of string buffer) |
| 0x110 | int | m_ProgressCounter | Counts to 10, then 11 for score submission |
| 0x114 | SmartPtr\<Texture\> | m_ExtraTex | Extra texture |
| 0x118 | int | m_Unknown_118 | Set to 0 |
| 0x11C | int | m_Unknown_11C | Set to -1 |
| 0x120 | char | m_ScoreSubmitted | Flag: set to 1 after score submission |
| 0x124 | int | m_ExpressionIdx | param_4 from constructor -- ninja expression |
| 0x128 | int | m_BgPatternIdx | param_5 |
| 0x12C | int | m_PomCount | param_6 |
| 0x130 | int | m_StarCount | param_7 |
| 0x134 | char | m_IsNoHighscore | `true` if game mode char == 0 |
| 0x138 | float | m_ScreenTransition | Camera/screen transition interpolator |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Entry animation: waits for entities to clear (Zen/Arcade check). Accumulates timer, uses sin-based scaling for bounce-in. Transitions to state 1 (or `SetStateWait` for classic). |
| 1 | Bonus phase: waits for entities to clear. Creates `BonusScreen` (0xC4) via `BonusManager::SetUpBonusScreen`. Scrolls title up alongside bonus content. |
| 6 | Main game-over display: Creates `FruitFactControl` (0xBC). Creates retry/quit buttons (`CreateRetryButton`, `CreateQuitButton`). Submits score: `FruitSaveData::AddToTotal`, `AchievementManager::Unlock*`, `LeaderboardManager::RefreshLeaderboard`, `SetCurrentModeHighscore`. Loads localised "game over" texture. Positions all elements based on `m_ScreenTransition`. |
| 7 | Wait for entities, reset waves, transition to state 8. |
| 8 | Fade camera: `camera.zoom *= 0.75`. When below threshold, resets wave manager, calls `WaveManager::NewGame`, `SetTerminate`. |
| 9 | Quit: waits for entities to clear, calls `QuitToMenu`, goes to state 11. |
| 10 | Multiplayer cleanup: waits for all entities, calls `NetworkManager::LaunchDashboard`. |
| 11 | Camera fade out: `zoom *= 0.75`. When below threshold, calls `SetTerminate`. |
| 14 | Quick restart: accumulates timer * 8, resets fruit fact display state, sets state=6. |

---

## GameModeScreen

**Constructor**: `0x0013e524` -- `GameModeScreen::GameModeScreen(bool)`
**Update**: `0x0013f10c` -- `GameModeScreen::Update(float)` (212 lines)
**CreateControls**: `0x0013e764` -- `GameModeScreen::CreateControls()` (194 lines)

**Base class**: `BaseScreen -> HUDControl3d`

**Struct size**: ~0xD0 (highest field = `field39_0xcc` + 4)

### Constructor Parameters

```c
GameModeScreen(bool isFromPause)  // param_1 stored at 0xB8
```

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x93 | | (BaseScreen base) | |
| 0x8C | float | m_TransitionAlpha | (in BaseScreen) |
| 0x90 | int | m_State | (in BaseScreen) |
| 0xA0 | MenuButton* | m_ClassicButton | First mode button, created in CreateControls |
| 0xA4 | float | m_ButtonDelay | Timer; -1.0 means inactive |
| 0xA8 | float | m_Unknown_A8 | Set to -1.0 in state 0 transition |
| 0xAC | int | m_Unknown_AC | Set to 0 |
| 0xB0 | int | m_Unknown_B0 | Set to 0 |
| 0xB4 | float | m_SecondaryAlpha | Starts at -2.5; lerped toward 0 then 1 |
| 0xB8 | bool | m_IsFromPause | Constructor param; affects online MP button |
| 0xB9 | char | m_Unknown_B9 | Set to 0 |
| 0xC4 | int | m_LayerFlagsAlt | Set to 0x80 |
| 0xC8 | float | m_FrameTimer | Accumulates `dt / DAT` when state > 2; clamped to 0 |
| 0xCC | int | m_Unknown_CC | Set to 0 |

### CreateControls

Creates 4 MenuButtons for game modes (allocated at 0x15C bytes each):

1. **Classic mode** button -- stored at `in_r0 + 0xA0`, added to HUD with `SetSingular`, scaled 0.75
2. **Zen mode** button -- positioned based on network availability (different X/Y if online), uses `Fruit::FruitType` for icon, stored in separate variable, added to HUD, `TutorialControl::ResetTutePos` called
3. **Arcade mode** button -- similar pattern, positioned from global coordinate data, fruit icon, scaled with global factor
4. **Multiplayer** button -- `Fruit::RotateFacingUp` called on attached fruit, positioned from global coords

All buttons use `QCallee<GameModeScreen>` for their press callbacks.

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Initial transition: lerps alpha (step from global). Checks `IsTransitionInFinished` via vtable call. If finished, sets state=2, calls vtable+0x40 (CreateControls). |
| 1 | Alternate entry: same lerp, checks `IsTransitionInFinished`, sets state=2, sets `m_LayerFlagsAlt=1`. |
| 2 | Active: if network enabled and not from-pause, calls `UpdateOnlineMultiplayerButton`. Continues lerping alpha to 1.0. Lerps `m_SecondaryAlpha` toward 0 (step 0.25). Manages button delay timer. |
| 3-6 | Mode selected / transition out: `alpha *= DAT`, `secondaryAlpha *= DAT`. Fades camera zoom. When below threshold, plays SFX, marks pending removal, sets GameState = 0x11. In same-screen MP, also calls `SlashEntity::ColoursChanged`. |
| 7 | Return to menu: fades alpha. When threshold met (online check), opens matchmaker via `NetworkManager::OpenMatchmaker`. Decrements alpha by dt. |
| 8 | Recover from matchmaker: lerps secondaryAlpha back toward 0 (step 0.25). Waits for entities to clear, then connects Game Center. Sets state=9. |
| 9 | Final return: lerps secondaryAlpha toward 0. When threshold reached, resets `m_FrameTimer`, `m_SecondaryAlpha`, sets state=1. |
| 14 (0xE) | Quick fade: `alpha *= 0.75`, `secondaryAlpha` tracks. At 0.25 crossing, sets GameState=8. When below threshold, marks pending removal. |

---

## Common Patterns

### BaseScreen (shared by DojoScreen, GameModeScreen)

```
Offset  Type     Field
0x8C    float    m_TransitionAlpha   -- interpolation factor for screen transitions
0x90    int      m_State             -- state machine variable
```

`BaseScreen::UpdateButtons(float)` is called at the start of DojoScreen::Update.

### Button Creation Pattern

All screens lazily create `MenuButton` instances (size 0x15C) in Update rather than
in the constructor. The pattern is:

1. Check if pointer is null
2. Load texture from content manager
3. Create position Vector3
4. Set up `Delegate0<void>` callback via `QCallee<ScreenType>`
5. `operator_new(0x15c)` + `MenuButton::MenuButton(...)`
6. Store pointer in screen field
7. Call `Init()` via vtable (offset 8)
8. `HUD::AddControl` to register
9. Optionally: `TutorialControl::ResetTutePos`, `Vec3_ScaleConst`, `SetSingular`

### Transition Pattern

Screens use two transition styles:
- **Lerp in**: `alpha = alpha + (1.0 - alpha) * factor` (exponential ease-in, typically factor=0.125 or 0.25)
- **Fade out**: `alpha = alpha * 0.75` (exponential decay)

Both compare against a threshold constant to determine completion.
