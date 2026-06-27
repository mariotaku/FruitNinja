#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/math3d.h"

// ASM-spec v1.6.1 MatrixManager::m_instance @ ram:0x0035ced4:
// Class-static global, initialized during static init via
// global.constructors.keyed.to.MatrixManager.cpp @ 0x002573b0.
// No guard variable, no lazy init. Matches binary's direct GOT access.
MatrixManager MatrixManager::s_instance;

// Binary ctor @ 0x00256f3c (C2) / 0x0010a520 (C1 = PLT thunk -> C2):
// calls MatrixStack::MatrixStack() on each of the 4 member stacks (= Reset()+version=1),
// then str-zeroes the 4 trailing version ints at +0x2124..+0x2130.
// No ResetAllStacks() call follows; the member ctors are sufficient.
// The port's extra m_ProjVersionUploaded=0 is port-specific (GLES2 shader path).
MatrixManager::MatrixManager()
    : m_ViewVersion(0)
    , m_ViewVersionUploaded(0)
    , m_WorldVersionUploaded(0)
    , m_TextureVersionUploaded(0)
    , m_ProjVersionUploaded(0)
{
}

// Binary dtors @ 0x0019e3b4 (C1) + 0x0019e434 (C2); singleton is never destroyed
// in practice but the vtable slots must exist.
MatrixManager::~MatrixManager() {}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0019e280..0x0019e2a9 (asm-inspector)
void MatrixManager::ResetAllStacks() {
    m_Projection.Reset();
    m_View.Reset();
    m_World.Reset();
    m_Texture.Reset();
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0019e5a4..0x0019e5cb + OrthoW body
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

// DIFFERS: v1.6.1 binary @ 0x0019e724 + LookAt43 @ 0x0019e82c (asm-inspector
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

// DIFFERS from binary v1.6.1 _UploadCurrentMatrices @0x00257018 (asm-inspector 2026-05-24): the binary
// emits a fixed-pipeline GL stream here -- glMatrixMode(GL_PROJECTION) +
// glLoadMatrixf(DisplayManager.m_OrientationMatrix * proj); optionally
// glMatrixMode(GL_TEXTURE) + glLoadMatrixf(tex); and a pop/load/push/mult
// dance on GL_MODELVIEW to keep `view` at stack depth 1 and `view*world`
// at depth 0. The port runs GLES2 with no fixed-pipeline matrix stack:
// MVP is cached as m_CachedProjView and uploaded as a shader uniform by
// Renderer::setup_3d_shader() per draw call. The orientation-matrix
// left-mul is skipped because the port targets a native-landscape window
// (already documented on SetupLookAt above).
//
// GL_TEXTURE matrix upload goes through the fixed-function shim
// (glMatrixMode(GL_TEXTURE_MATRIX) + glLoadMatrixf) matching the binary.
// Port shaders currently sample with raw a_uv and ignore the texture matrix,
// but the upload is preserved for binary-call-graph fidelity.
void MatrixManager::_UploadCurrentMatrices(bool skipProjection) {
    if (!skipProjection) {
        // Proj or view changed: recompute ProjView *once* and mark both uploaded.
        if (m_Projection.m_Version != m_ProjVersionUploaded ||
            m_View.m_Version != m_ViewVersionUploaded) {
            m_CachedProjView = m_Projection.m_Current * m_View.m_Current;
            m_ProjVersionUploaded = m_Projection.m_Version;
            m_ViewVersionUploaded = m_View.m_Version;
            m_WorldVersionUploaded = m_World.m_Version;
        } else if (m_World.m_Version != m_WorldVersionUploaded) {
            // Only world changed — no matrix multiply needed for this path.
            m_WorldVersionUploaded = m_World.m_Version;
        }
    } else {
        // ModelView-only path: projection is never touched.
        if (m_View.m_Version != m_ViewVersionUploaded) {
            m_ViewVersionUploaded = m_View.m_Version;
            m_WorldVersionUploaded = m_World.m_Version;
        } else if (m_World.m_Version != m_WorldVersionUploaded) {
            m_WorldVersionUploaded = m_World.m_Version;
        }
    }

    // Binary @ 0x00257018 (v1.6.1 _UploadCurrentMatrices): texture-matrix dirty
    // gate. Runs unconditionally of the skipProjection arg in the binary.
    if (m_Texture.m_Version != m_TextureVersionUploaded) {
        glMatrixMode(GL_TEXTURE_MATRIX);
        glLoadMatrixf(m_Texture.m_Current.ptr());
        m_TextureVersionUploaded = m_Texture.m_Version;
    }
}

Matrix44 MatrixManager::GetMVP() const {
    return m_CachedProjView * m_World.m_Current;
}

// Binary @ 0x0019e668 (asm-inspector). Builds a column-major GL perspective
// projection. Args: (top, bottom, aspect, near, far, out). If out==null a
// local matrix is used. The binary computes the divisions via Math::DivAsync
// (Set/Get) -- a VFP-pipelined async divide -- which is just plain `a/b`.
//
// Byte-offset map (data[i][j] in Ghidra == column-major m[i*4+j]):
//   m[0]  = (bottom/top)/aspect          [0x00]
//   m[5]  = bottom/top                    [0x14]
//   m[10] = (far+near)/(near-far)         [0x28]
//   m[11] = -1.0                          [0x2c]
//   m[14] = 2*near*far/(near-far)         [0x38]
// All other 11 entries = 0 (including m[15], unlike Identity).
void MatrixManager::SetupPerspective(float top, float bottom, float aspect,
                                     float nearVal, float farVal, Matrix44* out) {
    Matrix44 local;
    if (out == 0) {
        out = &local;
    }
    // 1.0/(near-far), reused for m[10] and m[14] (binary's first DivAsync).
    float invNF = 1.0f / (nearVal - farVal);
    float ba = bottom / top;

    for (int i = 0; i < 16; ++i) out->m[i] = 0.0f;
    out->m[5]  = ba;
    out->m[11] = -1.0f;
    out->m[10] = (farVal + nearVal) * invNF;
    out->m[14] = (nearVal + nearVal) * farVal * invNF;
    out->m[0]  = ba / aspect;

    m_Projection.SetCurrentMatrix(*out);
    UploadAll();
}
