# Menu & UI Flow

## Menu/UI Flow

### Screen Hierarchy

All screens are managed within Game State 2 (GameTask). The `MainScreen` is the primary menu container; other screens are sub-screens shown/hidden via internal state transitions.

```
MainScreen (dojo / main menu)
  ├─ GameModeCallback()  → GameModeScreen (mode selection)
  │    ├─ ClassicModeCallback()  → Game.gameMode = 0, start game
  │    ├─ ArcadeModeCallback()   → Game.gameMode = 1, start game
  │    ├─ ZenModeCallback()      → Game.gameMode = 3, start game
  │    ├─ VersusModeCallback()   → multiplayer setup
  │    └─ SetupLevel()           → PrepareForLevelStart()
  ├─ NewGameCallback()   → direct game start (skips mode select)
  ├─ AboutCallback()     → AboutScreen
  ├─ LeaderboardsCallback() → leaderboard view
  ├─ MoreGamesCallback() → external link
  └─ ShopCallback()      → ShopScreen (blade shop)

GameOverScreen (shown after game over)
  ├─ QuitCallback()      → HitMenuBomb → back to MainScreen
  ├─ RetryCallback()     → restart current mode
  ├─ LeaderboardsCallback()
  ├─ TwitterCallback()   → share score
  └─ FacebookCallback()  → share score

PauseScreen (in-game pause overlay)
  ├─ ContinueGameCallback() → unpause
  ├─ QuitGameCallback()     → back to menu
  └─ RetryGameCallback()    → restart
```

### Screen State Machine (MainScreen.field_0x10c)

| Value | State | Notes |
|-------|-------|-------|
| 0x02 | Start game directly | NewGameCallback |
| 0x0e | Mode selection | GameModeCallback → show GameModeScreen |
| 0x09 | Quitting | GameOverScreen::QuitCallback → HitMenuBomb |

### Mode Selection (GameModeScreen.field_0x90)

| Value | Action | Game.gameMode set to |
|-------|--------|---------------------|
| 3 | Classic selected | 0 |
| 3+ | Arcade selected | 1 (via hash check) |
| 6 | Zen selected | 3 |

After mode selection: `GameModeScreen::SetupLevel()` calls `PrepareForLevelStart()` which resets all game entities for a new round.

### Key Screen Functions

| Function | Address | Purpose |
|----------|---------|---------|
| MainScreen::GameModeCallback | 0x0014b068 | Open mode selection |
| MainScreen::NewGameCallback | 0x0014c384 | Direct game start + SFX |
| GameModeScreen::ClassicModeCallback | 0x0013dfb4 | Select Classic mode |
| GameModeScreen::ArcadeModeCallback | 0x0013e19c | Select Arcade mode |
| GameModeScreen::ZenModeCallback | 0x0013dffc | Select Zen mode |
| GameModeScreen::SetupLevel | 0x0013e21c | PrepareForLevelStart wrapper |
| GameOverScreen::QuitCallback | 0x00140620 | Return to menu via HitMenuBomb |
| GameOverScreen::RetryCallback | 0x0014105c | Retry current game |
| PauseScreen::ContinueGameCallback | 0x00153fe8 | Unpause |
| PauseScreen::QuitGameCallback | 0x00153ebc | Quit to menu |

---

## See Also

- [Main menu screen](../screens/main.md)
- [Game mode screen](../screens/game-mode.md)
- [Dojo screen](../screens/dojo.md)
- [Shop screen](../screens/shop.md)
- [Pause screen](../screens/pause.md)
- [Game Over screen](../screens/game-over.md)
- [About screen](../screens/about.md)
- [Game flow functions](../functions/game-flow.md) -- screen transition callbacks
