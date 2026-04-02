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
| +0x10c | Vec3 | field12_0x10c | Frustum / clip data |
| +0x11c | float | m_nearX | Init param3 |
| +0x120 | float | m_nearY | Init param4 |
| +0x124 | float | m_fovOrNear | Init param1; default = 1.0f |
| +0x128 | float | m_farPlane | Init param2 |

---

## FruitCamera : MortarCamera (size = 0x16c)

MortarCamera base = 0x12c bytes.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x12c | MortarCameraFns* | m_pFollowFns | Null = idle mode |
| +0x130 | int | m_CameraMode | 0 = idle; 1 = follow |
| +0x134 | ushort | m_field3_0x134 | Cast to float for +0x150 |
| +0x136 | ushort | m_field4_0x136 | Cast to float for +0x14c |
| +0x138 | Vec2 | m_ShakeDir | Shake direction |
| +0x140 | ushort | m_ShakeAngle | Atan2Idx result from CreateCameraShake |
| +0x144 | float | m_TargetX | Follow target X |
| +0x148 | float | m_TargetY | Follow target Y |
| +0x14c | float | m_field14c | = (float)m_field4_0x136 each UpdateCamera |
| +0x150 | float | m_field150 | = (float)m_field3_0x134 |
| +0x154 | float | m_DistanceMag | \|pos - someTarget\| magnitude |
| +0x158 | Vec3 | m_LookAtSnapshot | lookAt saved each UpdateCamera |
| +0x164 | float | m_ShakeIntensity | Shake amplitude |
| +0x168 | MortarCameraFns* | m_field168 | Set in CreateCameraShake |

**Key methods:** FruitCamera (0x180de0), UpdateCamera (0x180c8c), UpdateShake (0x180ea0), CreateCameraShake (0x180d10), SetupPerspective (0x1810ac), FollowEntity (0x180b2c)

---

## See Also

- [Rendering functions](../functions/rendering.md) -- GameDraw camera setup
