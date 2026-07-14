#include "render/MatrixStack.h"
#include "math/MathUtil.h"

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
void MatrixStack::Scale(const _Vector3<float>& s) {
    m_Current.ApplyScale(s.x, s.y, s.z);
    m_Version++;
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0012f97c (asm-inspector)
void MatrixStack::Translate(const _Vector3<float>& t) {
    m_Current.GlobalTranslate44(t);
    m_Version++;
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0011a130 (asm-inspector)
void MatrixStack::SetCurrentMatrix(const Matrix44& mat) {
    m_Current = mat;
    m_Version++;
}

// ASM-spec v1.6.1 MatrixStack::RotZ @0x001d0b80
void MatrixStack::RotZ(float deg) {
    unsigned short idx = (unsigned short)((int)(deg * 182.0f) & 0xffff);
    m_Current.RotZ44(Math::SinIdx(idx), Math::CosIdx(idx));  // left-mult
    m_Version++;
}

// ASM-spec v1.6.1 MatrixStack::TranslateLocal @0x0024a150
void MatrixStack::TranslateLocal(const _Vector3<float>& t) {
    m_Current.LocalTranslate44(t.x, t.y, t.z);  // right-mult (M*T)
    m_Version++;
}

// Row/left scale (S*M) -- mirrors Matrix44::Scale44 @0x0015d06c. Bumps m_Version
// like Scale/Translate so MatrixManager re-uploads on the next draw.
void MatrixStack::ScaleRows(float sx, float sy, float sz) {
    m_Current.Scale44(sx, sy, sz);
    m_Version++;
}
