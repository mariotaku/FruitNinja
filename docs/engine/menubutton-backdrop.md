# MenuButton 0x40-Layer "Scratch" Fruit/Bomb Backdrop Draw

RE'd: 2026-04-29.

## Summary

`MenuButton::Draw` (0x0014f9cc) has a two-phase layer flow controlled by
`HUDControl3d::m_LayerFlags` at `+0x34`:

* **Phase A — `m_LayerFlags == 0x40` (one-frame backdrop pass)**
  Draws a square "scratch" texture quad behind the spinning fruit/bomb
  entity, then promotes `m_LayerFlags` to `0x80` so the *next* frame
  falls into Phase B.
* **Phase B — `m_LayerFlags != 0x40` (normal post-splat pass)**
  Renders the regular button quad (m_SecondaryTex), the "new"-item
  star, the sparkle ring, and text labels. This is the path the port
  already implements.

The port currently demotes `m_LayerFlags` (correct) but skips the
backdrop draw entirely, so menu fruit/bomb buttons render without
their decorative scratch ring.

This doc specifies Phase A. Phase B is documented in
`docs/structs/gameplay-misc.md`.

## Layer-flag flow

Disassembly at the head of `MenuButton::Draw`:

```
0014fa24: ldr  r3, [r4, #0x34]    ; m_LayerFlags
0014fa26: cmp  r3, #0x40
0014fa28: bne  0x14fb06            ; Phase B path
0014fa2a: adds r3, #0x40           ; 0x40 -> 0x80 (promote, not assign)
0014fa2c: str  r3, [r4, #0x34]
0014fa2e: ldr  r3, [0x14fd0c]      ; GOT slot 0x77e0 (scratchs.tex)
0014fa30: ldr  r5, [r7, r3]        ; r5 = SmartPtr<Texture>* (static)
0014fa32: mov  r0, r5
0014fa34: blx  0x00104b80          ; SmartPtr::operator bool()
0014fa38: cmp  r0, #0
0014fa3a: beq  0x150218            ; if no texture, skip whole block
... draw block 0x14fa3e..0x14faf8 ...
0014faf8: ldr  r3, [0x14fd0c]
0014fafa: ldr  r3, [r7, r3]
0014fafc: ldr  r0, [r3, #0]        ; load Texture*
0014fafe: blx  0x00103848          ; Texture::UnSet
0014fb02: b    0x150218             ; function return (skip Phase B)
```

Phase A exits the function via `b 0x150218`, so the backdrop draw runs
**by itself on the first draw frame** of a `m_LayerFlags == 0x40`
button. The next frame (now `m_LayerFlags == 0x80`) goes through
Phase B.

## 1. Texture source — static class-level `scratchs.tex`

The bound texture is a **static (class-level) shared `SmartPtr<Texture>`**
loaded by `MenuButton::LoadContent` (0x0014f674), not a per-instance
field on `HUDControl3d`.

| Item | Value | Source |
|------|-------|--------|
| GOT-slot reading the SmartPtr | `0x000077e0` | `DAT_0014fd0c` in Draw (and `DAT_0014f6f8` in LoadContent — same slot) |
| Loaded asset path | `"scratchs.tex"` | String at `0x001bbd58` (LoadContent's first `LoadLocalisedTexture` call) |

LoadContent's three textures (in order) are: `scratchs.tex`,
`blurry_backing.tex`, `new_item.tex`. **scratchs.tex is the menu-button
scratch backdrop**; `blurry_backing.tex` is the Layer-3 sparkle-ring
texture (dead code in Bada build — see gameplay-misc.md); `new_item.tex`
is the "NEW" stamp star. RE-confirmed 2026-04-29 from literal pool
0x0014f6f4..0x0014f704 -> string addresses 0x001bbd58 / 0x001baefa /
0x001bbd65. (The earlier note mentioning `hud_cross.tex` here was
incorrect — that texture is loaded elsewhere.)

`Texture::Set(*scratchs)` is called at the start of the block;
`Texture::UnSet(*scratchs)` is called via 0x00103848 at the end.

## 2. Quad geometry — fixed 364×364 px, full UV, no rotation

The matrix sequence in 0x14fa3e..0x14fac4 is (heuristic decompile of
hard-float Vec3 calls):

```c
ResetMatrix();
// scale_vec = { ±1.0, 1.0, 1.0 }   (-1 if m_bFlipped, else +1)
// scale_matrix = MakeScale(scale_vec)
// translate_matrix = MakeTranslate(pos.x, pos.y, MENUBUTTON_BACKDROP_Z)
// final = scale * translate
SetCurrentMatrix(final);
UploadMatrices();
TintWhite(&white_tint);
white_tint.a = (byte)alpha;
DrawQuadUnCached(white_tint, 182.0f, 1.0f, 182.0f, 1.0f, NULL);
```

`Mortar::Mesh::DrawQuadUnCached(colour, halfW, uScale, halfH, vScale, fx)`
is called via `DrawQuadSized_PowerUpShop` (0x0014edf8) with arguments:

| Param | Constant | Decimal | Meaning |
|-------|----------|---------|---------|
| halfWidth | `MENUBUTTON_BACKDROP_HALFSIZE` (`DAT_0014fcfc`) | **182.0** | quad spans pos.x ± 182 |
| uScale | `0x3f800000` | 1.0 | full texture U |
| halfHeight | `MENUBUTTON_BACKDROP_HALFSIZE` | **182.0** | quad spans pos.y ± 182 |
| vScale | `0x3f800000` | 1.0 | full texture V |
| effects | `NULL` | — | no DrawEffectContainer |

**Full quad size = 364×364 px**, centred on `pos`. **No** rotation
(no `RotZ44`), **no** scale to button `size` (+0x20) or
`m_TargetSize` (+0x124) — the backdrop is a fixed 364×364
regardless of the button's hit-box. **No** shake offset (m_ShakeTimer
is not consulted in Phase A).

## 3. Mirror flip — sign-flipped X scale

Reads `m_bFlipped` (`+0xF0`, byte set in `Init` via `Rand32(2)`):

```
0014fa54: ldrb.w r3, [r4, #0xf0]
0014fa5e: cmp    r3, #0
0014fa70: vmov.f32 s0, 0xbf800000   ; s0 = -1.0 default
0014fa74: vmov.f32 s1, s16          ; s1 = +1.0
0014fa78: it     eq
0014fa7a: vmov.eq.f32 s0, s16        ; if m_bFlipped == 0, s0 = +1.0
0014fa7e: vmov.f32 s2, s16          ; s2 = +1.0
```

So scale Vec3 = `{ m_bFlipped ? -1.0 : +1.0, 1.0, 1.0 }`. The flip is
done **at the matrix level** (`MakeScale(±1, 1, 1)`), not by negating
`halfWidth` in the quad call. Implementer should use a matrix scale,
not a quad-width sign flip.

## 4. Size source — fixed literal, NOT button size

Phase A uses the **literal 182.0 half-size** (DAT_0014fcfc) for both
axes. It does **not** read `(this->base).super.size` (+0x20),
`m_TargetSize` (+0x124), or any per-button scaling. This is
intentionally the same fixed scratch backdrop for every menu button,
regardless of its hit-box.

(Phase B *does* scale by `size` — that distinction is preserved.)

## 5. Tint colour and alpha

Tint is **white** built via `TintWhite(float* out)` (0x00101298), with
alpha overridden to a counter-derived byte:

```c
if (this->m_FruitType < 0) {
    alpha = 0xFF;                                  // toggles: full opacity
} else {
    float n = (float)m_FadeCounter
              * MENUBUTTON_BACKDROP_ALPHA_NUM      // = 256.0
              / MENUBUTTON_BACKDROP_ALPHA_DEN;     // = 16380.0
    int a  = (int)n;
    if (a > 254) a = 0xFF;
    a &= ~(a >> 31);                                // clamp to >= 0
    alpha = (byte)a;
}
```

Note this alpha computation runs at function entry (before the
`m_LayerFlags == 0x40` test), so **the same `alpha` value is reused
for both Phase A and Phase B**.

| DAT | Address | Hex | Value |
|-----|---------|-----|-------|
| `MENUBUTTON_BACKDROP_ALPHA_NUM` | `0x0014fcf0` | `0x43800000` | **256.0f** |
| `MENUBUTTON_BACKDROP_ALPHA_DEN` | `0x0014fcf4` | `0x467ff000` | **16380.0f** |

Effective: `alpha = clamp((m_FadeCounter * 256) / 16380, 0, 255)`
≈ `clamp(m_FadeCounter / 64, 0, 255)`. At `m_FadeCounter = 64`,
alpha hits ~1; at `m_FadeCounter ~= 16320`, alpha saturates at 255.
(`m_FadeCounter` rises by 1000/255 ≈ 3.92 per frame in Phase B's
existing port code — at that rate it takes ~4170 frames to saturate;
likely the ramp uses a different increment for backdrop alpha purposes
and the wide range simply prevents premature clipping.)

The Colour pipeline is:
1. `TintWhite(&aCStack_44)` — produces a "white tinted by global tint"
   Colour-as-floats vector.
2. `aCStack_44[0].a = alpha` — overwrites alpha byte.
3. `Colour::Colour(&CStack_48, aCStack_44)` — copy-constructs final
   Colour into the DrawQuad arg.

## 6. UV rect

**Full texture: U = 0..1, V = 0..1**. No atlas. `uScale = vScale = 1.0`
in the DrawQuad call. The scratch texture is dedicated (one image,
filling the entire quad).

## 7. Translate target — `pos` only, Z = -5500

```c
Vec3 t = { pos.x, pos.y, MENUBUTTON_BACKDROP_Z };
MakeTranslate44(&translate_matrix, t);
```

| DAT | Address | Hex | Value |
|-----|---------|-----|-------|
| `MENUBUTTON_BACKDROP_Z` | `0x0014fcf8` | `0xc5abe000` | **-5500.0f** |

The translation is **only `pos`** — no `m_RandomOffset`, no shake, no
global menu offset. `pos` here is `(this->base).super.pos` at +0x08
(HUDControl::pos, the entity's screen-space anchor).

The Z = -5500 puts the backdrop deep behind the camera near plane in
the orthographic projection used by the menu, ensuring it sorts
behind the spinning fruit/bomb entity (which is the actual 3D mesh
drawn by `ActorManager::Draw`).

## 8. Draw call — `Mortar::Mesh::DrawQuadUnCached`

The actual binary call chain:

```
DrawQuadSized_PowerUpShop(182.0, 1.0, 182.0, 1.0, &whiteWithAlpha)
  -> Colour::Colour(&local, &whiteWithAlpha)               // 0x10212c
  -> Mortar::Mesh::DrawQuadUnCached(local, 182.0, 1.0,
                                    182.0, 1.0, NULL)      // 0x101e98
```

This is the same `DrawQuadUnCached` used by Phase B. **No** triangle
list is built (unlike the sparkle ring); **no** mesh batching; **no**
shared-backdrop draw list. It's a single immediate quad draw.

## Implementer dependencies / pairing rules

* **Texture::Set / UnSet must pair**: bind `scratchs.tex` before the
  matrix block; unbind after `DrawQuadUnCached`. The port's renderer
  state machine likely relies on this (see `docs/engine/rendering-pipeline.md`).
* **Matrix order is scale-then-translate** (not translate-then-scale).
  `final = MakeScale * MakeTranslate`. With column-vector convention
  `result = final * vert`, this means: vertex → scaled by ±1 on X →
  then translated by `pos`. The order doesn't matter mathematically
  for pure axis flip + translate, but matches what the binary builds
  (so future RotZ additions or vector additions to scale or translate
  would behave identically).
* **No blend-mode toggle** is visible in this block — it inherits the
  current renderer blend state. The port's standard alpha-blend mode
  for HUD draws should already be active.
* **Skip if `scratchs.tex` SmartPtr is empty** — the block is gated by
  `if (Mortar::SmartPtr::operator bool(scratchsPtr))`, so the
  implementer must `if (m_pScratchsTex && *m_pScratchsTex) { … }`.
* **Phase A then exits the function**. After the backdrop draw, the
  binary `b`s past Phase B (no star, no sparkle, no text on this
  frame). The implementer should `return` immediately after Phase A,
  not fall through.

## Constants summary (literal-pool addresses)

| Symbol | Address | Hex | Value | Use |
|--------|---------|-----|-------|-----|
| `MENUBUTTON_BACKDROP_ALPHA_NUM` | `0x0014fcf0` | `0x43800000` | 256.0 | numerator of `alpha = ctr * num / den` |
| `MENUBUTTON_BACKDROP_ALPHA_DEN` | `0x0014fcf4` | `0x467ff000` | 16380.0 | denominator |
| `MENUBUTTON_BACKDROP_Z`         | `0x0014fcf8` | `0xc5abe000` | -5500.0 | Z translation for backdrop |
| `MENUBUTTON_BACKDROP_HALFSIZE`  | `0x0014fcfc` | `0x43360000` | 182.0 | quad half-extent (W=H) |

GOT slots:

| Symbol | Address | Hex offset | Target |
|--------|---------|------------|--------|
| `DAT_0014fd0c` (in Draw) | `0x0014fd0c` | `0x000077e0` | static SmartPtr<Texture> for `scratchs.tex` |
| `DAT_0014f6f8` (in LoadContent) | `0x0014f6f8` | `0x000077e0` | same slot |

Function refs:

| Symbol | Address | Notes |
|--------|---------|-------|
| `MenuButton::Draw` | `0x0014f9cc` | top-level |
| Phase A draw block | `0x0014fa3e..0x0014faf8` | the backdrop draw |
| `MenuButton::LoadContent` | `0x0014f674` | loads scratchs.tex |
| `Mortar::Texture::Set` | `0x001066c8` | thunk |
| `Mortar::Texture::UnSet` | `0x00103848` | thunk |
| `Mortar::Mesh::DrawQuadUnCached` | `0x00101e98` | thunk |
| `DrawQuadSized_PowerUpShop` | `0x0014edf8` | wrapper used by both phases |
| `TintWhite` | `0x00101298` | builds white tint colour |
| `ResetMatrix_PowerUpShop` | `0x0014edb4` | MatrixStack::Reset |
| `SetMatrix_PowerUpShop` | `0x0014edd4` | MatrixStack::SetCurrentMatrix |
| `UploadMatrices_PowerUpShop` | `0x0014ed98` | MatrixManager::UploadCurrentMatrices(true) |

## Open question — undefined `+0xec` scalar

The asm at 0x14fa86 reads a single float from `MenuButton+0xec` and
multiplies the (±1, 1, 1) Vec3 by it before `Scale44`:

```
0014fa86: add.w r2, r4, #0xec        ; r2 = &this[0xec]
0014fa8a: ldr   r1, [sp, #0x10]      ; r1 = &scaleVec (±1,1,1)
0014fa8c: mov   r0, r10               ; r0 = output Vec3
0014fa8e: blx   0x001028b8            ; Vec3::operator*(out, vec, &scalar)
                                       ; -> out = vec * (*scalar_ptr)
```

`MenuButton+0xec` is not enumerated in the current struct layout
(`docs/structs/gameplay-misc.md` shows `m_RandomOffset` at +0xE8 and
`m_bFlipped` at +0xF0, leaving +0xEC..+0xEF undefined). `MenuButton::Init`
does not write +0xEC; `HUDControl3d::HUDControl3d` only zeroes up to
+0x78. The field's value at draw time appears to come from the
constructor's `std::list<MenuButtonAddOn>` initialiser (which sits
somewhere in +0xCC..+0xE7 range based on offsets) or from one of the
constructor variants (0x14f24c / 0x14f348 / 0x14f444 / 0x14f55c).

**Implementer guidance**: if the implementer has a struct field at
+0xEC that's reliably 1.0 (or 0.0) in a real game run, that's the
scalar. If undefined, treat it as 1.0 — it's a per-button scale on
the backdrop and 1.0 produces the original 364x364 size. RE follow-up
can chase whichever constructor writes it. (The other constructor
variants 0x14f24c/0x14f348/0x14f55c may write a `MenuButtonAddOn` list
node here, in which case the multiplied "scale" is a list-pointer
reinterpreted as a float — i.e. effectively non-zero garbage. This
would mean the backdrop scale **is intentionally fudged on a
per-button basis by the address of the addon list**, which would be a
bug-level RE finding worth verifying.)

## See also

* `docs/structs/gameplay-misc.md` — MenuButton struct + Phase B (button
  quad / star / sparkle) draw spec
* `docs/engine/menubutton-138.md` — `+0x138` back-key behaviour
* `docs/engine/rendering-pipeline.md` — DrawQuadUnCached pipeline
