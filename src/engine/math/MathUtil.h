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

// Matches Math::SinIdx / Math::CosIdx (0x00194d50 / 0x00194dcc).
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

// Binary @ 0x00194d50 — 4096-entry sin LUT @ 0x001be4a4; sinf() is equivalent
float SinIdx(unsigned short idx);

// Binary @ 0x00194d70 — phase-shifts SinIdx by +1024 (90 deg); shares sin LUT
float CosIdx(unsigned short idx);

// Binary @ 0x00194d98 — SinIdx/CosIdx with 100000.0f fallback when cos==0
float TanIdx(unsigned short idx);

// Binary @ 0x00194dcc — STUB in shipping binary (returns 0); no callers in game code
unsigned short AsinIdx(float x);

// Binary @ 0x00194dd0 — STUB in shipping binary (returns 0); no callers in game code
unsigned short AcosIdx(float x);

// Binary @ 0x00194dd4 — atan LUT @ GOT+0xd18 (129 int16); atan2f equivalent
short AtanIdx(float x);

// Binary @ 0x00194eb0 — quadrant-folded atan LUT lookup; equivalent to atan2f(y,x)*65536/(2pi)
short Atan2Idx(float y, float x);

// Binary @ 0x00195254 — vsqrt.f64 with NaN fallback to libc sqrt; sqrtf() is equivalent
float Sqrt(float x);

// Binary @ 0x0019521c — fake-async cache; Set computes sqrtf eagerly, Get returns cached
void SqrtAsyncSet(float x);
float SqrtAsyncGet();

// Binary @ 0x00194d14 — fake-async cache; Set computes a/b eagerly, Get returns cached
void DivAsyncSet(float a, float b);
float DivAsyncGet();

} // namespace Math

#endif
