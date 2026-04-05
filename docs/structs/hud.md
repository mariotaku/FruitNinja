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

## See Also

- [Rendering functions](../engine/rendering-functions.md) -- HUDControl3d::Draw
- [SlashEntity](../entities/slash-entity.md) -- MissControl combo display
