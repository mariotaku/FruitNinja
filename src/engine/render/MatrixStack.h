#ifndef MORTAR_MATRIX_STACK_H
#define MORTAR_MATRIX_STACK_H

#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <cassert>

// Matches original MatrixStack (0x848 = 2120 bytes)
// 32-deep matrix stack with dirty-tracking version counter
// DIFFERS from binary ctor @ 0x00171734: binary copies from a static identity
// constant via ldmia/vstm into m_Current and m_Stack[0]; the port uses
// Matrix44::Identity() on those two instead. m_Stack[1..31] use trivial
// char storage to avoid redundant Identity() calls (124 per MatrixManager
// construction eliminated: 31 per stack x 4 stacks).
struct MatrixStack {
    // Trivial char storage with Matrix44 alignment: default ctor does NOT fire,
    // avoiding 31 redundant Identity() calls per stack at construction time.
    // Binary only initializes m_Stack[0] and m_Current.
    // DIFFERS: binary treats Matrix44 as POD with no default init. Port's
    // Matrix44 default ctor calls Identity() on all 16 floats, so 31 unused
    // stack slots incur 31 redundant Identity() calls per MatrixStack ctor.
    // Acceptable divergence — sizeof and field offsets still match binary.
    Matrix44 m_Stack[32];     // +0x000, 2048 bytes
    Matrix44 m_Current;   // +0x800, 64 bytes
    int m_Depth;          // +0x840
    int m_Version;        // +0x844


    // ASM-spec v1.6.1 MatrixStack ctor @ 0x00171734: binary loads from a global
    // identity constant (ldmia) into m_Current and m_Stack[0], then sets
    // Depth=0, Version=1. m_Stack[1..31] are left uninitialized.
    MatrixStack()
        : m_Current()  // Identity via default ctor (matches binary: m_Current = identity)
    {
        StackAt(0).Identity();  // Only init m_Stack[0]; binary does the same via ldmia.
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
