#ifndef MORTAR_MATRIX_MANAGER_H
#define MORTAR_MATRIX_MANAGER_H

#include "render/MatrixStack.h"
#include "math/Matrix44.h"
#include "math/Matrix43.h"
#include "math/_Vector3.h"

// Binary MatrixManager: 8500 bytes, polymorphic (v1.6.1 MatrixManager::vtable @0x002d0738, 4 vfn slots).
// Layout: vptr(4) + 4*MatrixStack(2120) + 4*int(16) = 8500.
// Port adds m_CachedProjView + m_ProjVersionUploaded for the GLES2 shader path
// (DIFFERS, marked below). Binary size applies only under __bada__.
//
// ASM-spec v1.6.1 MatrixManager::m_instance @ ram:0x0035ced4:
// Binary uses a class-static global instance (namespace-scope, no lazy init,
// no guard variable). All 89 callers access it via direct GOT load.
// Port matches this with a class-static s_instance defined in the .cpp.
class MatrixManager {
public:
    // Binary @ ram:0x0035ced4 — direct global access, no guard.
    static MatrixManager& GetInstance() { return s_instance; }
    // vptr at +0x00 (emitted by the virtual dtor below; matches v1.6.1 MatrixManager::vtable @0x002d0738)
    MatrixStack m_Projection;  // +0x004, 0x848 bytes
    MatrixStack m_View;        // +0x84C, 0x848 bytes
    MatrixStack m_World;       // +0x1094, 0x848 bytes
    MatrixStack m_Texture;     // +0x18DC, 0x848 bytes

    // Binary trailing ints (v1.6.1 MatrixManager::MatrixManager @0x00256f3c):
    //   m_ViewVersion         @ +0x2124 (8484)  — init 0
    //   m_ViewVersionUploaded @ +0x2128 (8488)  — init 0
    //   m_WorldVersionUploaded@ +0x212C (8492)  — init 0
    //   m_TextureVersionUploaded @ +0x2130 (8496) — init 0
    // Binary total = 4 + 4*2120 + 4*4 = 8500. static_assert under __bada__.
    // Binary-faithful field; unused by the port's GLES2 upload path (which
    // tracks projection staleness via m_ProjVersionUploaded below instead).
    int m_ViewVersion;            // +0x2124
    int m_ViewVersionUploaded;    // +0x2128
    int m_WorldVersionUploaded;   // +0x212C
    int m_TextureVersionUploaded; // +0x2130

    // DIFFERS from binary: binary has no m_CachedProjView / m_ProjVersionUploaded.
    // Required by the GLES2 shader-uniform path (fixed-pipeline unavailable on GLES2).
    // Binary's _UploadCurrentMatrices @0x00257018 emits glLoad/Push/Pop/Mult instead.
    Matrix44 m_CachedProjView;    // Port specific
    int m_ProjVersionUploaded;    // Port specific

    // v1.6.1 MatrixManager::vtable @0x002d0738 (4 slots): slot 0+1 = dtor pair
    // (~MatrixManager @0x00256ec8, deleting-dtor @0x00256f20),
    // slot 2+3 = vfn (identity unknown). TODO: re-verify v1.6.1 vfn slot addresses.
    // Virtual dtor emits the vptr, matching binary isPolymorphic=true.
    virtual ~MatrixManager();

    // ASM-spec v1.6.1 MatrixManager::ResetAllStacks @0x00256fe8: Init is a
    // 2-instruction tail-call to ResetAllStacks. Inlining the call here is
    // the same effect.
    void Init() { ResetAllStacks(); }

    // ASM-spec v1.6.1 MatrixManager::SetupOrtho @0x002571d0: optional out-matrix param (parity with binary).
    // NOTE: parameter order is (top, bottom, left, right, near, far) — NOT standard GL
    void SetupOrtho(float top, float bottom, float left, float right,
                    float nearVal, float farVal, Matrix44* out = 0);

    // DIFFERS from v1.6.1 MatrixManager::SetupLookAt @0x002572ac. Binary's LookAt43
    // produces a non-canonical X-flipped view matrix that is compensated
    // by an orientation-matrix multiply in _UploadCurrentMatrices @
    // 0x00257018 (Bada portrait->landscape rotation). Port skips that
    // compensator, so this entrypoint uses canonical glLookAt math; arg
    // names are (eye, upHint, target) to keep positional parity with the
    // binary call sites (3rd slot is "unused" in binary, "target" here).
    // ASM-spec v1.6.1 MatrixManager::SetupLookAt @0x002572ac: optional out-matrix param (parity with binary).
    void SetupLookAt(const _Vector3<float>& eye, const _Vector3<float>& upHint, const _Vector3<float>& target, Matrix43* out = 0);

    // "Upload all" — called by SetupOrtho, SetupLookAt (skipProjection=false)
    void UploadAll();

    // "Upload modelview only" — called by HUD draw pipeline (skipProjection=true)
    void UploadModelViewOnly();

    void ResetAllStacks();

    // Port specific: helper for the SDL+ES2 shader pipeline. The binary
    // has no GetMVP() -- it uploads matrices through GL fixed-pipeline
    // inside _UploadCurrentMatrices. The port reads this from
    // Renderer::DrawTriList/DrawTriStrip to feed the shader uniform.
    Matrix44 GetMVP() const;

    MatrixStack& GetWorldStack() { return m_World; }
    MatrixStack& GetProjectionStack() { return m_Projection; }
    MatrixStack& GetViewStack() { return m_View; }
    MatrixStack& GetTextureStack() { return m_Texture; }

    const MatrixStack& GetWorldStack() const { return m_World; }
    const MatrixStack& GetProjectionStack() const { return m_Projection; }
    const MatrixStack& GetViewStack() const { return m_View; }
    const MatrixStack& GetTextureStack() const { return m_Texture; }

    // Binary @ v1.6.1 MatrixManager::SetupPerspective @0x00257164. Builds a column-major GL perspective projection and
    // applies it via m_Projection.SetCurrentMatrix + UploadAll(). Args are
    // (top, bottom, aspect, near, far, out); out==null uses a local matrix.
    //   m[0]=(bottom/top)/aspect, m[5]=bottom/top, m[10]=(far+near)/(near-far),
    //   m[11]=-1, m[14]=2*near*far/(near-far), all other entries 0.
    void SetupPerspective(float top, float bottom, float aspect,
                          float nearVal, float farVal, Matrix44* out);

private:
    // Matches v1.6.1 MatrixManager::_UploadCurrentMatrices @0x00257018 — recomputes cached matrices based on dirty versions
    void _UploadCurrentMatrices(bool skipProjection);

    MatrixManager();

    // ASM-spec v1.6.1 @ ram:0x0035ced4: class-static global instance.
    // Initialized during static init via global constructors keyed to
    // MatrixManager.cpp @ 0x002573b0. No guard variable, no lazy init.
    static MatrixManager s_instance;
};

#ifdef __bada__
// Binary size: vptr(4) + 4*MatrixStack(2120) + 4*int(16) = 8500.
// Port-specific extras (m_CachedProjView + m_ProjVersionUploaded) are excluded
// from this assert — they exist only in the port build.
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack size mismatch");
#endif

#endif
