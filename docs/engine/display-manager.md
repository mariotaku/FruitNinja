# DisplayManager & DisplayManagerBada

## DisplayManager (base class, size = 0x94 / 148 bytes)

Engine singleton for GL state management. Accessed via `GetInstance()` (0x0019da64) which lazily creates a `DisplayManagerBada` subclass.

### Struct Layout

| Offset | Size | Type | Name | Init Value |
|--------|------|------|------|------------|
| +0x00 | 4 | DisplayManagerFns* | vtable | |
| +0x04 | 4 | Colour | m_ClearColor | Colour::Colour() |
| +0x08 | 4 | Colour | m_DrawColor | Colour::Colour() |
| +0x0C | 16 | MortarRectangle | m_WindowRect | InitRect_Engine() |
| +0x1C | 12 | Vec3 | m_lightDirection | InitVec3Const_Engine() |
| +0x28 | 4 | Colour | m_GlobalAmbience | 0xff000000 |
| +0x2C | 1 | bool | m_bRenderingActive | false |
| +0x2D | 1 | byte | m_bSwapPending | 0 |
| +0x34 | 16 | char[16] | m_TextureOverloadPrefix | "" (empty) |
| +0x44 | 4 | int | m_MagFilterMode | 1 |
| +0x48 | 4 | int | m_MinFilterMode | 1 |
| +0x4C | 4 | int | m_WrapSMode | 1 |
| +0x50 | 4 | int | m_WrapTMode | 1 |
| +0x54 | 64 | float[16] | m_ScreenRotationMatrix | Identity; BeginFrame sets 90deg CCW |

### Screen Rotation Matrix (+0x54)

In BeginFrame, the 4x4 matrix at offset 0x54 is set to a 90-degree counter-clockwise rotation around Z:
```
 0  -1   0   0
 1   0   0   0
 0   0   1   0
 0   0   0   1
```
This transforms the Bada device's 480x800 portrait coordinates into 480x320 landscape.

### DisplayManager Base Methods

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| DisplayManager() | 0x0019dbd0 | `__thiscall (this)` | Constructor — inits colours, rect, light dir |
| ~DisplayManager | 0x0019da00 | `__thiscall (this)` | Destructor |
| GetInstance | 0x0019da64 | `static DisplayManager*()` | Lazy singleton, creates DisplayManagerBada |
| GetWindowSize | 0x0019dc94 | `__thiscall MortarRectangle(this)` | Returns m_WindowRect by value |
| SetWindowSize | 0x0019da3c | `__thiscall (this, l, t, r, b)` | Sets m_WindowRect |
| IsRenderingAllowed | 0x0019dd14 | `__thiscall bool(this)` | Always returns true |
| SetTextureOverloadPrefix | 0x0019da58 | `__thiscall (this, char*)` | strcpy to +0x34 |
| GetPlatformWrapS | 0x0019da50 | `__thiscall int(this)` | Base: returns 0 |
| GetPlatformWrapT | 0x0019da54 | `__thiscall int(this)` | Base: returns 0 |
| Destroy | 0x0019da38 | `__thiscall (this)` | Empty virtual (no-op) |

### DisplayManagerBada Methods (extends DisplayManager)

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| DisplayManagerBada() | 0x0019dfa8 | `__thiscall (this)` | Constructor |
| BeginFrame | 0x0019dfec | `__thiscall (this)` | Full GL frame setup: clear, blend, depth, rotation matrix |
| EndFrame | 0x0019dd1c | `__thiscall (this)` | Clears m_bRenderingActive |
| SwapBuffers | 0x0019dd2c | `__thiscall (this)` | Toggles m_bSwapPending |
| SetDrawColour | 0x0019dde4 | `__thiscall (this, Colour*)` | glColor4ub if changed |
| SetGlobalAmbience | 0x0019dd40 | `__thiscall (this, ulong)` | Sets m_GlobalAmbience |
| SetDepthBufferWrite | 0x0019de0c | `__thiscall (this, bool)` | glDepthMask |
| SetDepthBuffer | 0x0019de18 | `__thiscall (this, bool)` | GL_DEPTH_TEST enable/disable |
| Init | 0x0019de38 | `__thiscall (this, void*, char*, bool)` | GL init state |
| Destroy | 0x0019de30 | `__thiscall (this)` | Calls base Destroy |
| GetPlatformMagFilter | 0x0019dd44 | `__thiscall int(this)` | Lookup table: mode -> GL enum |
| GetPlatformMinFilter | 0x0019dd6c | `__thiscall int(this)` | Lookup table (6 modes) |
| GetPlatformWrapS | 0x0019dd94 | `__thiscall int(this)` | Lookup table (2 modes) |
| GetPlatformWrapT | 0x0019ddbc | `__thiscall int(this)` | Lookup table (2 modes) |

### Helper Functions (static, in same compilation unit)

| Function | Address | Notes |
|----------|---------|-------|
| MortarRectangle::Width | 0x0019dc80 | right - left |
| MortarRectangle::Height | 0x0019dc88 | bottom - top |
| InitVec3Const_Engine | 0x0019daec | Vec3 initialization helper |
| InitRect_Engine | 0x0019db0c | MortarRectangle init helper |

### BeginFrame GL Setup

```c
void DisplayManagerBada::BeginFrame() {
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glClearDepthf(1.0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_FOG);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    if (!m_bRenderingActive) {
        m_bRenderingActive = true;
        glDepthMask(1);
        // Initialize material/lighting state floats at +0x54..+0x90
        // Set m_ScreenRotationMatrix to 90deg CCW rotation
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}
```

### Port Notes

For the SDL2+GLES2 port:
- Replace `glColor4ub` (SetDrawColour) with shader uniform
- Replace `glMatrixMode`/`glLoadMatrixf` with MatrixManager's shader-based approach
- m_ScreenRotationMatrix handles portrait->landscape; in the port this is handled by SDL window orientation
- Filter mode lookups (GetPlatformMagFilter etc.) map to standard GL enums: GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, etc.

## See Also

- [MatrixManager](matrix-manager.md) — matrix stack system used alongside DisplayManager
- [Rendering detail](rendering-detail.md) — Model::Draw, TintColour, DrawQuadUnCached
- [Rendering functions](rendering-functions.md) — HUDControl3d::Draw pipeline
