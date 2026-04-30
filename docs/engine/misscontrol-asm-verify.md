# `MissControl` ASM-Level Fidelity Audit

Verified 2026-04-30 against `FruitNinja.exe`. Covers every method on the
class except `CreatePool` (already documented in
`docs/structs/miss-control-init.md`). Findings are catalogued per-method
with status, binary range, port file:line, and concrete diffs. Prior RE
established the GameInit step-3 flow; this audit covers the **runtime**
class internals (Update/Draw/Make*/Reset/etc.).

> **Scope of audit:** `src/hud/MissControl.{h,cpp}` end-to-end. Port stub
> uses simplified linear-fade Update and unit-quad Draw -- this audit
> shows how far that diverges from the binary.

## 0. Vtable @ `0x001e9b28`

| Idx | Address | Symbol | Port impl |
|-----|---------|--------|-----------|
| 0   | top-offset = 0 | -- | -- |
| 1   | `0x001e9b6c` | typeinfo | -- |
| 2   | `0x00151468` | `~MissControl` (D1) | `~MissControl` (default) |
| 3   | `0x001513d8` | `~MissControl` (D0, deleting) | -- |
| 4   | `0x00150fa4` | `Init()` | **MISSING** |
| 5   | `0x001513cc` | `Release()` | **MISSING** |
| 6   | `0x00150f14` | `Reset()` | **MISSING** |
| 7   | `0x0012f92c` | base `HUDControl::PostUpdate` | -- |
| 8   | `0x00150e00` | `PreDraw()` (no-op return arg) | -- |
| 9   | `0x00151f60` | `Draw(float*)` | `Draw` (port impl) |
| 10  | `0x0012f930` | base `HUDControl` slot | -- |
| 11  | `0x0012f93c` | base `HUDControl` slot | -- |
| 12  | `0x00151a60` | `Update(float)` | `Update` (port impl) |
| 13  | `0x0012fd54` | base slot (`SetToMultiplayerState` per HUDControl) | -- |
| 14  | `0x00152660` | `GetType()` returns `2` | **MISSING** |
| 15  | `0x00150e3c` | `Skip()` | **MISSING** |

User's request mapped Update to `vtable[10]` and Draw to `vtable[7]`; the
actual indices are `[12]` and `[9]` respectively. Symbol addresses are
correct.

## 1. Struct layout (size = 0x94)

Re-derived from `Init` / `MakeCritical` / `Update` / `Draw` writes.
Original `MissControl.h` comments place several fields at the wrong
offset. Authoritative layout:

| Off  | Type   | Name (binary semantics)                        | Port name              |
|------|--------|------------------------------------------------|------------------------|
| 0x00 | vptr   | vtable                                          | (inherited)            |
| 0x04 | u8     | `field_0x4 = 1` set by ctor                    | `m_bActive`            |
| 0x08 | Vec3   | `pos` (x,y,z)                                  | `pos`                  |
| 0x14 | Vec3   | `pivot`                                        | `pivot`                |
| 0x20 | Vec3   | `size` (set to (w+1, h+1, 0) then halved-and-clamped, then doubled back) | `size` (port keeps halved) |
| 0x2c | float  | rotation (degrees, used as `SinIdx(rot * 182)`) | -- (missing)           |
| 0x30 | u8     | **busy flag** -- 0 = free for `GetFree`, 1 = active | `m_bBusy`             |
| 0x32 | u8     | **pool-owned flag** (set by `CreatePool`)       | -- (missing)           |
| 0x33 | u8     | unknown (Init writes 0)                         | -- (missing)           |
| 0x34 | u32    | "configured" flag (Init writes 1)               | `m_LayerFlags = 0x200` (WRONG) |
| 0x38 | Delegate1 | `m_RemoveCallback`-style delegate (Update fires it) | (inherited from base?) |
| 0x5c | Colour | RGBA tint (`b,g,r,a` packed); Init copies a default colour from a DAT; `field_0x5f` is alpha = 0xff | -- (missing)           |
| 0x74 | SmartPtr<Texture> | bound texture (8 bytes)                | `m_Texture` (port stores raw GLuint, not SmartPtr) |
| 0x7c | u8     | `m_AnimState` (0 idle, 3 active fade)           | (port has at +0x84 in comments, wrong) |
| 0x7d | u8     | `m_bVisible` (visible-on-screen, gates jitter)  | -- (missing)           |
| 0x7e | u16    | jitter shake counter (decremented in Draw, adds rand offset) | -- (missing) |
| 0x80 | float  | `m_FadeAlpha` (init 1.81, NOT 0.808)            | `m_FadeAlpha`          |
| 0x84 | u8     | `m_bComboActive`                                | (port has at +0x7c in comments, wrong) |
| 0x85 | u8     | "use sound" flag (gates SFXPlay branch)         | -- (missing)           |
| 0x88 | i32    | `m_ComboCount`                                  | -- (missing)           |
| 0x8c | u8     | unknown flag (Init writes 1)                    | -- (missing)           |
| 0x90 | float  | `m_AlphaScale` (1.0 critical, 0.5 rare)         | `m_AlphaScale`         |

**Field offset comments in `MissControl.h` are swapped**: the header
claims `+0x7c = m_bComboActive` and `+0x84 = m_AnimState`, but binary
writes at those offsets are reversed. `+0x88 = m_AlphaScale` in the
header is also wrong (binary uses `+0x90`). These are documentation bugs,
not runtime bugs (port uses C++ field-name access, not raw offsets), but
should be corrected for future RE'ing.

## 2. `MissControl::MissControl()` -- ctor @ `0x00151068`

**Status: MINOR DIFF**. Binary range `0x00151068..0x00151164`. Port
`MissControl.cpp:53-59`.

Binary lazy-loads the shared texture set on first ctor (guarded by
`field_0x2c` static counter at GOT+DAT offset). It loads three fixed
textures into static SmartPtrs at `+0x28/+0x30/+0x34`, then a loop
`iVar3 = 1..10` builds `combo_%d.tex` for `iVar3 >= 3` (so combo_3 ..
combo_10, indices 2..9 in the SmartPtr array; indices 0,1 are the cross
glow / first two textures). After loading, increments the static
ref-count, calls `Init(this)`, then sets `field_0x30 = 0`.

Port:
- Calls `LoadContent()` separately as a free function (combo_*.tex
  textures are NOT loaded -- "deferred").
- Does not call `Init()` from the ctor; sets fields inline.
- Sets `m_LayerFlags = 0x200` -- this corresponds to binary
  `*(undefined4 *)&this->field_0x34 = 1`, which is NOT a layer-flag.
  See struct table for the correct semantics (a "configured" flag).

**Action:** Rename `m_LayerFlags` -> `m_ConfiguredFlag` (or similar) and
fix the comment. The `0x200` value is wrong; binary writes `1` here.
Combo textures (3..10) need to be loaded for the combo overlay to render.

## 3. `MissControl::Init()` -- vtable[4] @ `0x00150fa4`

**Status: MISSING**. Port has no `Init()` override at all.

Binary writes (in order):
```
field_0x84 = 0    // m_bComboActive = 0
field_0x30 = 1    // busy = 1 (overwritten to 0 by ctor afterwards!)
field_0x2c = 0.0  // rotation = 0
field_0x8c = 1    // unknown flag
field_0x34 = 1    // configured flag
field_0x7c = 0    // m_AnimState = 0
SmartPtr<Tex>(field_0x74) = static_pool[+0x28]   // default texture (cross)
field_0x80 = 0.0  // m_FadeAlpha = 0
field_0x30 = 1    // busy = 1 (still gets overwritten by ctor)
field_0x88 = 0    // m_ComboCount = 0
field_0x33 = 0
field_0x85 = 0
field_0x90 = 1.0  // m_AlphaScale = 1.0
size.xy = (tex.width/2 + 1, tex.height/2 + 1)
size.z  = 0.0
vtable[4]() called  // base init for transform
```

Port simulates the field-init parts in the ctor but is missing the
base-vtable call and the texture-derived `size` computation.

**Action:** Add `MissControl::Init()` override that mirrors the binary's
field writes and calls `HUDControl3d::Init()` afterwards. Required for
`Reset()` to work since `Init`-default state is what `Reset` restores
to.

## 4. `MissControl::Release()` -- vtable[5] @ `0x001513cc`

**Status: MISSING / OK BY ELISION**. Body is just
`SmartPtrNull(field_0x74)` -- nulls the texture SmartPtr to drop the
ref. Port doesn't store a SmartPtr (uses raw `GLuint m_Texture`), so
nothing to release. Acceptable as long as the port's `LoadContent`
SmartPtrs outlive the pool.

## 5. `MissControl::Reset()` -- vtable[6] @ `0x00150f14`

**Status: MISSING**.

Binary semantics:
```
Colour default = *DAT_00150f7c        // load 4-byte default colour (likely 0xFFFFFFFF)
field_0x5c..0x5f = default            // restore RGBA tint
field_0x7e = 0                         // clear jitter timer
field_0x5f = 0xff                      // alpha = 1.0
field_0x7d = 0                         // m_bVisible = 0
if (m_FadeAlpha > 0) {
    field_0x30 = 0                     // mark slot free
    field_0x5f = 0                     // alpha = 0
}
```

Port has no Reset; HUDControl base may call it via vtable when
preparing the pool for a new game. Pool slots that are still mid-fade
when Reset is called won't get cleared in the port.

**Action:** Add `Reset()` override that mirrors the binary. The
"if fading, mark free" branch is critical: it's how the pool gets
salvaged on game restart.

## 6. `MissControl::GetFree()` -- static @ `0x00150da4`

**Status: DIVERGES**. Port `MissControl.cpp:102-111`.

Binary semantics:
```c
int idx = curentFree;
int tries = 0;
while (pool[idx].field_0x30 != 0) {   // skip BUSY slots
    if (tries >= poolCount) break;
    idx = (idx + 1) % poolCount;
    tries++;
}
curentFree = idx;                      // <-- save the FOUND idx, not idx+1
return &pool[idx];
```

Port:
```cpp
int idx = s_NextSlot;
for (int tries = 0; tries < MISS_POOL_SIZE; ++tries) {
    if (s_Pool[idx] && s_Pool[idx]->m_bBusy == 0) break;
    idx = (idx + 1) % MISS_POOL_SIZE;
}
s_NextSlot = (idx + 1) % MISS_POOL_SIZE;   // <-- DIVERGES: advances past
return s_Pool[idx];
```

**Diff:** binary leaves `curentFree` at the returned slot's index; port
advances to the *next* slot. Effect: port skips one slot per `GetFree`
call, halving effective pool throughput when slots free up out of order.

Pool size also differs: port `MISS_POOL_SIZE = 9`; binary's pool is
allocated by `CreatePool(0xC, hud)` = **12 slots**. (See
`docs/structs/miss-control-init.md` step 3.)

**Action:**
1. Change `s_NextSlot = (idx + 1) % MISS_POOL_SIZE;` -> `s_NextSlot = idx;`
2. Bump `MISS_POOL_SIZE` from 9 to 12.

## 7. `MissControl::PreUpdate(float)` -- static @ `0x00150e04`

**Status: DIVERGES (placeholder)**.

Binary disasm:
```
s14 = 0.5
*s_numCriticals_ptr = old_value (loaded as int)
*s_numCriticals_ptr = 0
*s_dtMod_ptr = (float)old_value + 0.5
```

Port `MissControl.h:65`: `static void PreUpdate(float dt) { (void)dt; }` -- no-op.

Globals (resolved):
- `s_numCriticals @ 0x0023123c` (int) -- counter incremented per-frame in
  Update by busy MissControls; reset to 0 here.
- `s_dtMod @ 0x001f3d6c` (float) -- "scaled critical-count" used by
  Update to scale dt: `dt = dt * s_dtMod * m_AlphaScale`.

Effect of stub: combo timing in Update uses an undefined `s_dtMod`,
which means the port's combo-decay-rate scaling is broken. Port's
Update is also a stub so this isn't currently visible, but it gates the
correct fix.

**Action:** Implement PreUpdate as
```cpp
static int s_NumCriticals;     // 0x0023123c
static float s_DtMod;          // 0x001f3d6c
void MissControl::PreUpdate(float /*dt*/) {
    int n = s_NumCriticals;
    s_NumCriticals = 0;
    s_DtMod = (float)n + 0.5f;
}
```

## 8. `MissControl::Update(float dt)` -- vtable[12] @ `0x00151a60`

**Status: CRITICAL DIVERGES (port is a stub)**.

Binary's Update is ~150 instructions and runs:

1. **Visibility lazy-on** (lines 1-10): if `!m_bVisible` and
   `m_AnimState < player_count`, set jitter `field_0x7e = 0x1e`,
   alpha = 0xff, `m_bVisible = 1`.
2. **Combo separation force** (the `m_bComboActive != 0` block,
   ~50 instructions): walks the entire pool and applies a soft repulsion
   between this slot and every other busy slot whose distance < 4900
   (= `DAT_00151d58`, sqrt = 70 px). For each near neighbour, computes
   `dir = (other.pos - this.pos)`, normalises (or random-direction if
   distance == 0), and accumulates a velocity offset proportional to
   `(70 - dist) * 15.0` into `local_44/local_48`. Increments
   `s_numCriticals`. Applies the accumulated offset to `pos.xy`.
   Multiplies `dt = dt * s_dtMod * m_AlphaScale`.
3. **Already-faded short-circuit** (`fVar9 <= 0.0`): if not yet visible
   or anim-state below player-count, return; otherwise re-set jitter,
   alpha, and clear `m_bVisible`.
4. **Pause guard**: if `*(globalGameState + 2) != 0` (paused?), early
   return without decrementing fade.
5. **Fade decrement**: `pos.z = 0.0; m_FadeAlpha -= dt;`.
6. **Sound trigger**: if `m_FadeAlpha` crossed `1.66` going down (i.e.
   was >= 1.66 before, now < 1.66), AND `m_bComboActive`, AND
   `field_0x8c != 0`: build a sound name. Two paths:
    - `field_0x85 != 0` -> first try `ItemManager::PlayAlternateComboSound(m_ComboCount-3)`;
      if returns 0, fall through.
    - `field_0x85 == 0` -> use `<DAT_00151d88>` (a baked string, "miss"?).
    - Otherwise format `"<DAT_00151d84>"%d` with index based on
      `m_ComboCount` (clamped to 1..8 via the "ComboCount<4 -> 1, <10 -> -2, else 8" formula).
   Then `GameSound::SFXPlay(name, 0.25, 1.0, delegate)`.
7. **Slot release**: if `m_FadeAlpha <= 0.0`, fire
   `field_0x38(this)` (the remove delegate) and set `field_0x30 = 0`.

Port `MissControl.cpp:171-181`:
```cpp
if (m_bBusy == 0) return;
m_FadeAlpha *= FADE_DECAY;       // 0.95 per frame
if (m_FadeAlpha < FADE_CLEAR) {  // 0.02
    m_FadeAlpha = 0.0f;
    m_bBusy = 0;
    m_AnimState = 0;
}
```

The port has none of the combo separation, sound triggering, jitter,
visibility logic, or pause gate. **It also uses a multiplicative
exponential decay rather than the linear `m_FadeAlpha -= dt` of the
binary.** Comment in port acknowledges this as an approximation.

**Action:** Stub is acceptable for a placeholder, but to get correct
behavior the full state machine needs porting. Minimal correct version:

```cpp
void MissControl::Update(float dt) {
    if (!m_bBusy) return;
    if (m_FadeAlpha <= 0.0f) return;
    if (game_paused) return;
    pos.z = 0.0f;
    bool wasAboveSoundThresh = (m_FadeAlpha >= 1.66f);
    m_FadeAlpha -= dt * s_DtMod * m_AlphaScale;
    if (wasAboveSoundThresh && m_FadeAlpha < 1.66f &&
        m_bComboActive && /* field_0x8c */ true) {
        // play SFX...
    }
    if (m_FadeAlpha <= 0.0f) {
        if (m_RemoveCallback) m_RemoveCallback(this);
        m_bBusy = 0;
    }
}
```

## 9. `MissControl::Draw(float* hudScale)` -- vtable[9] @ `0x00151f60`

**Status: CRITICAL DIVERGES (port is a stub)**.

Binary computes a full transform chain:
1. **Jitter** (if `field_0x7e > 0`): adds random offset of `(rand[0..7]-4, rand[0..7]-4, 0)` to the translation. Decrements `field_0x7e`.
2. **Two paths based on `m_FadeAlpha`**:
   - `<= 0.0`: depending on `FailureEnabled() && !IsMultiplayer()`: shifts y by `pos.y * abs(camera.field_0xc) * -3.0` or `pos.y * -3.0` (a fall-off animation).
   - `> 0.0`: if `m_FadeAlpha > 1.66`, **early return** (label is in spawning phase, no draw yet); else compute pulse `s = sin((m_FadeAlpha/1.66) * 360 * 6 * 182) -> abs`, clamp to >= 0.65 in certain phases.
3. Bind texture `field_0x74` via `Mortar::Texture::Set`.
4. Build matrix: scale = field_0x20 * hudScale, rotZ if `rotation != 0` (uses `SinIdx/CosIdx` with `rot * 182`), translate = pos + (DAT_001522b8, DAT_001522bc, 0) * hudScale.
5. Tint: copy field_0x5c colour; multiply alpha by `MatrixManager.field_0x3c.field_0x20` (per-frame HUD tint scale). Clamp to [0..255].
6. Pick UV crop based on `m_bComboActive` / `m_bVisible`:
   - `m_bComboActive == 0 && m_bVisible == 0`: `(u0=0, v0=0.5, du=0.25, dv=0.75)` (half quad)
   - `m_bComboActive == 0 && m_bVisible == 1`: `(u0=0.5, v0=0, du=0.25, dv=0.75)` (other half)
   - `m_bComboActive != 0`:                   `(u0=0, v0=0,   du=1.0,  dv=1.0)` (full quad)
7. Call `Mortar::Mesh::DrawQuadUnCached(colour, u0, v0, du, dv, NULL)`.
8. `Mortar::Texture::UnSet`.

Port `MissControl.cpp:185-221`: builds a centred-unit quad, no UV crop,
no rotation, no jitter, no pulse, no per-frame HUD tint scale, no fade-out
fall animation, multiplies size by `(1.0, 1.0, 1.0)` (port stores size
as half-extents so the rendered quad is half-size).

**Diff highlights:**
- No rotation. `field_0x2c` (the rotation written by GameInit step 3 to
  the 3 visible widgets: ±5, ±10 deg) is silently dropped.
- No UV crop. The combo-active variant uses full UV; the rare/critical
  variant uses a 25%-wide vertical crop. Port renders the entire
  texture in all cases.
- Quad size is half what the binary draws (binary doubles back via
  `operator+=(this_00, this_00)`; port does not).
- Hard-coded `(uint8_t)(fade * 255)` ignores the per-frame HUD tint
  multiplier (`HUD::field_0x20`).

**Action:** Port the full transform/UV/colour pipeline. The simplest
visual fix is the rotation + UV crop + size doubling -- those alone
would make the 3 GameInit MissControls render at the right place.

## 10. `MissControl::MakeCritical(Vec3, int)` @ `0x00151764`

**Status: MINOR DIFF**. Port `MissControl.cpp:149-151` -> `PopulateOverlay`.

Differences vs binary:
- `m_FadeAlpha` init value: binary uses `DAT_001518b8 = 1.81f`, port
  uses `MISS_FADE_INIT = 0.808f`. **Port comment claims DAT = 0.808**
  but the bytes at `0x001518b8` are `14 ae e7 3f` = float `1.81`.
  **Port is wrong by 2.24x.** The Update fade decrements by
  `dt * s_dtMod * m_AlphaScale`; with init=0.808 instead of 1.81 the
  label vanishes much faster and never crosses the 1.66 sound threshold.
- `size.z`: binary writes `DAT_001518bc = 0.0`, port writes `1.0`.
- `size.xy` halving: binary writes `(w+1, h+1)`, halves to
  `(0.5*(w+1), 0.5*(h+1))`, clamps, then **doubles back** to `(w+1, h+1)`.
  Port keeps the halved values, so `size` is half what the binary
  finalises -- Draw then renders a quad half the texture size.
- `field_0x5f = 0xff` (alpha reset): port writes the alpha as part of
  `m_DrawColour.a` implicitly via `col` calculation in Draw, never sets
  the field directly. Acceptable since port's Draw doesn't read the
  alpha field separately.
- `field_0x7e = 0` (jitter clear): port doesn't model jitter.

**Action:**
1. `MISS_FADE_INIT = 1.81f` (not 0.808).
2. `size.z = 0.0f` (not 1.0).
3. After clamp, **double** `size.x` and `size.y`. Or refactor: store
   `size` as full extents and clamp using `pos +/- size*0.5`.

## 11. `MissControl::MakeRare(Vec3)` @ `0x001518d8`

**Status: MINOR DIFF**. Same diffs as MakeCritical (init constant,
size.z, halve+double size). Additionally:
- Binary picks texture from `static_pool[+0x30]` (the second loaded
  texture, NOT named `ultra_rare_plus_50.tex` here -- the actual filename
  comes from `DAT_00151194` in the ctor and is unverified by us).
- Sets `m_AlphaScale = 0.5f` *before* `Vec3_HalfScale(size)`. Port sets
  it via `PopulateOverlay`'s `alphaScale` parameter, equivalent.

Texture filename: port uses `"ultra_rare_plus_50.tex"`; whether the
binary loads the same name depends on `DAT_00151194` content, not
verified here. **Open RE gap.**

## 12. `MissControl::MakeDisappear(Vec3, int sizeMult, SmartPtr<Texture>)` @ `0x00151d94`

**Status: DIVERGES (two paths exist; port runs only one)**.

Binary has TWO branches based on `param_4` (the SmartPtr):
1. **SmartPtr is valid** (line 22+): bind it as `field_0x74`, set
   `field_0x8c = 0`, `m_bVisible = 1`, `m_AnimState = 3`,
   `m_FadeAlpha = DAT_00151f40` (separate fade init for "disappear"!),
   `m_bComboActive = 1`, `field_0x7e = 0`, size from texture, then
   `SetPlayer()`. **No screen clamp on this path.**
2. **SmartPtr is invalid** (else branch, line 49+): keep existing
   `field_0x74` texture (typically the fruit's), uses
   `pfVar9 = *DAT_00151f5c` (a Vec3 multiplier, e.g. `(0.5, 0.5, 1.0)`),
   computes `size = pfVar9 * (something)` TWICE (once before SetPlayer,
   once after), then **does** screen-clamp on `pos.x` against
   `+/- (DAT_00151f50 - size.x*0.5)` and same for y. Sets jitter
   `field_0x7e = 0x1e` if `param_3 >= 1` else `0`. `m_FadeAlpha` from
   `DAT_00151f48` (third fade init, distinct from path 1!).

Port `MissControl.cpp:157-167`:
```cpp
const SmartPtr<Mortar::Texture>& pick =
    tex.IsValid() ? tex : s_TexCross;
PopulateOverlay(this, pos, pick, /*alphaScale*/ 1.0f);
```
Always runs the "valid texture" path with `s_TexCross` as fallback,
re-using `MakeCritical/Rare`'s `PopulateOverlay`. This:
- Always screen-clamps (binary doesn't on path 1).
- Uses `MISS_FADE_INIT = 0.808` (binary path 1 uses
  `DAT_00151f40`; binary path 2 uses `DAT_00151f48` -- **both unread,
  port doesn't match either**).
- Drops the `sizeMult` parameter entirely (binary path 2 multiplies
  size by `pfVar9`, which encodes the bomb's `0x200` size hint).
- Drops the jitter timer (binary path 2 sets `field_0x7e = 0x1e` for
  `param_3 >= 1`).
- Always sets `field_0x8c = 1` via PopulateOverlay (binary path 1
  writes `field_0x8c = 0` -- "use sound" disabled for zen-bomb X overlay).

**Action:** Split MakeDisappear into the two-path form:
```cpp
void MissControl::MakeDisappear(Vec3 pos, int sizeMult,
                                 SmartPtr<Texture> tex) {
    HUDControl3d::Init(this);              // vtable[2] base
    this->pos = pos;
    field_0x5f = 0xff;
    if (tex.IsValid()) {
        field_0x8c = 0;                    // suppress sound
        field_0x74 = tex;
        m_bVisible = 1;
        m_AnimState = 3;
        m_FadeAlpha = DAT_00151f40;        // resolve constant
        field_0x7e = 0;
        m_bComboActive = 1;
        size = Vec3(tex.width+1, tex.height+1, 0.0f);
        SetPlayer();
        // NO screen clamp on this branch.
    } else {
        field_0x7e = (sizeMult >= 1) ? 0x1e : 0;
        m_FadeAlpha = DAT_00151f48;
        m_AnimState = 3;
        m_bVisible = 1;
        size = (*DAT_00151f5c) * size_in;  // need to RE pfVar9
        SetPlayer();
        size = (*DAT_00151f5c) * size;     // second multiply
        // screen-clamp pos.x/y against +/- (DAT_00151f50 - size.x*0.5)
    }
}
```
Resolve `DAT_00151f40`, `DAT_00151f44`, `DAT_00151f48`, `DAT_00151f50`,
`DAT_00151f54`, `DAT_00151f5c` before applying.

## 13. `MissControl::SetToMultiplayerState(...)` -- vtable[13]

**Status: NOT MissControl-OVERRIDDEN.** Vtable[13] is `0x0012fd54`
which is the `HUDControl` base symbol. MissControl does not override
`SetToMultiplayerState`. Port's claim that this is a vtable[11] override
is incorrect; there's nothing MissControl-specific to verify here.

## 14. `MissControl::GetType()` -- vtable[14] @ `0x00152660`

**Status: MISSING**. Binary returns `2` (a class-type tag). Port has no
override; would inherit the base default.

**Action:** Add `int GetType() const override { return 2; }`. Likely
unused at runtime by the port today but kept for parity.

## 15. `MissControl::Skip()` -- vtable[15] @ `0x00150e3c`

**Status: MISSING**.
```c
if (m_AnimState < player_count) {
    field_0x7e = 0;       // clear jitter
    field_0x5f = 0xff;    // alpha = 1.0
    field_0x7d = 1;       // m_bVisible = 1
}
```
Used to fast-forward the spawn animation when a critical/rare label
needs to appear immediately (e.g. on a tutorial trigger).

**Action:** Add `Skip()` override mirroring the binary.

## 16. Constants table

Resolved DAT values referenced above:

| Symbol           | Address | Hex value     | Float  | Used by |
|------------------|---------|---------------|--------|---------|
| Fade init (Make*) | `0x001518b8` | `0x3FE7AE14` | 1.81 | MakeCritical, MakeRare |
| Size.z init       | `0x001518bc` | `0x00000000` | 0.0  | MakeCritical, MakeRare |
| Clamp +x          | `0x001518c0` | `0x43700000` | 240.0 | Make* clamp |
| Clamp +y          | `0x001518c4` | `0x43200000` | 160.0 | Make* clamp |
| Clamp -x          | `0x001518c8` | `0xC3700000` | -240.0 | Make* clamp |
| Clamp -y          | `0x001518cc` | `0xC3200000` | -160.0 | Make* clamp |
| Sep dist sqr      | `0x00151d58` | `0x45992000` | 4900.0 | Update (sqrt = 70 px) |
| Sep target dist   | `0x00151d5c` | `0x428c0000` | 70.0   | Update separation |
| Pulse threshold   | `0x00151d64` | `0x3FD47AE1` | 1.66   | Update sound trigger |
| Draw fade thresh  | `0x00152298` | -- (above thresh) | 1.66 | Draw early-return |

Statics (file-scope):

| Symbol           | Address | Type   |
|------------------|---------|--------|
| `pool` (head ptr)  | `0x00231230` | `MissControl*` |
| `poolCount`        | `0x00231234` | `int` (= 12 from CreatePool(0xC, hud)) |
| `curentFree`       | `0x00231238` | `int` (round-robin cursor) |
| `s_numCriticals`   | `0x0023123c` | `int` (PreUpdate clears) |
| `s_dtMod`          | `0x001f3d6c` | `float` (PreUpdate writes) |

## 17. Summary of port-side actions

Ordered by user-visible impact (most-visible first):

1. **CRITICAL** `MakeCritical/Rare/Disappear`: change `MISS_FADE_INIT`
   from `0.808f` to `1.81f`. Port's labels currently fade in 1/2.24
   the time the binary uses.
2. **CRITICAL** `Draw`: implement rotation, UV crop, full-size quad,
   per-frame HUD tint multiplier. The 3 GameInit visible widgets are
   currently mis-rendered (no rotation, half-size, full UV).
3. **CRITICAL** `Update`: implement linear `m_FadeAlpha -= dt * s_dtMod * m_AlphaScale`,
   sound trigger at 1.66 crossing, and remove-delegate fire on
   completion. Pre-condition: implement `PreUpdate` to maintain
   `s_dtMod`.
4. **HIGH** `GetFree`: stop advancing `s_NextSlot` past the returned
   slot, and bump `MISS_POOL_SIZE` from 9 to 12.
5. **HIGH** `MakeDisappear`: split into two-path form to handle
   `sizeMult` and the no-texture fruit-portrait branch.
6. **MED** Add `Init`, `Reset`, `Skip`, `GetType` overrides.
7. **MED** Add struct fields for `m_bVisible`, jitter timer, RGBA
   tint, `m_ComboCount`, `field_0x32` (pool-owned), `rotation`.
8. **LOW** Fix offset comments in `MissControl.h` (`m_AnimState` and
   `m_bComboActive` are swapped).
9. **LOW** Rename `m_LayerFlags` -> `m_ConfiguredFlag`; value should
   be `1`, not `0x200`.
10. **LOW** Load `combo_3..combo_10.tex` in `LoadContent`.

## 18. Function index

| Symbol | Address |
|--------|---------|
| `MissControl::MissControl` (C2/C1) | `0x00151068` / `0x001511a0` |
| `MissControl::~MissControl` (D0/D1/D2) | `0x001513d8` / `0x00151468` / `0x001514f0` |
| `MissControl::Init` | `0x00150fa4` |
| `MissControl::Release` | `0x001513cc` |
| `MissControl::Reset` | `0x00150f14` |
| `MissControl::PreDraw` | `0x00150e00` |
| `MissControl::Draw` | `0x00151f60` |
| `MissControl::Update` | `0x00151a60` |
| `MissControl::PreUpdate` | `0x00150e04` |
| `MissControl::Skip` | `0x00150e3c` |
| `MissControl::GetType` | `0x00152660` |
| `MissControl::SetPlayer` | `0x00150dfc` |
| `MissControl::GetFree` | `0x00150da4` |
| `MissControl::CleanPool` | `0x00150e74` |
| `MissControl::CreatePool(int, HUD*)` | `0x001512d8` |
| `MissControl::MakeCombo` | `0x001515a4` |
| `MissControl::MakeCritical` | `0x00151764` |
| `MissControl::MakeRare` | `0x001518d8` |
| `MissControl::MakeDisappear` | `0x00151d94` |
| vtable | `0x001e9b28` |
