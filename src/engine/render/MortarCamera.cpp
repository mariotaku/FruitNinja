//
// MortarCamera — base camera class
// See docs/engine/camera.md for full decompilation.
//

#include "render/MortarCamera.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "math/math3d.h"

namespace Mortar {

// Matches constructor at 0x0019eb14
MortarCamera::MortarCamera()
    : m_pos(0.0f, 0.0f, 1.0f)      // via MakeVec3ZFirst(0, 1.0) → (0, 0, 1)
    , m_lookAt(0.0f, 0.0f, 0.0f)   // from global zero vec
    , m_up(0.0f, 1.0f, 0.0f)       // via MakeVec3ZFirst(1.0, 0) → (0, 1, 0)
    , m_bDirty(true)                // = 1
    , m_bInitialized(true)          // = 1, forces first recompute
    , m_fovX(0.0f)
    , m_fovY(0.0f)
    , m_fovOrNear(1.0f)
    , m_farPlane(1000.0f)           // DAT_0019ebd4
{
    // Identity43 for cached views, Identity44 for cached projections
    // (Matrix43/Matrix44 constructors handle this)
    m_viewportRect.left = 0;
    m_viewportRect.top = 0;
    m_viewportRect.right = 0;
    m_viewportRect.bottom = 0;
}

// Vtable slot 2 (0x0019e9f0)
void MortarCamera::Init(float fovOrNear, float farPlane, float fovX, float fovY) {
    m_fovOrNear = fovOrNear;
    m_farPlane = farPlane;
    m_fovX = fovX;
    m_fovY = fovY;
    m_bInitialized = true;  // force recompute
}

// Vtable slot 3 — empty in base class
void MortarCamera::UpdateCamera(float dt) {
    (void)dt;
}

// Vtable slot 5 (0x0019edfc) — LookAt + ortho from viewport
void MortarCamera::SetupOrtho() {
    MortarRectangle rect = DisplayManager::GetInstance().GetWindowSize();

    if (!m_bInitialized) {
        // Check if viewport changed from cached
        if (rect.Height() == m_viewportRect.Height() ||
            rect.Width() == m_viewportRect.Width()) {
            // Reuse ortho cache
            Matrix44 viewMat44;
            m_viewMatrix.ToMatrix44(viewMat44);
            MatrixManager& mm = MatrixManager::GetInstance();
            mm.GetViewStack().SetCurrentMatrix(viewMat44);
            mm.GetProjectionStack().SetCurrentMatrix(m_projOrtho);
            mm.UploadAll();
            goto cache_viewport;
        }
    }

    {
        // Recompute: LookAt + Ortho
        MatrixManager& mm = MatrixManager::GetInstance();

        // eye = (0, 0, 1), up = (0, 1, 0), target = origin
        Vec3 eye(0.0f, 0.0f, 1.0f);
        Vec3 up(0.0f, 1.0f, 0.0f);
        Vec3 origin(0.0f, 0.0f, 0.0f);
        mm.SetupLookAt(eye, up, origin);

        // Cache view as Matrix43
        m_viewMatrix = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

        // Ortho from half-viewport extents
        float halfH = (float)(rect.Height() / 2);
        float halfW = (float)(rect.Width() / 2);
        mm.SetupOrtho(halfH, -halfH, -halfW, halfW, -1.0f, 1000.0f);

        // Cache projection
        m_projOrtho = mm.GetProjectionStack().m_Current;
    }

cache_viewport:
    m_viewportRect = rect;
    m_bInitialized = false;

    // Reset world matrix
    MatrixManager::GetInstance().GetWorldStack().Reset();
}

// Vtable slot 4 (0x0019ece4) — LookAt + perspective projection
void MortarCamera::SetupPerspective() {
    MatrixManager& mm = MatrixManager::GetInstance();

    if (!m_bDirty) {
        // Reuse perspective cache
        Matrix44 viewMat44;
        m_localToWorld.ToMatrix44(viewMat44);
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projection);
        mm.UploadAll();
    } else {
        // Recompute LookAt
        mm.SetupLookAt(m_pos, m_up, m_lookAt);
        m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

        // Perspective from FOV (uses SinIdx/CosIdx with 182.0 conversion)
        // Port specific: using mat4_perspective as approximation
        // The original uses SinIdx/CosIdx-based perspective which gives
        // slightly different results, but functionally equivalent.
        float aspect = (m_fovY != 0.0f) ? m_fovX / m_fovY : 1.0f;
        Matrix44 proj;
        mat4_perspective(proj.ptr(), m_fovOrNear, aspect,
                         m_fovX > 0 ? m_fovX : 0.1f, m_farPlane);
        mm.GetProjectionStack().SetCurrentMatrix(proj);

        m_projection = mm.GetProjectionStack().m_Current;
        m_bDirty = false;
    }

    mm.GetWorldStack().Reset();
}

} // namespace Mortar
