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

## _Vector3\<float\> (12 bytes)

Ghidra struct fields: `a`, `b`, `c` (floats). Port uses `x`, `y`, `z`.

### Methods

Most are inlined by the compiler and duplicated per compilation unit (GOT-relative ARM32). Representative addresses shown.

#### Dot (0x00133c4c)
```c
float _Vector3::Dot(const _Vector3& other) const {
    return x * other.x + y * other.y + z * other.z;
}
```

#### MagnitudeSqr (0x00133c74)
```c
float _Vector3::MagnitudeSqr() const {
    return Dot(*this);  // x*x + y*y + z*z
}
```

#### Magnitude (0x00138cdc)
```c
float _Vector3::Magnitude() const {
    return Math::Sqrt(MagnitudeSqr());
}
```

#### Normalise (0x00138ce8)
```c
float _Vector3::Normalise() {
    if (x == 0.0f && y == 0.0f && z == 0.0f)
        return 0.0f;
    float mag = Magnitude();
    if (mag == 0.0f) {
        *this *= 1000000.0f;   // DAT_00138d5c = 0x49742400, scale-up for near-zero
        Normalise();           // recursive retry
    } else {
        *this /= mag;
    }
    return mag;                // returns original magnitude
}
```

#### Cross (0x0017ea04)
```c
// Static, ARM struct-return (r0 = output ptr)
void _Vector3::Cross(_Vector3& out, const _Vector3& a, const _Vector3& b) {
    out.x = a.y * b.z - b.y * a.z;
    out.y = b.x * a.z - a.x * b.z;
    out.z = a.x * b.y - b.x * a.y;
}
```

#### operator/= (0x00138b40)
```c
_Vector3& _Vector3::operator/=(float s) {
    x /= s; y /= s; z /= s;
    return *this;
}
```

### All Instances

Each method has multiple copies across compilation units. The implementations are identical.

| Method | Representative | Copies | Notes |
|--------|---------------|--------|-------|
| Dot | 0x00133c4c | 6 | `this.x*o.x + this.y*o.y + this.z*o.z` |
| MagnitudeSqr | 0x00133c74 | 4 | Calls `Dot(this)` |
| Magnitude | 0x00138cdc | 6 | Calls `Sqrt(MagnitudeSqr())` |
| Normalise | 0x00138ce8 | 6 | In-place normalize, returns mag, 1M retry for near-zero |
| Cross | 0x0017ea04 | 2 | Static, ARM struct-return |
| operator/= | 0x00138b40 | 4 | Component-wise divide |
| SetMagnitude | — | — | NOT a Vec3 method (FruitSlicedPacket field setter) |

### Constants

| Address | Hex | Value | Usage |
|---------|-----|-------|-------|
| 0x00138d58 | 0x00000000 | 0.0f | Returned when vector is zero |
| 0x00138d5c | 0x49742400 | 1000000.0f | Scale-up for zero-length Normalise retry |

### Also applies to _Vector2\<float\> and _Quaternion\<float\>

Same method pattern exists for 2D vectors and quaternions:
- `_Vector2<float>::Magnitude` (0x00173080), `Normalise` (0x00173098) — 2-component version
- `_Quaternion<float>::Magnitude` (0x0017abf4) — 4-component: `sqrt(a² + b² + c² + d²)`
- `_Quaternion<float>::Normalise` (0x0017ac1c) — divides all 4 components, falls back to `Identity()` if w==0

---

## _Matrix43\<float\> (48 bytes)

A 4×3 matrix (4 rows, 3 columns) — a Matrix44 without the 4th column (w/perspective). Used for view matrices where column 3 is always `(0, 0, 0, 1)`.

### Layout

```
float data[4][3];  // 48 bytes

Row 0: [right.x,     right.y,     right.z    ]  // X axis
Row 1: [up.x,        up.y,        up.z       ]  // Y axis
Row 2: [forward.x,   forward.y,   forward.z  ]  // Z axis
Row 3: [translate.x,  translate.y, translate.z]  // position
```

### LookAt43 (0x0019e82c)

Standard view matrix: `LookAt43(eye, target, up, out)`

```c
void LookAt43(Vec3& eye, Vec3& target, Vec3& up, Matrix43& out) {
    Vec3 forward = normalize(target - eye);
    Vec3 right   = normalize(cross(up, forward));
    Vec3 realUp  = cross(forward, right);

    out[0] = { right.x,   right.y,   right.z   };
    out[1] = { realUp.x,  realUp.y,  realUp.z  };
    out[2] = { forward.x, forward.y, forward.z };
    out[3] = { -dot(eye, right), -dot(eye, realUp), -dot(eye, forward) };
}
```

### Conversion Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| Copy44To43 | 0x00181c68 | `void Matrix44::Copy44To43(Matrix43& out)` | Drops column 3 (w) from each row |
| Copy43To44 | 0x00181cdc | `void Matrix43::Copy43To44(Matrix44& out)` | Adds column 3: `(0, 0, 0, 1)` |
| operator Matrix43 | 0x00181ccc | `Matrix43 Matrix44::operator Matrix43()` | Calls Copy44To43 |
| operator Matrix44 | 0x00181d5c | `Matrix44 Matrix43::operator Matrix44()` | Calls Copy43To44 |
| LookAt43 | 0x0019e82c | `static LookAt43(eye, target, up, out)` | Standard view matrix |

Copy43To44 detail:
```c
void Matrix43::Copy43To44(Matrix44& out) {
    out[0] = { data[0][0], data[0][1], data[0][2], 0.0f };
    out[1] = { data[1][0], data[1][1], data[1][2], 0.0f };
    out[2] = { data[2][0], data[2][1], data[2][2], 0.0f };
    out[3] = { data[3][0], data[3][1], data[3][2], 1.0f };
}
```

### Usage in Engine

- MortarCamera stores `m_localToWorld` (+0x04) and `m_viewMatrix` (+0x74) as Matrix43
- `MatrixManager::SetupLookAt` takes `Matrix43*` output, then converts to Matrix44 via Copy43To44 before uploading to the View stack
- All cast operators use ARM struct-return convention (r0 = return storage pointer)

---

## See Also

- [Camera](camera.md) — MortarCamera/FruitCamera use Matrix43 for view matrices
- [Rendering system](../systems/rendering.md) — How matrices flow through the render pipeline
- [HUD structs](../structs/hud.md) — HUDControl/HUDControl3d use the HUD draw pipeline pattern
