# Gameplay-Adjacent Structs

## MenuBackground

Simple background image drawn behind menu screens.

### Struct Layout (8 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | SmartPtr\<Texture\> | m_Texture | Background texture |
| +0x04 | int | m_field04 | |

### MenuBackground::Init (0x16f0a4)

Loads a localised texture via `TextureManager::LoadLocalisedTexture`.

Used by SplashInit and FrontendInit. Created with `operator_new(8)`.

---

## MenuButton : HUDControl3d : HUDControl (size = 0x15C, leaf class)

Interactive button used in all menus. Renders a 2D texture quad with optional sparkle ring, "new" indicator, and text labels. Each button optionally has a **real 3D Fruit/Bomb entity** spinning on top, drawn by the normal ActorManager render pipeline.

### Architecture

MenuButton has 3 rendering layers (drawn by `MenuButton::Draw`) plus one entity (drawn by `ActorManager::Draw`):

```
Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton)
  └─ Real Fruit/Bomb entity at +0x80, created via ActorManager::Add
     Position = button.pos, rotation speed = random 8-12 deg/frame
     Drawn by ActorManager::Draw() in GameDraw (depth-sorted 3D mesh)

Layer 1 (2D): Button texture quad (+0x74)
  └─ Scale(size) → RotZ(angle) → Translate(globalOffset + pos)
     Shake offset if field_0x158 > 0 (random ±3.0)
     TintColour with button colour + alpha → DrawQuadUnCached

Layer 2 (2D): "New item" star indicator (+0xFC >= 0)
  └─ Hop-up bounce via |SinIdx|*6, tinted grey (dim) or white (highlight)
     Uses shared `new_item.tex` ("NEW" stamp) from LoadContent

Layer 3 (2D): Sparkle ring (+0xF8 >= 0) — DEAD CODE in Bada build
  └─ 8 spike-quads × 6 verts = 48 QUADCUSTOMVERTEX tri-list
     Pre-baked geometry on first call; colour cycles through 8 brightness levels
     Texture: blurry_backing.tex (slot 2 of LoadContent)
     Trigger: SetLoadingSymbol(true) — but NEVER called anywhere in this binary

Text: BakedString labels drawn at button.pos with Y offsets
```

### Fruit Entity Creation (in Init, 0x0014ee40)

```c
// fruitType >= 0 creates a real entity; -1 (toggles) skips this
if (fruitType >= 0) {
    int entityType = (fruitType >= bombThreshold) ? 1 : 0;  // 0=Fruit, 1=Bomb
    Entity* entity = ActorManager::Add(entityType, true);
    entity->pos = button.pos;
    entity->vel = globalScale;     // written to +0x1c (velocity fields, NOT scale!)
    entity->Init(0, fruitType, NULL);  // scale param = NULL → 1.0
    // Init → SetFruitType computes: entity.scale = FruitInfo[type].scale * 0.01
    // Per-fruit scale from Data/xml/fruitlist.xml (e.g. watermelon=75 → 0.75)
    this->m_pEntity = entity;        // +0x80

    // POST-INIT: shrink fruit for menu display
    entity->scale *= 0.2;  // DAT_0014f194 = 0.2

    this->field_0x34 = 0x40;  // menu draw layer
    this->field_0xf4 = RandFloat(4.0) + 8.0;  // rotation speed 8-12
    if (Rand32(2) == 0) field_0xf4 = -field_0xf4;  // random direction

    // Clamp rotation magnitude
    entity->rotX = max(0.75, abs(entity->rotX)) * sign(entity->rotX);
    entity->rotY = max(0.50, abs(entity->rotY)) * sign(entity->rotY);
}
```

### Struct Layout (0x15C bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x73 | HUDControl | super | Base class |
| +0x74 | SmartPtr\<Texture\> | m_ButtonTex | Main button texture |
| +0x80 | Entity* | m_pEntity | 3D fruit/bomb spinning on button (NULL for toggles) |
| +0x84 | int | m_FruitType | -1 = no fruit, 0+ = fruit index, ≥bombThreshold = bomb |
| +0x88 | Delegate0\<void\> | m_ClickCallback | Fired on touch release |
| +0xAC | Delegate0\<void\> | m_DeletedCallback | Fired when button removed |
| +0xD0 | int | m_FadeCounter | Drives alpha fade (× 1000 / 255) |
| +0xE8 | float | m_RandomOffset | Random visual offset (-20 to +20) |
| +0xF0 | bool | m_bFlipped | Random horizontal flip |
| +0xF4 | float | m_RotationSpeed | 8-12 deg/frame, random sign |
| +0xF8 | float | m_SparkleTimer | ≥0 = sparkle ring active |
| +0xFC | float | m_NewIndicatorTimer | ≥0 = "new" star active |
| +0x100..+0x108 | Vec3 | m_HitBoundsScale | From constructor param_5 |
| +0x114 | BakedString* | m_pLabel1 | Curved text label (foreground colour). Set ONLY by `MenuButton::SetText` (0x0014ebc0) -- which is **never called** in this binary version. Stays NULL at runtime. See "Label fields are dead code" below. |
| +0x118 | BakedString* | m_pLabel2 | Curved text label (drop shadow, black @ alpha 0x50). Same lifecycle as `m_pLabel1` -- always NULL in shipped binary. |
| +0x11C | int | m_PlayerIndex | For multiplayer colour tint |
| +0x120 | byte | m_bScoreSubmitted | |
| +0x121 | byte | m_bVisible | = 1 |
| +0x122 | byte | m_bInteractive | = 1 |
| +0x123 | byte | m_bEnabled | = 1 |
| +0x124..+0x12C | Vec3 | m_TargetSize | Hit-test bounds target |
| +0x130 | bool | m_bHasHitArea | true if hitBounds > 0 |
| +0x131 | byte | m_bHighlighted | Affects tint (0.5 vs 1.0 alpha) |
| +0x134 | Fruit* | m_pFruitPiece | Direct fruit reference (for scale/rotate access) |
| +0x138 | byte | m_bRespondsToBackKey | Set to 1 by screen create-button code immediately after `Init` to mark this button as the screen's "default action" for the hardware Back / Menu key. `MenuButton::Update` (0x0014e9a8) reads it; if `m_bHighlighted && Game.m_BackKeyPressed && +0x138`, it auto-fires the button — slicing `m_pEntity` (vtable+0x24) for fruit/bomb buttons or calling `TouchReleased()` for toggles. RE'd 2026-04-29. NOT a fade-out / removal flag (Init writes 0; back-key behaviour is opt-in via screen creation code). See `docs/engine/menubutton-138.md`. |
| +0x13C | float | m_AnimScale | = 1.0 |
| +0x140..+0x148 | Vec3 | m_BounceParams | For "new" indicator bounce |
| +0x14C | float | m_AnimSpeed2 | = 5.0 |
| +0x150 | float | m_AnimSpeed | = 5.0 |
| +0x154 | float | m_field154 | |
| +0x158 | float | m_ShakeTimer | > 0 = shaking (random ±3.0 offset) |

### Key Functions

<!-- Analysed: 2026-04-15T16:00 -->

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| Init | 0x0014ee40 | 222 | Create entity, set callbacks, random rotation |
| Update | 0x0014e614 | — | Tick entity rotation, animations, shake |
| Draw | 0x0014f9cc | 359 | Render 3 layers: button quad + star + sparkle ring |
| SetText | 0x0014ebc0 | — | Set BakedString labels |
| AddPeice | 0x00150240 | — | Add sub-element (text, icon) |
| TouchReleased | 0x0014e5cc | — | Tap-release gate: skip click if fruit-typed, fire callback only for toggles |
| Clicked | 0x001507d8 | — | Fire click delegate (empty virtual stub) |
| LoadContent | 0x0014f674 | 28 | Load 3 shared textures: slot 1 `scratchs.tex` (Phase-A backdrop, GOT 0x77e0), slot 2 `blurry_backing.tex` (sparkle-ring texture, GOT 0x79dc), slot 3 `new_item.tex` (NEW-stamp star, GOT 0x7894). RE-confirmed 2026-04-29 from literal pool at 0x0014f6f0..0x0014f70c and bytes at strings 0x001bbd58 / 0x001baefa / 0x001bbd65. |
| SetNewSymbol | 0x0014e404 | 16 | `true` -> timer = 0 (only if currently <0); `false` -> timer = -1 |
| Remove | 0x0014ed18 | — | Animate removal |
| MakeCritical | 0x00151764 | — | Display "critical" overlay at slice point; position, fade, animate |
| MakeRare | 0x001518d8 | — | Display "rare" (special) overlay; same as critical but alpha=0.5 |

### "New item" star indicator (m_NewIndicatorTimer @ +0xFC)

<!-- RE'd: 2026-04-29 -->

Animated "NEW" stamp drawn over a button when ItemManager has unseen
shop items. Trigger lives in `MainScreen_Update` via `SetNewSymbol`
(0x0014e404): `true` sets timer = 0.0 (only if currently <0, i.e. edge-trigger);
`false` sets timer = -1.0. Gate is `>= 0` = visible.

#### Update tick (in `MenuButton::Update` @ 0x0014e644)

```c
if (0.0 <= this->m_NewIndicatorTimer) {
    this->m_NewIndicatorTimer += param_1 + param_1;     // += 2 * dt
    if (1.0 <= this->m_SparkleTimer) {
        this->m_NewIndicatorTimer = 0.0;                 // phase reset on sparkle
    }
}
```

- Rate: `+= 2 * dt` per frame. At dt=1/60 -> +1/30 per frame.
- No wrap; timer grows unbounded. (SinIdx in Draw wraps via uint16 cast.)
- Phase resets to 0 only when `m_SparkleTimer >= 1.0` (sparkle ring just fired).
- DAT_0014e970 = 0.0 is the reset literal.

#### Draw block (in `MenuButton::Draw` @ 0x0014fd18..0x0014fe98)

```c
if (0.0 <= this->m_NewIndicatorTimer) {
    float ratio = base.size.x / m_TargetSize.x;          // fade-in scale (0..1)
    Texture::Set(GOT[+0x7894 -> "new_item.tex"]);

    ResetMatrix();
    Scale44(M, ratio * 64.0, ratio * 32.0, 0.0);          // 64x32 quad

    // Bounce angle = uint16(timer * 180 * 182)
    //   period = 65536 / (2*dt * 180 * 182) ~= 60 frames = 1s @ 60fps
    float s = SinIdx((uint16_t)(timer * 180.0f * 182.0f));
    float bounce_y = fabsf(s) * 6.0f;                     // hop-up only

    Vec3 offset = {
        m_BounceParams.x * m_TargetSize.x * 0.5f,         // boX*W/2
        bounce_y + m_BounceParams.y * m_TargetSize.y * 0.5f, // hop + boY*H/2
        0.0f
    };
    offset *= ratio;                                      // shrinks with button
    GlobalTranslate44(M, button.pos + offset);
    SetMatrix(M); UploadMatrices();

    Colour tint = m_bHighlighted
        ? Colour(0xFF, 0xFF, 0xFF, alpha)                 // full white
        : Colour(0x80, 0x80, 0x80, alpha);                // dimmed grey
    tint = TintColour(tint);                              // applied once
    DrawQuadSized(0.0, 1.0, 0.0, 1.0, tint);
    Texture::UnSet(...);
}
```

`alpha` here is the same `local_2b0` byte computed once at Draw entry:
```c
if (m_FruitType < 0) alpha = 0xFF;                           // toggles
else alpha = clamp((m_FadeCounter * 256.0) / 16383.0, 0, 0xFF);  // fade-in
```

#### Resolved constants

| Address | Value | Use |
|---------|-------|-----|
| DAT_0014e970 | 0.0 | NewIndicatorTimer reset literal (sparkle phase-reset) |
| DAT_0014f240 | 0.85 | `m_BounceParams.x` and `.y` initial value |
| DAT_0014f244 | 0.0 | `m_BounceParams.z` initial value |
| DAT_0014fcf0 | 256.0 | Alpha numerator (FadeCounter * 256 / 16383) |
| DAT_0014fcf4 | 16383.0 | Alpha denominator |
| DAT_0015003c | 180.0 | First sin-frequency multiplier |
| DAT_00150040 | 182.0 | Second sin-frequency multiplier (180*182=32760, ~=uint16/2) |
| DAT_00150044 | 0.0 | Z component (always zero for 2D) |
| DAT_00150048 | 32.0 | Star quad scale Y (height) |
| DAT_0015004c | 64.0 | Star quad scale X (width) |
| GOT+0x7894 | SmartPtr\<Texture\>* | `new_item.tex` slot (DAT_0015005c) |

`new_item.tex` is the gold "NEW" stamp at `Data/textures/new_item.tex`.
Loaded by `MenuButton::LoadContent` (0x0014f674) into the second of three
shared static SmartPtr slots (1st: `scratchs.tex`, 2nd: `blurry_backing.tex`,
3rd: `new_item.tex`). Confirmed by reading the GOT pointers and the C-strings
at 0x001bbd58 / 0x001baefa / 0x001bbd65.

#### Animation summary (timer -> visual)

| Output | Formula |
|--------|---------|
| Visibility | timer >= 0 (else hidden) |
| Quad size | (64 * ratio, 32 * ratio) where ratio = size.x / TargetSize.x |
| Position offset | ratio * (0.425 * W, |sin(angle)|*6 + 0.425 * H, 0) |
| Bounce angle | uint16(timer * 32760), period ~= 1 sec at 60 fps |
| Tint colour | (0xFF,0xFF,0xFF) highlighted else (0x80,0x80,0x80) |
| Alpha | follows button fade-in (FadeCounter * 256 / 16383, clamped) |

#### Behaviour matrix (highlight vs dim)

| Condition | Tint | Notes |
|-----------|------|-------|
| `m_bHighlighted == 1` (default) | white (255,255,255,a) | Active / focused button |
| `m_bHighlighted == 0` | grey (128,128,128,a) | Dimmed when another button is highlighted in nav |

Unlike the main button quad (which has its own `bVar5` "shrink-when-not-highlighted"
ratio check), the star *texture stays fixed-size at 64x32* — only the position-offset
and `ratio` link it to the parent button's fade-in animation. No alpha pulse;
the alpha purely tracks the parent button's fade-in. The "pulse" the user sees
is the |sin|*6 px Y bounce, not a colour pulse.

### Label fields are dead code in the shipped binary

<!-- RE'd: 2026-04-29 -->

Both `m_pLabel1` (+0x114) and `m_pLabel2` (+0x118) are `BakedString*` fields
that store curved-arc text labels for a button. They are written ONLY by
`MenuButton::SetText` (binary @ 0x0014ebc0). A binary-wide cross-reference
scan finds **zero call sites** to that function (one EXTERNAL entry-point
reference, no actual `bl` from any code section). All four `MenuButton`
constructors initialise both fields to NULL, and `MenuButton::Init` does
not touch them.

**Conclusion**: in the shipped Bada Fruit Ninja binary, `m_pLabel1` and
`m_pLabel2` are always NULL at draw time. The label-draw block in
`MenuButton::Draw` (0x0014f9cc) is gated on **both** being non-NULL
(`m_pLabel1 != NULL && m_pLabel2 != NULL`), so the entire 4-call
BakedString sequence is dead code at runtime.

The port should:
1. Keep the fields (typed `BakedString*`) so the layout stays correct.
2. Initialise them to NULL in MenuButton's constructor.
3. NOT attempt to ship label rendering for MenuButton — there is nothing
   to port; the binary never displayed button labels at all. Any
   "missing menu text" the port appears to lack must be coming from a
   *different* widget (e.g. a separate Font::DrawString call from the
   owning Screen, or a different control type), not from MenuButton.

If a future content patch needs to surface labels (e.g. for accessibility
or localisation), the spec for `MenuButton::SetText` and the
`BakedString_Draw` layout are documented below for reference. They are
**not** required for fidelity with the shipped binary.

#### MenuButton::SetText (0x0014ebc0) -- the only setter

```c
// Allocates two BakedStrings on the heap and stores them at +0x114 / +0x118.
// param_2 = foreground gradient top colour (used by ApplyGradient)
// param_3 = foreground gradient bottom colour (NOT used after Colour::Colour copies)
// param_4 = circle-layout radius (passed to LayoutToCircle)
// Internally references a global "default tint" Colour at GOT[DAT_0014eccc]
// and the GameTask static block at GOT[DAT_0014ecd0] (for *(Font**)(gd + 0x54)).
void MenuButton::SetText(MenuButton *this, const char *text,
                         Colour param_2, Colour param_3, float radius)
{
    Font* font = *(Font**)(g_GameData + 0x54);  // font_fruit_ninja(_HD).fnt

    // ----- m_pLabel1 = main coloured label -----
    Colour fgInit = *(Colour*)GOT[DAT_0014eccc];     // global default tint
    Utf8StringIterator it(text);
    BakedString *fg = new BakedString(font, it, fgInit);  // operator_new(0x1c)
    fg = fg->ApplyGradient(0xd, 0xe, &param_2, 3);   // top-bottom gradient
    fg->LayoutToCircle(radius);                       // curve glyphs along arc
    this->m_pLabel1 = fg;

    // ----- m_pLabel2 = drop shadow (black @ alpha 0x50 = 80/255 = 31%) -----
    Colour shadowColour(0x00, 0x00, 0x00, 0x50);
    Utf8StringIterator it2(text);
    BakedString *sh = new BakedString(font, it2, shadowColour);
    sh->LayoutToCircle(radius);                       // same curve, no gradient
    this->m_pLabel2 = sh;
}
```

Notes:
* Both labels share the same text string, font, and arc radius -- they
  differ ONLY in colour (foreground gradient vs translucent black).
* `m_pLabel2` is the SHADOW (drawn first), `m_pLabel1` is the FILL
  (drawn second on top). The naming is misleading; "Label1 = upper /
  Label2 = lower" in the field-table is an **incorrect** earlier guess.
* Font slot read is `g_GameData + 0x54` (i.e. `font_fruit_ninja.fnt` /
  `font_fruit_ninja_HD.fnt`). See `docs/engine/font.md`.

#### Label-draw block in MenuButton::Draw (0x0015015e..0x0015020a)

When both labels are non-NULL, Draw issues **four** BakedString draw calls
in the order shown. All use scale=20.0, alignment=5 (no offset adjustment),
and rotate by `m_Timer` -- which the button quad also rotates by (button
quad uses SinIdx/CosIdx for a small spin animation).

```c
// pos = (this->base).super.pos          // copied for each call
// Both label calls draw at the SAME world position; only rotZ varies.
// Multiplayer same-screen support: the +180 deg pair flips the text for
// the second player viewing the device upside-down.

if (m_pLabel1 != NULL && m_pLabel2 != NULL) {
    Vec3 p = base.pos;

    // Pass 1: shadow @ m_Timer
    BakedString::Draw(m_pLabel2, /*scale*/20.0, /*rotZ*/m_Timer, /*z*/0,
                      /*align*/5, &p);

    // Pass 2: foreground @ m_Timer  (overlays shadow)
    BakedString::Draw(m_pLabel1, 20.0, m_Timer, 0, 5, &p);

    // Pass 3: shadow @ m_Timer + 180  (second-player view)
    BakedString::Draw(m_pLabel2, 20.0, m_Timer + 180.0f, 0, 5, &p);

    // Pass 4: foreground @ m_Timer + 180
    BakedString::Draw(m_pLabel1, 20.0, m_Timer + 180.0f, 0, 5, &p);
}
```

Resolved literal-pool constants (in `MenuButton::Draw`):

| DAT | Address | Hex | Float | Use |
|-----|---------|-----|-------|-----|
| `DAT_00150228` | `0x00150228` | `0x4199999a` | **20.0** | Label scale (em size in world units) |
| `DAT_0015022c` | `0x0015022c` | `0x43340000` | **180.0** | Second-player rotation offset (degrees) |
| `DAT_00150230` | `0x00150230` | `0x000449e8` | -- | GOT slot for sparkle-ring vert buffer (NOT label related) |
| `DAT_00150234` | `0x00150234` | `0x000079dc` | -- | GOT slot for `blurry_backing.tex` (sparkle ring) |

Note `MatrixStack::RotZ` (0x00198458) takes degrees and converts via
`idx = (degrees * 182)` for `SinIdx/CosIdx` (DAT_001984a4 = 182.0;
65536/360 = 182.04). 180.0 + m_Timer thus rotates 180 deg + spin angle
-- exactly opposite the first pair. `m_Timer` accumulates from
`m_Timer += dt * m_AnimSpeed` in Update; for a static label, `m_Timer`
might be near 0 and the two passes look like "label drawn upright +
upside-down".

#### Tint / alpha behaviour

* **Foreground colour**: baked into BakedString vertex data at
  construction via `Colour::PlatformColour(param_3)` of the BakedString
  ctor + `ApplyGradient` post-process. NOT modified at draw time.
* **Shadow colour**: hard-coded `(0,0,0,0x50)` = black @ 80/255 alpha
  (~31%). Baked at construction.
* **Highlight branch**: `m_bHighlighted` does **not** change label
  colour. Labels render identically whether highlighted or not.
* **Fade-in**: labels do **not** track `m_FadeCounter`. They are
  full-opacity (modulo their baked alpha) the moment they're set.
* **Tint stack**: `BakedString_Draw` does NOT call `TintColour` -- the
  per-vertex colour is uploaded directly to the shader. The button's
  global tint stack is irrelevant to label rendering.

#### Highlight DOES NOT affect labels

To be explicit: the only places `m_bHighlighted` is read in Draw are:
1. The button-quad shrink ratio decision (`bVar5`).
2. The "new item" star tint colour (white vs grey).

The label block reads neither. `m_bHighlighted` has zero visible effect
on label rendering.

### Sparkle ring (m_SparkleTimer @ +0xF8) — Layer 3, DEAD CODE in Bada build

<!-- RE'd: 2026-04-29 -->

8 spike-quads arranged radially around the button centre, each tinted
with a different brightness, advancing one segment per frame to create
a "rotating chase" effect. Used in the iOS/Android builds for the
"loading" state on shop / async buttons. **In the Bada Fruit Ninja
binary the trigger function `MenuButton::SetLoadingSymbol` exists but
is not called from anywhere** — confirmed by:

1. Direct ref scan: only ref to `0x0014e45c` is "EXTERNAL" entry-point.
2. Word-aligned data scan for `0x0014e45c` / `0x0014e45d`: zero hits
   in any data block (no vtable entry, no function-pointer table).
3. BL-target scan across all instructions: only the function's own
   internal `blt` (sign-test branch).

Init writes `m_SparkleTimer = -1.0`, Update only ticks if `>= 0.0`,
and the only positive write in the binary is gated by SetLoadingSymbol.
Therefore Layer 3 never renders in normal gameplay on Bada.

The full pipeline is documented below in case the port wants to wire
it up for fidelity with the iOS/Android visual (e.g. for a future
"loading" state in shop / network screens).

#### Trigger: MenuButton::SetLoadingSymbol(bool) (0x0014e45c)

```c
// param_1 == true  -> arm:    m_SparkleTimer = 0.0  (only if currently <0)
// param_1 == false -> disarm: m_SparkleTimer = -1.0 (only if currently >=0)
// Identical edge-trigger pattern as SetNewSymbol.
void MenuButton::SetLoadingSymbol(bool on) {
    float v = m_SparkleTimer;
    if (on) {
        if (v < 0.0f) m_SparkleTimer = 0.0f;       // DAT_0014e480 = 0.0
    } else {
        if (v >= 0.0f) m_SparkleTimer = -1.0f;
    }
}
```

#### Update tick (in MenuButton::Update @ 0x0014e644..0x0014e65c)

```c
if (m_SparkleTimer >= 0.0f) {
    m_SparkleTimer += dt * 8.0f;                   // rate = 8 segments/sec
    if (m_SparkleTimer >= 8.0f) m_SparkleTimer = 0.0f;  // wrap (DAT_0014e970)
}
// Side effect inside the m_NewIndicatorTimer block:
if (m_NewIndicatorTimer >= 0.0f && m_SparkleTimer >= 1.0f) {
    m_NewIndicatorTimer = 0.0f;                    // phase reset on sparkle tick
}
```

Period = 1 second per full ring rotation (8 segments × dt of 1/60 ×
8/sec = 8 segment-units per sec, wraps at 8 → 1 second loop).

#### Draw gate (in MenuButton::Draw @ 0x0014fe98..0x0015015e)

```c
if (m_SparkleTimer >= 0.0f && blurry_backing.tex.IsValid()) {
    // ...lazy build + colour cycle + matrix + draw...
}
```

#### Geometry — pre-baked once, lazy

A static 48-vertex `QUADCUSTOMVERTEX` buffer at GOT+DAT_00150064 is built
on the first frame the sparkle activates, gated by a 1-byte init flag at
the same address (the byte sits before the vertex array; vertex bytes
start at +4). The geometry is identical every frame; only colours and
the world matrix change per frame.

```c
// Static one-time build. uVar18 sweeps 0 -> 0xfff0 in steps of 0x1ffe.
// 0x1ffe / 0x10000 of full circle = 45° (8 segments).
// 0x3ffc / 0x10000 of full circle = 90° (sin(a+90°) = cos(a) shift).
//
// Per segment at angle a:
//   outer = (sin(a) * 0.5,         cos(a) * 0.5,         0)   // unit-radius * 0.5
//   tang  = (sin(a+90°) * 0.075,   cos(a+90°) * 0.075,   0)   // tangent dir, half-width 0.075
//         = (cos(a) * 0.075,       -sin(a) * 0.075,      0)
//   inner = (sin(a) * 0.5 * 0.6,   cos(a) * 0.5 * 0.6,   0)   // inner radius = 0.6 * outer = 0.3
//         = (sin(a) * 0.3,         cos(a) * 0.3,         0)
//
// 6 verts per segment (2 triangles per spike-quad):
//   V0 = outer - tang    (outer-left)
//   V1 = outer + tang    (outer-right)
//   V2 = inner - tang    (inner-left)
//   V3 = inner - tang    (DUPLICATE of V2 — degenerate index)
//   V4 = outer + tang    (DUPLICATE of V1)
//   V5 = inner + tang    (inner-right)
// Triangles via tri-list:  (V0,V1,V2)  and  (V3,V4,V5).
//
// Per-vertex fields:
//   x,y,z  = position above
//   z      = 0.0
//   alpha/weight (+0x14) = 1.0
//   the colour at +0x18 is filled per-frame in the second loop
//   (other QCV fields zeroed)
```

Final geometry: 8 disconnected "spike" quads at 45° intervals around the
button centre, each spanning radius 0.3..0.5 and tangential half-width
±0.075. Total subtended arc per spike ≈ 17° (out of 45° spacing),
leaving gaps between spikes — characteristic "8 dots / chase lights"
visual, not a continuous ring.

Resolved DATs:
| Address | Value | Use |
|---------|-------|-----|
| DAT_00150044 | 0.0 | Z component (always zero for 2D); also alpha/weight zeroes |
| DAT_00150050 | 0x80000007 | mask 7 (negative-aware idiom for `(int)timer & 7`) |
| DAT_00150054 | 0.075 | Tangential half-width per spike |
| DAT_00150058 | 0.6 | Inner radius / outer radius ratio |
| DAT_00150064 | 0x000449e8 | GOT-relative offset to BSS: byte init-flag + 48*0x24 vertex array |
| DAT_00150224 | 0x80000007 | mask 7 (per-segment colour index) |
| DAT_00150230 | 0x000449e8 | Same as DAT_00150064 (vertex buffer base) |

#### Colour cycle (every frame)

```c
// shift = uint16(SparkleTimer & 7), advancing 1 per tick (8 ticks/sec).
// Walks the segments in reverse so the brightest spike "rotates"
// clockwise around the ring.
int shift = ((int)m_SparkleTimer) & 7;     // saturating arithmetic
shift = 7 - shift;                          // 7..0 reverse
for (int seg = 0; seg < 8; ++seg) {
    int idx = (shift + seg) & 7;            // colour-index for this spike
    int shade = idx * 0x20;                 // 0, 32, 64, ..., 224
    if (shade > 0xfe) shade = 0xff;         // (only matters for idx=8 case)
    if (shade < 0x40) shade = 0x40;         // CLAMP LOW: idx 0,1 -> 64

    // Per-segment colour: greyscale, alpha=200 (0xC8 = 78% opaque)
    Colour c(shade, shade, shade, 200);
    c = TintColour(c);                       // applied once
    uint32_t platformColour = c.PlatformColour();   // packed BGRA

    // Write same colour to all 6 verts of this spike-quad
    QUADCUSTOMVERTEX* v = &vertexBuf[seg * 6];
    for (int j = 0; j < 6; ++j) v[j].colour = platformColour;
}
```

So the 8 effective shades (after clamp) are: **{64, 64, 64, 96, 128,
160, 192, 224}** rotating around the ring once per second. Two of the
8 spikes are at the floor luminance (64), giving an asymmetric chase.

#### Matrix + draw call

```c
Texture::Set(blurry_backing.tex);
ResetMatrix();
// scale = g_GlobalScreenScale * 0.75 * 0.75  (g_GlobalScreenScale @ 0x001f4334
//   = (1, 1, 1) at default render res, init by _GLOBAL__I_Utils.cpp)
Vec3 scale = g_GlobalScreenScale * 0.5625f;
MatrixStack* ms = (MatrixStack*)(GameData + 0x1094);   // GOT+0x7348 -> +0x1094
ms->Scale(scale);
ms->Translate(button.pos);
UploadMatrices();

Mortar::Mesh::DrawTriList(vertexBuf, 0x30 /* 48 verts */, false, NULL);
Texture::UnSet(blurry_backing.tex);
```

So the world-space outer radius of the ring = `0.5 * 0.5625 ≈ 0.281`
units at default scale. The button quad's typical size is ~64 units
wide, so the sparkle is intentionally a small 24x24-ish sparkle around
the button's centre, not encircling the entire button.

#### Resolved constants

| Address | Value | Use |
|---------|-------|-----|
| DAT_0014e480 | 0.0 | SetLoadingSymbol(true) arm value (timer reset to 0) |
| DAT_0014e970 | 0.0 | Update wrap value (timer >= 8 -> 0) |
| DAT_00150234 | 0x000079dc | GOT entry pointing to `blurry_backing.tex` SmartPtr |
| DAT_00150238 | 0x000077cc | GOT entry pointing to `g_GlobalScreenScale` Vec3 |
| DAT_0015023c | 0x00007348 | GOT entry pointing to `GameData` (for MatrixStack at +0x1094) |
| GOT+0x77cc | g_GlobalScreenScale @ 0x001f4334 | Default (1,1,1) from _GLOBAL__I_Utils.cpp |

#### Port implementation notes (if/when wiring)

The port should:
1. Implement `MenuButton::SetLoadingSymbol(bool)` so any future call site
   (e.g. networking screens) works correctly.
2. Implement the Update tick (already partial in port; just add the
   SparkleTimer branch).
3. Implement the Draw block: lazy 48-vert build + per-frame colour cycle.
4. Use blurry_backing.tex for the sparkle texture.
5. **Do NOT spend time wiring trigger sites** — the binary has zero,
   so this is purely a forward-compatibility / completeness concern.
   Marking the codepath dead-but-implemented is fine.

The implementer can wire the geometry from this spec without re-RE'ing
the binary. The clamping/cycling is unusual (8 shades, two clamped to
floor 64) and should be matched exactly.

### Constructor Variants

| Address | Signature | Notes |
|---------|-----------|-------|
| 0x0014f24c | `MenuButton(pos, clickCb, fruitType, hitBounds, deletedCb, ...)` | Full constructor |
| 0x0014f348 | Similar | Variant |
| 0x0014f444 | Similar | Variant — called via thunk 0x000f36cc |
| 0x0014f55c | Similar | Variant |
| 0x000f36cc | Thunk | Dispatches to 0x0014f444 |
| 0x000f747c | Thunk | Most-used entry point (38 callers) |

All constructors internally call `MenuButton::Init` (0x001073d0 → 0x0014ee40).

### Usage Across Screens (38 call sites via constructor thunk 0x000f747c)

MenuButton is the primary interactive widget used in virtually every screen:

| Caller Function | Address Range | Buttons Created |
|----------------|---------------|-----------------|
| **MainScreen_Update** | 0x0014b342–0x0014bca0 | Play, Dojo, Arcade, Zen, Multiplayer, Sensei (6 buttons) |
| **AboutScreen::Update** | 0x0012f0cc–0x0012f242 | About screen navigation |
| **UpdateButtons** | 0x00130b58 | Dynamic button creation |
| **GameOverScreen::CreateControls** | 0x0013e7f8–0x0013ebea | Retry, menu, share buttons |
| **UpdateOnlineMultiplayerButton** | 0x0013edd8–0x0013eeaa | Online multiplayer toggle |
| **GameOverScreen::Update** | 0x001384fa–0x00138802 | 3 dynamic buttons |
| **UpdateLeaderboard** | 0x0013b136 | Leaderboard entry buttons |
| **CreateRetryButton** | 0x00141208 | Pause/GameOver retry |
| **CreateQuitButton** | 0x0014136e, 0x00148c06 | Quit buttons (2 screens) |
| **Leaderboard nav** | 0x00148d50–0x00149508 | Friends, Global, Local, Weekly, PageUp, PageDown |
| **PauseScreen::Update** | 0x00145d18 | Pause menu buttons |
| **PowerUpShop::Update** | 0x001547dc–0x00154bb0 | 4 shop item buttons |
| **TimeControl::Update** | 0x0015e352–0x0015e7ca | 3 buttons |
| **UpsellScreen::CreateBuyNowRing** | 0x00164da6 | Buy now button |
| **UpsellScreen::Update** | 0x001651c8 | Upsell buttons |
| **SpeedControl::Update** | 0x00156684 | Speed control button |

### Helper Functions (static, not methods)

| Function | Address | Purpose |
|----------|---------|---------|
| DrawQuad_MenuButton | 0x00149f34 | Static wrapper for Mesh::DrawQuadUnCached |
| MakeColourFromGlobal_MenuButton | 0x00149ef4 | Static: construct Colour from global pointer |
| DeleteStackDelegate_MenuButton | 0x0014a170 | Static: StackAllocatedPointer::Delete |
| MainScreen_DeleteMenuButtons | 0x0014aee8 | Remove all MenuButtons from MainScreen |

---

## EffectImage

Screen overlay image used by power-up screen effects (freeze ice, frenzy sides).

### Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| Parse | 0x0011dda4 | Load from XML `<image>` element |
| LoadTextures | 0x0011d1e4 | Load referenced textures |
| EffectImage ctor | 0x0011ba7c | 2-param constructor |

Parsed from `poweruplist.xml` `<effect><image>` elements with attributes: texture, pos, timeStart, timeEnd, transitionMoveIn/Out, transitionTime, transition, drawOrder, scaleToScreen, anchor, pulseSpeed, pulseScale.

---

## QUADCUSTOMVERTEX (vertex format)

Used by SlashEntity blade trail, SlashEntityGhost, and SplatEntity.

### Layout (0x24 = 36 bytes per vertex)

| Offset | Type | Name |
|--------|------|------|
| +0x00 | float | x |
| +0x04 | float | y |
| +0x08 | float | z? |
| +0x0c | float | u? |
| +0x10 | float | v? |
| +0x14 | float | ? |
| +0x18 | uint | colour (packed BGRA) |
| +0x1c | float | alpha/weight |
| +0x20 | float | ? |

Confirmed 0x24 stride from SlashEntityGhost::Update loop (`iVar4 += 0x24`).

---

## See Also

- [Screens & effects functions](../functions/screens-effects.md) -- MenuButton callbacks
- [Coin entity](../entities/coin.md) -- Coin struct and functions
- [SlashEntity](../entities/slash-entity.md) -- SlashEntityGhost (blade trail ghost)
