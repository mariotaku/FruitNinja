# `MissControl` — GameInit Step 3 Creation Spec

Resolved 2026-04-30 from `FruitNinja.exe` Ghidra disassembly. Step 3 of
`GameInit` (0x0016c644) creates **3 visible HUD MissControls** at scripted
positions, then calls `MissControl::CreatePool(0xC, hud)` which allocates
**12 additional pooled MissControls** and adds them to the HUD as well.
The port currently calls `MissControl::AllocatePool()` only, which gives
12 instances at `Vec3(0,0,0)` and zero of the 3 visible widgets.

## 1. Position table

| Symbol | Address | Resolution |
|--------|---------|------------|
| GOT base | `0x001EC130` | `DAT_0016c9d0 (=0x0007FADC) + 0x0016C654` |
| Table pointer reg-base | `0x001F3D7C` | `GOT + DAT_0016c9dc (=0x00007C54)` |
| First-iter `missCtrlPos` | `0x001F3DB4` | `tableBase + 0x30` |
| Actual table start | `0x001F3DAC` | first-iter `missCtrlPos[-2]` lands here |
| Stride | 16 bytes | `missCtrlPos += 4` floats per iter |
| Iterations | 3 | `do {...} while (tmp != 3);` (tmp pre-increment) |

Layout per row (16 bytes; offsets relative to row base):

| Off | Type | Name | Used as |
|-----|------|------|---------|
| 0x0 | float | posX | `(-posX * playerScale)` -> Vec3.x |
| 0x4 | float | posY | `(-posY * playerScale)` -> Vec3.y |
| 0x8 | float | rotation | written as `-rotation` to `field_0x2c` |
| 0xC | float | scaleAux | unused by the iteration; only read on row N+1 by next iter's posX path? No — only row[0..2] are read. Row[3] is dead in this loop. |

`playerScale = 1.0` is hard-coded above the loop. `Z = DAT_0016c9ac = 50.0`
(read from `0x0016c9ac`).

### Decoded table (`read_memory 0x001F3DAC, 64`)

```
0x001F3DAC: 00 00 9e 42  00 00 20 41  00 00 a0 c0  00 00 40 3f   row0: 79.0  10.0  -5.0  0.75
0x001F3DBC: 00 00 50 42  00 00 50 41  00 00 a0 40  00 00 80 3f   row1: 52.0  13.0   5.0  1.0
0x001F3DCC: 00 00 a0 41  00 00 90 41  00 00 20 41  9a 99 99 3f   row2: 20.0  18.0  10.0  1.2
0x001F3DDC: ff 00 00 00  ...                                     end (next data, unrelated)
```

### Final Vec3 positions (port ortho space, after sign-flip)

Recall the binary writes `Vec3(-posX, -posY, 50.0)` (see `_Vector3<float>` ctor
call inside the loop body). Iteration index `tmp` starts at 0, increments
post-AddControl, and is stored into `m_AnimState` BEFORE the increment.

| Iter (m_AnimState) | pos_x | pos_y | pos_z | field_0x2c (rotation) |
|--------------------|-------|-------|-------|-----------------------|
| 0                  | -79.0 | -10.0 | 50.0  | +5.0  |
| 1                  | -52.0 | -13.0 | 50.0  | -5.0  |
| 2                  | -20.0 | -18.0 | 50.0  | -10.0 |

Note the rotation is **negated** when stored: `field_0x2c = -posX'`, where
`posX' = *missCtrlPos` (the rotation column). So row0's `-5.0` becomes
`+5.0`, row1's `+5.0` becomes `-5.0`, row2's `+10.0` becomes `-10.0`.

Pivot Vec3 written to `field_0x14..0x1c` is `(0.5, 0.5, 0.0)`
(`DAT_0016c9b0 = 0.0`).

Scale Vec3 at `field_0x20..0x28` is built by `(64,64,64) * (64,64,64) * (64,64,64)`
on the stack (`DAT_0016c9b4 = 64.0`). Final scale stored is
`(64*64*64, 64*64*64, 64*64*64) = (262144, 262144, 262144)` if naive — but
`_Vector3::operator*` is component-multiply with the second operand passed
by float pointer. Likely there is an intermediate matrix-form scale here
that the existing `MissControl::Init()` recomputes; treat the scale as
**don't-care for the port** unless the existing pool's scale already works
visually. `field_0x34 = 1` is also written (unknown semantics; likely a
"customised" or "configured" flag distinguishing the 3 HUD-driven widgets
from the 12 pool entries).

## 2. Per-iteration pseudocode

```c
int tmp = 0;
float* missCtrlPos = (float*)(GOT + DAT_0016c9dc + 0x30);  // 0x001F3DB4
HUD::Release(hud);
const float playerScale = 1.0f;

do {
    MissControl* mc = new MissControl();           // operator_new(0x94)
    float posX = missCtrlPos[-2];
    float posY = missCtrlPos[-1];
    mc->field_0x30 = 1;                            // mark visible/active
    mc->pos = Vec3(-posX * playerScale,
                   -posY * playerScale,
                   50.0f);                          // DAT_0016c9ac
    mc->pivot = Vec3(0.5f, 0.5f, 0.0f);            // field_0x14..0x1c
    float rot = *missCtrlPos;                      // rotation column
    missCtrlPos += 4;                              // advance row (16 bytes)
    mc->field_0x2c = -rot;                         // negated
    // ... scale chain (Vec3(64,64,64) cubed via operator*) into field_0x20..0x28
    mc->m_AnimState = (uint8_t)tmp;                // 0, 1, 2
    tmp++;
    mc->field_0x34 = 1;                            // configured flag
    HUD::AddControl(hud, mc, /*bAddFront=*/false); // push_back
} while (tmp != 3);

MissControl::CreatePool(0xC, hud);                 // 12 more, all added to HUD with field_0x32=1
```

## 3. `MissControl::CreatePool(int n, HUD* hud)` semantics

Verified at `0x001512d8`. The pool is **separate** from the 3 visible
widgets:

1. If a previous pool exists, walks it backwards calling each dtor, then
   `operator delete[]` the backing buffer.
2. Allocates `n * 0x94 + 8` bytes; the first 8 bytes hold `[0x94, n]`
   (per-element size + count, array-new bookkeeping).
3. Calls each `MissControl::MissControl()` ctor in place (counter
   `iVar8` walks `n-1 .. 0`).
4. Stores the array head pointer + `n` into a static slot, zeroes a
   third static slot (current-active count or similar).
5. **For each pool entry**: `HUD::AddControl(hud, mc, false)` then
   `mc->field_0x32 = 1`. So all 12 pool entries are visible in the HUD's
   draw list, but tagged `field_0x32 = 1` to mark them as pool-owned.

So total MissControls in the HUD list after step 3 = **3 (visible widgets,
field_0x30=1, field_0x32=0) + 12 (pool, field_0x32=1) = 15**.

## 4. `HUD::AddControl(hud, ctrl, bAddFront)` third arg

Verified at `0x00144db0`:
- `false` -> `std::list::push_back(...)` (append, drawn last/on-top)
- `true`  -> `std::list::push_front(...)` (prepend, drawn first/below)

GameInit step 3 always passes `false`. **Not** an ownership flag.

## 5. `MissControl` ctor at `0x00151068`

Calls `HUDControl3d::HUDControl3d()` base, sets `field_0x4 = 1`, sets
vtable, lazy-loads the shared static texture set on first instance:

| GOT slot | Texture |
|----------|---------|
| `DAT_00151190` | `cross_glow.tex` (or similar — unverified by string read; see ctor RE if needed) |
| `DAT_00151194` | second texture |
| `DAT_00151198` | third texture |
| `OS_SPrintf(buf, "<fmt>", iVar3)` for `iVar3 = 3..10` | indexed digit textures |

Then `Init(this)` (private inline init) is called and `field_0x30 = 0`
is the ctor's final write — but **GameInit overwrites `field_0x30 = 1`
right after**, so the running value for the 3 visible widgets is 1.

Existing port docs already describe this in `src/hud/MissControl.h` as a
"pool busy flag" (0 = slot free). For the 3 GameInit widgets it means
"this slot is permanently in use by a HUD-driven widget, not pool-owned."

## 6. Port-side action for `src/game/GameInit.cpp`

Replace the current placeholder block:

```cpp
// step 3: ...
// TODO: real MissControl positions come from GOT+0x30 table -- using Vec3(0,0,0) placeholder.
MissControl::AllocatePool();
```

With a binary-faithful version of step 3 that:

1. Creates **3 MissControls** at the resolved positions (table below).
2. Adds them to the HUD with `bAddFront=false`.
3. Then calls the existing pool path (`MissControl::CreatePool(12, hud)`
   if exposed, or the existing `AllocatePool()` if it already encapsulates
   the count-12 + per-instance HUD::AddControl behavior).

Recommended port code shape (pseudocode — implementer to adapt to existing
MissControl API):

```cpp
// step 3: 3 visible MissControls + 12-entry pool. Source: docs/structs/miss-control-init.md
static const struct { float x, y, rot; } kMissCtrlInit[3] = {
    { -79.0f, -10.0f,  +5.0f },   // m_AnimState=0 (player 1?)
    { -52.0f, -13.0f,  -5.0f },   // m_AnimState=1
    { -20.0f, -18.0f, -10.0f },   // m_AnimState=2
};
HUD* hud = game.GetHUD();
hud->Release();
for (int i = 0; i < 3; i++) {
    MissControl* mc = new MissControl();
    mc->field_0x30   = 1;
    mc->pos          = Vec3(kMissCtrlInit[i].x, kMissCtrlInit[i].y, 50.0f);
    mc->pivot        = Vec3(0.5f, 0.5f, 0.0f);
    mc->field_0x2c   = kMissCtrlInit[i].rot;       // already pre-negated above
    mc->m_AnimState  = (uint8_t)i;
    mc->field_0x34   = 1;
    hud->AddControl(mc, /*bAddFront=*/false);
}
MissControl::CreatePool(12, hud);   // or AllocatePool() if already wired
```

## 7. Open RE gaps (intentionally not blocking)

- **`field_0x34 = 1` semantics.** Written by GameInit (the 3 visibles)
  but not by `CreatePool` (the 12 pool entries). May be the
  "configured / scripted" bit. Pool entries are presumably zero here.
  Worth checking from `MissControl::Update` to see if it gates layout
  recompute.
- **Scale chain `(64,64,64) * (64,64,64) * (64,64,64)`.** The decompile
  shows three `_Vector3::operator*` calls but only one operand is
  visible per call. The final stored `field_0x20..0x28` may simply be
  `(64,64,64)` if `MissControl::Init()` overwrites it, or may genuinely
  be a cubed scale used as a mesh transform. Defer until the existing
  port's pool MissControls render correctly with their own Init path —
  if the 3 widgets render at the same scale as the pool-default, no
  further work needed.
- **Texture filenames in MissControl ctor.** Three GOT-relative strings
  at `DAT_00151190/94/98` not resolved here; existing port comment
  references `hud_cross.tex`. Confirm by reading the actual strings if
  the port's textures look wrong.

## 8. Function / address index

| Symbol | Address |
|--------|---------|
| `GameInit` (full) | `0x0016c644` |
| GameInit step-3 loop body | `0x0016c694..0x0016c742` (approx; the `do { } while(tmp != 3)`) |
| `MissControl::MissControl()` | `0x00151068` |
| `MissControl::CreatePool(int, HUD*)` | `0x001512d8` |
| `HUD::AddControl(HUDControl*, bool)` | `0x00144db0` |
| Position table | `0x001F3DAC` (3 rows x 16 bytes) |
| `DAT_0016c9ac` (Z = 50.0) | `0x0016c9ac` |
| `DAT_0016c9b0` (pivot Z = 0.0) | `0x0016c9b0` |
| `DAT_0016c9b4` (scale = 64.0) | `0x0016c9b4` |
