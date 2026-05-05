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

    // Cached Projection * View (recomputed on upload when dirty)
    Matrix44 m_CachedProjView;

    // Version tracking for dirty upload
    int m_ViewVersionUploaded;      // +0x2128
    int m_WorldVersionUploaded;     // +0x212C
    int m_TextureVersionUploaded;   // +0x2130
    int m_ProjVersionUploaded;

    // Matches 0x0019e2ac — called by GameInitialise after singleton creation
    // Just calls ResetAllStacks (constructor already does this, but matches original)
    void Init() { ResetAllStacks(); }

    // Matches 0x0019e5a4
    // NOTE: parameter order is (top, bottom, left, right, near, far) — NOT standard GL
    void SetupOrtho(float top, float bottom, float left, float right,
                    float nearVal, float farVal);

    // Matches 0x0019e724
    void SetupLookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    // "Upload all" — called by SetupOrtho, SetupLookAt (skipProjection=false)
    void UploadAll();

    // "Upload modelview only" — called by HUD draw pipeline (skipProjection=true)
    void UploadModelViewOnly();

    void ResetAllStacks();

    // Get combined Projection * View * World matrix (uses cached ProjView)
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
