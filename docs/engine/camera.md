# Camera Structs

## Mortar::MortarCamera (size = 0x12C / 300 bytes)

Base camera class in the Mortar engine. Provides LookAt-based view setup with both perspective and orthographic projection paths. Caches separate matrix pairs for each path to avoid recomputation.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x000 | 4 | MortarCameraFns* | fns | Vtable pointer (15 virtual methods) |
| +0x004 | 48 | Matrix43 | m_localToWorld | Cached view result (perspective path) |
| +0x034 | 64 | Matrix44 | m_projection | Cached projection matrix (perspective path) |
| +0x074 | 48 | Matrix43 | m_viewMatrix | Cached view result (ortho path) |
| +0x0A4 | 64 | Matrix44 | m_projOrtho | Cached projection matrix (ortho path) |
| +0x0E4 | 12 | Vec3 | m_pos | Camera world position |
| +0x0F0 | 12 | Vec3 | m_lookAt | Look-at target point |
| +0x0FC | 12 | Vec3 | m_up | Up vector |
| +0x108 | 1 | byte | m_bDirty | = 1 when pos/lookAt changed; triggers matrix recompute |
| +0x109 | 1 | byte | m_bInitialized | = 1 after Init(); forces first recompute |
| +0x10A | 2 | -- | (padding) | |
| +0x10C | 16 | MortarRectangle | m_viewportRect | Cached viewport dimensions for change detection |
| +0x11C | 4 | float | m_fovX | FOV X component; GetFOVx() returns this |
| +0x120 | 4 | float | m_fovY | FOV Y component; GetFOVy() returns this |
| +0x124 | 4 | float | m_fovOrNear | Near plane (perspective); default = 1.0 |
| +0x128 | 4 | float | m_farPlane | Far plane; default = 1000.0 |

**Key insight**: The 224-byte "gap" (+0x04 to +0xE3) is four cached matrices:
- Two pairs of (view Matrix43 + projection Matrix44), one for perspective, one for ortho
- Each pair: 48 + 64 = 112 bytes, two pairs = 224 bytes

### Constructor Defaults (0x0019eb14)

```c
MortarCamera::MortarCamera() {
    fns = &MortarCamera_vtable;
    m_bDirty = 1;
    m_bInitialized = 1;           // force first recompute
    m_lookAt = Vec3(0, 0, 0);     // from global constant (all zeros)
    m_pos = Vec3(0, 0, 1);        // via MakeVec3ZFirst(0, 1.0)
    m_up = Vec3(0, 1, 0);         // via MakeVec3ZFirst(1.0, 0)
    m_fovX = 0.0;
    m_fovY = 0.0;
    m_fovOrNear = 1.0;
    m_farPlane = 1000.0;          // DAT_0019ebd4 = 0x447a0000
    Identity43(&m_localToWorld);   // perspective view cache
    Identity44(&m_projection);     // perspective projection cache
    Identity43(&m_viewMatrix);     // ortho view cache
    Identity44(&m_projOrtho);      // ortho projection cache
    m_viewportRect = {uninitialized stack values};
}
```

### Vtable (MortarCameraFns, 15 entries)

Base vtable at 0x001eb548 (data starts at +8 from RTTI header at 0x001eb540).

| Slot | Address | Prototype | Notes |
|------|---------|-----------|-------|
| 0 | 0x0019e9d4 | `~MortarCamera()` | Non-deleting destructor |
| 1 | 0x0019ea08 | `~MortarCamera()` | Deleting destructor (calls operator_delete) |
| 2 | 0x0019e9f0 | `void Init(float fovOrNear, float farPlane, float fovX, float fovY)` | Sets FOV/near/far, marks initialized |
| 3 | 0x0019ef6c | `void UpdateCamera(float dt)` | Empty in base class (virtual) |
| 4 | 0x0019ece4 | `void SetupPerspective()` | LookAt + perspective projection |
| 5 | 0x0019edfc | `void SetupOrtho()` | LookAt + ortho projection from viewport |
| 6 | 0x00181b90 | `float GetAspectRatio()` | Returns m_fovX / m_fovY |
| 7 | 0x00181ba0 | `float GetFOVx()` | Returns m_fovX (+0x11C) |
| 8 | 0x00181ba8 | `float GetFOVy()` | Returns m_fovY (+0x120) |
| 9 | 0x00181bb0 | `void SetLookAt(Vec3*)` | Sets m_lookAt, marks dirty |
| 10 | 0x00181c18 | `Vec3 GetLookAt()` | Returns copy of m_lookAt |
| 11 | 0x00181bc8 | `void SetPos(Vec3*)` | Sets m_pos, marks dirty |
| 12 | 0x00181c08 | `Vec3 GetPos()` | Returns copy of m_pos |
| 13 | 0x00181be0 | `void SetUp(Vec3*)` | Sets m_up, marks dirty |
| 14 | 0x00181bf8 | `Vec3 GetUp()` | Returns copy of m_up |

### Init (vtable slot 2, 0x0019e9f0)

```c
void MortarCamera::Init(float fovOrNear, float farPlane, float fovX, float fovY) {
    m_fovOrNear = fovOrNear;  // +0x124
    m_farPlane = farPlane;     // +0x128
    m_fovX = fovX;             // +0x11C
    m_fovY = fovY;             // +0x120
    m_bInitialized = 1;        // +0x109, force recompute
}
```

### SetupPerspective (vtable slot 4, 0x0019ece4)

Uses LookAt from m_pos/m_up/m_lookAt, then perspective projection with FOV-based parameters.

```c
void MortarCamera::SetupPerspective() {
    if (m_bDirty == 0) {
        // Reuse perspective cache
        Matrix44 tmp; Cast43to44(&tmp, &m_localToWorld);
        SetMatrixByIndex(&tmp, VIEW);        // slot 1
        SetMatrixByIndex(&m_projection, PROJECTION); // slot 0
    } else {
        // Recompute
        MatrixManager* mgr = g_MatrixManager;
        mgr->SetupLookAt(&m_pos, &m_up, &m_lookAt, NULL);
        m_localToWorld = Cast44to43(mgr->m_View.m_Current);  // cache

        // Perspective from FOV
        // 182.0 is the angle-to-index conversion constant (same as HUDControl3d)
        float sinFov = SinIdx((ushort)(int)(182.0f * m_fovY));
        float cosFov = CosIdx((ushort)(int)(182.0f * m_fovY));
        float aspect = m_fovX / m_fovY;
        mgr->SetupPerspective(sinFov, cosFov, aspect, m_fovOrNear, m_farPlane, NULL);
        m_projection = mgr->m_Projection.m_Current;  // cache
    }
    ResetMatrix();
}
```

**FOV interpretation**: m_fovY is the vertical FOV angle (degrees). The SinIdx/CosIdx with 182.0 multiplier converts degrees to the engine's 16-bit angle index. m_fovX stores the horizontal FOV for aspect ratio computation.

### SetupOrtho (vtable slot 5, 0x0019edfc)

Uses viewport dimensions to create symmetric orthographic projection.

```c
void MortarCamera::SetupOrtho() {
    Rect viewport;
    DisplayManager::GetInstance()->GetWindowSize(&viewport);

    if (m_bInitialized == 0) {
        // Check if viewport changed from cached
        if (Height(viewport) == Height(m_viewportRect) ||
            Width(viewport) == Width(m_viewportRect)) {
            // Reuse ortho cache
            Matrix44 tmp; Cast43to44(&tmp, &m_viewMatrix);
            SetMatrixByIndex(&tmp, VIEW);
            SetMatrixByIndex(&m_projOrtho, PROJECTION);
            goto cache_viewport;
        }
    }

    // Recompute
    Vec3 eye = MakeVec3ZFirst(0, 1.0);   // (0, 0, 1)
    Vec3 up  = MakeVec3ZFirst(1.0, 0);   // (0, 1, 0)
    mgr->SetupLookAt(&eye, &up, g_origin, NULL);
    m_viewMatrix = Cast44to43(mgr->m_View.m_Current);

    int halfH = viewport.bottom / 2;   // 160
    int halfW = viewport.right / 2;    // 240
    mgr->SetupOrtho((float)halfH, (float)-halfH,
                    (float)-halfW, (float)halfW,
                    -1.0f, 1000.0f, NULL);
    // -> SetupOrtho(160, -160, -240, 240, -1.0, 1000.0)
    m_projOrtho = mgr->m_Projection.m_Current;

cache_viewport:
    m_viewportRect = viewport;
    ResetMatrix();
}
```

### Getter/Setter Details

All setters (SetPos, SetLookAt, SetUp) set `m_bDirty = 1` before writing the new value.
All getters (GetPos, GetLookAt, GetUp) return by value (copy Vec3 to caller-provided output pointer via ARM hidden return register r0).

```c
float GetAspectRatio() { return m_fovX / m_fovY; }  // +0x11C / +0x120
float GetFOVx()        { return m_fovX; }            // +0x11C
float GetFOVy()        { return m_fovY; }            // +0x120
```

---

## FruitCamera : MortarCamera (size = 0x16C / 364 bytes)

Game-specific camera extending MortarCamera. Adds camera shake, entity following, and a 4-type ortho perspective system for standard/multiplayer views.

### Struct Layout

MortarCamera base = 0x12C (300) bytes. FruitCamera own fields follow.

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x000 | 300 | MortarCamera | base | Base class |
| +0x12C | 4 | int* | m_pFollowEntity | Entity pointer for follow mode; 0 = none |
| +0x130 | 4 | int | m_CameraMode | 0 = idle, 1 = follow |
| +0x134 | 2 | ushort | field_0x134 | Angle ushort; cast to float -> field_0x150 |
| +0x136 | 2 | ushort | field_0x136 | Angle ushort; cast to float -> field_0x14c |
| +0x138 | 4 | float | m_ShakeDir_x | Shake direction X (from impact angle) |
| +0x13C | 4 | float | m_ShakeDir_y | Shake direction Y |
| +0x140 | 2 | ushort | m_ShakeAngle | Atan2Idx result from CreateCameraShake |
| +0x142 | 2 | -- | (padding) | |
| +0x144 | 4 | float | m_TargetX | Camera target X; from global config; shake modifies |
| +0x148 | 4 | float | m_TargetY | Camera target Y; from global config; shake modifies |
| +0x14C | 4 | float | field_0x14c | = (float)field_0x136 each UpdateCamera frame |
| +0x150 | 4 | float | field_0x150 | = (float)field_0x134 each UpdateCamera frame |
| +0x154 | 4 | float | m_DistanceMag | |pos - target| magnitude, computed each frame |
| +0x158 | 12 | Vec3 | m_LookAtSnapshot | lookAt saved each UpdateCamera frame |
| +0x164 | 4 | float | m_ShakeIntensity | Current shake amplitude; decays by dt each frame |
| +0x168 | 4 | float | m_ShakeIntensityInit | Initial shake intensity (set by CreateCameraShake) |

### Vtable (FruitCamera, 15 entries)

Vtable at 0x001ea620 (data starts at +8 from RTTI header at 0x001ea618).

| Slot | Address | Override? | Function |
|------|---------|-----------|----------|
| 0 | 0x00180d6c | YES | ~FruitCamera() |
| 1 | 0x00180db4 | YES | ~FruitCamera() (deleting) |
| 2 | 0x0019e9f0 | inherited | MortarCamera::Init |
| 3 | 0x00180c8c | **YES** | FruitCamera::UpdateCamera |
| 4 | 0x0019ece4 | inherited | MortarCamera::SetupPerspective |
| 5 | 0x0019edfc | inherited | MortarCamera::SetupOrtho |
| 6-14 | (same) | inherited | GetAspectRatio/GetFOVx/GetFOVy/SetLookAt/GetLookAt/SetPos/GetPos/SetUp/GetUp |

Only slot 3 (UpdateCamera) is overridden with game logic. Slots 4-5 are inherited from MortarCamera.
The game-specific `FruitCamera::SetupPerspective(perspType, forceUpdate)` at 0x001810ac is a **separate non-virtual method** called directly from GameDraw, not a vtable override.

### Constructor (0x00180e40, also at 0x00180de0)

<!-- Analysed: 2026-04-06T00:45 -->

```c
FruitCamera::FruitCamera() {
    MortarCamera::MortarCamera();              // init base class (300 bytes)
    base.fns = &MortarCameraFns_001ea620;      // FruitCamera vtable

    field_0x136 = 0;                           // +0x136, angle ushort
    m_pFollowEntity = NULL;                    // +0x12C
    m_Target.x = _Vector2<float>::Zero.x;      // +0x144, = 0.0
    m_Target.y = _Vector2<float>::Zero.y;      // +0x148, = 0.0
    m_ShakeIntensity = 0.0f;                   // +0x164
    m_CameraMode = 0;                          // +0x130, idle
    field_0x134 = 0;                           // +0x134, angle ushort
    m_ShakeDir.x = _Vector2<float>::Zero.x;    // +0x138, = 0.0
    m_ShakeDir.y = _Vector2<float>::Zero.y;    // +0x13C, = 0.0
}
```

Note: `_Vector2<float>::Zero` is a static `_Vector2<float>` in BSS initialized to `(0.0, 0.0)` by `_GLOBAL__I_FruitCamera.cpp` (0x00181870). The field assignment order matches the binary exactly — the compiler interleaves reads from the global with writes to struct fields. Both `m_Target` and `m_ShakeDir` start at the origin; the shake system later modifies `m_ShakeDir` while `m_Target` lerps toward it.

### Destructors

```c
~FruitCamera() {               // 0x00180d6c (non-deleting)
    base.fns = &FruitCamera_vtable;  // restore vtable for correct dtor chain
    MortarCamera::~MortarCamera();
}

~FruitCamera() {               // 0x00180db4 (deleting)
    base.fns = &FruitCamera_vtable;
    MortarCamera::~MortarCamera();
    operator_delete(this);
}
```

### UpdateCamera (0x00180c8c) -- vtable slot 3 override

```c
float FruitCamera::UpdateCamera(float dt) {
    // Convert ushort angle fields to float for rendering
    field_0x14c = (float)field_0x136;
    field_0x150 = (float)field_0x134;

    // Compute distance to target
    Vec3 delta = m_pos - someTarget;
    m_DistanceMag = Magnitude(delta);

    // Snapshot current lookAt
    m_LookAtSnapshot = m_lookAt;

    // Dispatch by mode
    if (m_CameraMode == 0)
        return UpdateIdle(dt);
    else if (m_CameraMode == 1)
        UpdateFollow(dt);
}
```

### UpdateIdle (0x00180a08)

Empty function -- camera stays fixed in idle mode. The menu/gameplay camera position is entirely controlled by `SetupOrtho` or `FruitCamera::SetupPerspective`.

### UpdateFollow (0x00180c50)

```c
void FruitCamera::UpdateFollow(float dt) {
    if (m_pFollowEntity == 0) {
        IdleCamera();       // no target, go idle
        return;
    }

    // Compute lookAt delta before update
    Vec3 delta = dt_vec - m_lookAt;   // dt reinterpreted as Vec3 (ARM calling convention artifact)

    // Update lookAt to entity position
    Entity *entity = m_pFollowEntity;
    m_lookAt.x = entity->pos_x;      // entity+0x10
    m_lookAt.y = entity->pos_y;      // entity+0x14
    m_lookAt.z = entity->pos_z;      // entity+0x18

    // Shift camera position by same delta
    m_pos -= delta;
}
```

### IdleCamera (0x00180a20)

```c
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;   // +0x12C
    m_CameraMode = 0;      // +0x130
}
```

---

## FruitCamera::SetupPerspective (0x001810ac) -- Non-virtual, 4-type dispatch

`void __thiscall FruitCamera::SetupPerspective(int perspType, int forceUpdate)`

Called directly from GameDraw as `FruitCamera::SetupPerspective(camera, 0, 0)`. NOT a vtable override -- it is a separate method with different signature from the base class virtual.

### Cache path (all types)

When `m_bDirty == 0` AND `forceUpdate == 0`:
```c
// Reuse cached perspective matrices -- skip recomputation
Matrix44 tmp; Cast43to44(&tmp, &m_localToWorld);
SetMatrixByIndex(&tmp, VIEW);            // view from cache
SetMatrixByIndex(&m_projection, PROJECTION); // projection from cache
```

### perspType dispatch

| Type | Usage | LookAt eye | LookAt up | LookAt target | Ortho params |
|------|-------|-----------|-----------|---------------|--------------|
| 0 | Standard (GameDraw) | (targetX, targetY, 0) + (0,0,1) | (0, 1, 0) | (targetX, targetY, 0) | (160, -160, -240, 240) |
| 1 | Multiplayer P1 | (-targetY, targetX, 0) + (0,0,1) | (1, 0, 0) | (-targetY, targetX, 0) | (-240, 240, 160, -160) SWAPPED |
| 2 | Multiplayer P2 | (targetY, -targetX, 0) + (0,0,1) | (1, 0, 0) | (targetY, -targetX, 0) | (240, -240, -480, 160) SWAPPED |
| 3 | Alternate | (0, 0, 1) | (0, 1, 0) | g_origin | (160, -160, -240, 240) same as 0 |

### Ortho Constants (verified from literal pool at 0x001813d8)

| Address | Hex | Float | Name |
|---------|-----|-------|------|
| 0x1813d8 | 0x43200000 | 160.0 | ORTHO_X_LEFT |
| 0x1813dc | 0xC3200000 | -160.0 | ORTHO_X_RIGHT |
| 0x1813e0 | 0xC3700000 | -240.0 | ORTHO_Y_BOTTOM |
| 0x1813e4 | 0x43700000 | 240.0 | ORTHO_Y_TOP |
| 0x1813e8 | 0xC3F00000 | -480.0 | ORTHO_ALT_BOUND |
| 0x1813ec | 0x44FA0000 | 2000.0 | ORTHO_NEAR |
| 0x1813f0 | 0xC5BB8000 | -6000.0 | ORTHO_FAR |

### perspType == 0 (standard) -- full pseudocode

```c
// Eye position: camera at (0,0,1) + target offset
Vec3 eye(0.0, 0.0, 1.0);
Vec3 targetOffset(m_TargetX, m_TargetY, 0.0);
eye = targetOffset + eye;  // = (m_TargetX, m_TargetY, 1.0)

Vec3 up(0.0, 1.0, 0.0);
Vec3 lookDir(m_TargetX, m_TargetY, 0.0);

mgr->SetupLookAt(eye, up, lookDir);
m_localToWorld = Cast44to43(mgr->m_View.m_Current);

mgr->SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f, NULL);

m_projection = mgr->m_Projection.m_Current;
```

### perspType == 1 (Multiplayer P1) -- rotated 90 deg CW

```c
Vec3 eye(-m_TargetY, m_TargetX, 0.0);
// ... + (0,0,1)
Vec3 up(1.0, 0.0, 0.0);
mgr->SetupLookAt(eye + Vec3(0,0,1), up, Vec3(-m_TargetY, m_TargetX, 0));
m_localToWorld = Cast44to43(mgr->m_View.m_Current);

// Ortho: axes swapped for rotation
mgr->SetupOrtho(-240.0f, 240.0f, 160.0f, -160.0f, 2000.0f, -6000.0f, NULL);
m_projection = mgr->m_Projection.m_Current;
```

### perspType == 2 (Multiplayer P2) -- rotated 90 deg CCW

```c
Vec3 eye(m_TargetY, -m_TargetX, 0.0);
// ... + (0,0,1)
Vec3 up(1.0, 0.0, 0.0);
mgr->SetupLookAt(eye + Vec3(0,0,1), up, Vec3(m_TargetY, -m_TargetX, 0));
m_localToWorld = Cast44to43(mgr->m_View.m_Current);

// Ortho with -480 alternate bound
mgr->SetupOrtho(240.0f, -240.0f, -480.0f, 160.0f, 2000.0f, -6000.0f, NULL);
m_projection = mgr->m_Projection.m_Current;
```

### perspType == 3 (Alternate)

```c
Vec3 eye(0.0, 0.0, 1.0);
Vec3 up(0.0, 1.0, 0.0);
mgr->SetupLookAt(eye, up, g_origin);
// Same ortho as type 0
mgr->SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f, NULL);
```

### Ortho constant analysis

The ortho params `(160, -160, -240, 240)` for type 0 create:
- X range: top - bottom = 160 - (-160) = **320 units** (physical screen height)
- Y range: right - left = 240 - (-240) = **480 units** (physical screen width)

This is a **symmetric 320x480 centered ortho**, matching the 480x320 landscape game in portrait coordinate space.

### Port implications

The Bada binary's ortho params are designed for portrait-device-landscape-game rendering. The SDL port
runs landscape-native, so the ortho should remain `SetupOrtho(160, -160, -240, 240, 2000, -6000)`.
Combined with HUDControl3d's Vec3(480, 320, 0) offset, HUD elements are positioned correctly.

---

## Camera Shake System

Camera shake is initiated by `CreateCameraShake` and applied each frame by `UpdateShake`.

### CreateCameraShake (0x00180d10)

`void __thiscall FruitCamera::CreateCameraShake(Vec3 *impact, float intensity, float dirScale)`

```c
void FruitCamera::CreateCameraShake(Vec3 *impact, float intensity, float dirScale) {
    m_ShakeAngle = Atan2Idx(impact->y, impact->x);

    m_ShakeDir_x = CosIdx(m_ShakeAngle) * 9.0f;
    m_ShakeDir_y = SinIdx(m_ShakeAngle) * 9.0f;
    Vec2Scale(&m_ShakeDir, dirScale);  // *= dirScale

    m_ShakeIntensityInit = intensity;  // +0x168
    m_ShakeIntensity = intensity;      // +0x164, will decay each frame
}
```

Called from:
- `HitBomb()`: `CreateCameraShake(bombPos, DAT_intensity, 2.0f)` -- bomb explosion
- `Bomb::CollisionResponse` (zen mode): `CreateCameraShake(bombPos, 2.0f, 3.0f)` -- lighter shake

### UpdateShake (0x00180ea0)

`void __thiscall FruitCamera::UpdateShake(float dt)`

Constants (verified from literal pool at 0x00181068):

| Address | Hex | Float | Purpose |
|---------|-----|-------|---------|
| 0x181068 | 0x3E4CCCCD | 0.2 | Lerp factor for target movement |
| 0x18106c | 0xBC23D70A | -0.01 | Negative snap threshold |
| 0x181070 | 0x3C23D70A | 0.01 | Positive snap threshold |
| 0x181074 | 0x3F4CCCCD | 0.8 | Decay multiplier (no-shake dampening) |
| 0x181078 | 0x00000000 | 0.0 | Snap-to-zero value |

```c
void FruitCamera::UpdateShake(float dt) {
    if (m_ShakeIntensity <= 0.0f) {
        // No active shake: decay target back to zero
        for each axis (X, Y) {
            if (abs(target) <= 0.01f)
                target = 0.0f;        // snap to zero when close
            else
                target *= 0.8f;       // gradual decay
        }
    } else {
        // Active shake
        m_ShakeIntensity -= dt;        // intensity decays linearly

        // Check if target reached shake direction
        Vec2 delta = m_TargetXY - m_ShakeDir;
        if (MagnitudeSqr(delta) < 16.0f) {
            // Randomize shake angle (jitter via RNG)
            m_ShakeAngle += 0x6388 + Random(0x38E0);
            // ~140-230 degrees jump

            // Scale by remaining intensity ratio
            float scale = (m_ShakeIntensity / m_ShakeIntensityInit) * 9.0f;
            m_ShakeDir_x = CosIdx(m_ShakeAngle) * scale;
            m_ShakeDir_y = SinIdx(m_ShakeAngle) * scale;
        }

        // Move target toward shake direction
        Vec2 diff = m_ShakeDir - m_TargetXY;
        float t = m_ShakeIntensity / m_ShakeIntensityInit + 1.0f;
        m_TargetXY += diff * 0.2f * t;

        m_bDirty = 1;  // mark camera matrices for recalculation
    }
}
```

### Shake Flow

1. **Initiation**: `CreateCameraShake(impactPos, intensity, dirScale)`
   - Computes angle from impact position: `Atan2Idx(impact.y, impact.x)`
   - Sets initial shake direction: `9.0 * dirScale` magnitude along angle
   - Stores initial intensity for ratio calculations

2. **Per-frame (UpdateShake)**:
   - `m_ShakeIntensity` decreases linearly by `dt`
   - Shake angle randomized each time target reaches shake direction
   - Direction magnitude proportional to `(intensity / initialIntensity) * 9.0`
   - Camera target lerps toward shake direction, overshooting for oscillation

3. **Decay**: When `m_ShakeIntensity <= 0`, target damps back to (0,0) via 0.8x multiplier per frame

### Shake Triggers
- **Bomb hit (Classic/Arcade)**: `HitBomb` -> `CreateCameraShake(pos, DAT_intensity, 2.0f)`
- **Bomb hit (Zen)**: `CollisionResponse` -> `CreateCameraShake(pos, 2.0f, 3.0f)`

---

## OrthoW (0x00116c4c) -- Static utility

```c
void OrthoW(float top, float bottom, float left, float right,
            float near, float far, float w, Matrix44* out) {
    Identity44(out);
    out[3][3] = 1.0;
    float invTB = 1.0 / (top - bottom);
    float invRL = 1.0 / (right - left);
    out[3][2] = near / (near - far);
    out[2][2] = 1.0 / (far - near);
    out[3][0] = -((right + left) * invRL);
    out[3][1] = -((top + bottom) * invTB);
    out[0][0] = invRL + invRL;    // = 2/(right-left)
    out[1][1] = invTB + invTB;    // = 2/(top-bottom)
    // NOTE: 'w' parameter is unused (dead code)
}
```

---

## HUDControl3d::Draw constant pool (at 0x001443dc)

| Address | Value | Name |
|---------|-------|------|
| 0x1443dc | 182.0 | ROT_SPEED (rotation constant for SinIdx/CosIdx) |
| 0x1443e0 | **480.0** | HUD_SCREEN_WIDTH |
| 0x1443e4 | **320.0** | HUD_SCREEN_HEIGHT |
| 0x1443e8 | 0.0 | Z offset (always zero) |

---

## Key Methods Summary

| Function | Address | Class | Purpose |
|----------|---------|-------|---------|
| MortarCamera() | 0x0019eb14 | MortarCamera | Constructor (real) |
| MortarCamera() | 0x0019ea44 | MortarCamera | Constructor (variant) |
| ~MortarCamera() | 0x0019e9d4 | MortarCamera | Non-deleting destructor |
| ~MortarCamera() | 0x0019ea08 | MortarCamera | Deleting destructor |
| Init | 0x0019e9f0 | MortarCamera | Set FOV/near/far (virtual) |
| UpdateCamera | 0x0019ef6c | MortarCamera | Empty base (virtual) |
| SetupPerspective | 0x0019ece4 | MortarCamera | LookAt + perspective (virtual) |
| SetupOrtho | 0x0019edfc | MortarCamera | LookAt + ortho from viewport (virtual) |
| GetAspectRatio | 0x00181b90 | MortarCamera | fovX / fovY (virtual) |
| GetFOVx | 0x00181ba0 | MortarCamera | Returns m_fovX (virtual) |
| GetFOVy | 0x00181ba8 | MortarCamera | Returns m_fovY (virtual) |
| SetLookAt | 0x00181bb0 | MortarCamera | Sets m_lookAt + dirty (virtual) |
| GetLookAt | 0x00181c18 | MortarCamera | Returns m_lookAt copy (virtual) |
| SetPos | 0x00181bc8 | MortarCamera | Sets m_pos + dirty (virtual) |
| GetPos | 0x00181c08 | MortarCamera | Returns m_pos copy (virtual) |
| SetUp | 0x00181be0 | MortarCamera | Sets m_up + dirty (virtual) |
| GetUp | 0x00181bf8 | MortarCamera | Returns m_up copy (virtual) |
| FruitCamera() | 0x00180e40 | FruitCamera | Constructor (real) |
| FruitCamera() | 0x00180de0 | FruitCamera | Constructor (variant) |
| ~FruitCamera() | 0x00180d6c | FruitCamera | Non-deleting destructor |
| ~FruitCamera() | 0x00180d90 | FruitCamera | Destructor (variant) |
| ~FruitCamera() | 0x00180db4 | FruitCamera | Deleting destructor |
| UpdateCamera | 0x00180c8c | FruitCamera | Per-frame dispatch (override) |
| UpdateIdle | 0x00180a08 | FruitCamera | Idle mode (empty) |
| UpdateFollow | 0x00180c50 | FruitCamera | Follow entity mode |
| IdleCamera | 0x00180a20 | FruitCamera | Switch to idle mode |
| CreateCameraShake | 0x00180d10 | FruitCamera | Start shake from impact |
| UpdateShake | 0x00180ea0 | FruitCamera | Apply shake per frame |
| SetupPerspective | 0x001810ac | FruitCamera | 4-type ortho dispatch (NON-virtual) |
| ZeroInit_FruitCamera | 0x0017ff64 | FruitCamera | `*param = 0` (singleton init) |
| _GLOBAL__I_FruitCamera.cpp | 0x00181870 | FruitCamera | Static initializer |
| OrthoW | 0x00116c4c | (static) | Standard ortho matrix (w param unused) |

---

## See Also

- [Rendering functions](rendering-functions.md) -- GameDraw camera setup
- [Rendering pipeline](rendering-pipeline.md) -- Full render loop
