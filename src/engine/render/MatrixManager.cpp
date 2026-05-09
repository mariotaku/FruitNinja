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

// ASM-verified: 2026-05-09 binary @ 0x0019e724 + LookAt43 body @
// 0x0019e82c (asm-inspector). The binary's LookAt43 is NON-CANONICAL:
//   forward = normalise(-p1)            // p3 (target/up) is IGNORED
//   right   = normalise(cross(p2, forward))
//   up'     = cross(forward, right)
//   translation = (-dot(p1, right), -dot(p1, up'), -dot(p1, forward))
// `p1` is treated as the eye-relative-to-target vector (i.e. the caller
// is expected to pre-subtract or assume target is at origin); `p2` is
// the up-hint; `p3` is read but never used. Port's earlier mat4_look_at
// canonical implementation diverged for non-zero target positions
// (camera shake in FruitCamera). The port now mirrors LookAt43 exactly.
//
// Param naming: the slots are kept as (eye, upHint, _unused) so the
// call sites read positionally the same as the binary's call sites.
void MatrixManager::SetupLookAt(const Vec3& eye, const Vec3& upHint, const Vec3& /*unused*/) {
    // forward = normalise(-eye)
    Vec3 fwd(-eye.x, -eye.y, -eye.z);
    {
        float len = sqrtf(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
        if (len > 0.0f) { float inv = 1.0f / len; fwd.x *= inv; fwd.y *= inv; fwd.z *= inv; }
    }
    // right = normalise(cross(upHint, fwd))
    Vec3 right(
        upHint.y * fwd.z - upHint.z * fwd.y,
        upHint.z * fwd.x - upHint.x * fwd.z,
        upHint.x * fwd.y - upHint.y * fwd.x);
    {
        float len = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
        if (len > 0.0f) { float inv = 1.0f / len; right.x *= inv; right.y *= inv; right.z *= inv; }
    }
    // up' = cross(fwd, right)
    Vec3 upOrth(
        fwd.y * right.z - fwd.z * right.y,
        fwd.z * right.x - fwd.x * right.z,
        fwd.x * right.y - fwd.y * right.x);
    // Build column-major view matrix (Matrix44 stores m[col*4 + row]).
    // Column 0 = right, Column 1 = up', Column 2 = fwd, Column 3 = translation.
    Matrix44 view;
    view.Identity();
    view.m[0] = right.x;  view.m[1] = upOrth.x;  view.m[2]  = fwd.x;
    view.m[4] = right.y;  view.m[5] = upOrth.y;  view.m[6]  = fwd.y;
    view.m[8] = right.z;  view.m[9] = upOrth.z;  view.m[10] = fwd.z;
    view.m[12] = -(eye.x * right.x + eye.y * right.y + eye.z * right.z);
    view.m[13] = -(eye.x * upOrth.x + eye.y * upOrth.y + eye.z * upOrth.z);
    view.m[14] = -(eye.x * fwd.x   + eye.y * fwd.y   + eye.z * fwd.z);
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
