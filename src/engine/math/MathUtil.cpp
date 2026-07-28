// Analysed: 2026-05-04T00:00
#include "MathUtil.h"

namespace {
    float g_sqrtAsyncResult = 0.0f;
    float g_divAsyncResult  = 0.0f;
}

namespace Math {

// Port specific: SinIdx/CosIdx/AtanIdx/Atan2Idx are an accepted sinf/cosf/atan2f
// approximation of the binary's lookup tables (4096-entry sin LUT @0x001be4a4;
// quadrant-folded atan LUT). They are within the LUT's quantization (sub-degree),
// so visually identical, but a `sinf` call can never byte-pair with a table
// index+load -- they are EXCLUDED from the cross-build/asm-verify (left as-is by
// design; see feedback_index_trig_approximation_ok). The host build uses them.
#if !defined(__bada__)
// v1.6.1 Math::SinIdx @0x002420ac — 4096-entry sin LUT; sinf() is equivalent
float SinIdx(unsigned short idx) {
    return sinf((float)idx * (2.0f * (float)M_PI / 65536.0f));
}

// v1.6.1 Math::CosIdx @0x002420d4 — phase-shifts SinIdx by +1024 (90 deg); shares sin LUT
float CosIdx(unsigned short idx) {
    return cosf((float)idx * (2.0f * (float)M_PI / 65536.0f));
}
#endif

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::TanIdx @ 0x00242104 (asm-inspector)
// SinIdx/CosIdx with 100000.0f (=0x47C35000) fallback when cos==0.
float TanIdx(unsigned short idx) {
    float c = CosIdx(idx);
    return (c == 0.0f) ? 100000.0f : SinIdx(idx) / c;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::AsinIdx @ 0x00242144 (asm-inspector)
// Binary body is a no-op stub (4-byte body: movs r0,#0; bx lr); no callers. Ported faithfully.
unsigned short AsinIdx(float /*x*/) {
    return 0; // matches binary stub @0x00242144
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::AcosIdx @ 0x0024214c (asm-inspector)
// Binary body is a no-op stub (4-byte body: movs r0,#0; bx lr); no callers. Ported faithfully.
unsigned short AcosIdx(float /*x*/) {
    return 0; // matches binary stub @0x0024214c
}

// Port specific: atan LUT approximation -- excluded from cross-build/asm-verify
// (see SinIdx/CosIdx note above). atan2f never byte-pairs with the atan LUT lookup.
#if !defined(__bada__)
// v1.6.1 Math::AtanIdx @0x00242154 — atan LUT @ GOT+0xd18 (129 int16); atan2f equivalent
short AtanIdx(float x) {
    return (short)(atan2f(x, 1.0f) * (32768.0f / (float)M_PI));
}

// v1.6.1 Math::Atan2Idx @0x00242258 — quadrant-folded atan LUT lookup; equivalent to atan2f(y,x)*65536/(2pi)
short Atan2Idx(float y, float x) {
    if (y == 0.0f && x == 0.0f) return 0;
    return (short)(atan2f(y, x) * (32768.0f / (float)M_PI));
}
#endif

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::Sqrt(float) @ 0x00241fa4 (asm-inspector)
// Disasm: vcvt.f64.f32 / vsqrt.f64 / vcmp / libc fallback / vcvt.f32.f64.
// Binary widens to f64 then vsqrt.f64 + narrow back; port stays in f32 with
// fsqrts. Output identical for all valid inputs (hardware sqrt correctly
// rounded in both precisions). NaN fallback to libc in both.
float Sqrt(float x) {
    return sqrtf(x);
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::SqrtAsyncSet(float) @ 0x00241fcc (asm-inspector)
void SqrtAsyncSet(float x) {
    g_sqrtAsyncResult = sqrtf(x);
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::SqrtAsyncGet() @ 0x00242010 (asm-inspector)
float SqrtAsyncGet() {
    return g_sqrtAsyncResult;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::DivAsyncSet(float,float) @ 0x00242030 (asm-inspector)
void DivAsyncSet(float a, float b) {
    g_divAsyncResult = a / b;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 Math::DivAsyncGet() @ 0x00242054 (asm-inspector)
float DivAsyncGet() {
    return g_divAsyncResult;
}

} // namespace Math
