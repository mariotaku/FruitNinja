#ifndef MORTAR_CAMERA_H
#define MORTAR_CAMERA_H

//
// MortarCamera (0x12C / 300 bytes)
// Vtable: 15 entries (full layout RE'd).
//

#include "math/Matrix44.h"
#include "math/Matrix43.h"
#include "math/Vec3.h"
#include "core/MortarTypes.h"

namespace Mortar {

class MortarCamera {
public:
    // +0x04: cached view (perspective path, stored as Matrix43)
    Matrix43 m_localToWorld;

    // +0x34: cached projection (perspective path)
    Matrix44 m_projection;

    // +0x74: cached view (ortho path, stored as Matrix43)
    Matrix43 m_viewMatrix;

    // +0xA4: cached projection (ortho path)
    Matrix44 m_projOrtho;

    // +0xE4
    Vec3 m_pos;

    // +0xF0
    Vec3 m_lookAt;

    // +0xFC
    Vec3 m_up;

    // +0x108: = 1 when pos/lookAt changed
    bool m_bDirty;

    // +0x109: = 1 after Init; forces first recompute
    bool m_bInitialized;

    // +0x10C: cached viewport for change detection
    MortarRectangle m_viewportRect;

    // +0x11C
    float m_fovX;

    // +0x120
    float m_fovY;

    // +0x124: near plane (default 1.0)
    float m_fovOrNear;

    // +0x128: far plane (default 1000.0)
    float m_farPlane;

    // Binary @ 0x00257d6c (C1/C2 ctor)
    MortarCamera();
    virtual ~MortarCamera() {}

    // Vtable slot 2
    virtual void Init(float fovOrNear, float farPlane, float fovX, float fovY);

    // Vtable slot 3 — empty in base, overridden by FruitCamera
    virtual void UpdateCamera(float dt);

    // Vtable slot 4, binary @ 0x00257aac — LookAt + perspective projection
    virtual void SetupPerspective();

    // Vtable slot 5, binary @ 0x00257758 — LookAt + ortho from viewport
    virtual void SetupOrtho();

    // DIFFERS: v1.6.1 binary @ 0x001ee9b4 does m_fovX / m_fovY unprotected (no zero guard).
    // Port adds defensive zero-check.
    // Vtable slot 6
    float GetAspectRatio() const { return (m_fovY != 0.0f) ? m_fovX / m_fovY : 1.0f; }

    // Vtable slots 7-8
    float GetFOVx() const { return m_fovX; }
    float GetFOVy() const { return m_fovY; }

    // Vtable slots 9-14: setters mark dirty, getters return copies
    void SetLookAt(const Vec3& v) { m_bDirty = true; m_lookAt = v; }
    Vec3 GetLookAt() const { return m_lookAt; }
    void SetPos(const Vec3& v) { m_bDirty = true; m_pos = v; }
    Vec3 GetPos() const { return m_pos; }
    void SetUp(const Vec3& v) { m_bDirty = true; m_up = v; }
    Vec3 GetUp() const { return m_up; }
};

} // namespace Mortar

#endif
