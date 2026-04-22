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

#endif
