# Mortar::Touch Internals

## Touch Struct (468 bytes)

Double-buffered 8-slot multitouch system with ring buffer event queue.

### Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x000 | 224 | State[8] | states1 | Front buffer (read by game) |
| +0x0E0 | 224 | State[8] | states2 | Back buffer (written by event handler) |
| +0x1C0 | 16 | RingBufferT\<TEvnt\> | m_EventRing | 10-entry ring buffer (200 bytes heap) |
| +0x1D0 | 4 | int | m_NextTouchId | Monotonic touch ID counter |

### State (28 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | float | startX | Touch-down position |
| +0x04 | 4 | float | startY | |
| +0x08 | 4 | float | currentX | Current position |
| +0x0C | 4 | float | currentY | |
| +0x10 | 4 | int | pointerId | Bada OS slot index |
| +0x14 | 4 | int | touchId | Mortar-assigned monotonic ID |
| +0x18 | 4 | int | phase | -1=justPressed, 0=held, 1=released |

### TEvnt (20 bytes)

| Offset | Size | Type | Name |
|--------|------|------|------|
| +0x00 | 4 | uint | pointerId |
| +0x04 | 4 | bool | isDown |
| +0x08 | 4 | float | x |
| +0x0C | 4 | float | y |
| +0x10 | 4 | float | timestamp |

### Data Flow

```
Bada OS touch event (OnTouchPressed/Moved/Released on GlesForm)
  → coordinate transform (90° rotation: gameX = rawY*480/800, gameY = 319 - rawX*320/480)
  → Touch::__UpdateInternal (0x195690)
    → pushes TEvnt into ring buffer

Touch::Update(dt) — called each frame
  → drains events where timestamp <= dt
  → dispatches to ___UpdateInternal
    → updates states2 (back buffer)
  → Touch::_Update()
    → copies states2 to states1 (front buffer swap)

SendIndividualTouchCallbacks
  → reads states1 (front buffer)
  → emits InputDevice::AxisEvent (x, y position)
  → emits ButtonPressed (button IDs 0x89-0x90 for 8 fingers)
  → InputManager dispatches to registered action callbacks
```

---

## See Also

- [Input manager](input-manager.md) — Action-based callback dispatch
- [Touch input overview](touch-input.md) — Coordinate transform details