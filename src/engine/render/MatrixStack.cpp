#include "render/MatrixStack.h"
#include "math/MathUtil.h"

// ASM-spec v1.6.1 MatrixStack::Reset @0x0013f98c
void MatrixStack::Reset() {
    m_Current.Identity();
    StackAt(0).Identity();
    m_Depth = 0;
    m_Version++;
}

// DIFFERS: original = slot-indexed Store(uint8)/Restore(uint8) (v1.6.1
// MatrixStack::Store @0x001a3624 / MatrixStack::Restore @0x001a3654), using
// depth-based Push/Pop because the port's draw call sites nest save/restore
// around transforms and never allocate slot ids, so a depth cursor gives the
// same m_Current save/restore with no caller-side slot bookkeeping.
// Store does NOT bump m_Version in the binary (only Restore does); the port's
// Push bumps it, costing at most one redundant matrix re-upload.
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

// ASM-verified: 2026-07-27T14:40Z v1.6.1 MatrixStack::Scale @ 0x0015d100 (asm-inspector)
// Tail-calls _Matrix44<float>::Scale44 (PLT stub 0x0010370c -> GOT 0x2c15c0, .rel.plt
// type-22 reloc to _ZN9_Matrix44IfE7Scale44Efff), then bumps m_Version. LEFT-multiply
// (S*M), consistent with Translate/RotZ; TranslateLocal is the lone right-multiply.
void MatrixStack::Scale(const _Vector3<float>& s) {
    m_Current.Scale44(s.x, s.y, s.z);
    m_Version++;
}

// ASM-verified: 2026-07-27T14:40Z v1.6.1 MatrixStack::Translate @ 0x0015d040 (asm-inspector)
void MatrixStack::Translate(const _Vector3<float>& t) {
    m_Current.GlobalTranslate44(t);
    m_Version++;
}

// ASM-spec v1.6.1 MatrixStack::SetCurrentMatrix @0x00143720
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
