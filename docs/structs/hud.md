# HUD & UI Structs

## Class Hierarchy

```
HUDControl (base, 0x60 bytes, vtable 15 entries)
 └─ HUDControl3d (0x7C bytes)
     ├─ MenuButton (0x15C bytes) — interactive button with optional 3D fruit entity
     │    (leaf class — no subclasses)
     │    MenuButtonAddOn — plain data struct attached via AddPeice(), NOT a subclass
     │
     ├─ CheckBox (0x88+ bytes) — toggle control, layer 0x80
     │
     ├─ GenericHUDControl (0x1C8+ bytes) — base for animated screen controls
     │    └─ Has TranisitionInfo×4 + PulseInfo×4
     │
     ├─ MissControl — combo text display (pool of 9)
     │
     ├── Screen classes (all extend HUDControl3d directly):
     │    ├─ MainScreen (0x120 bytes) — main menu, 25-state machine
     │    ├─ GameOverScreen (0x13C bytes)
     │    ├─ GameModeScreen
     │    ├─ PauseScreen (0xD8 bytes)
     │    ├─ LeaderboardScreen
     │    ├─ PowerUpShop
     │    ├─ ShopScreen
     │    ├─ UpsellScreen
     │    ├─ AboutScreen
     │    ├─ DojoScreen
     │    ├─ BonusScreen
     │    ├─ FruitFactControl
     │    ├─ ComboControl
     │    ├─ CoinCounter (0xD4 bytes)
     │    ├─ SpeedControl
     │    └─ BonusAwardHud
     │
     └── Entity-related (also HUDControl3d):
          └─ SlashEntity — blade trail (16 instances)

ScreenButton (standalone struct, NOT HUDControl subclass)
  — Has Delegate3<bool, MenuButton*, float, ScreenButton&>
  — Works alongside MenuButton but is a separate type

DialogButton (Mortar::Dialog inner class, NOT related to MenuButton)
```

### Key relationships

- **MenuButton** is a leaf class with no subclasses. `MenuButtonAddOn` is a plain data struct (texture + pos + size, ~0x20 bytes) added via `AddPeice()`.
- **ScreenButton** references MenuButton via a delegate but is NOT in the HUDControl hierarchy.
- All screen classes (MainScreen, GameOverScreen, etc.) are **siblings** of MenuButton — they all extend HUDControl3d directly.
- The `Delegate1<void, HUDControl*>::Callee<T>` template instantiations confirm which classes participate in the HUD callback system: MenuButton, MainScreen, GameOverScreen, GameModeScreen, LeaderboardScreen, PauseScreen, PowerUpShop, ScreenButton, ShopScreen, SlashEntity, UpsellScreen.

---

## HUD & UI

### HUD (size ~0x20)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | list\<HUDControl*\> | controls | std::list = 8 bytes on this ABI |
| +0x08 | float[6] | scales | All init = 1.0f |

#### HUD Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| HUD::HUD | 0x144bc0 | 8 | Init empty list + 6 scales = 1.0 |
| HUD::AddControl | 0x105b40 (PLT) | — | `controls.push_back(ctrl)` (or push_front if bool=true) |
| HUD::RemoveControl | 0x144c40 | 6 | Fire removal callback (+0x38), list.remove(ctrl) |
| HUD::Update | 0x144d40 | ~40 | Iterate: Update active, erase pendingRemoval (callback + optional dtor) |
| HUD::Draw | 0x144a90 | ~30 | Iterate: filter active + layerMask, PreDraw then Draw |
| HUD::BeginDraw | 0x144b28 | ~10 | Iterate: call BeginDraw on active |
| HUD::Release | 0x144c5c | ~20 | Destroy all controls, clear list |
| HUD::OnPause | 0x144c00 | ~15 | Iterate: call OnPause, special-case ScrollingMenu |
| HUD::ResetControls | 0x144ba0 | ~8 | Iterate: call Reset |
| HUD::Save | 0x144a40 | ~8 | Iterate: call Save |
| HUD::SetToMultiplayerState | 0x144e00 | ~20 | Remove non-multiplayer controls |

**HUD::Update lifecycle:**
```
MissControl::PreUpdate(dt)
for each control:
    if active → control->Update(dt)           // vtable +0x28
    if pendingRemoval:
        fire m_RemoveCallback(control)         // delegate at +0x38
        if m_bNoDestructor == 0 → dtor(control)
        list.erase(it)
```

**HUD::Draw pipeline:**
```
for each control:
    if active AND (layerMask & control->field_0x34):
        control->PreDraw(scales)               // vtable +0x20
        control->Draw(scales, layerMask)       // vtable +0x24
```

**HUD::AddControl (for porting):**
```cpp
void HUD::AddControl(HUDControl* ctrl, bool pushFront) {
    if (pushFront) controls.push_front(ctrl);
    else controls.push_back(ctrl);  // always false in observed calls
}
```

### HUDControl (base class, size = 0x60)

Verified from decompilation of constructors at 0x144104 and 0x1441c0.

| Offset | Type | Name | Default | Notes |
|--------|------|------|---------|-------|
| +0x00 | HUDControlFns* | vtable | (set by ctor) | Virtual methods |
| +0x04 | int | field_0x04 | 0 | |
| +0x08 | Vec3 | pos | (0,0,0) | From CopyGlobalVec3 |
| +0x14 | Vec3 | pivot | (0,0,0) | From CopyGlobalVec3 (same call covers +0x08..+0x1f) |
| +0x20 | Vec3 | size | from global | Half-extents |
| +0x2c | float | m_Timer | 0.0 | Rotation angle / animation state |
| +0x30 | byte | m_bActive | 1 | Non-zero = active |
| +0x31 | byte | field_0x31 | 0 | |
| +0x32 | byte | m_bNoDestructor | 0 | If set, HUD won't call dtor on removal |
| +0x33 | byte | m_bPendingRemoval | 0 | Set → remove next HUD::Update |
| +0x34 | int | m_LayerFlags | 1 | Bit mask for layered drawing |
| +0x38 | Delegate1\<void,HUDControl*\> | m_RemoveCallback | (delegate) | Called before removal (24 bytes) |
| +0x50 | | (delegate padding) | | |
| +0x5c | Colour | m_DrawColour | (from global, likely white) | Packed BGRA tint colour |

**Vtable layout** (verified from MainScreen vtable at 0x1E9A50):

| VTable Offset | Method | Notes |
|---------------|--------|-------|
| +0x00 | ~dtor (deleting) | |
| +0x04 | ~dtor | |
| +0x08 | Init() | |
| +0x0c | Release() | cleanup resources |
| +0x10 | Reset() | |
| +0x14 | BeginDraw(float dt) | |
| +0x18 | PreDraw(float* hudScale) | called by PreDrawOrder |
| +0x1c | **Draw(float* hudScale)** | actual rendering |
| +0x20 | PreDrawOrder(float*,int) | wrapper → calls vtable+0x18 |
| +0x24 | DrawOrder(float*,int) | wrapper → calls vtable+0x1c |
| +0x28 | **Update(float dt)** | tick logic |
| +0x2c | SetToMultiplayerState() | |
| +0x30 | GetType() | returns int |
| +0x34 | Skip() | |
| +0x38 | Save() | |

**Draw dispatch**: HUD::Draw calls `PreDrawOrder` (+0x20) then `DrawOrder` (+0x24). These are thin wrappers that dispatch to the actual `PreDraw` (+0x18) and `Draw` (+0x1c). HUD::Update calls `Update` (+0x28).
| +0x28 | Update(float dt) — second update? |

### HUDControl3d : HUDControl (size = 0x7C)

Verified from decompilation of constructors at 0x1443f4/0x144434, and Draw at 0x14428c.

| Offset | Type | Name | Default | Notes |
|--------|------|------|---------|-------|
| +0x00..+0x5f | HUDControl | super | | Base class (0x60 bytes) |
| +0x60 | SmartPtr\<Texture\> | m_PauseTitleTex | NULL (zeroed) | Main display texture. NULL = don't draw |
| +0x64 | float | m_UVLeft | | UV rect left |
| +0x68 | float | m_UVTop | | UV rect top |
| +0x6c | float | m_UVRight | | UV rect right |
| +0x70 | float | m_UVBottom | | UV rect bottom |
| +0x74 | SmartPtr\<Texture\> | field_0x74 | | Secondary texture (used by screens) |
| +0x78 | int | field_0x78 | 0 (zeroed) | |

**Constructor** (0x1443f4/0x144434):
```c
HUDControl3d() {
    HUDControl::HUDControl(this);
    this->vtable = HUDControl3d_vtable + 8;
    SmartPtr::SetNull(&this->m_PauseTitleTex);   // +0x60 = NULL
    SmartPtr::SetNull(&this->field_0x78);         // +0x78 = 0
    this->super.m_Timer = 0.0f;                   // DAT_00144468 = 0.0
}
```

#### HUDControl3d::Draw (0x14428c, 57 lines) — fully verified

```c
void HUDControl3d::Draw(float* hudScaleParam) {
    if (!SmartPtr::IsValid(m_PauseTitleTex) || m_Alpha == 0) return;

    Texture::Set(m_PauseTitleTex);
    MatrixStack::Reset(matrixMgr->stack);          // at matrixMgr + 0x1094

    Matrix44 mat = Scale44(this->size);            // from HUDControl +0x20

    if (m_Timer != 0.0) {
        // Rotation: SinIdx/CosIdx with speed = 182.0 (DAT_001443dc)
        float sinA = SinIdx((ushort)(int)(m_Timer * 182.0f));
        float cosA = CosIdx((ushort)(int)(182.0f * m_Timer));
        RotZ44(&mat, sinA, cosA);
    }

    // Position offset: Vec3(480, 320, 0) * hudScaleParam + this->pos
    Vec3 offset(HUD_SCREEN_WIDTH, HUD_SCREEN_HEIGHT, 0.0f);  // (480, 320, 0)
    Vec3 scaled = hudScaleParam * offset;                      // component multiply
    Vec3 finalPos = scaled + this->pos;
    GlobalTranslate44(&mat, finalPos);

    matrixMgr->stack.SetCurrentMatrix(mat);
    matrixMgr->UploadCurrentMatrices(true);

    Colour tint = TintColour(m_DrawColour);
    // Alpha applied via tint
    DrawQuadUnCached(tint, m_UVLeft, m_UVRight, m_UVTop, m_UVBottom);

    Texture::UnSet(m_PauseTitleTex);
}
```

**Key detail**: The `hudScaleParam` is a Vec3 loaded from a global in HUD::Draw. At runtime this is **(1.0, 1.0, 1.0)** (verified via read_memory at 0x1BB9A0). So the offset becomes `(480, 320, 0) * (1,1,1) + pos`. This means **control positions are in a centered coordinate system where (0,0) maps to screen position (480, 320)** — i.e., adding 480 to X and 320 to Y shifts from the original centered coords to the actual draw position.

**HUD::Draw pipeline** (0x144a90, verified):
```c
void HUD::Draw(int layerMask) {
    Vec3 globalPos = *GOT_HUD_POS;  // = (1.0, 1.0, 1.0)
    for (control in controls) {
        if (control->m_bActive && (layerMask & control->m_LayerFlags)) {
            if (control->m_PauseTitleTex == NULL)
                control->PreDraw(&globalPos, layerMask);   // vtable+0x20
            else
                control->PreDraw(&this->scale);             // use HUD's own scale
            control->Draw(pfVar2, layerMask);               // vtable+0x24
        }
    }
}
```

### GenericHUDControl : HUDControl3d (BaseScreen)

Discovered at 0x143828. This is the shared base for all game screens (MainScreen, DojoScreen, etc.).

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x7b | HUDControl3d | super | |
| +0x7c | float | m_field7c | |
| +0x80 | TranisitionInfo | m_TransIn | Transition animation in |
| +0x98 | TranisitionInfo | m_TransOut | Transition animation out |
| +0xb0 | TranisitionInfo | m_Trans3 | |
| +0xc8 | TranisitionInfo | m_Trans4 | |
| +0xe0 | PulseInfo | m_Pulse1 | Pulse animation |
| +0x108 | PulseInfo | m_Pulse2 | |
| +0x130 | PulseInfo | m_Pulse3 | |
| +0x158 | PulseInfo | m_Pulse4 | |

---

### MenuButton : HUDControl3d (size = 0x15C)

<!-- Analysed: 2026-04-25T17:00 -->

#### MenuButton timer behaviour

`m_Timer` is the `HUDControl` field at +0x2c. It drives the rotation of the back-icon ring in `HUDControl3d::Draw` via `RotZ44(SinIdx(m_Timer * 182.0), CosIdx(m_Timer * 182.0))` when `m_Timer != 0.0`.

**Init** (`MenuButton::Init @ 0x0014ee40`):

The binary explicitly writes:
```c
this->base.super.m_Timer = 0.0f;   // DAT_0014ee68 = 0.0 (MENUBUTTON_INIT_ZERO)
this->m_bHighlighted     = 1;      // byte at MenuButton +0x131
```

Both writes happen unconditionally for every MenuButton::Init call, regardless of fruit type or position.

`m_RotationSpeed` at +0xF4 is set to `RandFloat_PowerUpShop(4.0) + 8.0` (range 8..12, DAT_0014f17c/0x0014f180) if the bomb entity creation succeeds, or left 0.0 if it fails. Sign: 50% chance negative (random direction).

**Update** (`MenuButton::Update @ 0x0014e614`):

```c
if (m_FruitType >= 0 && dt > 0.0f) {
    m_Timer += dt * m_RotationSpeed;       // unconditional tick
    if (m_Timer < 0.0f) m_Timer += 360.0f; // DAT_0014e974 = 360.0
}
```

The tick is **unconditional** — no hover gate, no `m_bHighlighted` gate. `m_bHighlighted` gates only a size-pulse animation (touch feedback), not the rotation timer.

**Port defect** (`src/hud/MenuButton.cpp`):
The port's `MenuButton::Init` is missing both:
1. `m_Timer = 0.0f;` — without this, m_Timer may start non-zero from uninitialized memory
2. `m_bHighlighted = 1;` — without this, the highlighted state is wrong from the start

Both screens (DojoScreen, ShopScreen) use `fruitType = FruitInfo_GetCount()` (>= 0, the bomb type) for their back button, so both should tick m_Timer each frame and rotate. Any "no rotation" observation in the port is caused by the missing Init writes, not a per-screen difference.

---

### MissControl : HUDControl3d : HUDControl (combo text display)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x7c | byte | m_AnimState | 0=idle, 3=animating |
| +0x7d | byte | m_bVisible | = 1 after MakeCombo |
| +0x80 | float | field5_0x80 | Fade alpha scale |
| +0x84 | byte | m_bActive | = 1 = combo display active |
| +0x85 | byte | m_bFlag | = 1 in MakeCombo |
| +0x88 | int | m_ComboCount | Number of fruits in combo |
| +0x8c | byte | m_bFlag8c | = 1 in Init |
| +0x90 | float | field15_0x90 | = 1.0f |

Pool: up to 9 combo text sprites (digit textures 1..9). `GetFree` (0x00150da4) scans pool for inactive instance.

---

### TutorialControl : HUDControl3d (size = 0xA0 bytes)

<!-- Analysed: 2026-04-25T10:30 -->

Draws a "swipe here" animated arrow during first-play tutorial. Only visible
when `CanShowTute()` returns true (transition running, or slow-motion active).
2.75-second animation: fade-in (0..0.35 s), bounce (0.35..0.60 s), hold
(0.60..2.25 s), fade-out (2.25..2.75 s). Inactive sentinel = -10.0.

#### Binary References

| Function | Address | Notes |
|----------|---------|-------|
| TutorialControl::TutorialControl | 0x001636f8 | ctor |
| TutorialControl::Init | 0x00162e38 | sets LayerFlags=8, calls Reset |
| TutorialControl::Release | 0x00162e48 | no-op |
| TutorialControl::Reset | 0x00162e4c | m_AnimTimer = -10.0 |
| TutorialControl::ResetTutePos(MenuButton*) | 0x00162f04 | copy pos, compute halfWidth/flip |
| TutorialControl::ResetTutePos(Vec3) | 0x00162f84 | set pos directly, reset timer |
| TutorialControl::CanShowTute | 0x00162fb8 | returns bool |
| TutorialControl::Update | 0x00163014 | animation tick |
| TutorialControl::Draw | 0x00163360 | 4-quad trail + arrow draw |
| TutorialControl::ButtonPressedAtPos | 0x00162e58 | advance timer on press |
| ButtonPressedAtPos (PLT thunk) | 0x00105024 | |

#### Struct Layout

Ghidra's declared struct is 148 (0x94) bytes — it is missing the tail fields.
True size verified from all function accesses is **0xA0** (160 bytes).

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x7B | HUDControl3d | super | base (0x7C bytes) |
| +0x7C | float | m_AnimTimer | -10.0=inactive sentinel, 0..2.75=animating |
| +0x80 | Vec3 | m_DrawPos | Computed each Update; off-screen = (-1000,-1000,-1000) |
| +0x8C | SmartPtr\<Texture\> | m_PressTex | press_indicate.tex (ARROW graphic — used for trail quads!) |
| +0x90 | Colour | m_Colour | RGBA; alpha driven by animation phase |
| +0x94 | int | m_bHidden | 0=visible(UV frame 0), 1=hidden(UV frame 1) — NOT a draw guard |
| +0x98 | float | m_HalfWidth | Half-width of target button; set by ResetTutePos |
| +0x9C | bool | m_bFlipX | true if button is right of center (arrow flips) |

**Texture assignment (constructor @ 0x001636f8):**
- `swipe_fruit_begin.tex` → `HUDControl3d::field_0x74` (= `super.m_SecondaryTex` at +0x74)
- `press_indicate.tex` → `TutorialControl::m_PressTex` at +0x8C

**In Draw, the texture usage is the OPPOSITE of the field names:**
- Trail-quad loop: `Texture::Set(m_PressTex)` (+0x8C) — uses `press_indicate.tex`
- Arrow block: `Texture::Set(super.m_SecondaryTex)` (+0x74) — uses `swipe_fruit_begin.tex`

The `press_indicate.tex` atlas has the full-width arrow split into two UV halves.
`m_bHidden` selects the UV frame: 0 → UV[0.0, 0.5]; 1 → UV[0.5, 1.0].
`m_bHidden` is **not** used as a visibility gate — the port's `if (m_bHidden) return;` is wrong.

#### ButtonPressedAtPos @ 0x00162e58

**Signature:** `void TutorialControl::ButtonPressedAtPos(TutorialControl* this, MenuButton* btn)`

**Behaviour:** Advances an already-running or nearly-complete animation.
Guard: only acts when `m_AnimTimer < 0.0` (animation is inactive/reset).
If `btn != nullptr`: copies button pos → `this->pos`, computes `halfWidth` from
`btn->field_0x124 - btn->field_0x14c * 2.0 - 10.0`, caps at 256.0 (DAT_00162efc),
then sets `m_bFlipX` from `pos.x > 0.0` (XOR'd with `btn->field_0x120` flag).
Always (btn or not): `m_AnimTimer += 9.5` — shifts the -10.0 sentinel to -0.5,
so the next Update sees timer ≈ -0.5 and the animation starts in ~0.5 s.
If `m_AnimTimer > 0` after the add: clamps to 0.0 (DAT_00162f00 = 0.0).

**Callers (via PLT @ 0x00105024):**

| Caller | Address | Context |
|--------|---------|---------|
| ShopScreen::ClickedOnShopItem | 0x0015d4e4 | item unlocked + equip button present |
| MenuButton::TouchReleased | 0x0014e5fe | button touched but not "highlighted" path |
| UpsellScreen::Update | 0x001650da | buy ring created — called twice in a row |
| UpsellScreen::Update | 0x001650e4 | (second call, same ring creation block) |

Note: all callers retrieve TutorialControl via `GameTaskState + 0x168`.

#### Draw Trail Block @ 0x00163488

**Phase gate:** `m_AnimTimer > PHASE_BOUNCE (0.60)` AND `< 2.25` (PHASE_HOLD_END).

**Loop:** 4 iterations (`iVar3 = 0..3`).

**Per-quad algorithm:**
```c
float timer  = m_AnimTimer;
int   rem    = (int)(timer * 2000.0f) % 1000;   // __aeabi_idivmod
float frac   = (float)quad_index + (float)rem / 1000.0f;   // 0..4 range
// Quartic alpha base: 255 + (frac - 3.0) * (-255.0)  =  255*(4 - frac)
float alpha_base = 255.0f + (frac - 3.0f) * (-255.0f);
alpha_base = clamp(alpha_base, 0.0f, 255.0f);   // [0, 255]

// Fade-in ramp: during 0.60 < timer < 0.85
if (timer >= 0.85f) {  // ARM idiom: fVar7 < 0.85 → fires when timer >= 0.85
    // no ramp: alpha = alpha_base
} else if (timer > 2.0f) {
    // fade-out tail: alpha = alpha_base + (timer - 2.0) * (-4.0) * alpha_base
    //              = alpha_base * (1 - 4*(timer-2.0))
} else {
    // fade-in: alpha = (float)alpha_base * (timer - 0.60) * 4.0
}
alpha = clamp(alpha, 0, 255);

// Scale: trail quad size = m_HalfWidth (via GOT-dereferenced pointer)
// Translate: m_DrawPos (this + 0x80)
// Colour: (255, 255, 255, alpha)
// UV: (0.0, 1.0)  — full width
DrawQuadSized(0.0f, 1.0f, Colour(255, 255, 255, alpha));
```

**Key constants (all from 0x001635ac block):**

| Address | Value | Role |
|---------|-------|------|
| DAT_001635ac | 0.60 | trail phase start (PHASE_BOUNCE) |
| DAT_001635b0 | 2000.0 | `timer * 2000` before mod-1000 |
| DAT_001635b4 | 255.0 | alpha ceiling |
| DAT_001635b8 | 1000.0 | divisor for fractional part |
| DAT_001635bc | -255.0 | quartic slope: `255 + (frac-3)*(-255)` |
| DAT_001635c0 | 0.85 | fade-in/hold threshold |
| DAT_001635c4 | 0.0 | UV left (trail DrawQuadSized arg1) |
| DAT_001635c8 | 96.0 | ARROW_SCALE (confirmed) |
| 0x3f800000 | 1.0 | UV right (trail DrawQuadSized arg2) |
| DAT_00162efc | 256.0 | HALFWIDTH_CAP (ButtonPressedAtPos) |
| DAT_00162f80 | 256.0 | HALFWIDTH_CAP (ResetTutePos) |
| DAT_00162f00 | 0.0 | clamp-to-zero in ButtonPressedAtPos |

**Note on `local_2c`:** The decompiler emits `local_2c = (2*frac)^2 = 4*frac^2`
in the loop body but this value is never passed to any subsequent call. It is
dead code in the compiled output, likely an inlining artifact.

#### Update — Phase Logic vs Port

The binary Update (0x00163014) constants verified match the port exactly:
- Off-screen sentinel: (-1000.0, -1000.0, -1000.0) ✓
- scaleStart: (-0.5, -0.075, 0.0) ✓
- scaleEnd: (1.0, 0.15, 0.0) ✓
- PHASE_FADEIN: 0.35 ✓  PHASE_BOUNCE: 0.60 ✓  PHASE_HOLD_END: 2.25 ✓
- PHASE_FADEOUT: 2.75 ✓  BOUNCE_OFFSET: 20.0 ✓

**Drift found in Update structure:**
- Binary `else if (fVar6 >= 2.75)` branch (past fadeout end): sets `m_DrawPos.y += 20.0`
  and resets timer to -10.0 (0xC1200000). The port sets `m_AnimTimer = ANIM_INACTIVE`
  which is correct (-10.0 = 0xC1200000 ✓), but the **branch condition is wrong** in the port.
  Binary ARM idiom: `if (-1 < (int)((uint)(fVar6 < 2.75) << 0x1f))` fires when `timer >= 2.75`.
  Port code: `else if (m_AnimTimer < PHASE_FADEOUT)` is the fade-out branch, and the
  `else` (past 2.75) is `if (m_AnimTimer >= 2.75)` — that part is correct.

**Drift found in Draw — m_bHidden semantics:**
- Port: `if (m_bHidden) return;` — treats as draw skip flag
- Binary: `m_bHidden` at +0x94 is the UV frame selector for `press_indicate.tex`.
  UV0 = `m_bHidden * 0.5`, UV1 = `m_bHidden * 0.5 + 0.5`.
  There is **no early-out** on m_bHidden in the binary Draw. The only guard is
  `if (m_AnimTimer <= 0.0) return early`.
  The port silently suppresses the arrow whenever `m_bHidden=1` (which is the
  default set every Update frame before the phase checks clear it to 0).

**Drift found in Draw — texture assignment:**
- Port `m_PrimaryTex` (named "press_indicate") draws the arrow; `m_SecondaryTex` (named "swipe_fruit_begin") is used for trail quads.
- Binary uses them the opposite way (see Struct Layout above). The port labels are correct for the *texture content* but the *field usage* in Draw is inverted: `field_0x74` (swipe_fruit_begin) is the arrow, `field_0x8C` (press_indicate) is the trail.

**Drift found in Draw — trail quads unimplemented:**
- Port has a TODO comment block; the 4-quad trail loop body is empty.

---

## See Also

- [Rendering functions](../engine/rendering-functions.md) -- HUDControl3d::Draw
- [SlashEntity](../entities/slash-entity.md) -- MissControl combo display
