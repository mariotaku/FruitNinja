# GameOverScreen

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


## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
- [Data classes](../structs/data-classes.md) -- FNHighscore, Bonus structs
