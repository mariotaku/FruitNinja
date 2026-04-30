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

### TimeControl : HUDControl3d (size = 0x108 = 264 bytes)

<!-- Analysed: 2026-04-30T12:00 -->

Timer display for Arcade and Zen modes. Counts down from initial value, triggers GameOver when time runs out (m_TimeRemaining < 0.5). Source file: `TimeControl.cpp`. Mangled: `_ZTV11TimeControl` (vtable), `_ZTI11TimeControl` (typeinfo).

#### Constructor
**Address:** 0x001622e8 (PLT thunk at 0x000f765c)

Signature: `TimeControl::TimeControl(TimeControl* this)` — **no parameters**

**Behavior:**
```c
TimeControl* TimeControl::TimeControl(TimeControl* this) {
    HUDControl3d::HUDControl3d(&this->base);
    this->base.super.vtable = TimeControl_vtable + 8;           // GOT[0x7ad0] -> 0x001ea158
    SmartPtr<Texture>::SetNull(&this->base.m_SecondaryTex);     // +0x74
    this->m_CountdownStart = -1.0f;                             // +0xC0  (sentinel "uninitialized")
    Vec3 size(0.0f, 18.0f, 0.0f);
    this->base.super.size = size;
    Vec3 pos((480.0 - size.x)*0.5f - 5.0f, (320.0 + size.y)*0.5f - 5.0f, 0.0f);  // (235.0, 169.0, 0.0)
    this->m_TextBuffer[0] = '\0';                               // +0xC8 (guards Draw)
    this->base.super.m_bNoDestructor = 0;
    this->base.super.pos = pos;
    Reset();
    return this;
}
```

DAT pool (ctor constants):
- DAT_001622d8 = 320.0 (screen height)
- DAT_001622dc = 480.0 (screen width)

#### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x7B | HUDControl3d | super | base 0x7C bytes |
| +0x7C | float | m_TimeRemaining | live countdown value (seconds) |
| +0x80 | char[64] | m_TextBuffer | OS_SPrintf("%i:%02i", min, sec) output buffer |
| +0xC0 | float | m_CountdownStart | initial seconds; -1.0 sentinel = "no countdown configured" |
| +0xC4 | float | m_TickFrame | timer for 0..5.5 animation phase |
| +0xC8 | char[64] | m_PowerupOverlay | "+%i" overlay text; [0]==0 means hide |

**Size:** 0x108 = 264 bytes. Storage: `g_GameData + 0x180`.

#### Vtable (15 entries)

| Idx | Offset | Address | Method | Notes |
|-----|--------|---------|--------|-------|
| 0 | +0x00 | 0x00162400 | ~TimeControl (deleting) | |
| 1 | +0x04 | 0x001623c0 | ~TimeControl | |
| 2 | +0x08 | 0x001620e4 | Init | calls vtable[0x10] = Reset |
| 3 | +0x0c | 0x001623b4 | Release | SmartPtr<Texture>::SetNull(+0x74) |
| 4 | +0x10 | 0x00162168 | Reset | see below |
| 5 | +0x14 | 0x0012f92c | BeginDraw | inherited HUDControl3d (no-op) |
| 6 | +0x18 | 0x001620f8 | PreDraw | returns param |
| 7 | +0x1c | 0x001628d8 | **Draw** | text + optional UI tick mark |
| 8 | +0x20 | 0x0012f930 | PreDrawOrder | inherited (dispatches to PreDraw) |
| 9 | +0x24 | 0x0012f93c | DrawOrder | inherited (dispatches to Draw) |
| 10 | +0x28 | 0x001624a4 | **Update** | timer tick; GameOver trigger |
| 11 | +0x2c | 0x00162128 | SetToMultiplayerState | calls Reset |
| 12 | +0x30 | 0x00162e34 | GetType | returns 4 |
| 13 | +0x34 | 0x001620fc | Skip | restore from save: `+0x7c = FruitSaveData[0x10C]`, `+0xc4 = 0` |
| 14 | +0x38 | 0x0012f950 | Save | inherited (no-op) |

#### Methods

**CountDown** (0x001620f0)
```c
void TimeControl::CountDown(float startSeconds) {
    this->m_CountdownStart = startSeconds;
}
```

**GetCountDown** (0x00162134)
```c
float TimeControl::GetCountDown() const {
    if (g_GameData.gameMode != 2 /*Arcade*/ && !IsMultiplayer())
        return 60.9f;
    return this->m_CountdownStart;
}
```

**AddTime** (0x001204f0)
```c
void TimeControl::AddTime(float delta) {
    this->m_TimeRemaining += delta;
}
```

**Reset** (0x00162168)
```c
void TimeControl::Reset() {
    this->m_PowerupOverlay[0] = '\0';
    float startSecs = this->m_CountdownStart;
    if (startSecs < 0.0f) startSecs = 0.0f;
    this->m_TimeRemaining = startSecs;
    if (g_GameData.gameMode == 2 /*Arcade*/ || IsMultiplayer()) {
        this->m_TimeRemaining = 60.9f;
        FruitSaveData* sd = g_GameData.pSaveData;
        if (sd[0x10C] == 0.0 && g_GameData.cameraTransition < 0.0f)
            sd[0x10C] = 60.9f;
    }
    this->m_TickFrame = 0.0f;
    this->base.m_DrawColour = white;
}
```

**Update** (0x001624a4) — **GameOver trigger**

When `m_TimeRemaining < 0.5f`, calls `GameOver(-1, -1.0f, -1)`.

**Behavior:**
- Ticks `m_TickFrame` (cycles 0..5.5, resets on wrap)
- Visual flash at 8/4/2 Hz as time runs low (red tint when m_TimeRemaining in ranges: 0..10s, 0..5s, 0..2s)
- Plays Tick/Tock SFX for time in range 0..11s (calls once per phase)
- Writes `m_TimeRemaining` to `FruitSaveData[0x10C]` for save persistence
- **GameOver trigger:** if `m_TimeRemaining < 0.5f` and `pauseFlag == 0`, calls `GameOver(-1, -1.0f, -1)`

**Draw** (0x001628d8)

Uses `g_GameData.pFontNumbers` (at g_GameData+0x58, loaded from `fonts/fruit_ninja_numbers.fnt`).
Formats text via `OS_SPrintf("%i:%02i", min, sec)` into `m_TextBuffer`.
Tick-tock visual indicator (`m_SecondaryTex`) is dead code — never assigned a texture.

#### Countdown Values

- **GameInit** calls `CountDown(90.9)` — initial value for Zen mode
- **Arcade mode:** forced via `Reset()` override to 60.9s
- **Zen mode:** 90.9s (from GameInit call)
- **Other modes:** hidden via `m_LayerFlags=0`

#### DAT Pool (Constants)

| Address | Value | Use |
|---------|-------|-----|
| DAT_001621e8 | 0.0 | reset sentinel |
| DAT_001621ec | 60.9 | Arcade/MP starting time |
| DAT_001622d4 | 0.0 | ctor pos.z |
| DAT_001622d8 | 320.0 | ctor screen height |
| DAT_001622dc | 480.0 | ctor screen width |
| DAT_0016215c | 60.9 | GetCountDown fallback |
| DAT_0016c9cc | 90.9 | GameInit-supplied countdown (Zen) |
| DAT_001627a0 | 0.0 | timer reset on game over |
| DAT_00162b04 | -0.6 | text x-multiplier of size.x |
| DAT_00162b08 | 0.0 | DrawString z arg |
| DAT_00162b0c | 32.0 | powerup overlay y-offset |
| DAT_001628c4 | 60.0 | seconds-per-minute divisor |
| DAT_001628c8 | 64.0 | MP X anchor multiplier |
| DAT_001628cc | 320.0 | layout screen-height |
| GOT-rel "+%i" | 0x001BC298 | powerup overlay format |
| GOT-rel "time-up" | 0x001B972B | time expired SFX name |
| GOT-rel "Time-tock" | 0x001BC29C | SFX ID (lo phase) |
| GOT-rel "Time-tick" | 0x001BC2A6 | SFX ID (hi phase) |
| GOT-rel "%i:%02i" | 0x001BC2B0 | printf format string |

---

### CoinCounter : HUDControl3d (size = 0xD4 = 212 bytes)

<!-- Analysed: 2026-04-30T16:00 -->

Coin-count display HUD. **Vestigial / dead code in the shipped binary** — Init is a true
no-op, no texture is ever loaded into the icon slot, the text buffer is never written,
and `m_CoinCount` is never modified after the ctor zero. The Draw function is fully
wired but renders nothing visible at runtime because every input it relies on is
either null or zero. Storage: `Game+0x178`. Source file: `CoinCounter.cpp`.

Mangled symbols: `_ZTV11CoinCounter` (vtable), `_ZTI11CoinCounter` (typeinfo),
`_ZTS11CoinCounter` (typeinfo name).

Supersedes the older 0x94-bound estimate in `docs/structs/ui-widgets.md ## 9 CoinCounter`.

#### Constructor

**Address:** `CoinCounter::CoinCounter()` @ 0x00135600 (real), 0x00135644 (alias),
thunk wrapper 0x000f43d4. Signature: `CoinCounter::CoinCounter(CoinCounter* this)` —
no parameters.

```c
CoinCounter* CoinCounter::CoinCounter(CoinCounter* this) {
    HUDControl3d::HUDControl3d(&this->base);
    this->base.super.vtable     = CoinCounter_vtable + 8;   // GOT[0x71dc] -> 0x001E91B0+8
    this->m_CoinCount           = 0;          // +0x7C  (uint16_t)
    this->m_field_0x80          = 0.0f;       // DAT_00135638 = 0.0
    this->m_field_0x84          = 0;          // int
    this->m_field_0x88          = 0.0f;
    this->m_Alpha               = 0.0f;       // +0x90  (Reset will overwrite to 1.0)
    this->base.super.field_0x31 = 1;          // [4] cast = 1 (HUDControl flag)
    return this;
}
```

DAT pool (ctor):
- DAT_0013563c = 0x000B6B28 (PC→GOT base offset; GOT_base = 0x001EC130)
- DAT_00135638 = 0.0f (zero-init constant)
- DAT_00135640 = 0x000071DC (GOT offset to vtable ptr → vtable @ 0x001E91B0)

#### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x7B | HUDControl3d | super | base 0x7C bytes (icon would go in `super.field_0x74`) |
| +0x7C | uint16_t | m_CoinCount | "displayed" coin count — **never written** after ctor zero |
| +0x7E | uint16_t | (pad) | structure alignment |
| +0x80 | float | m_field_0x80 | ctor 0.0; not read by Draw / Reset / Update |
| +0x84 | int32_t | m_field_0x84 | ctor 0; not read |
| +0x88 | float | m_field_0x88 | ctor 0.0; not read |
| +0x8C | float | m_DrawAlpha | ctor 0.0; **Draw gate** (`> 0.0` else early-return); Reset clamps to [0,1] |
| +0x90 | float | m_Alpha | ctor 0.0; Reset writes 1.0 (declared but not consumed by Draw) |
| +0x94..+0xD3 | char[64] | m_TextBuffer | OS_SPrintf-shaped buffer iterated by Draw — **never written** |

**Size:** 0xD4 = 212 bytes (verified by `operator_new(0xd4)` in GameInit). Storage:
`*(CoinCounter**)(Game+0x178) = coinCtrl;` (set in GameInit step 5 of 0x0016c644).

#### Vtable (15 entries) — symbol `_ZTV11CoinCounter` @ 0x001E91B0

Layout (Itanium ABI): `[+0x00 = offset_to_top=0]`, `[+0x04 = typeinfo @ 0x001E91F4]`,
then 15 method slots starting at +0x08. The `vtable+8` value (set by ctor) is what
goes into `*this`.

| Idx | Vt-Off | Address | Method | Notes |
|-----|--------|---------|--------|-------|
| 0 | +0x08 | 0x001355B8 | ~CoinCounter (D1, in-place) | super dtor + restore vptr |
| 1 | +0x0C | 0x0013558C | ~CoinCounter (D0, deleting) | dtor + `operator_delete(this)` |
| 2 | +0x10 | 0x00135544 | **Init** | empty body — `return` (no LoadContent path) |
| 3 | +0x14 | 0x0013557C | Release | empty body — `return` |
| 4 | +0x18 | 0x00135548 | Reset | clamps `m_DrawAlpha` to [0,1]; `m_Alpha = 1.0f` |
| 5 | +0x1C | 0x0012F92C | BeginDraw(float) | inherited HUDControl3d (no-op) |
| 6 | +0x20 | 0x00135584 | PreDraw(float*) | `return param` — passthrough |
| 7 | +0x24 | 0x0013569C | **Draw(float*)** | full coin-quad + text path (see below) |
| 8 | +0x28 | 0x0012F930 | PreDrawOrder(float*,int) | inherited (dispatches to PreDraw) |
| 9 | +0x2C | 0x0012F93C | DrawOrder(float*,int) | inherited (dispatches to Draw) |
| 10 | +0x30 | 0x00135580 | **Update(float)** | empty body — confirmed no-op |
| 11 | +0x34 | 0x0012FD54 | SetToMultiplayerState | inherited HUDControl3d default |
| 12 | +0x38 | 0x00135AF4 | GetType | returns **3** (NOTE: src stub had 1 — fix to 3) |
| 13 | +0x3C | 0x00135588 | Skip | empty body — `return` |
| 14 | +0x40 | 0x0012F950 | Save | inherited (no-op) |

The deleting/in-place dtor ordering above (D1 first, D0 second) follows the Itanium
ABI convention used elsewhere in this binary; verified from each dtor's body.

Mangled per-slot symbols also exported: `_ZN11CoinCounter4InitEv`, `_ZN11CoinCounter5ResetEv`,
`_ZN11CoinCounter7ReleaseEv`, `_ZN11CoinCounter6UpdateEf`, `_ZN11CoinCounter7PreDrawEPf`,
`_ZN11CoinCounter4DrawEPf`, `_ZN11CoinCounter4SkipEv`, `_ZN11CoinCounter7GetTypeEv`,
`_ZN11CoinCounterC1Ev`, `_ZN11CoinCounterC2Ev`, `_ZN11CoinCounterD0Ev`,
`_ZN11CoinCounterD1Ev`, `_ZN11CoinCounterD2Ev`.

#### Reset (0x00135548) — full body

```c
void CoinCounter::Reset() {
    float a = this->m_DrawAlpha;        // +0x8C
    this->m_Alpha = 1.0f;               // +0x90
    float clamped = 0.0f;               // DAT_00135578 = 0.0
    if (a > 0.0f) {
        clamped = (a >= 1.0f) ? 1.0f : a;
    }
    this->m_DrawAlpha = clamped;        // +0x8C clamped to [0, 1]
}
```

#### Draw (0x0013569C) — full pseudocode

```c
void CoinCounter::Draw(CoinCounter* this, float* hudScale) {
    /* GOT_base    = 0x001EC130 (lit 0x000B6A80 + PC 0x001356B0)
       MatrixManager = *(GOT_base + 0x7348) = MatrixManager*  @ 0x002748D4
       g_GameData  = *(GOT_base + 0x7990) = GameData*         @ 0x001F43B8
       Colour ptr  = *(GOT_base + 0x73A4)                     @ 0x001F34D4
       UV ptr      = *(GOT_base + 0x78C0) = float[2] {u, v}   @ 0x001F4340 */

    if (!(this->m_DrawAlpha > 0.0f)) return;            // +0x8C gate

    /* --- 1. Build world matrix from base.size and base.pos --- */
    Matrix44 m;
    Matrix44_Scale44(&this->base.super.size, &m);       // size @ +0x20
    Matrix44_GlobalTranslate44(&m, &this->base.super.pos); // pos @ +0x08

    MatrixManager* mm = MatrixManager_singleton;
    MatrixStack::Reset(&mm->m_World);                   // m_World @ mm + 0x1080+0x14
    MatrixStack::SetCurrentMatrix(&mm->m_World, &m);
    MatrixManager::UploadCurrentMatrices(mm, true);

    /* --- 2. Draw coin icon quad using +0x74 texture --- */
    Mortar::Texture::Set(this->base.m_SecondaryTex);    // +0x74 — NEVER LOADED in ctor/Init
    Colour tint;
    TintWhite(&tint);                                   // (1,1,1,1)
    Mortar::Mesh::DrawQuadUnCached(tint, /*u0,u1,v0,v1=defaulted*/ 0);
    Mortar::Texture::UnSet(this->base.m_SecondaryTex);

    /* --- 3. Render number text at pos.x - 15, pos.y --- */
    Vec3 textPos = this->base.super.pos;                // +0x08 .. +0x14
    Font* font   = ((GameData*)g_GameData)->pFontReserved1;  // GameData +0x5C — ALWAYS NULL
    textPos.x   -= 15.0f;                               // shift left of icon

    Utf8StringIterator iter;
    Utf8StringIterator::ctor(&iter, &this->m_TextBuffer); // +0x94 — empty (never written)

    Vec3   posCopy = textPos;
    Colour col;
    Colour::Colour(&col, *(Colour**)(GOT + 0x73A4));    // default Colour for HUD
    /* uv pair [u, v] from GOT entry (DAT_001357E0) — copied to two ints on stack */
    int uv0 = ((int*)(GOT + 0x78C0))[0];
    int uv1 = ((int*)(GOT + 0x78C0))[1];

    /* Mortar::Font::DrawString — VFP arg pack (hard-float ABI):
         s0 = 30.0          (lineHeight / size)
         s1 = 1.0           (scale)
         s2 = -0.503...     (DAT_001357C8 = 0xBF008FF0; horizontal nudge?)
         s3 = 0.0           (DAT_001357CC; z arg)
         r0 = font          (Font* — null for CoinCounter)
         r1 = &posCopy      (Vec3*)
         r2 = &iter         (Utf8StringIterator*)
         r3 = &col          (Colour*)
         [sp+0]  = &uv      (UV pair pointer)
         [sp+4]  = 0xE      (alignment flag = 14, same as TimeControl)
         [sp+8]  = 0        (style flag) */
    Mortar::Font::Font_DrawString(font, &posCopy, &iter, &col,
                                  /*uv*/ &uv0,
                                  /*align*/ 0xE,
                                  /*style*/ 0,
                                  30.0f, 1.0f, -0.5036f, 0.0f);
    Utf8StringIterator::~Utf8StringIterator(&iter);
}
```

#### DAT Pool (Constants) — Draw

| Address | Value | Use |
|---------|-------|-----|
| DAT_001357CC | 0.0f | DrawString z arg (s3) |
| DAT_001357D0 | 0x000B6A80 | PC→GOT_base offset (GOT_base = 0x001EC130) |
| DAT_001357D4 | 0x00007348 | GOT offset → MatrixManager singleton ptr |
| DAT_001357D8 | 0x00007990 | GOT offset → g_GameData ptr |
| DAT_001357DC | 0x000073A4 | GOT offset → Colour template ptr |
| DAT_001357E0 | 0x000078C0 | GOT offset → default UV pair (Vec2) ptr |
| DAT_001357C8 | 0xBF008FF0 (-0.5036) | DrawString s2 arg |
| inline 0x41F00000 | 30.0f | DrawString s0 arg (line height/scale) |
| inline 0x3F800000 | 1.0f | DrawString s1 arg (scale) |
| inline -15.0f (`vmov 0x41700000` then sub) | 15.0 | text X-offset from icon |

Float literal note: `vmov.f32 s15,0x41700000` loads **15.0** (not 0x41700000 as integer).
The subsequent `vsub.f32 s15, s14, s15` gives `pos.x - 15.0f`.

#### Who writes m_CoinCount? (answer: nobody)

Searched both for direct writes to `(CoinCounter*) + 0x7C` and for any
`SetCoins`/`AddCoins` member of CoinCounter:

- `AddCoins(int)` @ 0x0010A3BC — exists, but writes to `pSaveData+0x20` and
  `pSaveData+0x24` (lifetime + total earned), NOT to any CoinCounter field.
  Callers: `PowerUp::Activate` @ 0x001191A4 (debit), `ItemManager::BuyItem` @
  0x001124DE (debit), `CoinArrived(Coin*)` @ 0x0017320C (credit).
- `Coin::ClearCoins(bool)` @ 0x001731B8 — entity static; flags every Coin entity
  with the `+0x11` removal bits (or routes through `Arrived`). Touches no CoinCounter
  field.
- `COIN_CHANCEINATOR::GetCoins` @ 0x00121778 — random per-fruit roll, returns int.
- `WaveManager::RequestCoins` @ 0x00121A1C — gates COIN_CHANCEINATOR rolls.

Xrefs to `m_CoinCount` (+0x7C of CoinCounter*): only the ctor (`strh.w r2,[r4,#0x7c]`
at 0x00135632). The runtime balance lives in `g_SaveData` (FruitSaveData / MainSaveData),
not in this widget. The text buffer at +0x94 is never targeted by any
`OS_SPrintf`/`memcpy` chain that resolves to a CoinCounter.

#### Why the widget is invisible at runtime

Three independent conditions each suffice to render nothing — none rely on the
others. (1) `vtable[2]=Init` is a no-op, so `super.m_SecondaryTex` (+0x74) stays
null and `Texture::Set(NULL)` binds nothing. (2) `m_DrawAlpha` (+0x8C) is 0.0
after ctor and Reset only clamps it back to 0; the `if (m_DrawAlpha > 0.0f)` gate
in Draw fails every frame. (3) Even if (1) and (2) flipped, the text branch reads
`g_GameData.pFontReserved1` at +0x5C, which `docs/structs/game.md` confirms is
"always null. … CoinCounter::Draw reads it but pointer is always null." The
+0x94 text buffer is also never written, so the iterator would walk a zero-length
string anyway.

**Implication for the port:** the existing src stub at `src/hud/CoinCounter.{h,cpp}`
is functionally correct already (Update no-op, Draw no-op, Reset clamps). RE-driven
fixes for the implementer: (a) `GetType()` returns **3**, not 1; (b) split
`m_fields_7e[0x56]` into named fields `m_field_0x80, m_field_0x84, m_field_0x88,
m_DrawAlpha (+0x8C), m_Alpha (+0x90), char m_TextBuffer[64] (+0x94..+0xD3)` so
sizeof matches the binary's 0xD4. No behavior change required — the original
ships with this HUD widget invisible.

#### Per-player MP positioning

None. CoinCounter has no `IsSameScreenMultiplayer` branch in any of its functions.
GameInit creates one global instance at `Game+0x178`; SetToMultiplayerState is the
inherited HUDControl3d default (no override). Position is left at the HUDControl
default `(0, 0, 0)` — GameInit does NOT call `pos = …` or `size = …` for it.

#### Binary References (summary)

All vtable slots above (0x00135544..0x00135AF4) plus: ctor 0x00135600 / 0x00135644
(alias) + thunk 0x000F43D4; three dtors at 0x0013558C / 0x001355B8 / 0x001355DC
(D0 deleting / D1 in-place / D2 subobject); TU init `_GLOBAL__I_CoinCounter.cpp`
@ 0x001357E4 (constructs identity matrix + two Vec3 globals + a Colour, registered
via `__aeabi_atexit`).

---

### ScoreControl : HUDControl3d (size = 0x100 = 256 bytes)

<!-- Analysed: 2026-04-30 (Phase B1) -->

Main score HUD: numeric score readout with per-digit alpha fade-in animation,
sin-wobble per-digit pulse on score change, scale pulse driven by wave/combo,
multiplier overlay (`x2`, `x4`, `x8`...), and "new best score" banner with
animated colour interpolation. Source file: `ScoreControl.cpp`. Mangled:
`_ZTV12ScoreControl` at vtable address **0x001E9D48**, typeinfo at 0x001E9D8C.

#### Constructor
**Address:** 0x00158C7C (real), 0x00158D4C (alias), thunk wrapper 0x000F6BDC.

Signature: `ScoreControl::ScoreControl(ScoreControl* this)` — **no parameters**.

```c
ScoreControl* ScoreControl::ScoreControl(ScoreControl* this) {
    HUDControl3d::HUDControl3d(&this->base);
    this->base.super.vtable = ScoreControl_vtable + 8;        // GOT[0x72D4] -> 0x001E9D48 + 8
    SmartPtr<Texture>::SmartPtr(&this->m_ScoreIconTex);       // +0xA0 zero-init
    SmartPtr<Texture>::SmartPtr(&this->m_HighscoreBannerTex); // +0xA4 zero-init
    SmartPtr<Texture>::SmartPtr(&this->m_FruitDigitTex);      // +0xF8 zero-init
    this->m_ScoreSmoothed   = 0.0f;          // +0x80 (DAT_00158d3c = 0.0)
    this->base.super.m_Timer = 0.0f;         // +0x2c
    this->m_PlayerIdx       = 0;             // +0xFC
    this->m_bDirty          = 1;             // +0x7C (offset matches scoreCtrl[4]=0x1 in decompile)
    this->m_PulseAngle      = 0;             // +0x7E (ushort)
    this->m_DisplayedScore  = 0;             // +0x88
    this->m_BannerStartTimer = -1.0f;        // +0x8C  (-1.0f initial, then ramps to 1.0)
    this->m_ScalePulse      = 1.0f;          // +0x90
    this->m_BannerScaleTime = -2.0f;         // +0xA8 (-2.0f sentinel = inactive banner)
    LoadLocalisedTexture(&tmp, "hud_fruit.tex"); // DAT_00158D48 -> 0x001BBF58
    SmartPtr<Texture>::operator=(&this->m_FruitDigitTex, &tmp);
    SmartPtr<Texture>::~SmartPtr(&tmp);
    Reset();   // initializes m_DigitAlpha[16] to 0, copies +0xF8 -> +0x74
    return this;
}
```

DAT pool (ctor):
| Address | Value | Use |
|---------|-------|-----|
| DAT_00158D3C | 0.0 | m_ScoreSmoothed init, m_Timer init |
| DAT_00158D40 | 0x000934A8 | GOT base offset (anchor 0x158C88, GOT=0x001EC130) |
| DAT_00158D44 | 0x000072D4 | GOT entry → vtable @ 0x001E9D48 |
| DAT_00158D48 | -0x301D8 | "hud_fruit.tex" string @ 0x001BBF58 |

#### Storage

Heap-owned by `HUD::AddControl(...)` (called in `GameInit` step 4 around 0x16C7A4).
There is **no `Game+0xNN` slot** — HUD is the sole owner. After ctor, GameInit
loads three additional textures into the instance (NOT done by ctor):

| Field | Address | Texture |
|-------|---------|---------|
| +0x74 (super.m_SecondaryTex) | DAT_0016C9E0 → 0x1BBF58 | `hud_fruit.tex` (multiplier fruit icon) |
| +0xA0 (m_ScoreIconTex) | DAT_0016C9E4 → 0x1BC919 | `score.tex` (note: tail-shared with "new_best_score.tex"; both literals point into the same string-pool block) |
| +0xA4 (m_HighscoreBannerTex) | DAT_0016C9E8 → 0x1BC910 | `new_best_score.tex` |

GameInit also sets `size = DAT_0016C9B8 * globalScale` and computes `pos`
from `DisplayManager::GetWindowSize()`. (Caller-side, not part of ctor body.)

#### Struct Layout (size = 0x100 = 256 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x7B | HUDControl3d | super | Base 0x7C bytes; uses super.m_SecondaryTex (+0x74) for fruit-multiplier icon |
| +0x7C | byte | m_bDirty | 1 = re-snap m_ScoreSmoothed to `GetCurrentScore()` next Update. Cleared after read. |
| +0x7D | byte | _pad | |
| +0x7E | uint16 | m_PulseAngle | sin-table angle (ushort). Set to 0x8000 on score increase, decays toward 0; drives digit wobble |
| +0x80 | float | m_ScoreSmoothed | Easing-target score (float, smoothed toward `GetCurrentScore`) |
| +0x84 | int | m_DisplayedScore | Truncated `(int)m_ScoreSmoothed` — drives the formatted text |
| +0x88 | int | m_HighscoreToShow | Highscore value displayed in banner (0 = no banner) |
| +0x8C | float | m_BannerStartTimer | Banner activation timer; ctor inits to -1.0f |
| +0x90 | float | m_ScalePulse | Multiplier scale factor (1.0..2.0 during wave-active scale pulse) |
| +0x94 | float | m_DrawPosX | Cached draw X (computed in Update) |
| +0x98 | float | m_DrawPosY | Cached draw Y |
| +0x9C | float | m_DrawPosZ | Cached draw Z (always 0.0) |
| +0xA0 | SmartPtr\<Texture\> | m_ScoreIconTex | `score.tex` — score icon next to digits |
| +0xA4 | SmartPtr\<Texture\> | m_HighscoreBannerTex | `new_best_score.tex` |
| +0xA8 | float | m_BannerScaleTime | Banner scale animation timer; -1.5..1.0; -2.0 = inactive sentinel |
| +0xAC | uint16 | m_BannerSinIdx | sin-table angle for banner wobble |
| +0xAE | byte[2] | _pad | |
| +0xB0 | int | m_DigitCount | Active digit count (0..16). Mirrors width of `m_DisplayedScore` |
| +0xB4 | int | m_LastDigitCount | Previous digit count (for change detection — triggers fade-out cascade) |
| +0xB8 | float[16] | m_DigitAlpha | Per-digit alpha 0..1; index 0 = ones place |
| +0xF8 | SmartPtr\<Texture\> | m_FruitDigitTex | `hud_fruit.tex` (loaded by ctor; copied to +0x74 by Reset) |
| +0xFC | int | m_PlayerIdx | 0 (player 1) or 1 (player 2). Drives `IsMultiplayer` gating + `m_LayerFlags = 1<<idx` |

#### Vtable (15 entries) — at 0x001E9D48

| Idx | Offset | Address | Method | Notes |
|-----|--------|---------|--------|-------|
| 0   | +0x08  | 0x00158418 | ~ScoreControl (in-place) | resets vtable, calls Release, dtors 3 SmartPtrs (+0xF8,+0xA4,+0xA0), HUDControl3d dtor |
| 1   | +0x0C  | 0x00158394 | ~ScoreControl (deleting) | same + `operator delete(this)` |
| 2   | +0x10  | 0x00158190 | Init | dispatches to vtable[+0x10] = Reset |
| 3   | +0x14  | 0x00158370 | Release | SmartPtrNull on +0xF8, +0x74, +0xA0, +0xA4 |
| 4   | +0x18  | 0x001582E4 | **Reset** | see below |
| 5   | +0x1C  | 0x0012F92C | BeginDraw | inherited HUDControl3d (no-op) |
| 6   | +0x20  | 0x00158E1C | **PreDraw** | text + multiplier + highscore banner rendering |
| 7   | +0x24  | 0x001581D4 | **Draw** | alpha gate, then HUDControl3d::Draw for super.m_SecondaryTex quad |
| 8   | +0x28  | 0x0012F930 | PreDrawOrder | inherited (dispatches to PreDraw) |
| 9   | +0x2C  | 0x0012F93C | DrawOrder | inherited (dispatches to Draw) |
| 10  | +0x30  | 0x0015853C | **Update** | score smoothing + digit anim + banner anim + SFX |
| 11  | +0x34  | 0x0012FD54 | SetToMultiplayerState | inherited no-op (`bx lr`) |
| 12  | +0x38  | 0x00159D18 | GetType | returns 3 |
| 13  | +0x3C  | 0x001581A0 | Skip | restore from save: `m_DisplayedScore = GetCurrentScore(m_PlayerIdx)`; if game-over flag, force banner active (`m_BannerScaleTime = 1.0f`) |
| 14  | +0x40  | 0x0012F950 | Save | inherited (no-op) |

#### Reset (0x001582E4)

```c
void ScoreControl::Reset(ScoreControl* this) {
    SmartPtr<Texture>::operator=(&this->base.m_SecondaryTex, &this->m_FruitDigitTex);
    this->m_PulseAngle = 0;                       // +0x7E
    this->m_bDirty     = 1;                       // +0x7C
    Vec3 globalHudScale = *(Vec3*)0x001F38FC;     // global Vec3 (HUD/window scale)
    Vec3 sized = (input pos vec) * globalHudScale;
    this->base.super.size = sized;                // +0x20..+0x28
    for (int i = 0; i < 16; i++) m_DigitAlpha[i] = 0.0f;
    this->m_DigitCount     = 0;                   // +0xB0
    this->m_LastDigitCount = 0;                   // +0xB4
    this->base.super.m_LayerFlags = 1 << this->m_PlayerIdx;  // +0x34
    // local_14 = 40.0 (DAT_00158354) — dead store
}
```

#### Update (0x0015853C, ~335 lines)

Player-mode gate first; then 8 stages. Compressed pseudocode:

```c
void ScoreControl::Update(ScoreControl* this, float dt) {
    int currentScore = GetCurrentScore(this->m_PlayerIdx);
    if (this->m_PlayerIdx >= 1 && !IsMultiplayer()) {
        this->base.super.m_bPendingRemoval = 1;
        return;
    }
    // Stage 1: Per-digit alpha cascade. mode==1 (Classic)
    //   if digitsActive == m_LastDigitCount: ramp m_DigitAlpha[0..N-1] up at +6/sec
    //   else: fade m_DigitAlpha[0..15] down at -16/sec; once index 0 hits 0, commit m_LastDigitCount
    // non-Classic: static-timer driven (0.25s gate) ramp up/fade down at same rates
    int digitsActive = clamp(*comboCountPtr, 0, 15);  // GOT 0x7478
    m_DigitCount = digitsActive;
    UpdateDigitAlphaCascade(this, dt);  // see logic above

    // Stage 2: Score easing toward currentScore
    if (m_bDirty) { m_bDirty=0; m_ScoreSmoothed=(float)currentScore; m_DisplayedScore=currentScore; }
    int  mult     = GetScoreMultiplyer(0);
    float baseRate = (g_GameData.gameMode == 2) ? 10.0f : 1.0f;
    float catchup  = (currentScore + (currentScore<0 ? -0.6f : 0.6f) - m_ScoreSmoothed) * 0.1f;
    float maxStep  = (float)mult * 0.3f * baseRate;
    m_ScoreSmoothed += min(catchup, maxStep);
    int prevDisplay = m_DisplayedScore;
    m_DisplayedScore = (int)m_ScoreSmoothed;
    if (sfxCooldown > 0) sfxCooldown -= dt;

    // Stage 3: Score-increase pulse + Arcade-bonus SFX
    if (m_DisplayedScore > prevDisplay) {
        if (sfxCooldown <= 0 && g_GameData.gameMode == 2
            && g_GameData.pCurrentWave && g_GameData.pCurrentWave[0x80] > 0
            && g_GameData.pCurrentWave[0x84] > 0.0f) {
            sfxCooldown = 0.05f;
            GameSound::SFXPlay(g_GameData.pSound, "Bonus-count-up", 1, 1, &delegate);
        }
        m_PulseAngle = 0x8000;   // peak of sin
    }

    // Stage 4: Pulse decay (rate -327680/sec); also m_ScalePulse from waveTimer
    float pulseSin  = SinIdx(m_PulseAngle);
    float waveTimer = g_GameData.field_0xC;
    m_ScalePulse    = (waveTimer > 0) ? ((waveTimer >= 1) ? 2.0f : 1.0f + waveTimer) : 1.0f;

    // Stage 5: Highscore tracking
    if (g_GameData.field_0x05 == 0 || currentScore == 0)
        m_HighscoreToShow = (GetCurrentModeHighscore() != 0)
                          ? max(m_DisplayedScore, GetCurrentModeHighscore()) : 0;

    // Stage 6: Position + layer flags + (wave-mode) text-width centering
    Vec3 base(-218, 138, 0);
    this->pos = base - Vec3(200,0,0) * playerScale;
    if (waveTimer > 0) {
        m_LayerFlags = 8 << m_PlayerIdx;
        m_DrawPos    = pos + Vec3(24, 0, 0);
        float w      = Font::MeasureString(font, "%d"%currentScore) * m_ScalePulse * 48.0f;
        m_DrawPos   += (Vec3(-160 - w*0.5f, 80, 0) - pos) * playerScale;
    } else {
        m_LayerFlags = 1 << m_PlayerIdx;
        m_DrawPos    = pos + Vec3(24, 0, 0);
    }

    // Stage 7: Highscore banner anim (rises to 1.0 over 0.2s, falls -20/sec to -1.5)
    bool wantBanner = (waveTimer > 0.99f) && g_GameData.pSaveData[300];
    if (wantBanner) {
        float prev = m_BannerScaleTime;
        m_BannerScaleTime = min(1, m_BannerScaleTime + dt*5);
        m_BannerSinIdx = (m_BannerScaleTime == 1) ? max(0, m_BannerSinIdx + dt*49140) : 0;
        if (m_BannerScaleTime > 0 && prev <= 0)
            GameSound::SFXPlay(g_GameData.pSound, "New-best-score", 1, 1, &delegate);
    } else {
        m_BannerScaleTime = max(-1.5f, m_BannerScaleTime - dt*20);
    }

    // Stage 8: Size pulse — base.size.x = base.size.y = 40.0 + pulseSin * 10.0
    float sizeVal = 40.0f + pulseSin * 10.0f;
    this->base.super.size.x = this->base.super.size.y = sizeVal;
}
```

**Key behaviours:**
- `m_PulseAngle` (uint16) is a 16-bit sin-table index. 0x8000 = peak; decays by ~5460/frame at 60fps.
- `g_GameData.pCurrentWave[0x80] > 0` AND `pCurrentWave[0x84] > 0.0f` is the wave-active gate that triggers `Bonus-count-up` in Arcade.
- `New-best-score` SFX fires once when the highscore banner first activates (m_BannerScaleTime crosses 0).
- `m_LayerFlags` toggles between `1<<m_PlayerIdx` (default) and `8<<m_PlayerIdx` (wave-active).
- Score easing: per-frame delta step = min(catchupRate*0.1, multiplier*0.3*(gameMode==2 ? 10 : 1)).

#### Draw (0x001581D4) — alpha gate, ~22 lines

```c
void ScoreControl::Draw(ScoreControl* this, float* hudScale) {
    if (!(this->m_PlayerIdx == 0 && IsMultiplayer())  // skip P1 in MP
        && g_GameData.someTimer >= -1.0f) {
        float alphaF = 255.0f * g_GameData.cameraIntensity;
        byte alpha   = clamp_u8(alphaF);
        this->base.super.m_DrawColour.a = alpha;        // +0x5F (super)
        HUDControl3d::Draw(&this->base, hudScale);      // base draws +0x74 fruit-icon quad
    }
}
```

#### PreDraw (0x00158E1C) — main rendering (~290 lines)

PreDraw is where text + multiplier + highscore banner all render. HUD::Draw
calls PreDraw before Draw, so the score text appears underneath the +0x74
fruit-icon quad that Draw delegates to HUDControl3d::Draw.

Five sections (A-E):

```c
void ScoreControl::PreDraw(ScoreControl* this, float* hudScale) {
    byte alpha = clamp_u8(255.0f * g_GameData.cameraIntensity);
    if (m_PlayerIdx == 0 && IsMultiplayer()) return;
    if (g_GameData.someTimer < -1.0f) goto draw_quads;

    // A. Score digits with adaptive width clamp.
    //   if score >= 1000: cache MeasureString("000")*0.75 (cxa-guard once),
    //   if printed-width > cached: scaleX = cached/printed; offsetX = (printed-cached)*0.5
    //   Font::DrawString(font, drawX+offsetX, drawY, 0, 48*scalePulse*scaleX,..., flags=0xD)

    // B. Per-mode multiplier overlay
    //   mode == 1 (Classic): per-digit fruit-icon overlay. For each i in 0..15 with
    //     m_DigitAlpha[i] > 0, OS_SPrintf("%d", 1<<(i+1)) => "2","4","8"... draw at
    //     scale = SinIdx(135 * m_DigitAlpha[i] * 182) * (45 + i*6). Tint = FRUIT_INFO->m_FactColour.
    //     Font texture rebinds to FRUIT_INFO->m_pFruitTexture for these draws.
    //   mode == 2 (Arcade): if PowerUpManager::GetScoreGainMultiplier() > 1 AND !flag_0x05:
    //     OS_SPrintf("x%d", mult); draw at (pos.x-18, pos.y-52) scale=48*scalePulse*0.75

    // C. Highscore banner text (active when |cameraTimer| < 1.0 AND m_HighscoreToShow > 0)
    //   Base colour = Colour(0xB4, 0x80, 0x05, 200) — orange-ish.
    //   If m_HighscoreToShow == m_DisplayedScore: pulse between base and (0x64,0x96,0x19,200)
    //     via CosIdx(staticBannerSinIdx * 0xB6) * -0.5 + 0.5 (lerp t in 0..1).
    //   staticBannerSinIdx ticks +6 per frame (or +0 if g_GameData.flag_0x02), capped 0xB3.
    //   Draws localised "NEW BEST SCORE" label (GETSTRING(0xB5,0)) and the score number.

draw_quads:
    // D. Score-icon texture quad (+0xA0 = score.tex)  — guarded by cameraTimer > 0
    //    Texture::Set, MatrixStack::Reset, scale by tex(W,H), translate to
    //    Vec3(IsMultiplayer ? 64*cameraTimer-mpAnchor : spAnchor, drawY+5.5, 0),
    //    DrawQuad(Colour(255,255,255,alpha)), Texture::UnSet.
    // E. Highscore banner texture (+0xA4 = new_best_score.tex) — guarded by m_BannerScaleTime > 0
    //    Scale44 with (texW+1, texH+1) * bannerScale * wobbleScale,
    //    RotZ44 by SinIdx(0xE38)/CosIdx(0xE38) (~5 degrees),
    //    GlobalTranslate44 to (iconW*0.5 - 64, drawY+0.5, 0), DrawQuad.
    //    bannerScale = SinIdx(m_BannerScaleTime * RATE);
    //    wobbleScale = SinIdx(m_BannerSinIdx) * 0.15 + 1.0
}
```

#### DAT Pool (Constants)

Score-easing (Update):

| Address | Value | Use |
|---------|-------|-----|
| DAT_001588A4 | -0.6 | catchup correction, negative score |
| DAT_001588A8 | 0.6 | catchup correction, positive score |
| DAT_001588AC | 0.3 | maxStep multiplier base |
| DAT_001588B0 | 0.1 | catchup rate |
| DAT_001588B4 | 0.05 | bonus SFX cooldown |
| DAT_001588B8 | 16384.0 | pulse angle midpoint (0x4000) |
| DAT_001588D0 | -327680.0 | pulse angle decay rate |
| DAT_001588D4 | 32768.0 | pulse angle wrap (0x8000) |

Position / layer (Update Stage 6):

| Address | Value | Use |
|---------|-------|-----|
| DAT_00158C44 / 48 / 4C | -218, 138, 0 | base pos (X, Y, Z) |
| DAT_00158C50 | 200.0 | MP X delta |
| DAT_00158C54 / 58 / 5C | 48, 80, -160 | wave-mode text scale, Y centre, X centre |
| DAT_00158C60 | 0.99... | banner activation threshold |
| DAT_00158C64 | 49140.0 | banner sin-idx delta |
| DAT_00158C68 | 40.0 | size base |

PreDraw rendering:

| Address | Value | Use |
|---------|-------|-----|
| DAT_00159090 | 0.75 | "000" baseline-width scale |
| DAT_00159094 | 48.0 | text scale (PreDraw) |
| DAT_00159098 | 1.0 | text scale-x identity |
| DAT_001593C0 | 135.0 | per-digit Sin pre-mul |
| DAT_001593C4 | 182.0 | Sin angle scale (= 65536/360) |
| DAT_001593C8 | 45.0 | per-digit base scale |
| DAT_001593D0 | 155.0 | digit overlay scale |
| DAT_001593D4 | 52.0 | "x%d" overlay y-offset |
| DAT_001593D8 | 48.0 | "x%d" overlay scale |
| DAT_001597BC | 0.15 | banner wobble amplitude |

GOT references (resolved via GOT base 0x001EC130):

| GOT offset | Target | Role |
|------------|--------|------|
| 0x72D4 | 0x001E9D48 | ScoreControl vtable |
| 0x7478 | (g_GameData global ptr) | combo/digit count |
| 0x7060 | (FRUIT_INFO count ptr) | fruit-info clamp |
| 0x7990 | (g_GameData global ptr) | game state |
| 0x45180 | (.bss block) | static caches (sfx cooldown, banner idx, "000" width) |

Texture / format strings (all GOT-rel):

| Address | String | Use |
|---------|--------|-----|
| 0x001BBF58 | `hud_fruit.tex` | super.m_SecondaryTex (+0x74) and m_FruitDigitTex (+0xF8) |
| 0x001BC910 | `new_best_score.tex` | m_HighscoreBannerTex (+0xA4) |
| 0x001BC919 | `score.tex` | m_ScoreIconTex (+0xA0) — tail-shared with new_best_score.tex |
| 0x001BBF66 | `000` | width-measurement key string |
| 0x001BBF6A | `x%d` | multiplier overlay format |
| 0x001BCCCC | `%d` | digit format string |
| 0x001B96BA | `New-best-score` | highscore SFX name |
| 0x001B9716 | `Bonus-count-up` | bonus-counter SFX name |

#### Notes for Implementer

1. **GameInit loads 3 textures** into +0x74, +0xA0, +0xA4 *after* the ctor returns. The ctor only loads `hud_fruit.tex` into +0xF8 (and Reset copies it to +0x74). Mirror this split when porting — don't fold all texture loads into the ctor.
2. **GetType() returns 3** (TimeControl is 4).
3. **m_LayerFlags is dynamic**: `1<<m_PlayerIdx` default, `8<<m_PlayerIdx` when wave/intensity timer is active.
4. **PreDraw is the rendering function**, NOT Draw. Draw is a thin alpha-gate that delegates to HUDControl3d::Draw for the +0x74 fruit-icon quad.
5. **The "x%d" multiplier overlay** only appears in Arcade (gameMode==2) when `PowerUpManager::GetScoreGainMultiplier() > 1 && !flag_0x05`.
6. **Per-digit fruit-icon overlay** only appears in mode==1 (Classic) — each `m_DigitAlpha[i] > 0` slot draws "2","4","8"... (powers of 2) at a Sin-curved scale.
7. **m_ScoreSmoothed** snaps to currentScore only on `m_bDirty` (Reset, Skip). Otherwise it eases at `min(catchup*0.1, mult*0.3*(gameMode==2?10:1))` per frame.
8. **Skip()** restores `m_DisplayedScore = GetCurrentScore(m_PlayerIdx)`; if game-over flag set, also force `m_BannerScaleTime = 1.0f`.
9. **Static cache state** (sfx cooldown, banner sin idx, "000" cached width) lives in `.bss` at GOT-base+0x45180 (cxa-guard-protected). Use C++11 function-local statics or per-instance members in the port.

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
