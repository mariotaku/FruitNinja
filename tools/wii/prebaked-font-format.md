# Wii prebaked font format (task #51, supersample task #52)

Bake-time output of `tools/wii/bake-fonts.py`. Replaces runtime stb_truetype
rasterization on Wii (stb clips CJK glyphs and breaks Korean composition;
FreeType renders both correctly -- see `test_cjk_grid`). Consumed by the Wii
font loader `src/engine/render/BakedFontWii.{h,cpp}` +
`FontCacheObjectTTF::TryBakedGlyph`/`TryBakedEffectGlyph`.

## Device supersample (BAKE_SS, task #52)

The Wii renders the 480x320 LOGICAL ortho filling the full 640x480 EFB, so a
logical texel maps to ~1.333x (horizontal) .. 1.5x (vertical) screen pixels.
Baking at the LOGICAL size then bilinear-upscaling to the EFB left Latin blurry
and thin bars (chonpu/hyphen) under-resolved. The baker rasterizes each canonical
LOGICAL size `S` at `round(S * BAKE_SS)` physical px (`BAKE_SS = 1.5`, the larger
device axis) so atlas texels ~= screen pixels. The `.idx` glyph rects (x/y/w/h)
and metrics (bearingX/Y, advance) are therefore in **SUPERSAMPLED px**. The
loader reads `BAKE_SS` from the header and divides the METRIC px back by it to
recover LOGICAL layout, while keeping the atlas RECT in supersampled texels
mapped onto a logical-size quad (magnification drops to 1/BAKE_SS => crisp) --
the exact scheme the host uses with `kFontSupersample=3`, at 1.5 for Wii.

Footprint: 1.5x per axis grows the atlas ~2.25x (the ~37MB 1x set -> ~80MB on
disc). Lazy-loaded per (size,page), so resident RAM stays bounded to the active
language's touched sizes. (A shared-Latin dedup across languages is a separate
later optimization.)

The old FNT1 magic is bumped to **FNT2** because an FNT1-era atlas read by the
FNT2 loader would render 1.5x too big (records unscaled). The loader rejects
FNT1. **Bumping the format requires re-baking ALL atlases.**

## Face-level metrics (FNT3, task #54)

Task #54 removes stb_truetype (and the runtime `.ttf` open) from the Wii
build entirely. `FontCacheObjectTTF::GetAscender/GetDescender/GetLineHeight`
previously called into `TtfFace::m_Face` (the FreeType/stb face object) --
once nothing opens the `.ttf` at runtime, those calls have no face to read.
The `.idx` header now also carries the FreeType **face-level** metrics
(`ascender`, `descender`, `lineHeight` from freetype-py's `Face.size`, 26.6 fixed
point) captured at the SAME physical bake size as the glyphs
(`physical_size(size)`), truncated to whole SUPERSAMPLED px exactly like a
glyph's `advance` field. `BakedFontWii::GetAscender/GetDescender/GetLineHeight`
return these RAW (still SUPERSAMPLED px, plus the snapped native size and
BAKE_SS) rather than pre-dividing -- `FontCacheObjectTTF` (Wii-only) applies
the exact same `pxToWorld = invFontScale * sizeScale / ss` formula it already
uses for glyph bearing/advance (see `TryBakedGlyph`), so a face metric and a
glyph's own bearingY land in the same world-unit space at the same
requestedSize.

Magic bumped **FNT2 -> FNT3** because the header grows (an FNT2 loader has no
field for the new metrics, and an FNT3 file parsed with the FNT2-shaped
16-byte header would misread the glyph table, which now starts 6 bytes
later). The loader rejects both FNT1 and FNT2. **Bumping the format requires
re-baking ALL atlases.**

## Directory layout

Staged under the Wii data root, mirroring the existing `fonts/` /
`fontstruetype/` convention:

```
Data/fonts/prebaked/<lang>/<size>.idx        -- metrics index (see below)
Data/fonts/prebaked/<lang>/<size>_p0.gxtx    -- atlas page 0 (GXT1 container, GX_TF_IA8)
Data/fonts/prebaked/<lang>/<size>_p1.gxtx    -- atlas page 1 (only if the glyph set overflowed one page)
...
```

`<lang>` is the bake-plan language key (`english_us`, `korean`, `arabic`,
...). `<size>` is one of the 9 canonical sizes: `10 12 14 16 20 22 30 50 56`
(see `tmp/prebake/bake_plan.json` `canonical_sizes`). A runtime font-size
request first goes through `snap_map` (also in `bake_plan.json`) to pick the
nearest canonical size -- the loader does not need `snap_map` itself, only
the offline planner did, but the mapping is documented here so a size miss
at runtime is diagnosable.

Each `.gxtx` page is the same container `tools/lib/gx_encoder.py encode_gxtx`
already produces for game textures (reader: `ReadGxtx` in
`src/engine/asset/TextureFileFormat.cpp`; uploader: `Wii_UploadTiledGX` in
`src/engine/render/gl_funcsWii.cpp`) -- 12-byte BE header (`GXT1` magic,
u16 w, u16 h, u8 gxFormat=3 [`GX_TF_IA8`], u8 version=1, u16 reserved=0)
followed by the GX-tiled texel body. A prebaked page can be routed through
the exact same `ReadGxtx` + `Wii_UploadTiledGX(GX_TF_IA8)` path used for
game textures -- no new GX upload code needed, only a new loader that reads
the `.idx` sidecar and looks up glyph rects instead of calling FreeType /
`FontInterface::PackGlyph` at runtime.

## Atlas texel format

Same IA8 layout as the runtime dynamic atlas (`FontInterface.cpp`'s Wii LA8
path + `gl_funcsWii.cpp`'s `TileIA8`): each texel is `I=255` (opaque white)
`A=coverage` (0..255 FreeType 8-bit coverage), packed as one BIG-ENDIAN u16
`(A<<8)|I` per texel in GX's 4x4-tile raster order. `GL_MODULATE` (the port's
ES1-equivalent constant-color modulate) then multiplies by the draw's vertex
colour unchanged regardless of glyph colour, exactly like the dynamic atlas.

Page dimensions are NOT fixed at 512x512 -- `bake-fonts.py` picks the
smallest power-of-two page (128/256/512) that fits the glyph set with a 2px
transparent shelf gutter (`SHELF_PAD=2`), spilling to additional pages only when
a size's glyph set doesn't fit one page. At the physical bake size (task #52,
`size*(100/72)*BAKE_SS` -- roughly 2x the logical px per axis) packed cells are
~4x the 1x area, so more sizes spill / use the 512 dim; `pack_glyphs` escalates
dim then page-count automatically.

UV overscan: the loader uses the glyph rect UV with an **exact `uvOverscan`
texel overscan** into the transparent shelf gutter, where `uvOverscan =
1.0f/pxToWorld` (== `ss/(inv*sizeScale)`) -- NOT a fixed `+1` texel and NOT the
rounded `ssi = round(ss)` texels. Both of those were tried and regressed:

- A first pass used a fully TIGHT rect (no overscan), reasoning the 2px gutter
  (`SHELF_PAD=2`) alone gives clean edge AA -- regressed on real GX hardware:
  hairline glyphs (chonpu U+30FC, hyphen) rendered fully BLANK, because a UV
  rect whose edges land exactly on the ink boundary leaves the GX texture
  unit's texel-center/edge rounding no headroom, and it can walk a 2-3-texel
  sample off the ink rows entirely (invisible on a normal 20+-row glyph, fatal
  on a 2-3-row one).
- A second pass restored a fixed `+1` texel overscan in the base (NONE) path
  while the effect path kept its pre-existing `+ssi` (rounded) overscan --
  regressed differently: `FinishMesh` (`BakedStringTTF.cpp`, ASM-verified)
  grows EVERY drawable glyph's world quad by a FIXED `+1.0` world unit on the
  far edge, independent of cellW/pad/ss. For the overscanned UV edge to land
  exactly on that `+1.0`-grown quad edge (zero stretch of the true ink), the
  overscan in TEXELS must equal exactly `1.0/pxToWorld`. A flat `+1` texel
  (base) and a rounded `+2` texels (`ssi`, effect) each stretch the ink by a
  few tenths of a world unit on the far edge -- by DIFFERENT amounts between
  the two layers, so a zero-offset effect (e.g. the About-screen shadow, which
  must sit exactly under the base ink) visibly misregistered by about a texel.

The exact `uvOverscan` formula depends only on `ss`/`inv`/`sizeScale` -- never
on `cellW` or pad -- so it is IDENTICAL between `TryBakedGlyph` (base) and
`TryBakedEffectGlyph` (effect) for the same glyph at the same `requestedSize`,
making the ink stretch (and thus the registration) exactly zero in both.

A THIRD regression surfaced once the exact formula was in place: `uvOverscan =
1.0f/pxToWorld` grows WITHOUT BOUND as `sizeScale` shrinks (`sizeScale =
requestedSize/nativeSize`, and `BakedStringBox`'s shrink-to-fit loop can request
well under the native canonical size for a tight box -- e.g. MainScreen's
"slice fruit" 3-line CJK plate, `75x30` box, `baseFontSize=9`, shrunk further by
the vertical-fit loop). At `sizeScale` around 0.6-0.7 the exact overscan exceeds
2 texels -- the ONLY gap `bake-fonts.py`'s packer actually guarantees between
ANY two neighbouring glyphs (`SHELF_PAD=2`, both horizontally within a shelf and
vertically between shelves). Past that point the overscanned UV samples real ink
from whichever glyph happens to be packed next to/below the current one -- seen
on-device as a dark dash/fragment a few texels below the baseline, present only
on glyphs whose packed neighbour-gap happens to sit at exactly the guaranteed
minimum (confirmed: U+30A4 i-katakana has a 2-texel gap to its packed neighbour
in the japanese/10 atlas and showed the artifact; U+3057 shi has a 3-texel gap
at the same size and did not). Fixed by clamping `uvOverscan` to
`kAtlasGutterTexels = 2.0f` (a named constant matching `SHELF_PAD` exactly) in
both `TryBakedGlyph` and `TryBakedEffectGlyph`. Clamping trades a sub-texel ink
stretch (imperceptible at the already-shrunk size that triggers it) for
guaranteed bleed-free sampling; since both functions clamp with the identical
formula and constant, the clamp cannot reintroduce the base/effect misalignment
the exact formula fixed above -- both layers still stretch (or don't) by
exactly the same amount for the same glyph.

A SECOND, independent alignment bug in the same area: the effect path's
`cellOriginX/Y` (and the legacy `bearingX/Y` pad shift) were computed as
`padL*inv`/`padT*inv` (the exact, unrounded pad in world units) while the
padded CELL BUFFER itself is sized with the rounded `padLT = padL*ssi` texels.
On the host these are the same value (`ss` is a clean integer, so
`padLT*invLogical == padL*inv` exactly); on Wii `ss=1.5` is fractional, so
`ssi=2 != ss`, and `padL*inv` silently claimed a smaller origin than the
buffer's actual size -- another ~0.33-world-unit mismatch, additive with the
overscan bug above. Fixed by deriving `cellOriginX/Y` and the bearing shift
from `padLT/padTT*pxToWorld` (the same rounded integers the buffer/UV overscan
already use), so the declared origin always matches the buffer that was
actually packed.

As a further safety net (independent of the two fixes above), the loader also
FLOORS a baked glyph's world-space `cellW`/`cellH` (and the legacy
`width`/`height`) at `1.0f` whenever the baked rect has ink (`w>0 && h>0`) --
see `TryBakedGlyph` in `FontCacheObjectTTF.cpp`. `FinishMesh` culls any glyph
whose world quad size is `< 1.0` in either axis; the dynamic FreeType path
never trips this because it always rasterises directly at the target size (a
bitmap with any coverage has >=1 row/col at ITS OWN raster size). The baked
path instead rasterises once at `BAKE_SS` and divides by it post-hoc, which can
push an already-thin baked rect below the 1.0-world floor purely from that
division -- the floor restores parity with the dynamic path without touching
`FinishMesh` itself.

## Metrics index (`.idx`) binary format

Fixed-size binary, big-endian (Wii-native), no compression. Read the whole
file, binary-search by codepoint (glyphs are sorted ascending).

```
Header (22 bytes):
  offset 0   char[4]  magic = "FNT3"   (task #54; FNT2/FNT1 rejected -- see below)
  offset 4   u16      atlasDim      (page width == height, e.g. 512)
  offset 6   u8       pageCount     (number of <size>_pN.gxtx siblings)
  offset 7   u8       reserved      = 0
  offset 8   u32      glyphCount    (number of glyph records that follow)
  offset 12  u16      supersample   BAKE_SS as 8.8 fixed-point (round(SS*256); 1.5 -> 384)
  offset 14  u16      reserved2     = 0
  offset 16  s16      ascender      Face.size.ascender >> 6, SUPERSAMPLED px (task #54)
  offset 18  s16      descender     Face.size.descender >> 6, SUPERSAMPLED px (negative)
  offset 20  s16      lineHeight    Face.size.height >> 6, SUPERSAMPLED px

Glyph record (20 bytes), starting at offset 22, repeated glyphCount times, sorted by codepoint ascending:
  u32   codepoint     Unicode code point (UTF-32)
  u8    page          index into the <size>_pN.gxtx siblings (0-based)
  u8    reserved      = 0
  u16   x             texel X of the glyph's bitmap rect within its page (SUPERSAMPLED texels)
  u16   y             texel Y of the glyph's bitmap rect within its page (SUPERSAMPLED texels)
  u16   w             bitmap rect width, SUPERSAMPLED texels  (FreeType bitmap.width)
  u16   h             bitmap rect height, SUPERSAMPLED texels (FreeType bitmap.rows)
  s16   bearingX      FreeType glyph.bitmap_left, SUPERSAMPLED px
  s16   bearingY      FreeType glyph.bitmap_top, SUPERSAMPLED px
  u16   advance       FreeType glyph.advance.x >> 6, SUPERSAMPLED px (rounded down)
```

Total file size = 22 + 20*glyphCount bytes.

All px/texel fields are at the PHYSICAL bake size = `round(size * (100/72) *
BAKE_SS)` (see `physical_size` in the baker):

- `(100/72)` reproduces the runtime `FontCacheObjectTTF::SetCharSize` DPI scale
  and STAYS folded into the world size (the loader does NOT divide it out) so
  baked world-size == host-FreeType world-size at the same requested size. The
  1x baker omitted this, which made baked glyphs render 0.72x too small on-device
  (task #52 shrink fix).
- `BAKE_SS` (from the header field) is the device supersample; the loader DIVIDES
  the metric px (bearing/advance + the glyph's world width/height) by it to
  recover LOGICAL layout, while the atlas RECT (x/y/w/h) stays supersampled and
  maps onto a logical-size quad (magnification 1/BAKE_SS => crisp).

Concrete size math (requested logical size 14, BAKE_SS=1.5):
  physical bake px = round(14 * 100/72 * 1.5) = round(29.17) = 29
  loader world = physicalMetricPx / BAKE_SS * (requested/native) * m_InvFontScale
               = (14 * 100/72 * 1.5) / 1.5 * 1 * inv = 14 * (100/72) * inv
  host-FT world (NONE path) = glyph_px * invLogical
               = (14 * kss * 100/72) * inv / kss = 14 * (100/72) * inv   -- IDENTICAL.

Field semantics otherwise match the FreeType rasterization contract 1:1 (no
baked-cell / padding transform applied -- unlike the binary's runtime `GlyphTTF`
baked-bearing model, this is the "legacy" separate-bearing contract already
used by `Mortar::GlyphAtlasEntry`'s `u0..v1`/`bearingX/Y`/`advanceX` fields
in `FontInterface.h`, so a loader can map an `.idx` record straight onto a
`GlyphAtlasEntry` after the BAKE_SS divide):

- `bearingX/bearingY` are `glyph.bitmap_left` / `glyph.bitmap_top` (top-left
  bearing from the pen origin, FreeType's raster-space convention, integer
  supersampled px -- the loader divides by BAKE_SS).
- `advance` is `glyph.advance.x >> 6` (26.6 fixed-point pen advance,
  truncated to whole supersampled px; loader divides by BAKE_SS).
- A codepoint with an empty bitmap (e.g. space, U+0020) still gets a record:
  `w=h=0`, `x=y=page=0`, `bearingX=bearingY=0`, `advance` from FreeType
  (nonzero). The loader must treat `w==0 || h==0` as "no ink, don't sample
  the atlas" rather than skipping the record -- layout (advance) still
  applies.

## Codepoint coverage

A codepoint is only baked if `FT_Get_Char_Index(face, cp) != 0` in the
selected face (`gangofchinese.ttf` for every language except `arabic`, which
uses `arabic.ttf`). Missing glyphs are reported by the baker (see its
`--report` output / stderr) and simply absent from the `.idx` -- the runtime
loader's lookup miss behavior (fallback glyph, `.notdef`, or silent skip) is
a loader-side decision, out of scope for the baker.
