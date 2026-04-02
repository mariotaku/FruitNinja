# GameModeScreen

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


## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
