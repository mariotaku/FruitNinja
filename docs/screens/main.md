# MainScreen

## MainScreen : HUDControl3d (size = 0x120)

The main menu / dojo screen. Contains all menu buttons, toggle buttons (sound/music), textures, and a font.

### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x7c | float | m_OrigSizeX | Copy of size_x |
| +0x80 | float | m_OrigSizeY | Copy of size_y |
| +0x84 | float | m_OrigSizeZ | Copy of size_z |
| +0x88 | SmartPtr\<Texture\> | m_TexPlayButton | "Play" button texture |
| +0x8c | SmartPtr\<Texture\> | m_TexShopButton | Shop/dojo button texture |
| +0x90 | SmartPtr\<Texture\> | m_TexAboutButton | |
| +0x94 | SmartPtr\<Texture\> | m_TexLeaderboard | |
| +0x98 | SmartPtr\<Texture\> | m_TexMoreGames | |
| +0x9c | MenuButton* | pPlayButton | Created lazily in state 1 |
| +0xa0 | MenuButton* | pShopButton | Created lazily in state 1 |
| +0xa4 | MenuButton* | pLeaderboardBtn | Created lazily in state 1 |
| +0xa8 | MenuButton* | pMoreGamesBtn | |
| +0xac | MenuButton* | pSoundToggle | Created in state 0 check |
| +0xb0 | MenuButton* | pMusicToggle | Created in state 0 check |
| +0xb4 | SmartPtr\<Texture\> | m_TexLogo | |
| +0xc4 | SmartPtr\<Texture\> | m_TexSoundOn | Toggle button variants |
| +0xc8 | SmartPtr\<Texture\> | m_TexSoundOff | |
| +0xcc | SmartPtr\<Texture\> | m_TexMusicOn | |
| +0xd0 | SmartPtr\<Texture\> | m_TexMusicOff | |
| +0xd4 | SmartPtr\<Model\> | m_Model | |
| +0xd8 | SmartPtr\<Texture\> | m_TexBg | |
| +0xe8 | float | m_Alpha | = 1.0 |
| +0xfc | float | m_WindowCenter | Computed from window height |
| +0x104 | int | m_field104 | |
| +0x108 | float | m_field108 | |
| +0x10c | int | m_State | State machine variable |
| +0x110 | float | m_Timer | Transition countdown |
| +0x114 | SmartPtr\<Texture\> | m_TexExtra | |
| +0x118 | float | m_Timer2 | Second timer (accumulates dt) |
| +0x11c | Font* | m_pFont | Loaded from font file |

### State Machine (field_0x10c)

| State | Purpose |
|-------|---------|
| 0 | **Camera transition** — zoom/fade from splash to menu. Creates sound/music toggle buttons. Transitions to state 1 after timer expires and camera zoom completes. |
| 1 | **Create menu buttons** — lazily creates Play, Shop, Leaderboard, MoreGames buttons. Checks for new shop items. Creates leaderboard button. |
| 2+ | **Active menu** — buttons visible, waiting for callbacks. Handles news display, network state updates. |
| 0x0e | **Mode selection transition** — triggered by GameModeCallback. Shows GameModeScreen. |

### Button Layout (from Update)

Each button is 0x15C bytes (MenuButton), created via `operator_new(0x15c)` + `MenuButton::MenuButton(texture, position, callback, fruitType, scale, deletedCallback)`.

Button positions (game coords, bottom-left origin):
- Sound toggle: top area
- Music toggle: top area
- Play button: center, large
- Shop button: below play
- Leaderboard: side
- More games: side

### Key Callbacks

| Callback | Address | Action |
|----------|---------|--------|
| GameModeCallback | 0x0014b068 | Set state=0x0e, open mode selection |
| NewGameCallback | 0x0014c384 | Direct game start (set state=2) |
| AboutCallback | 0x0014afc4 | Create AboutScreen |
| SoundCallback | 0x0014af64 | Toggle SFX, swap button texture |
| MusicCallback | 0x0014ac9c | Toggle music, swap button texture |
| LeaderboardsCallback | 0x0014b010 | Open leaderboard |
| MoreGamesCallback | 0x0014b000 | Open URL |
| QuitGamesCallback | 0x0014b1a0 | Exit app |

### Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| MainScreen ctor | 0x0014c430 | 159 | Load 13 textures, create font |
| Update | 0x0014b278 | 675 | State machine, lazy button creation |
| Draw | 0x0014d4ec | — | Render background, buttons, logo |
| Hide | 0x0014ad04 | — | Transition out |
| UpdateScreenElements | 0x0014ad3c | — | Adjust layout for screen size |
| DeleteMenuButtons | 0x0014aee8 | — | Remove all buttons |
| Release | 0x0014cd20 | — | Cleanup |

---

