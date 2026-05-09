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

    MatrixStack() {
        m_Depth = 0;
        m_Version = 1;
        m_Current.Identity();
        m_Stack[0].Identity();
    }

    // ASM-verified: 2026-05-09 binary @ 0x001175d4 (asm-inspector)
    // Binary copies a global identity matrix into both m_Stack[0] and
    // m_Current. Calling Identity() on each is functionally equivalent.
    // (Binary stores m_Depth as a single byte (strb.w) -- port uses
    // 32-bit store; upper 3 bytes are unused either way.)
    void Reset() {
        m_Current.Identity();
        m_Stack[0].Identity();
        m_Depth = 0;
        m_Version++;
    }

    // Port specific: per-stack Push/Pop. The binary has no MatrixStack
    // method by this name -- GL push/pop happens via glPushMatrix/
    // glPopMatrix inside MatrixManager::_UploadCurrentMatrices, not on
    // the C++ stack. Port keeps this client-side abstraction so callers
    // (Font, HUD, etc.) can save/restore m_Current without touching GL
    // directly. Functionally a strict superset of the binary's flow.
    void Push() {
        assert(m_Depth < 31);
        m_Stack[m_Depth] = m_Current;
        m_Depth++;
        m_Version++;
    }

    void Pop() {
        assert(m_Depth > 0);
        m_Depth--;
        m_Current = m_Stack[m_Depth];
        m_Version++;
    }

    // ASM-verified: 2026-05-09 binary @ 0x0012fa34 (asm-inspector)
    void Scale(const Vec3& s) {
        m_Current.ApplyScale(s.x, s.y, s.z);
        m_Version++;
    }

    // ASM-verified: 2026-05-09 binary @ 0x0012f97c (asm-inspector)
    void Translate(const Vec3& t) {
        m_Current.GlobalTranslate44(t);
        m_Version++;
    }

    // ASM-verified: 2026-05-09 binary @ 0x0011a130 (asm-inspector)
    void SetCurrentMatrix(const Matrix44& mat) {
        m_Current = mat;
        m_Version++;
    }
};

#ifdef __bada__
static_assert(sizeof(MatrixStack) == 2120, "MatrixStack must be 0x848 bytes");
#endif

#endif
