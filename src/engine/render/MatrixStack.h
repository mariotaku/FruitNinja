#ifndef MORTAR_MATRIX_STACK_H
#define MORTAR_MATRIX_STACK_H

#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <cassert>

// Matches original MatrixStack (0x848 = 2120 bytes)
// 32-deep matrix stack with dirty-tracking version counter
struct MatrixStack {
    Matrix44 m_Stack[32]; // +0x000, 2048 bytes
    Matrix44 m_Current;   // +0x800, 64 bytes
    int m_Depth;          // +0x840
    int m_Version;        // +0x844

    // ASM-spec v1.6.1 MatrixStack ctor @ 0x00171734: binary loads from a global
    // identity constant (ldmia) into m_Current and m_Stack[0], rather than
    // calling Matrix44::Identity() which writes 16 floats individually. The
    // port's _Matrix44<T>() default ctor also calls Identity(), so
    // m_Current and m_Stack[0] are already identity-initialized before the
    // ctor body runs. Removing the redundant body calls saves ~160 stores per
    // MatrixManager construction (4 stacks x 2 redundant Identity() x 20 stores).
    // DIFFERS: original = body copies from static identity matrix via ldmia/vstm;
    // port relies on Matrix44 default ctor + member initialization chain instead.
    MatrixStack() {
        m_Depth = 0;
        m_Version = 1;
    }

    // ASM-verified: 2026-05-09 binary @ 0x001175d4 (asm-inspector)
    void Reset();

    // Port specific: no binary equivalent (GL push/pop happens inside
    // MatrixManager::_UploadCurrentMatrices). Port keeps this abstraction
    // so callers can save/restore m_Current without touching GL directly.
    void Push();
    void Pop();

    // ASM-verified: 2026-05-09 binary @ 0x0012fa34 (asm-inspector)
    void Scale(const Vec3& s);

    // ASM-verified: 2026-05-09 binary @ 0x0012f97c (asm-inspector)
    void Translate(const Vec3& t);

    // ASM-verified: 2026-05-09 binary @ 0x0011a130 (asm-inspector)
    void SetCurrentMatrix(const Matrix44& mat);
};

#ifdef __bada__
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack must be 0x848 bytes");
#endif

#endif
