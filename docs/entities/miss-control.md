# MissControl Entity

<!-- Analysed: 2026-04-15T16:00 -->

Overlay label that displays "critical" or "rare" text at a slice point. Extends `HUDControl3d`, pool of 9 instances at stride 0x94.

## Struct Layout (0x94 bytes, extends HUDControl3d)

Inherits HUDControl3d fields. Key overrides:
- Constructor loads overlay textures + printf-formatted combo indices 3–10
- `DrawQuad_MissControl @ 0x0014b170`: renders `Mesh::DrawQuadUnCached` with full UV quad at world position

## Pool Access

| Function | Address | Purpose |
|----------|---------|---------|
| GetFree | 0x00150da4 | Find next free MissControl slot |

## Make Functions

### MakeCritical @ 0x00151764

Activates a critical-hit label at a slice point.

```c
MakeCritical(Vec3 pos, int playerIdx) {
    this->pos = pos;
    this->m_FadeAlpha = 0.808;        // DAT_001518b8 = 0x3FE7AE14
    this->m_AnimState = 3;
    
    // Compute half-size from texture dimensions
    // Clamp to screen bounds: ±240 X, ±160 Y
    //   DAT_001518c0 = -240 (min X)
    //   DAT_001518c4 = 240  (max X)
    //   DAT_001518c8 = -160 (min Y)
    //   DAT_001518cc = 160  (max Y)
    ScreenClamp(this->pos, halfSize);
}
```

### MakeRare @ 0x001518d8

Similar to MakeCritical, but for special fruit slices. Additionally sets:
- `m_AlphaScale = 0.5` — half-opacity to distinguish from critical

```c
MakeRare(Vec3 pos) {
    MakeCritical(pos, ???);     // shared logic
    this->m_AlphaScale = 0.5;   // DAT_0014e978 = 0.5
}
```

## Display

Texture selection: Two named overlays preloaded in constructor, plus combo indicators (`combo_%d.tex` for indices 3–10).

Animation: `m_AnimState = 3` triggers a standard fade-in / scale-up sequence (reused from other HUDControl types).

Life: Temporary overlay that fades and removes itself (lifetime controlled by fade state machine, not explicit timer).

---

See also:
- `docs/structs/gameplay-misc.md` — HUDControl3d base class
