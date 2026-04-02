# Touch Input Pipeline

## Flow

```
Bada OS Touch Event
  → GlesForm::OnTouchPressed/Moved/Released (0x18334c / 0x1833c4 / 0x1832e4)
    → GlesForm::TransformTouchPos (0x18327c)
      → x = (int)(rawX * 320.0 / screenWidth)
      → y = 319 - (int)(rawY * 480.0 / 320.0)
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

Converts raw Bada touch coordinates to game space (320x480):

```c
Point TransformTouchPos(Point raw) {
    Point result;
    result.x = (int)((float)raw.x * 320.0f / screenWidth);
    result.y = 319 - (int)((float)raw.y * 480.0f / 320.0f);
    return result;
}
```

Note: Y is flipped — Bada has origin at top-left, game has origin at bottom-left.

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
- [Entity structs](../structs/entities.md) -- SlashEntity touch tracking
