# Critical Flash Effect

<!-- Analysed: 2026-04-15T16:00 -->

`CriticalFlash @ 0x0016a9a4` — singleton-state function that triggers a full-screen tint flash for critical hits and special-fruit slices.

## Behavior

Not a pooled entity — a direct state write into the FruitGame singleton. Takes a `Colour` parameter and:

1. Writes the supplied Colour to two adjacent 12-byte slots at FruitGame:
   - `+0xe4`: Primary tint colour
   - `+0xf0`: Secondary tint colour (reserved for special cases)

2. Resets the `m_TimeScale` field at offset `+0x2c` of a second GOT-resolved object (likely `ScreenTint` or `ScreenFadeControl`), triggering the visual effect.

## Call Sites

| Context | Address | Colour | Use Case |
|---------|---------|--------|----------|
| Fruit critical hit | `Fruit::CollisionResponse` (0x001780b0) | Gold / bright yellow | Critical slice bonus visual |
| Special fruit slice | `Fruit::CollisionResponse` (0x001780b0) | White half-alpha | Rare/special fruit indicator |

## Visual

The actual rendering is driven by whichever render pass reads `+0xe4`/`+0xf0` — a full-screen tint quad, typically composited after the normal game layer.

## Port Notes

The effect is purely visual state management. No pools or temporal tracking needed. Implement as a write to the Game state, then the render pipeline will pick it up on the next frame.

---

See also:
- `docs/entities/fruit.md` — Fruit::CollisionResponse entry point
