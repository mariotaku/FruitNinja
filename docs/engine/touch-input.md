# Touch Input Pipeline

## Flow

```
Bada OS Touch Event
  → GlesForm::OnTouchPressed/Moved/Released (0x18334c / 0x1833c4 / 0x1832e4)
    → GlesForm::TransformTouchPos (0x18327c)
      → game.x = (int)(raw.y * const / const)       ← phys Y → game X (wide axis)
      → game.y = 319 - (int)(raw.x * const / const) ← phys X → game Y (narrow, flipped)
    → Mortar::Touch::__UpdateInternal (0x195690)
      → Pushes TEvnt{id, pressed, x, y, z} to RingBuffer
    → Mortar::Touch::Update (0x195630)
      → Processes ring buffer → dispatches InputEvents via InputManager
        → Registered callbacks: TouchDownCallback, PointerMoveCallback
          → SlashEntity::TouchDown (0x17d61c)
          → SlashEntity::TouchMoveX/Y (0x17c50c / 0x17c490)
          → SlashEntity::UpdateTouchDown (0x17d2e4)
```

## GlesForm::TransformTouchPos (0x18327c)

Converts raw Bada portrait touch coordinates to game landscape space (480×320). Swaps axes — physical Y→game X, physical X→game Y (flipped):

```c
// Actual decompilation shows axes are SWAPPED (portrait device → landscape game):
Point TransformTouchPos(Point raw) {
    Point result;
    result.x = (int)(raw.y * DAT_001832d0 / DAT_001832d4);       // phys Y → game X
    result.y = 319 - (int)(raw.x * DAT_001832d8 / DAT_001832d0); // phys X → game Y
    return result;
}
```

Note: Axes are swapped AND Y is flipped. The Bada device is physically portrait (480×800), but the game coordinate space is landscape (480×320). Physical Y (long axis, held sideways) maps to game X (wide axis). Physical X maps to game Y (narrow axis, inverted — Bada top-left origin → game bottom-left).

## GlesForm Touch Handlers

**OnTouchPressed** (0x18334c):
1. Get Bada point ID (0-7)
2. Assign internal touch ID: `touchIds[pointId] = nextId++`
3. TransformTouchPos → game coordinates
4. `Touch::__UpdateInternal(touchId, true, x, y, 0)`

**OnTouchMoved** (0x1833c4): Same but `pressed = varies`

**OnTouchReleased** (0x1832e4):
1. TransformTouchPos
2. `Touch::__UpdateInternal(touchId, false, x, y, 0)`

## Mortar::Touch::__UpdateInternal (0x195690)

Pushes a `TEvnt` struct (0x14 bytes) to a ring buffer:

| Field | Type | Content |
|-------|------|---------|
| a | uint | Touch ID |
| b | bool | Pressed (true) or released (false) |
| c | float | Game X coordinate |
| d | float | Game Y coordinate |
| e | float | Pressure (always 0) |

If ring buffer full: forces `Touch::Update()` to flush, then retries push.

## SlashEntity Touch Callbacks

**TouchDown** (0x17d61c): Called on new touch. Resets blade state, calls `UpdateTouchDown`.

**TouchMoveX** (0x17c50c): Maps input X to entity position:
```c
this->pos_x = inputEvent.value + (windowSize.x - windowWidth) * -0.5;
```

**TouchMoveY** (0x17c490): Maps input Y to entity position:
```c
this->pos_y = -(inputEvent.value + (windowSize.y - windowHeight) * -0.5);
```

Both check `Game.bombHitTimer <= 0` before updating (no input during bomb hit).

## Multi-Touch

- GlesForm supports 8 simultaneous touches (touchIds[8] at +0x1d4)
- Each touch gets a unique incrementing ID from `field_0x1f4`
- In same-screen multiplayer: `SlashEntity.m_SplitPoint` divides the screen

---

## See Also

- [Game flow functions](../functions/game-flow.md) -- touch event dispatch
- [Game struct](../structs/game.md) -- GlesForm touch fields
- [SlashEntity](../entities/slash-entity.md) -- SlashEntity touch tracking
