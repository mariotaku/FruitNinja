#include "render/MortarCamera.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "math/math3d.h"

namespace Mortar {

MortarCamera::MortarCamera()
    : m_pos(0.0f, 0.0f, 0.0f)
    , m_lookAt(0.0f, 0.0f, -1.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_bDirty(true)
    , m_bInitialized(false)
    , m_nearX(0.0f)
    , m_nearY(0.0f)
    , m_fovOrNear(1.0f)
    , m_farPlane(1000.0f)
{
    m_localToWorld.Identity();
    m_projection.Identity();
    m_viewMatrix.Identity();
    m_field4.Identity();
    m_clipData = Vec3(0.0f, 0.0f, 0.0f);
}

void MortarCamera::SetupOrtho() {
    MortarRectangle rect = DisplayManager::GetInstance().GetWindowSize();
    float hw = (float)rect.Width() / 2.0f;
    float hh = (float)rect.Height() / 2.0f;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho(hh, -hh, -hw, hw, m_fovOrNear, m_farPlane);

    // Store copy of projection
    m_projection = mm.GetProjectionStack().m_Current;
}

void MortarCamera::SetupLookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    m_pos = eye;
    m_lookAt = target;
    m_up = up;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupLookAt(eye, target, up);

    // Store copy of view matrix
    m_viewMatrix = mm.GetViewStack().m_Current;
    m_bDirty = false;
}

void MortarCamera::SetupPerspective() {
    MortarRectangle rect = DisplayManager::GetInstance().GetWindowSize();
    float aspect = (float)rect.Width() / (float)rect.Height();

    Matrix44 proj;
    mat4_perspective(proj.ptr(), m_fovOrNear, aspect, m_nearX > 0 ? m_nearX : 0.1f, m_farPlane);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetProjectionStack().SetCurrentMatrix(proj);

    m_projection = proj;
}

} // namespace Mortar
