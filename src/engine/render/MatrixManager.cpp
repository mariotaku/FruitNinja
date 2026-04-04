#include "render/MatrixManager.h"
#include "math/math3d.h"

namespace Mortar {

MatrixManager::MatrixManager()
    : m_ViewVersionUploaded(0)
    , m_WorldVersionUploaded(0)
    , m_TextureVersionUploaded(0)
    , m_ProjVersionUploaded(0)
{
    ResetAllStacks();
}

void MatrixManager::ResetAllStacks() {
    m_Projection.Reset();
    m_View.Reset();
    m_World.Reset();
    m_Texture.Reset();
}

// Matches 0x0019e5a4
// NOTE: param order is (top, bottom, left, right) — NOT standard GL (left, right, bottom, top)
void MatrixManager::SetupOrtho(float top, float bottom, float left, float right,
                               float nearVal, float farVal) {
    Matrix44 ortho;
    Matrix44::OrthoW(top, bottom, left, right, nearVal, farVal, 1.0f, ortho);
    m_Projection.SetCurrentMatrix(ortho);
    UploadAll();
}

// Matches 0x0019e724
void MatrixManager::SetupLookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Matrix44 view;
    mat4_look_at(view.ptr(),
                 eye.x, eye.y, eye.z,
                 target.x, target.y, target.z,
                 up.x, up.y, up.z);
    m_View.SetCurrentMatrix(view);
    UploadAll();
}

void MatrixManager::UploadAll() {
    _UploadCurrentMatrices(false);
}

void MatrixManager::UploadModelViewOnly() {
    _UploadCurrentMatrices(true);
}

// Matches 0x0019e2b4 — recomputes cached matrices based on dirty versions
void MatrixManager::_UploadCurrentMatrices(bool skipProjection) {
    // If NOT skipping projection: recompute cached projection * view
    if (!skipProjection) {
        if (m_Projection.m_Version != m_ProjVersionUploaded ||
            m_View.m_Version != m_ViewVersionUploaded) {
            m_CachedProjView = m_Projection.m_Current * m_View.m_Current;
            m_ProjVersionUploaded = m_Projection.m_Version;
        }
    }

    // ModelView dirty check
    if (m_View.m_Version != m_ViewVersionUploaded) {
        // View changed — must recompute ProjView too
        m_CachedProjView = m_Projection.m_Current * m_View.m_Current;
        m_ViewVersionUploaded = m_View.m_Version;
        m_WorldVersionUploaded = m_World.m_Version;
    } else if (m_World.m_Version != m_WorldVersionUploaded) {
        // Only world changed
        m_WorldVersionUploaded = m_World.m_Version;
    }

    m_TextureVersionUploaded = m_Texture.m_Version;
}

Matrix44 MatrixManager::GetMVP() const {
    return m_CachedProjView * m_World.m_Current;
}

} // namespace Mortar
