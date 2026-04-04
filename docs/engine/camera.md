# Camera Structs

## Mortar::MortarCamera (size = 0x12c)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x000 | MortarCameraFns* | fns | Vtable-like function ptr |
| +0x004 | Matrix43 | m_localToWorld | 48 bytes |
| +0x034 | Matrix44 | m_projection | 64 bytes |
| +0x074 | Matrix43 | m_viewMatrix | 48 bytes |
| +0x0a4 | Matrix44 | m_field4 | 64 bytes |
| +0x0e4 | Vec3 | m_pos | Camera world position |
| +0x0f0 | Vec3 | m_lookAt | Look-at target point |
| +0x0fc | Vec3 | m_up | Up vector |
| +0x108 | byte | m_bDirty | = 1 when pos/lookAt changed |
| +0x109 | byte | field_0x109 | = 1 after Init |
| +0x10c | Rect | m_viewportRect | 16 bytes; cached viewport dimensions |
| +0x11c | float | m_nearX | Init param3 |
| +0x120 | float | m_nearY | Init param4 |
| +0x124 | float | m_fovOrNear | Init param1; default = 1.0f |
| +0x128 | float | m_farPlane | Init param2 |

---

## FruitCamera : MortarCamera (size = 0x16c / 364 bytes)

MortarCamera base = 0x12c bytes. FruitCamera own fields follow.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x12c | int | m_pFollowEntity | Pointer to followed entity; 0 = idle mode |
| +0x130 | int | m_CameraMode | 0 = idle; 1 = follow |
| +0x134 | ushort | m_field134 | Cast to float for +0x150 each frame |
| +0x136 | ushort | m_field136 | Cast to float for +0x14c each frame |
| +0x138 | float | m_ShakeDir_x | Shake direction X (computed from impact angle) |
| +0x13c | float | m_ShakeDir_y | Shake direction Y |
| +0x140 | ushort | m_ShakeAngle | Atan2Idx result from CreateCameraShake |
| +0x144 | float | m_TargetX | Follow target X; init from global config |
| +0x148 | float | m_TargetY | Follow target Y; init from global config |
| +0x14c | float | m_field14c | = (float)m_field136 each UpdateCamera |
| +0x150 | float | m_field150 | = (float)m_field134 |
| +0x154 | float | m_DistanceMag | `|pos - target|` magnitude |
| +0x158 | Vec3 | m_LookAtSnapshot | lookAt saved each UpdateCamera |
| +0x164 | float | m_ShakeIntensity | Shake amplitude; decays over time |
| +0x168 | float | m_ShakeIntensityInit | Initial shake intensity (set by CreateCameraShake) |

### Constructor (0x00180de0)

```c
FruitCamera::FruitCamera() {
    MortarCamera::MortarCamera();  // init base class
    m_pFollowEntity = 0;
    m_CameraMode = 0;               // idle
    m_field134 = 0;
    m_field136 = 0;
    m_ShakeIntensity = DAT_initial;  // global config value

    // Target position from global config Vec2
    float *cfg = globalCameraConfig;
    m_TargetX = cfg[0];
    m_TargetY = cfg[1];
    m_ShakeDir_x = cfg[0];
    m_ShakeDir_y = cfg[1];
}
```

### UpdateCamera (0x00180c8c)

Called each frame to update camera state:

```c
float FruitCamera::UpdateCamera(float dt) {
    // Convert ushort angle fields to float for rendering
    m_field14c = (float)m_field136;
    m_field150 = (float)m_field134;

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

Empty function -- camera stays fixed in idle mode. The menu/gameplay camera position is entirely controlled by `SetupOrtho`.

### UpdateFollow (0x00180c50)

Camera follows an entity:

```c
void FruitCamera::UpdateFollow(float dt) {
    if (m_pFollowEntity == 0) {
        IdleCamera();  // no target, go idle
        return;
    }

    // Save old lookAt delta
    Vec3 oldDelta = dt - m_lookAt;

    // Update lookAt to entity position
    int entity = m_pFollowEntity;
    m_lookAt = Vec3(entity->pos_x, entity->pos_y, entity->pos_z);

    // Shift camera position by same delta
    m_pos -= oldDelta;
}
```

### IdleCamera (0x00180a20)

```c
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;
    m_CameraMode = 0;
}
```

### CreateCameraShake (0x00180d10)

`void __thiscall FruitCamera::CreateCameraShake(FruitCamera *this, Vec3 *impact, float intensity, float dirScale)`

Initiates a camera shake from an impact point:

```c
void FruitCamera::CreateCameraShake(Vec3 *impact, float intensity, float dirScale) {
    // Compute shake direction from impact position
    m_ShakeAngle = Atan2Idx(impact->y, impact->x);

    // Set shake direction vector (magnitude = 9.0 * dirScale)
    m_ShakeDir_x = Cos(m_ShakeAngle) * 9.0f;
    m_ShakeDir_y = Sin(m_ShakeAngle) * 9.0f;
    Vec2Scale(&m_ShakeDir, dirScale);  // *= dirScale

    // Set intensity
    m_ShakeIntensityInit = intensity;  // +0x168
    m_ShakeIntensity = intensity;      // +0x164, will decay each frame
}
```

Called from:
- `HitBomb()`: `CreateCameraShake(bombPos, DAT_intensity, 2.0f)` -- bomb explosion
- `Bomb::CollisionResponse` (zen mode): `CreateCameraShake(bombPos, 2.0f, 3.0f)` -- lighter shake

### SetupOrtho (0x0019edfc)

`void MortarCamera::SetupOrtho()` -- called during render setup:

```c
void MortarCamera::SetupOrtho() {
    Rect viewport = DisplayManager::GetViewport();

    if (!field_0x109 && viewport matches cached) {
        // Reuse cached matrices
        SetMatrixByIndex(m_viewMatrix, VIEW);
        SetMatrixByIndex(m_projection, PROJECTION);
    } else {
        // Recompute: LookAt + Ortho
        MatrixManager::SetupLookAt(eyePos, lookAtPos, upVec, NULL);
        m_viewMatrix = matrixMgr.m_View;

        // Ortho: half-viewport extents
        int halfH = viewport.bottom / 2;  // 160
        int halfW = viewport.right / 2;   // 240
        MatrixManager::SetupOrtho(halfH, -halfH, -halfW, halfW, -1.0f, DAT_far);
        // -> SetupOrtho(160, -160, -240, 240, -1.0, farPlane)

        m_projection = matrixMgr.m_Projection;
    }

    // Cache viewport
    m_viewportRect = viewport;
    ResetMatrix();
}
```

This confirms the centered ortho coordinate system:
- Top = +160, Bottom = -160 (320 units height)
- Left = -240, Right = +240 (480 units width)
- Near = -1.0, Far = DAT (large positive)

---

## Camera Shake System

Camera shake is initiated by `CreateCameraShake` and applied each frame by `UpdateShake`.

### UpdateShake (0x00180ea0)

`void __thiscall FruitCamera::UpdateShake(FruitCamera *this, float dt)`

```c
void FruitCamera::UpdateShake(float dt) {
    if (m_ShakeIntensity <= 0.0f) {
        // No active shake: decay target back to zero
        // For each axis (X and Y):
        if (abs(m_TargetX) <= threshold) {
            m_TargetX = 0.0f;     // snap to zero when close
        } else {
            m_TargetX *= dampFactor;  // DAT ~0.9, gradual decay
        }
        // Same for m_TargetY
    } else {
        // Active shake
        m_ShakeIntensity -= dt;  // intensity decays linearly

        // Check if target is close enough to shake direction
        Vec2 delta = m_TargetXY - m_ShakeDir;
        if (MagnitudeSqr(delta) < 16.0f) {
            // Randomize shake angle (jitter via RNG)
            m_ShakeAngle += 0x6388 + Random(0x38E0);  // ~+140-230 degrees

            // Scale by remaining intensity ratio
            float scale = (m_ShakeIntensity / m_ShakeIntensityInit) * 9.0f;
            m_ShakeDir_x = Cos(m_ShakeAngle) * scale;
            m_ShakeDir_y = Sin(m_ShakeAngle) * scale;
        }

        // Move target toward shake direction
        Vec2 diff = m_ShakeDir - m_TargetXY;
        float t = m_ShakeIntensity / m_ShakeIntensityInit + 1.0f;
        m_TargetXY += diff * DAT_lerp * t;

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

3. **Decay**: When `m_ShakeIntensity <= 0`, target damps back to (0,0) via multiplicative factor

### Shake Triggers
- **Bomb hit (Classic/Arcade)**: `HitBomb` -> `CreateCameraShake(pos, DAT_intensity, 2.0f)`
- **Bomb hit (Zen)**: `CollisionResponse` -> `CreateCameraShake(pos, 2.0f, 3.0f)`

---

## Key Methods Summary

| Function | Address | Purpose |
|----------|---------|---------|
| FruitCamera() | 0x00180de0 | Constructor |
| ~FruitCamera() | 0x00180d6c | Destructor |
| UpdateCamera | 0x00180c8c | Per-frame update dispatch |
| UpdateIdle | 0x00180a08 | Idle mode (empty) |
| UpdateFollow | 0x00180c50 | Follow entity mode |
| IdleCamera | 0x00180a20 | Switch to idle mode |
| CreateCameraShake | 0x00180d10 | Start shake from impact |
| UpdateShake | 0x00180ea0 | Apply shake per frame; decays intensity, randomizes angle |
| SetupOrtho | 0x0019edfc | Setup ortho projection from viewport |
| ZeroInit_FruitCamera | 0x0017ff64 | `*param = 0` (singleton init) |

---

## See Also

- [Rendering functions](rendering-functions.md) -- GameDraw camera setup
- [Rendering pipeline](rendering-pipeline.md) -- Full render loop
