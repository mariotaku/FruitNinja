# MatrixManager & MatrixStack

## MatrixManager (size = 0x2134 / 8500 bytes)

Global singleton accessed via GOT pointers. Wraps OpenGL ES 1.x fixed-function matrix modes (GL_PROJECTION, GL_MODELVIEW, GL_TEXTURE) with 4 MatrixStack instances and dirty-tracking version counters.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | MatrixManagerFns* | fns | vtable (2 function pointers) |
| +0x04 | 0x848 | MatrixStack | m_Projection | Stack 0 — Projection matrix (GL_PROJECTION = 0x1701) |
| +0x84C | 0x848 | MatrixStack | m_View | Stack 1 — View matrix (GL_MODELVIEW base) |
| +0x1094 | 0x848 | MatrixStack | m_World | Stack 2 — World/Model matrix (GL_MODELVIEW local) |
| +0x18DC | 0x848 | MatrixStack | m_Texture | Stack 3 — Texture matrix (GL_TEXTURE = 0x1702) |
| +0x2124 | 4 | int | m_ViewVersion | Cached version of m_View |
| +0x2128 | 4 | int | m_ViewVersionUploaded | Last uploaded version of m_View |
| +0x212C | 4 | int | m_WorldVersionUploaded | Last uploaded version of m_World |
| +0x2130 | 4 | int | m_TextureVersionUploaded | Last uploaded version of m_Texture |

### Key Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| MatrixManager::MatrixManager | 0x0019e478 | `(this)` | Constructs 4 MatrixStacks, zeros version counters |
| ~MatrixManager | 0x0019e3b4 | `(this)` | Destructs 4 MatrixStacks in reverse order |
| SetupOrtho | 0x0019e5a4 | `(this, float top, float bottom, float left, float right, float near, float far, Matrix44* out)` | Calls OrthoW then sets m_Projection. Param order is (top,bottom,left,right) NOT standard GL (left,right,bottom,top) |
| SetupLookAt | 0x0019e724 | `(this, Vec3& eye, Vec3& target, Vec3& up, Matrix43* out)` | LookAt43 → sets m_View |
| _UploadCurrentMatrices | 0x0019e2b4 | `(this, bool forceProjection)` | Dirty-check uploads to GL matrix modes |

### _UploadCurrentMatrices Logic

```
if (!forceProjection):
    glMatrixMode(GL_PROJECTION)
    combined = DisplayManager_something * m_Projection.current
    store combined to global, glLoadMatrixf

if (m_Texture.version != m_TextureVersionUploaded):
    glMatrixMode(GL_TEXTURE)
    glLoadMatrixf(m_Texture.current)

if (m_View.version != m_ViewVersionUploaded):
    glMatrixMode(GL_MODELVIEW)
    glPopMatrix; glLoadMatrixf(m_View.current); glPushMatrix; glMultMatrixf(m_World.current)
elif (m_World.version != m_WorldVersionUploaded):
    glMatrixMode(GL_MODELVIEW)
    glPopMatrix; glPushMatrix; glMultMatrixf(m_World.current)
```

---

## MatrixStack (size = 0x848 / 2120 bytes)

Stack of up to 32 Matrix44 entries with a "current" matrix and version counter for dirty-tracking.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x000 | 2048 | Matrix44[32] | m_Stack | 32-entry matrix stack array |
| +0x800 | 64 | _Matrix44<float> | m_Current | Current (top) matrix — used for all operations |
| +0x840 | 4 | int | m_Depth | Stack depth (0 = reset) |
| +0x844 | 4 | int | m_Version | Modification counter, incremented on every change |

### Key Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| MatrixStack::MatrixStack | 0x0019e910 | `(this)` | Calls Reset(), sets m_Version = 1 |
| Reset | 0x001175d4 | `(this)` | Identity into stack[0] and m_Current, m_Depth=0, m_Version++ |
| SetCurrentMatrix | 0x0011a130 | `(this, Matrix44& m)` | Copies 16 floats into m_Current, m_Version++ |
| Translate | 0x0012f97c | `(this, Vec3& t)` | GlobalTranslate44 on m_Current, m_Version++ |
| Scale | 0x0012fa34 | `(this, Vec3& s)` | Scale44 on m_Current, m_Version++ |

---

## _Matrix44<float> Key Methods

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| Identity44 | 0x0019e758 | `(Matrix44& out)` | Standard 4x4 identity |
| OrthoW | 0x0019e7a8 | `(top, bottom, left, right, near, far, w, Matrix44& out)` | Orthographic projection. Note non-standard param order |
| Scale44 | 0x0012f9a0 | `(this, float sx, float sy, float sz)` | Column-scale: col[i] *= s[i] for i=0,1,2 and col[3] |
| GlobalTranslate44 | 0x0012f954 | `(this, float tx, float ty, float tz)` | col[3] += (tx, ty, tz) — world-space translate |
| LocalTranslate44 | 0x0019a3d4 | `(this, float tx, float ty, float tz)` | col[3] += col[0]*tx + col[1]*ty + col[2]*tz — local-space |

### OrthoW Formula

```
Identity(out)
out[3][3] = 1.0   // (w parameter is always 1.0 in SetupOrtho)
invTB = 1.0 / (top - bottom)
invRL = 1.0 / (right - left)
out[2][2] = 1.0 / (far - near)
out[3][2] = near / (near - far)
out[3][1] = -(right + left) * invRL
out[3][0] = -(top + bottom) * invTB
out[0][0] = 2 * invRL      // 2/(right-left)
out[1][1] = 2 * invTB      // 2/(top-bottom)
```

---

## Engine Wrapper Functions

All `*_HUD`, `*_Draw`, `*_Engine` etc. are thin wrappers accessing the global MatrixManager singleton and calling MatrixStack methods on `m_World` (+0x1094). Each compilation unit has its own copy due to GOT-relative addressing.

### HUD Draw Pipeline Pattern

```
ResetMatrix_HUD()          → MatrixStack::Reset(manager.m_World)
ScaleMatrix_HUD(scale)     → MatrixStack::Scale(manager.m_World, scale)
TranslateMatrix_HUD(pos)   → MatrixStack::Translate(manager.m_World, pos)
UploadMatrices_HUD()       → MatrixManager::_UploadCurrentMatrices(true)
DrawQuad_HUD(colour)       → Mesh::DrawQuadUnCached(colour, ...)
```

### Wrapper Function Addresses (HUD context)

| Function | Address | Calls |
|----------|---------|-------|
| ResetMatrix_HUD | 0x00140a6c | MatrixStack::Reset on m_World |
| ScaleMatrix_HUD | 0x00140ab0 | MatrixStack::Scale on m_World |
| TranslateMatrix_HUD | 0x00140a8c | MatrixStack::Translate on m_World |
| UploadMatrices_HUD | 0x00140a50 | _UploadCurrentMatrices(true) |
| DrawQuad_HUD | 0x00140ad4 | Mesh::DrawQuadUnCached |

### SetMatrixByIndex_Engine (0x0019ecbc)

Direct index into the 4 stacks: `manager.stacks[index * 0x848 + 4]`
- Index 0 = Projection, 1 = View, 2 = World, 3 = Texture

### MortarCamera::SetupOrtho (0x0019edfc)

Camera-level ortho setup that gets window size, computes `SetupOrtho(height/2, -height/2, -width/2, width/2, -1.0, far)`.
For 480x320: `SetupOrtho(160, -160, -240, 240, -1.0, far)`.

---

## See Also

- [Rendering system](../systems/rendering.md) — How matrices flow through the render pipeline
- [HUD structs](hud.md) — HUDControl/HUDControl3d use the HUD draw pipeline pattern
