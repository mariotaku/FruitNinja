// Transition / easing curves -- v1.6.1 @0x0014e8c4..@0x0014eaa0
// Contiguous TU in the binary. All global scope (no namespace prefix in mangling).
// SinIdx substitution: binary uses a 4096-entry LUT; ::SinIdx (MathUtil.h) uses
// sinf() which is sub-degree equivalent -- accepted per project policy.
// DIFFERS: sinf via ::SinIdx for the binary's sin LUT (accepted per project policy).

#include "util/Transition.h"
#include "math/MathUtil.h"
#include <cstdint>

// ASM-spec v1.6.1 SquareTransition @0x0014e8c4
// s is ignored; binary disassembly: vmul.f32 s0,s0,s0; bx lr.
float SquareTransition(float t, float /*s*/) {
    return t * t;
}

// ASM-spec v1.6.1 FullTransition @0x0014e8e0
// Disassembly: vmov.f32 s0,#1.0; bx lr. Always 1.0.
float FullTransition(float /*t*/, float /*s*/) {
    return 1.0f;
}

// ASM-spec v1.6.1 StraightTransition @0x0014e8e8
// Clamped scaled linear: clamp(t*s, 0, 1).
float StraightTransition(float t, float s) {
    float v = t * s;
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v;
}

// ASM-spec v1.6.1 InAndOut @0x0014e918
// Trapezoid: ramp up over [0,s], plateau over [s,1-s], ramp down over [1-s,1].
float InAndOut(float t, float s) {
    if (s <= t) {
        if (1.0f - s < t) return (1.0f - t) / s;
        return 1.0f;
    }
    return t / s;
}

// ASM-spec v1.6.1 JumpySinPulse @0x0014e948
// a = (0.5*s*s*((t+1)^3 - 1)) / 7.0; return SinIdx(clamp_u16(a*32768)) * (1-t).
// Constants 0.5, 7.0, 32768.0 read from binary.
float JumpySinPulse(float t, float s) {
    float tp1 = t + 1.0f;
    float a = (0.5f * s * s * (tp1 * tp1 * tp1 - 1.0f)) / 7.0f;
    float v = a * 32768.0f;
    uint16_t idx = (v <= 0.0f) ? 0u : (uint16_t)(uint32_t)v;
    return SinIdx(idx) * (1.0f - t);
}

// ASM-spec v1.6.1 JumpyPulse @0x0014e9ac
// Two phases. Phase1 (t<=s): full sine arch over [0,s].
// Phase2 (t>s): small -0.2 undershoot. Constants 32768.0, -0.2 read from binary.
float JumpyPulse(float t, float s) {
    if (t <= s) {
        float v = (t / s) * 32768.0f;
        uint16_t idx = (v <= 0.0f) ? 0u : (uint16_t)(uint32_t)v;
        return SinIdx(idx);
    }
    float v = ((t - s) / (1.0f - s)) * 32768.0f;
    uint16_t idx = (v <= 0.0f) ? 0u : (uint16_t)(uint32_t)v;
    return SinIdx(idx) * -0.2f;
}

// ASM-spec v1.6.1 SinPulse @0x0014ea18
// SinIdx(clamp_u16(t*32768*s)) == sinf(pi*t*s). Tail-calls SinIdx in binary.
float SinPulse(float t, float s) {
    float v = t * 32768.0f * s;
    uint16_t idx = (v <= 0.0f) ? 0u : (uint16_t)(uint32_t)v;
    return SinIdx(idx);
}

// ASM-spec v1.6.1 SinTransition @0x0014ea38
// Normalised sine: sin(t*deg degrees) / sin(deg degrees).
// 182.0 ~ 65536/360 converts degrees to 16-bit idx domain.
// idxMax uses SIGNED vcvt.s32.f32; forward idx uses unsigned-saturating (vcvt.u32.f32).
// Constant 182.0 read @0x14ea88.
float SinTransition(float t, float deg) {
    uint16_t idxMax = (uint16_t)(int32_t)(deg * 182.0f);
    float v = t * (float)idxMax;
    uint16_t idx = (v <= 0.0f) ? 0u : (uint16_t)(uint32_t)v;
    float num = SinIdx(idx);
    float den = SinIdx(idxMax);
    return num / den;
}

// ASM-spec v1.6.1 SinInAndOut @0x0014ea8c
// Constant 115.0 read @0x14eaa0.
float SinInAndOut(float t, float s) {
    return SinTransition(InAndOut(t, s), 115.0f);
}
