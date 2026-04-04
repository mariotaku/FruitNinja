#ifndef MORTAR_CAMERA_H
#define MORTAR_CAMERA_H

#include "math/Matrix44.h"
#include "math/Vec3.h"

namespace Mortar {

// Matches original MortarCamera (0x12C bytes)
// Base camera with ortho/perspective projection setup
class MortarCamera {
public:
    Matrix44 m_localToWorld;     // +0x04 (48 bytes used as Matrix43)
    Matrix44 m_projection;       // +0x34
    Matrix44 m_viewMatrix;       // +0x74 (48 bytes used as Matrix43)
    Matrix44 m_field4;           // +0xA4
    Vec3 m_pos;                  // +0xE4
    Vec3 m_lookAt;               // +0xF0
    Vec3 m_up;                   // +0xFC
    bool m_bDirty;               // +0x108
    bool m_bInitialized;         // +0x109
    Vec3 m_clipData;             // +0x10C
    float m_nearX;               // +0x11C
    float m_nearY;               // +0x120
    float m_fovOrNear;           // +0x124 (default 1.0f)
    float m_farPlane;            // +0x128

    MortarCamera();
    virtual ~MortarCamera() {}

    // Sets up orthographic projection via MatrixManager
    // Uses DisplayManager window size to compute half-width/height
    void SetupOrtho();

    // Sets up look-at view matrix via MatrixManager
    void SetupLookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    // Sets up perspective projection via MatrixManager
    void SetupPerspective();
};

} // namespace Mortar

#endif
