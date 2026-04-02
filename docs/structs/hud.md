# HUD & UI Structs

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

### HUDControl (base class)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | HUDControlFns* | vtable | |
| +0x08 | Vec3 | pos | |
| +0x20 | Vec3 | size | Half-extents, clamped to screen |
| +0x2c | float | field_0x2c | Timer or alpha |
| +0x30 | int | m_bActive | Non-zero = active |
| +0x32 | char | m_bNoDestructor | = 0 → call dtor on removal |
| +0x33 | char | m_bPendingRemoval | Set → remove next HUD::Update |
| +0x34 | int | field49_0x34 | = 1 after init |
| +0x38 | Delegate1 | m_RemoveCallback | Called before removal |
| +0x5f | byte | m_Alpha | = 0xff = fully opaque |

### HUDControl3d : HUDControl (size ~0x7C)

Extends HUDControl with a texture, rotation, UV rect, and 3D-style Draw.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x5f | HUDControl | super | Base class (0x60 bytes) |
| +0x5c | Colour | m_DrawColour | Packed BGRA at +0x5c..0x5f |
| +0x60 | SmartPtr\<Texture\> | m_PauseTitleTex | Main display texture (null = don't draw) |
| +0x64 | float | m_UVLeft | UV rect left |
| +0x68 | float | m_UVTop | UV rect top |
| +0x6c | float | m_UVRight | UV rect right |
| +0x70 | float | m_UVBottom | UV rect bottom |
| +0x74 | SmartPtr\<Texture\> | field_0x74 | Secondary texture (used by screens) |
| +0x78 | int | field_0x78 | Initialized to 0 |

Constructor (0x1443f4): Calls `HUDControl::HUDControl()`, sets vtable, zeroes texture SmartPtrs, sets `m_Timer = DAT` (rotation speed constant).

#### HUDControl3d::Draw (0x14428c, 57 lines)

Renders a textured quad with rotation and colour tint:

```
if texture is valid AND alpha != 0:
    1. Texture::Set(m_PauseTitleTex)
    2. ResetMatrixStack
    3. Scale44 from HUDControl.size
    4. If m_Timer != 0: RotZ44(sin(timer * speed), cos(timer * speed))
    5. Translate to HUDControl.pos (with scale offset)
    6. SetCurrentMatrix + UploadMatrices
    7. TintColour(m_DrawColour, globalScale)
    8. DrawQuadUnCached(colour, uvLeft, uvRight, uvTop, uvBottom)
    9. Texture::UnSet
```

- **PreDraw**: no-op (returns param)
- **Update**: delegates to `HUDControl::Update`

This is the base class for all screens (MainScreen, GameOverScreen, etc.) and most HUD widgets (MissControl, ScoreControl, etc.).

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

- [Rendering functions](../functions/rendering.md) -- HUDControl3d::Draw
- [SlashEntity functions](../functions/slash-entity.md) -- MissControl combo display
