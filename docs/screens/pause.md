# PauseScreen

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

