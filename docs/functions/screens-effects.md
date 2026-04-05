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

## Effect Functions — Moved

Entity effect functions have been reorganized into `docs/entities/`:

- [BombFlash](../entities/bomb-flash.md) — BombFlash::Update
- [BombBlast](../entities/bomb-blast.md) — BombBlast::Update
- [SlashEntity](../entities/slash-entity.md) — SlashEntityGhost::Update
- [Coin](../entities/coin.md) — Coin::_Update
