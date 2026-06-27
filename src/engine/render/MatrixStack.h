#ifndef MORTAR_MATRIX_STACK_H
#define MORTAR_MATRIX_STACK_H

#include "math/Matrix44.h"
#include "math/Vec3.h"
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

    // TODO: re-verify v1.6.1 MatrixStack::Reset address (cited 0x001175d4 was stale
    // v1.5.x -- resolves to QueAchievement in v1.6.1; method is inlined into
    // MatrixManager, no standalone symbol confirmed)
    void Reset();

    // Port specific: no binary equivalent (GL push/pop happens inside
    // MatrixManager::_UploadCurrentMatrices). Port keeps this abstraction
    // so callers can save/restore m_Current without touching GL directly.
    void Push();
    void Pop();

    // TODO: re-verify v1.6.1 MatrixStack::Scale address (cited 0x0012fa34 was stale
    // v1.5.x -- resolves to a BonusManager static-ctor blob in v1.6.1; method is
    // inlined into MatrixManager, no standalone symbol confirmed)
    void Scale(const Vec3& s);

    // TODO: re-verify v1.6.1 MatrixStack::Translate address (cited 0x0012f97c was
    // stale v1.5.x -- resolves to a BonusManager static-ctor blob in v1.6.1; method
    // is inlined into MatrixManager, no standalone symbol confirmed)
    void Translate(const Vec3& t);

    // TODO: re-verify v1.6.1 MatrixStack::SetCurrentMatrix address (cited 0x0011a130
    // was stale v1.5.x -- resolves to SetMissCount in v1.6.1; method is inlined into
    // MatrixManager, no standalone symbol confirmed)
    void SetCurrentMatrix(const Matrix44& mat);
};

#ifdef __bada__
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack must be 0x848 bytes");
#endif

#endif
