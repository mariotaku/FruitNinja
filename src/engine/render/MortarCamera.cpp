//
// MortarCamera — base camera class
//

#include "render/MortarCamera.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "math/math3d.h"

namespace Mortar {

// Binary @ 0x00257d6c (C1/C2 ctor)
// DIFFERS: m_lookAt init is (0,0,0) — binary uses GOT[DAT_00257e98] (GOT-relocated Vec3 const,
// not statically readable; assumed zero but low-confidence pending relocation-table resolution).
MortarCamera::MortarCamera()
    : m_pos(0.0f, 0.0f, 1.0f)      // (0,0,1) via MakeVec3ZFirst
    , m_lookAt(0.0f, 0.0f, 0.0f)   // low-confidence: see DIFFERS above
    , m_up(0.0f, 1.0f, 0.0f)       // (0,1,0) via MakeVec3ZFirst
    , m_bDirty(true)                // = 1
    , m_bInitialized(true)          // = 1, forces first recompute
    , m_fovX(0.0f)
    , m_fovY(0.0f)
    , m_fovOrNear(1.0f)
    , m_farPlane(1000.0f)           // DAT_00257e9c = 1000.0f
{
    // Identity43 for cached views, Identity44 for cached projections
    // (Matrix43/Matrix44 constructors handle this)
    // Binary copies uninitialized stack into m_viewportRect; port zero-inits
    // (harmless: overwritten on first SetupOrtho call).
    m_viewportRect.left = 0;
    m_viewportRect.top = 0;
    m_viewportRect.right = 0;
    m_viewportRect.bottom = 0;
}

// Vtable slot 2 (0x00257720)
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

// Vtable slot 5, binary @ 0x00257758 — LookAt + ortho from viewport
void MortarCamera::SetupOrtho() {
    MortarRectangle rect = DisplayManager::GetInstance().GetWindowSize();

    // Cache gate @ 0x00257758: take cache path iff ALL THREE hold:
    //   (1) m_bInitialized == 0  (already set after first call)
    //   (2) vp[3]-vp[1] unchanged  (rect.bottom - rect.top, i.e. Height())
    //   (3) vp[2]-vp[0] unchanged  (rect.right  - rect.left, i.e. Width())
    // Binary uses '&&' across all three; port had nested '||' which was wrong.
    if (!m_bInitialized &&
        (rect.bottom - rect.top) == (m_viewportRect.bottom - m_viewportRect.top) &&
        (rect.right - rect.left) == (m_viewportRect.right - m_viewportRect.left)) {
        // Reuse ortho cache
        Matrix44 viewMat44;
        m_viewMatrix.ToMatrix44(viewMat44);
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projOrtho);
        mm.UploadAll();
    } else {
        // Recompute: LookAt + Ortho
        MatrixManager& mm = MatrixManager::GetInstance();

        // eye=(0,0,1), up=(0,1,0)
        // TODO: 0x00257758 GOT[DAT_00257aa8] — binary passes a GOT-relocated Vec3
        // forward-const as the LookAt target; value not statically readable.
        // Using (0,0,0) as low-confidence placeholder.
        Vec3 eye(0.0f, 0.0f, 1.0f);
        Vec3 up(0.0f, 1.0f, 0.0f);
        Vec3 target(0.0f, 0.0f, 0.0f);
        mm.SetupLookAt(eye, up, target);

        // Cache view as Matrix43
        m_viewMatrix = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

        // Ortho extents from binary @ 0x00257758 recompute block:
        //   L = +(vp[3]>>1) = +(rect.bottom>>1)   (local_44 in decompile)
        //   R = -(vp[3]>>1) = -(rect.bottom>>1)
        //   B = -(vp[2]>>1) = -(rect.right>>1)    (iStack_48 in decompile)
        //   T = +(vp[2]>>1) = +(rect.right>>1)
        //   near = -1.0, far = DAT_00257a9c = 1000.0f
        float halfVP3 = (float)(rect.bottom >> 1);
        float halfVP2 = (float)(rect.right  >> 1);
        mm.SetupOrtho(halfVP3, -halfVP3, -halfVP2, halfVP2, -1.0f, 1000.0f);

        // Cache projection
        m_projOrtho = mm.GetProjectionStack().m_Current;

        // NOTE: Binary NEVER clears m_bInitialized after ctor/Init, so the
        // ortho cache guard (!m_bInitialized) always fails -- cache path is dead
        // code in both binary and port.  The cached matrices m_orthoView/m_orthoProj
        // are written each frame but never read.
    }

    // Always write cache and reset world stack
    m_viewportRect = rect;
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

        // DIFFERS: binary @ 0x00257aac computes half=m_fovY*182.0 (DAT_00257d60,
        // deg->16bit-angle units), then calls MatrixManager::SetupPerspective(
        //   s=Math::SinIdx(half&0xffff), c=Math::CosIdx(half&0xffff),
        //   aspect=m_fovX/m_fovY, near=m_fovOrNear(+0x124), far=m_farPlane(+0x128)).
        // Port substitutes glibc mat4_perspective (no SinIdx/CosIdx LUT) AND
        // passes m_fovOrNear as fovy and m_fovX as znear -- both wrong scalars.
        // Port specific: mat4_perspective substitution pending MatrixManager::SetupPerspective port.
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
