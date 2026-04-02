# Common Screen Patterns

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

---

## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
