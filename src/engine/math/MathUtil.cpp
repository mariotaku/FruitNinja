// Analysed: 2026-05-04T00:00
#include "MathUtil.h"

namespace {
    float g_sqrtAsyncResult = 0.0f;
    float g_divAsyncResult  = 0.0f;
}

namespace Math {

// Binary @ 0x00194d50 — 4096-entry sin LUT @ 0x001be4a4; sinf() is equivalent
float SinIdx(unsigned short idx) {
    return sinf((float)idx * (2.0f * (float)M_PI / 65536.0f));
}

// Binary @ 0x00194d70 — phase-shifts SinIdx by +1024 (90 deg); shares sin LUT
float CosIdx(unsigned short idx) {
    return cosf((float)idx * (2.0f * (float)M_PI / 65536.0f));
}

// Binary @ 0x00194d98 — SinIdx/CosIdx with 100000.0f fallback when cos==0
float TanIdx(unsigned short idx) {
    float c = CosIdx(idx);
    return (c == 0.0f) ? 100000.0f : SinIdx(idx) / c;
}

// Binary @ 0x00194dcc — STUB in shipping binary (returns 0); no callers in game code
unsigned short AsinIdx(float /*x*/) {
    return 0; // Defunct: stubbed in binary
}

// Binary @ 0x00194dd0 — STUB in shipping binary (returns 0); no callers in game code
unsigned short AcosIdx(float /*x*/) {
    return 0; // Defunct: stubbed in binary
}

// Binary @ 0x00194dd4 — atan LUT @ GOT+0xd18 (129 int16); atan2f equivalent
short AtanIdx(float x) {
    return (short)(atan2f(x, 1.0f) * (32768.0f / (float)M_PI));
}

// Binary @ 0x00194eb0 — quadrant-folded atan LUT lookup; equivalent to atan2f(y,x)*65536/(2pi)
short Atan2Idx(float y, float x) {
    if (y == 0.0f && x == 0.0f) return 0;
    return (short)(atan2f(y, x) * (32768.0f / (float)M_PI));
}

// Binary @ 0x00195254 — vsqrt.f64 with NaN fallback to libc sqrt; sqrtf() is equivalent
float Sqrt(float x) {
    return sqrtf(x);
}

// Binary @ 0x0019521c — computes sqrtf eagerly, stores in cache for SqrtAsyncGet
void SqrtAsyncSet(float x) {
    g_sqrtAsyncResult = sqrtf(x);
}

// Binary @ 0x00194cf8 — returns last value computed by SqrtAsyncSet
float SqrtAsyncGet() {
    return g_sqrtAsyncResult;
}

// Binary @ 0x00194d14 — computes a/b eagerly, stores in cache for DivAsyncGet
void DivAsyncSet(float a, float b) {
    g_divAsyncResult = a / b;
}

// Binary @ 0x00194d34 — returns last value computed by DivAsyncSet
float DivAsyncGet() {
    return g_divAsyncResult;
}

} // namespace Math
