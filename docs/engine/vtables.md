# Engine Vtable Layouts

## DisplayManager Vtable (20 entries)

Base vtable at 0x001eb440:

| Index | Offset | Method | Notes |
|-------|--------|--------|-------|
| 0 | +0x00 | ~DisplayManager (scalar dtor) | |
| 1 | +0x04 | ~DisplayManager (vector dtor) | |
| 2 | +0x08 | Init | Pure virtual |
| 3 | +0x0C | BeginFrame | Pure virtual |
| 4 | +0x10 | EndFrame | Pure virtual |
| 5 | +0x14 | SwapBuffers | Pure virtual |
| 6 | +0x18 | SetDrawColour | Pure virtual |
| 7 | +0x1C | SetDepthBufferWrite | Pure virtual |
| 8 | +0x20 | SetDepthBuffer | Pure virtual |
| 9 | +0x24 | Destroy | Base: no-op |
| 10 | +0x28 | SetWindowSize(l,t,r,b) | |
| 11 | +0x2C | SetWindowSize(MortarRectangle) | Overload |
| 12 | +0x30 | GetWindowSize | Returns MortarRectangle |
| 13 | +0x34 | GetAspectWvH | Width / Height |
| 14 | +0x38 | GetAspectHvW | Height / Width |
| 15 | +0x3C | ShowSystemCursor | |
| 16 | +0x40 | SetGlobalAmbience | |
| 17 | +0x44 | GetGlobalAmbience | |
| 18 | +0x48 | ShouldUseHDFonts | |
| 19 | +0x4C | IsRenderingAllowed | Base: always true |

### DisplayManagerBada Vtable (20 entries, at 0x001eb4b8)

Overrides slots 2-8 (Init, BeginFrame, EndFrame, SwapBuffers, SetDrawColour, SetDepthBufferWrite, SetDepthBuffer) and slot 9 (Destroy) and slot 16 (SetGlobalAmbience) with Bada GL implementations. Other 11 slots inherited from base.

---

## HUDControl Vtable (15 entries) — CONFIRMED

Vtable at 0x001e96f8. Verified by reading all 15 function pointers.

| Index | Offset | Method | Notes |
|-------|--------|--------|-------|
| 0 | +0x00 | ~dtor (scalar) | Deleting destructor |
| 1 | +0x04 | ~dtor (vector) | |
| 2 | +0x08 | Init() | |
| 3 | +0x0C | Release() | Cleanup resources |
| 4 | +0x10 | Reset() | |
| 5 | +0x14 | BeginDraw(float dt) | |
| 6 | +0x18 | PreDraw(float* hudScale) | Called by PreDrawOrder |
| 7 | +0x1C | Draw(float* hudScale) | Actual rendering |
| 8 | +0x20 | PreDrawOrder(float*, int) | Wrapper → calls PreDraw |
| 9 | +0x24 | DrawOrder(float*, int) | Wrapper → calls Draw |
| 10 | +0x28 | Update(float dt) | Tick logic |
| 11 | +0x2C | SetToMultiplayerState() | |
| 12 | +0x30 | GetType() | Returns int |
| 13 | +0x34 | Skip() | |
| 14 | +0x38 | Save() | |

**Draw dispatch**: HUD::Draw calls `PreDrawOrder` (+0x20) then `DrawOrder` (+0x24). These are thin wrappers that dispatch to `PreDraw` (+0x18) and `Draw` (+0x1C).

---

## MortarGame Vtable (16 entries)

Vtable at 0x001eae58. See [structs/game.md](../structs/game.md) for full table.

---

## Entity Vtable (verified from ActorManager usage)

| Index | Offset | Method |
|-------|--------|--------|
| 0 | +0x00 | ~dtor (scalar) |
| 1 | +0x04 | ~dtor (vector) |
| 2 | +0x08 | OnActivate |
| 3 | +0x0C | OnDeactivate |
| 4 | +0x10 | Update(float dt) |
| 5 | +0x14 | Draw() |
| 6 | +0x18 | PostUpdate(float dt) |

---

## See Also

- [Display manager](display-manager.md) — DisplayManager/Bada struct and methods
- [HUD structs](../structs/hud.md) — HUDControl/HUDControl3d class hierarchy
- [Game structs](../structs/game.md) — MortarGame vtable details