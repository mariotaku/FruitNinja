#ifndef FN_ENGINE_UTIL_TRANSITION_H
#define FN_ENGINE_UTIL_TRANSITION_H

// GROUP B -- easing / transition curves (v1.6.1 @0x0014e8c4..@0x0014eaa0).
//
// All functions at global scope (no namespace prefix in binary mangling: _Z..ff).
// First arg t = progress (usually [0,1]), second arg s = per-curve parameter.
// SinTransition is a required helper for SinInAndOut; also called directly from
// FruitCamera.cpp and ExplodyFruitModifier.cpp inline copies (follow-up: switch
// those inline copies to call SinTransition directly once this TU exists).

// ASM-spec v1.6.1 SquareTransition @0x0014e8c4  -- t*t, s ignored
float SquareTransition(float t, float s);

// ASM-spec v1.6.1 FullTransition @0x0014e8e0  -- constant 1.0, both args ignored
float FullTransition(float t, float s);

// ASM-spec v1.6.1 StraightTransition @0x0014e8e8  -- clamped linear: clamp(t*s, 0, 1)
float StraightTransition(float t, float s);

// ASM-spec v1.6.1 InAndOut @0x0014e918  -- trapezoid: ramp up/plateau/ramp down
float InAndOut(float t, float s);

// ASM-spec v1.6.1 JumpySinPulse @0x0014e948
// a = 0.5*s^2*((t+1)^3 - 1) / 7.0; return SinIdx(clamp_u16(a*32768)) * (1-t)
float JumpySinPulse(float t, float s);

// ASM-spec v1.6.1 JumpyPulse @0x0014e9ac
// Phase1 (t<=s): full sine arch; phase2 (t>s): small -0.2 undershoot
float JumpyPulse(float t, float s);

// ASM-spec v1.6.1 SinPulse @0x0014ea18  -- SinIdx(clamp_u16(t*32768*s))
float SinPulse(float t, float s);

// ASM-spec v1.6.1 SinTransition @0x0014ea38  -- sin(t*deg_degrees)/sin(deg_degrees)
// deg in degrees; 182.0 ~ 65536/360 converts to the 16-bit idx domain.
float SinTransition(float t, float deg);

// ASM-spec v1.6.1 SinInAndOut @0x0014ea8c  -- SinTransition(InAndOut(t,s), 115.0)
float SinInAndOut(float t, float s);

#endif  // FN_ENGINE_UTIL_TRANSITION_H
