#ifndef MORTAR_MATRIX_MANAGER_H
#define MORTAR_MATRIX_MANAGER_H

#include "render/MatrixStack.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include "core/Singleton.h"

// Binary MatrixManager: 8500 bytes, polymorphic (vtable @ 0x001eb528, 3 vfn slots).
// Layout: vptr(4) + 4*MatrixStack(2120) + 4*int(16) = 8500.
// Port adds m_CachedProjView + m_ProjVersionUploaded for the GLES2 shader path
// (DIFFERS, marked below). Binary size applies only under __bada__.
class MatrixManager : public Mortar::Singleton<MatrixManager> {
    friend class Mortar::Singleton<MatrixManager>;

public:
    // vptr at +0x00 (emitted by the virtual dtor below; matches binary vtable @ 0x001eb528)
    MatrixStack m_Projection;  // +0x004, 0x848 bytes
    MatrixStack m_View;        // +0x84C, 0x848 bytes
    MatrixStack m_World;       // +0x1094, 0x848 bytes
    MatrixStack m_Texture;     // +0x18DC, 0x848 bytes

    // Binary trailing ints (binary @ ctor 0x0019e478):
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
    // Binary's _UploadCurrentMatrices @ 0x0019e2b4 emits glLoad/Push/Pop/Mult instead.
    Matrix44 m_CachedProjView;    // Port specific
    int m_ProjVersionUploaded;    // Port specific

    // Binary vtable @ 0x001eb528: slot 0+1 = dtor pair (0x0019e3b4, 0x0019e434),
    // slot 2 = vfn @ 0x00277264 (identity unknown; non-pure, 1-instruction stub).
    // Virtual dtor emits the vptr, matching binary isPolymorphic=true.
    virtual ~MatrixManager();

    // ASM-verified: 2026-05-09 binary @ 0x0019e2ac (asm-inspector)
    // Binary is a 2-instruction tail-call to ResetAllStacks. Inlining
    // the call here is the same effect.
    void Init() { ResetAllStacks(); }

    // Matches 0x0019e5a4
    // NOTE: parameter order is (top, bottom, left, right, near, far) — NOT standard GL
    void SetupOrtho(float top, float bottom, float left, float right,
                    float nearVal, float farVal);

    // DIFFERS from binary @ 0x0019e724 (asm-inspector). Binary's LookAt43
    // produces a non-canonical X-flipped view matrix that is compensated
    // by an orientation-matrix multiply in _UploadCurrentMatrices @
    // 0x0019e2b4 (Bada portrait->landscape rotation). Port skips that
    // compensator, so this entrypoint uses canonical glLookAt math; arg
    // names are (eye, upHint, target) to keep positional parity with the
    // binary call sites (3rd slot is "unused" in binary, "target" here).
    void SetupLookAt(const Vec3& eye, const Vec3& upHint, const Vec3& target);

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

    // TODO: 0x0019e668 -- build a perspective projection matrix and apply it.
    //   Binary args (top, bottom, near, far, ?, out): m[1][1]=bottom/top,
    //   m[0][0]=(bottom/top)/arg3, m[2][2]=(far+near)/(near-far),
    //   m[3][2]=2*near*far/(near-far), m[2][3]=-1, rest 0; if out==null use a
    //   local. Then m_Projection.SetCurrentMatrix(out) and UploadAll().
    void SetupPerspective(float, float, float, float, float, Matrix44*);

private:
    // Matches 0x0019e2b4 — recomputes cached matrices based on dirty versions
    void _UploadCurrentMatrices(bool skipProjection);

    MatrixManager();
};

#ifdef __bada__
// Binary size: vptr(4) + 4*MatrixStack(2120) + 4*int(16) = 8500.
// Port-specific extras (m_CachedProjView + m_ProjVersionUploaded) are excluded
// from this assert — they exist only in the port build.
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack size mismatch");
#endif

#endif
