#include "render/MatrixStack.h"

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x001175d4 (asm-inspector)
void MatrixStack::Reset() {
    m_Current.Identity();
    StackAt(0).Identity();
    m_Depth = 0;
    m_Version++;
}

// Port specific: per-stack Push/Pop. The binary has no MatrixStack
// method by this name -- GL push/pop happens via glPushMatrix/
// glPopMatrix inside MatrixManager::_UploadCurrentMatrices, not on
// the C++ stack.
void MatrixStack::Push() {
    // assert(m_Depth < 31);
    StackAt(m_Depth) = m_Current;
    m_Depth++;
    m_Version++;
}

void MatrixStack::Pop() {
    // assert(m_Depth > 0);
    m_Depth--;
    m_Current = StackAt(m_Depth);
    m_Version++;
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0012fa34 (asm-inspector)
void MatrixStack::Scale(const Vec3& s) {
    m_Current.ApplyScale(s.x, s.y, s.z);
    m_Version++;
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0012f97c (asm-inspector)
void MatrixStack::Translate(const Vec3& t) {
    m_Current.GlobalTranslate44(t);
    m_Version++;
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0011a130 (asm-inspector)
void MatrixStack::SetCurrentMatrix(const Matrix44& mat) {
    m_Current = mat;
    m_Version++;
}
