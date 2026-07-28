#ifndef MORTAR_MATHUTIL_H
#define MORTAR_MATHUTIL_H

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// ASM-spec v1.6.1 GetSmallestDelta @ 0x001da7e0:
//   Returns signed angular delta from b to a, wrapped to (-180, +180].
//   Math::Abs<float> in binary (PLT @ 0x00114b38) = fabsf().
inline float GetSmallestDelta(float a, float b) {
    float delta = a - b;
    if (fabsf(delta) > 180.0f) {
        if (a <= b)
            return (a + 360.0f) - b;
        else
            return delta - 360.0f;
    }
    return delta;
}

// TODO: v1.6.1 0x001d8d74 (GetSmallestDeltaIdx) -- 16-bit angle-index analog of
//   GetSmallestDelta; wraps in the 16-bit index domain. Not yet RE'd.

// Matches v1.6.1 Math::SinIdx @0x002420ac / Math::CosIdx @0x002420d4.
// Binary reads from a 4096-entry float LUT keyed by (idx >> 4);
// we use sinf/cosf directly — same output, no LUT needed on modern FPUs.
// Returns float in [-1, 1]. Takes uint16_t so the angle wraps naturally
// for signed 16-bit inputs (0x8000..0xFFFF = negative half turn).
inline float SinIdx(uint16_t idx) {
    return sinf((float)idx * (float)(2.0 * M_PI / 65536.0));
}

inline float CosIdx(uint16_t idx) {
    return cosf((float)idx * (float)(2.0 * M_PI / 65536.0));
}

namespace Math {

// Min/Max templates -- match the binary's calling convention (Math::Min<T>(a,b)).
// No binary address; standard SAT helpers used by ColAABBLine @ 0x001b5ca8.
template<class T>
inline T Min(T a, T b) { return a < b ? a : b; }

template<class T>
inline T Max(T a, T b) { return a > b ? a : b; }


// v1.6.1 Math::SinIdx @0x002420ac — 4096-entry sin LUT; sinf() is equivalent
float SinIdx(unsigned short idx);

// v1.6.1 Math::CosIdx @0x002420d4 — phase-shifts SinIdx by +1024 (90 deg); shares sin LUT
float CosIdx(unsigned short idx);

// v1.6.1 Math::TanIdx @0x00242104 — SinIdx/CosIdx with 100000.0f fallback when cos==0
float TanIdx(unsigned short idx);

// v1.6.1 Math::AsinIdx @0x00242144 — binary body is a no-op stub (movs r0,#0; bx lr); no callers in game code. Ported faithfully.
unsigned short AsinIdx(float x);

// v1.6.1 Math::AcosIdx @0x0024214c — binary body is a no-op stub (movs r0,#0; bx lr); no callers in game code. Ported faithfully.
unsigned short AcosIdx(float x);

// v1.6.1 Math::AtanIdx @0x00242154 — atan LUT @ GOT+0xd18 (129 int16); atan2f equivalent
short AtanIdx(float x);

// v1.6.1 Math::Atan2Idx @0x00242258 — quadrant-folded atan LUT lookup; equivalent to atan2f(y,x)*65536/(2pi)
short Atan2Idx(float y, float x);

// v1.6.1 Math::Sqrt @0x00241fa4 — vsqrt.f64 with NaN fallback to libc sqrt; sqrtf() is equivalent
float Sqrt(float x);

// v1.6.1 Math::SqrtAsyncSet @0x00241fcc / Math::SqrtAsyncGet @0x00242010 — fake-async
// cache; Set computes sqrtf eagerly, Get returns cached
void SqrtAsyncSet(float x);
float SqrtAsyncGet();

// v1.6.1 Math::DivAsyncSet @0x00242030 / Math::DivAsyncGet @0x00242054 — fake-async
// cache; Set computes a/b eagerly, Get returns cached
void DivAsyncSet(float a, float b);
float DivAsyncGet();

} // namespace Math

#endif
