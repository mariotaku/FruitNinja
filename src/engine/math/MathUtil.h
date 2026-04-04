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

// 16-bit angle index sine: original uses a lookup table,
// approximate with sinf(idx * 2*PI / 65536)
inline uint16_t SinIdx(uint16_t idx) {
    float rad = (float)idx * (float)(2.0 * M_PI / 65536.0);
    float val = sinf(rad);
    // Map [-1, 1] to [0, 65535]
    return (uint16_t)((val * 0.5f + 0.5f) * 65535.0f);
}

inline uint16_t CosIdx(uint16_t idx) {
    float rad = (float)idx * (float)(2.0 * M_PI / 65536.0);
    float val = cosf(rad);
    return (uint16_t)((val * 0.5f + 0.5f) * 65535.0f);
}

#endif
