#ifndef MORTAR_MATRIX_MANAGER_H
#define MORTAR_MATRIX_MANAGER_H

#include "render/MatrixStack.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include "core/Singleton.h"

// Matches original MatrixManager (0x2134 = 8500 bytes)
// 4 MatrixStacks with dirty-tracking version counters
class MatrixManager : public Mortar::Singleton<MatrixManager> {
    friend class Mortar::Singleton<MatrixManager>;

public:
    // +0x04 (vtable at +0x00 is implicit)
    MatrixStack m_Projection;  // +0x004, 0x848 bytes
    MatrixStack m_View;        // +0x84C, 0x848 bytes
    MatrixStack m_World;       // +0x1094, 0x848 bytes
    MatrixStack m_Texture;     // +0x18DC, 0x848 bytes

    // ASM-verified: 2026-05-09 binary @ 0x0019e2b4 (asm-inspector)
    // Binary's MatrixManager has NO m_CachedProjView and NO
    // m_ProjVersionUploaded -- only the 3 ints below. Total class size
    // is exactly 0x2134 bytes (0x4 vtable + 4 stacks * 0x848 + 3 ints).
    // The port keeps m_CachedProjView for its ES2 shader pipeline
    // (Renderer reads GetMVP() each draw call) -- DIFFERS from binary,
    // which uploads matrices through GL fixed-pipeline calls inside
    // _UploadCurrentMatrices and lets later draws inherit GL state.
    // TODO: rewrite the upload path to match binary exactly; for now
    // the cache field is a port-side concession.
    Matrix44 m_CachedProjView;       // Port specific
    int m_ProjVersionUploaded;       // Port specific (binary lacks this)

    // Version tracking for dirty upload (matches binary @ +0x2128/+0x212C/+0x2130).
    int m_ViewVersionUploaded;      // +0x2128
    int m_WorldVersionUploaded;     // +0x212C
    int m_TextureVersionUploaded;   // +0x2130

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

private:
    // Matches 0x0019e2b4 — recomputes cached matrices based on dirty versions
    void _UploadCurrentMatrices(bool skipProjection);

    MatrixManager();

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: MatrixManager::SetupPerspective -- auto stub from binary missing-symbol set
    void SetupPerspective(float, float, float, float, float, Matrix44*);
    // ---- end AUTO-STUB MERGE ----
};

#endif
