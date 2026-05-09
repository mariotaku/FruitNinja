#include "render/MatrixManager.h"
#include "math/math3d.h"

MatrixManager::MatrixManager()
    : m_ViewVersionUploaded(0)
    , m_WorldVersionUploaded(0)
    , m_TextureVersionUploaded(0)
    , m_ProjVersionUploaded(0)
{
    ResetAllStacks();
}

// ASM-verified: 2026-05-09 binary @ 0x0019e280..0x0019e2a9 (asm-inspector)
void MatrixManager::ResetAllStacks() {
    m_Projection.Reset();
    m_View.Reset();
    m_World.Reset();
    m_Texture.Reset();
}

// ASM-verified: 2026-05-09 binary @ 0x0019e5a4..0x0019e5cb + OrthoW body
// @ 0x0019e7a8..0x0019e829 (asm-inspector). Arg order (top, bottom, left,
// right, near, far) is genuinely non-standard for glOrtho. Verified via
// callee-body trace: s0->top, s1->bottom, s2->left, s3->right, s4->near,
// s5->far, with m[1][1] = 2/(top-bottom) and m[0][0] = 2/(right-left).
void MatrixManager::SetupOrtho(float top, float bottom, float left, float right,
                               float nearVal, float farVal) {
    Matrix44 ortho;
    Matrix44::OrthoW(top, bottom, left, right, nearVal, farVal, 1.0f, ortho);
    m_Projection.SetCurrentMatrix(ortho);
    UploadAll();
}

// DIFFERS: binary @ 0x0019e724 + LookAt43 @ 0x0019e82c (asm-inspector
// 2026-05-09) computes a NON-CANONICAL view matrix:
//   forward = normalise(-p1)            // p3 (target/up) is IGNORED
//   right   = normalise(cross(p2, forward))   // up x forward (NOT f x u)
//   up'     = cross(forward, right)
//   translation = -dot(p1, axis) for each axis row
// With (eye=+Z, up=+Y) this gives `right = -X` -- view is X-flipped vs
// canonical glLookAt. The binary compensates for this by multiplying the
// projection by `DisplayManager::m_OrientationMatrix` inside
// `_UploadCurrentMatrices` @ 0x0019e2b4 (Bada is physically portrait;
// the 90deg rotation includes an axis-sign flip that cancels the view's
// X-flip on the wire).
//
// The port runs on a true landscape display and skips that orientation
// matmul -- so a binary-literal LookAt43 produces visible X-mirroring.
// Use canonical `mat4_look_at(eye, target, up)` here. The third arg
// (named `unused` for binary-shape ABI parity) is interpreted as the
// canonical `target` for this port path; callers must pass it.
//
// To match the binary byte-for-byte we'd also need to port the
// orientation-matrix step (TODO), at which point this can be swapped
// for the LookAt43 form.
void MatrixManager::SetupLookAt(const Vec3& eye, const Vec3& upHint, const Vec3& target) {
    Matrix44 view;
    mat4_look_at(view.ptr(),
                 eye.x,    eye.y,    eye.z,
                 target.x, target.y, target.z,
                 upHint.x, upHint.y, upHint.z);
    m_View.SetCurrentMatrix(view);
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

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: MatrixManager::SetupPerspective -- auto stub
void MatrixManager::SetupPerspective(float, float, float, float, float, Matrix44*) {}
// ---- end AUTO-STUB MERGE ----
