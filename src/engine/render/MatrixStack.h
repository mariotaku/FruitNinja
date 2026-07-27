#ifndef MORTAR_MATRIX_STACK_H
#define MORTAR_MATRIX_STACK_H

#include "math/Matrix44.h"
#include "math/_Vector3.h"
#include <cassert>

// Matches original MatrixStack (0x848 = 2120 bytes)
// 32-deep matrix stack with dirty-tracking version counter
// DIFFERS from binary ctor v1.6.1 MatrixStack::MatrixStack @0x00257658: binary copies from a static identity
// constant via ldmia/vstm into m_Current and m_Stack[0]; the port uses
// Matrix44::Identity() on those two instead. m_Stack[1..31] use char
// storage to avoid redundant Identity() calls (124 per MatrixManager
// construction eliminated: 31 per stack x 4 stacks).
struct MatrixStack {
    static const int STACK_SIZE = 32;

    // Char storage: default ctor does NOT fire on individual slots, avoiding
    // 31 redundant Identity() calls per stack at construction time. Binary only
    // initializes m_Stack[0] and m_Current. The port's Matrix44 default ctor
    // would Identity() all 64 floats of unused slots if this were Matrix44[32];
    // char storage prevents that. Natural struct alignment >= 4 is sufficient
    // for 4-byte-aligned float access matching binary behavior.
    char m_StackData[sizeof(Matrix44) * STACK_SIZE]; // +0x000, 2048 bytes
    Matrix44 m_Current;   // +0x800, 64 bytes
    int m_Depth;          // +0x840
    int m_Version;        // +0x844

    Matrix44& StackAt(int i) {
        return *reinterpret_cast<Matrix44*>(m_StackData + i * sizeof(Matrix44));
    }
    const Matrix44& StackAt(int i) const {
        return *reinterpret_cast<const Matrix44*>(m_StackData + i * sizeof(Matrix44));
    }

    // ASM-spec v1.6.1 MatrixStack::MatrixStack @0x00257658: binary loads from a global
    // identity constant (ldmia) into m_Current and m_Stack[0], then sets
    // Depth=0, Version=1. m_Stack[1..31] are left uninitialized.
    MatrixStack()
        : m_Current()  // Identity via default ctor (matches binary: m_Current = identity)
    {
        StackAt(0).Identity();  // Only init m_Stack[0]; binary does the same via ldmia.
        m_Depth = 0;
        m_Version = 1;
    }

    // ASM-spec v1.6.1 MatrixStack::Reset @0x0013f98c: identity m_Current and
    // m_Stack[0], Depth=0, ++Version. SpeedControl.cpp's Draw call sites use
    // this through MatrixManager::GetWorldStack().Reset().
    void Reset();

    // DIFFERS: original = slot-indexed Store(uint8)/Restore(uint8) (v1.6.1
    // MatrixStack::Store @0x001a3624 / MatrixStack::Restore @0x001a3654), using
    // depth-based Push/Pop because the port's draw call sites nest save/restore
    // around transforms and never allocate slot ids, so a depth cursor gives the
    // same m_Current save/restore against m_Stack with no caller-side slot
    // bookkeeping. Store does NOT bump m_Version in the binary (only Restore
    // does); the port's Push bumps it, costing one redundant matrix re-upload.
    void Push();
    void Pop();

    // ASM-verified: 2026-07-27T14:40Z v1.6.1 MatrixStack::Scale @ 0x0015d100 (asm-inspector)
    // LEFT-multiply m_Current by diag(s.x, s.y, s.z, 1) via _Matrix44<float>::Scale44
    // (S*M), then ++Version. Rows 0/1/2 are scaled, INCLUDING the translation
    // elements m[12]/m[13]/m[14] -- an already-translated matrix has its translation
    // scaled too. Call sites that want the scale to leave an existing translation
    // alone must Reset() (or Push/Reset) first; every in-port call site does.
    // Called from MenuButton::Draw's sparkle-ring block @0x0019cca4.
    void Scale(const _Vector3<float>& s);

    // ASM-verified: 2026-07-27T14:40Z v1.6.1 MatrixStack::Translate @ 0x0015d040 (asm-inspector)
    // World-space offset added to the translation column (T*M).
    void Translate(const _Vector3<float>& t);

    // ASM-spec v1.6.1 MatrixStack::SetCurrentMatrix @0x00143720: copy mat into
    // m_Current, ++Version.
    void SetCurrentMatrix(const Matrix44& mat);

    // ASM-spec v1.6.1 MatrixStack::RotZ @0x001d0b80: convert deg to a 16-bit angle
    // index (deg*182), then LEFT-multiply m_Current by RotZ44(SinIdx, CosIdx).
    void RotZ(float deg);

    // ASM-spec v1.6.1 MatrixStack::TranslateLocal @0x0024a150: RIGHT-multiply
    // m_Current by a local-space translate (M*T). Distinct from Translate above,
    // which adds a world-space offset to the translation column (T*M).
    void TranslateLocal(const _Vector3<float>& t);
};

#ifdef __bada__
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack must be 0x848 bytes");
#endif

#endif
