# Screen Callbacks & Effect Functions

## Screen Callbacks

| Function | Address | Action |
|----------|---------|--------|
| MainScreen::GameModeCallback | 0x0014b068 | Open mode selection |
| MainScreen::NewGameCallback | 0x0014c384 | Direct game start |
| GameModeScreen::ClassicModeCallback | 0x0013dfb4 | Game.gameMode=0 |
| GameModeScreen::ArcadeModeCallback | 0x0013e19c | Game.gameMode=1 |
| GameModeScreen::ZenModeCallback | 0x0013dffc | Game.gameMode=3 |
| GameModeScreen::SetupLevel | 0x0013e21c | PrepareForLevelStart() |
| GameOverScreen::QuitCallback | 0x00140620 | HitMenuBomb → menu |
| GameOverScreen::RetryCallback | 0x0014105c | Restart game |
| PauseScreen::ContinueGameCallback | 0x00153fe8 | Unpause |
| PauseScreen::QuitGameCallback | 0x00153ebc | Quit to menu |

---

## Effect Functions

### BombFlash::Update (0x00171038, 61 lines)

| Address | Signature |
|---------|-----------|
| 0x00171038 | `void BombFlash::Update(float dt)` |

### BombBlast::Update (0x00171170, 32 lines)

| Address | Signature |
|---------|-----------|
| 0x00171170 | `void BombBlast::Update(float dt)` |

### SlashEntityGhost::Update (0x0017eb60, 47 lines)

| Address | Signature |
|---------|-----------|
| 0x0017eb60 | `void SlashEntityGhost::Update(float dt)` |

### Coin::_Update (0x00173790, 241 lines)

| Address | Signature |
|---------|-----------|
| 0x00173790 | `void Coin::_Update(float dt)` |
