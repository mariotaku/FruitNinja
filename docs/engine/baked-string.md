# Mortar::BakedString

RE'd: 2026-04-29.

## Summary

`BakedString` is the engine's **pre-baked / cached text mesh**. Given a
`Font*`, a string iterator, and a `Colour`, the constructor walks every
glyph once, builds per-page vertex buffers, and stores them inside the
object. `BakedString::Draw` later just binds the page texture and
issues `Mesh::DrawTriStrip` calls -- no font-walk, no per-frame layout
work.

It is the "expensive once, cheap to draw repeatedly" counterpart of
`Font::Font_DrawString` (which lays out and uploads glyphs every call).

In the shipped Fruit Ninja Bada binary, BakedString is referenced from:
* `MenuButton::SetText` (0x0014ebc0) -- but that function is itself
  never called, so MenuButton labels never render (label fields are dead code).
* (Possibly other widgets -- this doc is the spec, not an inventory.)

This doc covers the **minimum spec** to port BakedString. Heap-allocated
size is `0x1c` (28 bytes).

## Class size and field layout

`operator_new(0x1c)` allocates the object. Fields used by Draw and the
destructor:

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| `+0x00` | `void*` | `vtable_or_pad` | Read but not dispatched in BakedString_Draw; the dtor doesn't read it. Probably unused / vtable for a base class. |
| `+0x04` | `SmartPtr<Texture2D>*` | `m_PageTexPtrs` | Pointer to a heap array of `SmartPtr<Texture2D>`, one per Font page. The allocation is `(numPages + 2) * 4` bytes; the pointer stored here is `&base[2]` so the two prefix slots `base[0] = 4` (refcount?) and `base[1] = numPages` are accessible at `m_PageTexPtrs[-2]` and `m_PageTexPtrs[-1]`. The dtor walks `m_PageTexPtrs[-1]` slots backward, calls `~SmartPtr` on each, then `operator_delete__(base)`. |
| `+0x08` | `uint32_t` | `m_NumPages` | Number of font pages this string spans. The Draw loop runs `for (i = 0; i < m_NumPages; ++i)`. Most strings use only 1 page. |
| `+0x0c` | `QUADCUSTOMVERTEX**` | `m_PageVerts` | Heap array of `m_NumPages` pointers. `m_PageVerts[i]` is a tristrip vertex buffer of `m_PageVertCounts[i]` vertices. The dtor `delete[]`s every non-null `m_PageVerts[i]`, then `delete[]` the outer array. |
| `+0x10` | `uint32_t*` | `m_PageVertCounts` | Heap array of `m_NumPages` per-page vertex counts. The dtor `delete[]`s the array. |
| `+0x14` | `float` | `m_Width` | Maximum cursor X reached during baking, in `lineHeight`-normalised units (i.e. the width of the bounding box if `scale = 1`). Used by `BakedString_Draw` for horizontal alignment offsets. |
| `+0x18` | `float` | `m_Height` | Maximum `glyph.yoffset + glyph.height` reached during baking, also in lineHeight-normalised units. Used by Draw for vertical alignment offsets. |

Vertex format: each glyph contributes **6 vertices** in tristrip layout
(4 corners + 2 degenerates), stride 0x24 bytes (= QUADCUSTOMVERTEX).

## Construction (0x00197d64 / thunk @ 0x000fa314)

```c
// Heap layout:
//   m_PageTexPtrs[-2] = 4    (mystery ref-count or sentinel; binary writes 4)
//   m_PageTexPtrs[-1] = numPages
//   m_PageTexPtrs[0..n-1] = SmartPtr<Texture2D> (page i font texture)
//
//   m_PageVerts[i]       = QUADCUSTOMVERTEX[6 * glyph_count_on_page_i]
//   m_PageVertCounts[i]  = 6 * glyph_count_on_page_i
//
// Per-page, per-glyph: build 6 vertices (TL, BL, TR, BR, BR-dup, BL-dup)
// using pre-normalised CharTemplate fields (see docs/engine/font.md).
//   cx = cursor_x + glyph.xoffset                  (lineHeight units)
//   cy = -glyph.yoffset                              (Y-up)
//   The rest match Font_DrawString's per-glyph quad construction, but
//   without rotation/scale -- those happen at Draw time.
// Per-vertex: Colour::PlatformColour(param_3) packed BGRA into +0x18.
//
// After loop:
//   m_Width  = max over all glyphs of (cursor_x at glyph start + glyph.xadvance)
//   m_Height = max over all glyphs of (glyph.yoffset + glyph.height)
//
// param_3 (Colour) is captured into every vertex; later post-processing
// (ApplyGradient) rewrites per-vertex colours.
BakedString::BakedString(Font *font, Utf8StringIterator iter, Colour colour);
```

The constructor uses two scratch arrays sized `font->page_count`:
`pvVar1[i]` = output-buffer index for page `i` (or `-1` if unused), and
`pvVar2[i]` = glyph count on page `i`. After the first pass it allocates
output buffers, then a second pass fills them.

## Draw (0x0019738c / thunk @ 0x000f4098)

Decompiler signature (from Ghidra):

```c
void BakedString::Draw(BakedString *this,
                       _Vector3<float> param_1,  // (scale, rotZ, _) packed
                       float param_2,            // unused (s1 dup)
                       float param_3,            // unused (s2 dup)
                       ALIGNMENT_TYPE param_4);  // r2 = align flags
```

ARM-grounded reading (hard-float; floats in s-regs):
* `s0` = scale (uniform world-units-per-em-height)
* `s1` = rotZ in **degrees**
* `r2` = alignment flags (`uint`)
* `[sp,#?]` = `Vec3* pos` (world-space anchor)

Pseudocode:

```c
// 1. Push current world matrix.
MatrixStack::Push(&MatrixManager::g_World);

// 2. Apply alignment offsets in LOCAL matrix space (before scale).
//    All offsets translate into the local frame, so they scale with `scale`.
if ((align & 0x3) == 0x2) {
    // Right-align: shift cursor LEFT by full width
    MatrixStack::TranslateLocal(-m_Width, 0, 0);
} else if ((align & 0x3) == 0x3) {
    // Centre-H: shift cursor LEFT by half width
    MatrixStack::TranslateLocal(-m_Width * 0.5f, 0, 0);
}
// (align & 0x3) == 0x0 or 0x1 -> no horizontal offset.

if ((align & 0xC) == 0x8) {
    // Bottom-align: shift cursor UP by full height (Y-up world)
    MatrixStack::TranslateLocal(0, m_Height, 0);
} else if ((align & 0xC) == 0xC) {
    // Centre-V: shift cursor UP by half height
    MatrixStack::TranslateLocal(0, m_Height * 0.5f, 0);
}
// (align & 0xC) == 0x0 or 0x4 -> no vertical offset.

// 3. Apply scale uniformly on X and Y.
MatrixStack::Scale(Vec3(scale, scale, 1.0f));

// 4. Apply Z rotation (degrees -> SinIdx/CosIdx via *182).
MatrixStack::RotZ(rotZ);

// 5. Translate to world anchor position.
MatrixStack::Translate(pos);

// 6. Upload MVP to shader.
MatrixManager::UploadCurrentMatrices(true);

// 7. Per page, bind texture and draw cached vertices.
for (uint i = 0; i < m_NumPages; ++i) {
    Texture::Set(m_PageTexPtrs[i].ptr);
    Mesh::DrawTriStrip(m_PageVerts[i], m_PageVertCounts[i], false, NULL);
    Texture::UnSet(m_PageTexPtrs[i].ptr);
}

// 8. Pop world matrix back.
MatrixStack::Pop();
```

### Alignment flags

| Bits | Mask | Effect |
|------|------|--------|
| 0..1 | `0x3` | `0x2` = right-align (shift -m_Width); `0x3` = centre H (shift -m_Width/2); `0x0`/`0x1` = no offset (cursor-anchored at 0) |
| 2..3 | `0xC` | `0x8` = bottom-align (shift +m_Height UP); `0xC` = centre V (shift +m_Height/2 UP); `0x0`/`0x4` = no offset |

Note: BakedString's alignment scheme **differs from `Font_DrawString`'s
alignment scheme** (defined in `docs/engine/font.md`). Be careful when
porting either.

### Tint / colour pipeline

`BakedString_Draw` does **not** call `TintColour`, does **not** read any
global tint, and does **not** modify the per-vertex colour at draw time.
Each vertex's colour byte was set during construction (or rewritten by
`ApplyGradient`) and is uploaded directly to the shader. To change a
BakedString's colour, you must rebuild it (or call `ApplyGradient`).

### Texture binding

Per-page bind/unbind is internal to BakedString_Draw -- callers do **not**
need to bind the font texture. The font's own page-textures are accessed
via `m_PageTexPtrs[i]`. SmartPtr ref-counting is implicit through the
copy taken during construction (`SmartPtr<Texture2D>::operator=` from
`Font::GetPage(i)+4`).

## Helper methods (referenced from MenuButton::SetText)

### ApplyGradient (binary @ 0x001971cc / thunk @ 0x0010569c)

Rewrites per-vertex colour to interpolate between two colours along the
glyph's local Y axis (top -> bottom). Signature:

```c
BakedString* BakedString::ApplyGradient(BakedString *this,
                                        int topVtxIdx,    // 0xd in MenuButton::SetText
                                        int botVtxIdx,    // 0xe in MenuButton::SetText
                                        Colour *topCol,   // top colour
                                        int alphaIdx);    // 3 in MenuButton::SetText
```

Returns `this` for fluent chaining. Exact implementation not RE'd in
this pass -- only the call signature from MenuButton::SetText.

### LayoutToCircle (binary @ 0x0019762c / thunk @ 0x000fdcd4)

Re-arranges baked vertices so the glyph row sits along an arc instead of
a straight line. Takes a single float `radius` parameter. Used by
MenuButton::SetText to curve labels to match the round button.

```c
void BakedString::LayoutToCircle(BakedString *this, float radius);
```

## Destructor (binary @ 0x001975b8 / `~BakedString`)

Frees all heap allocations:
1. Walks `m_PageTexPtrs[i]` from `numPages-1` down to 0, calling
   `~SmartPtr<Texture2D>()` on each (releases page texture refs).
2. `operator_delete__(&m_PageTexPtrs[-2])` -- frees the array including
   the 8-byte prefix.
3. For `i = 0..m_NumPages-1`: if `m_PageVerts[i] != NULL`,
   `operator_delete__(m_PageVerts[i])` (the QUADCUSTOMVERTEX buffer).
4. `operator_delete__(m_PageVerts)` -- the outer array.
5. `operator_delete__(m_PageVertCounts)`.

The `vtable_or_pad` field at `+0x00` is not touched by the dtor.

## Constants summary

| Symbol | Address | Hex | Float | Use |
|--------|---------|-----|-------|-----|
| `DAT_00197554` | `0x00197554` | `0x00000000` | 0.0 | Translate-Y filler in alignment (`Vec3(_, 0, 0)`) |
| `DAT_00197d60` | `0x00197d60` | `0x00000000` | 0.0 | Initial `m_Width = m_Height = 0`, also vertex-Z filler |
| `DAT_001984a4` | `0x001984a4` | `0x43360000` | 182.0 | RotZ degrees->idx multiplier (used by `MatrixStack::RotZ`) |

## Function map

| Symbol | Address | Notes |
|--------|---------|-------|
| `BakedString::BakedString(Font*, Utf8StringIterator, Colour)` | `0x00197d64` | Main ctor (the longer one) |
| `BakedString::BakedString(...)` (alt) | `0x0019789c` | Shorter ctor variant |
| `BakedString::BakedString` (thunk) | `0x000fa314` | -> 0x0019789c |
| `BakedString::Draw` | `0x0019738c` | Per-page render |
| `BakedString::Draw` (thunk) | `0x000f4098` | -> 0x0019738c |
| `BakedString::ApplyGradient` | `0x001971cc` | Per-vertex colour gradient |
| `BakedString::ApplyGradient` (thunk) | `0x0010569c` | -> 0x001971cc |
| `BakedString::LayoutToCircle` | `0x0019762c` | Curve-along-arc reposition |
| `BakedString::LayoutToCircle` (thunk) | `0x000fdcd4` | -> 0x0019762c |
| `BakedString::~BakedString` | `0x001975b8` | Frees all heap allocs |
| `BakedString::~BakedString` | `0x00197564` | Variant (likely deleting dtor) |
| `BakedString::~BakedString` (thunk) | `0x000fa494` | -> 0x00197564 |

## Open RE questions (intentionally NOT chased in this pass)

* `+0x00` field. Not read by Draw or dtor; possibly a base-class vtable
  or unused padding. Safe to treat as `void*` and zero-init.
* `m_PageTexPtrs[-2] = 4`. Reads `4` from the `puVar3[0] = 4` write in
  the ctor; meaning unclear (allocation-tag? array-stride?). Only the
  `puVar3[1] = numPages` slot is actually consumed (by the dtor's
  reverse walk).
* `ApplyGradient` internals -- only the call signature from
  `MenuButton::SetText` is RE'd, not the gradient algorithm.
* `LayoutToCircle` internals -- only the call signature.

These are all unnecessary for the MenuButton port (since the labels are
dead code there), and any future caller that needs them should RE in
that consumer's context.

## See also

* `docs/engine/font.md` -- Font class, glyph layout, normalised CharTemplate
