// Analysed: 2026-05-04T00:00
#include "Math.h"
#include <cmath>

namespace Math {

// ASM-verified: 2026-08-06T05:16Z v1.6.1 Math::ClosestPointOnLine @ 0x0027542c..0x002754b7 (asm-inspector)
// Foot-of-perpendicular on the INFINITE line through A,B (not the segment --
// despite the name there is no t-clamp). Special cases write only x,y; out.z
// is left untouched (general path writes z=0). Branch order: A.y==B.y first.
//
// The port keeps -1/m where the binary keeps +1/m, so the asm reads vmov #240 /
// vmls / vsub against the binary's vmov #112 / vmla / vadd. That is ALGEBRAIC
// FOLDING, not a sign inversion, and it is bit-exact rather than merely close:
// IEEE division flips only the sign bit and RNE is symmetric under negation, so
// mp == -k exactly, and non-fused VFP vmls/vmla round the product once with
// sign-symmetric rounding. P.y - mp*P.x and P.y + k*P.x therefore produce the
// same bits, as do m - mp and m + k. Verified numerically at the perpendicular
// foot and beyond both endpoints. Do not "correct" the signs.
//
// One residual, unreachable and deliberately not matched: the binary stores
// out.x then RELOADS A.x from [r0] @0x2754a4, where the port keeps A.x in a
// register. These differ only if &out == &A, and the sole caller
// (ColSphereLine @0x0025d164) passes a distinct stack local.
void ClosestPointOnLine(_Vector3<float>& A, _Vector3<float>& B, _Vector3<float>& P, _Vector3<float>& out) {
    if (A.y == B.y) {
        out.x = P.x;
        out.y = A.y;
        return;
    }
    if (A.x == B.x) {
        out.x = A.x;
        out.y = P.y;
        return;
    }
    float m  = (B.y - A.y) / (B.x - A.x);
    float mp = -1.0f / m;
    float x  = ((P.y - mp * P.x) - A.y + m * A.x) / (m - mp);
    float y  = A.y + m * (x - A.x);
    out.x = x;
    out.y = y;
    out.z = 0.0f;
}

// ASM-verified: 2026-07-31T00:40Z v1.6.1 Math::LineIntersect @ 0x002752cc..0x0027542b (asm-inspector)
// 2D segment-segment intersection via determinant + AABB containment.
// Returns false if denom==0 OR the intersection point falls outside either
// segment's XY bounding box. On success writes only out.x and out.y -- out.z
// is intentionally untouched (matches binary). DEAD CODE in shipping binary
// (no callers).
bool LineIntersect(_Vector3<float>& A1, _Vector3<float>& A2, _Vector3<float>& B1, _Vector3<float>& B2, _Vector3<float>& out) {
    float dxA = A2.x - A1.x;
    float dyA = A2.y - A1.y;
    float dxB = B2.x - B1.x;
    float dyB = B2.y - B1.y;
    // Binary @0x2752f8: vmul s6,s2,s7 (dyB * -dxA) then @0x275304 vnmls s6,s3,s1
    // (s6 = -s6 + dyA*-dxB), i.e. dxA*dyB - dyA*dxB. The port had a PLUS here and
    // a comment asserting the minus, so the comment described correct algebra the
    // code did not implement.
    float denom = dxA * dyB - dyA * dxB;
    if (denom == 0.0f) return false;

    float S_A = dyA * A1.x - dxA * A1.y;
    // Binary @0x275314/0x27531c: dyB*B1.x - dxB*B1.y. The port had the sign
    // flipped AND mixed B2.x with B1.y.
    float S_B = dyB * B1.x - dxB * B1.y;
    float X   = (dxA * S_B - dxB * S_A) / denom;

    float minAx = A1.x < A2.x ? A1.x : A2.x;
    float maxAx = A1.x > A2.x ? A1.x : A2.x;
    if (X < minAx || X > maxAx) return false;

    // Binary @0x275364: vmul s12,s2,s4 then vnmls s12,s3,s5 -> dyA*S_B - dyB*S_A.
    // The port had this negated.
    float Y = (dyA * S_B - dyB * S_A) / denom;
    float minAy = A1.y < A2.y ? A1.y : A2.y;
    float maxAy = A1.y > A2.y ? A1.y : A2.y;
    if (Y < minAy || Y > maxAy) return false;

    float minBx = B1.x < B2.x ? B1.x : B2.x;
    float maxBx = B1.x > B2.x ? B1.x : B2.x;
    if (X < minBx || X > maxBx) return false;

    float minBy = B1.y < B2.y ? B1.y : B2.y;
    float maxBy = B1.y > B2.y ? B1.y : B2.y;
    if (Y < minBy || Y > maxBy) return false;

    out.x = X;
    out.y = Y;
    return true;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 binary @ 0x001b32d8 (re-analyst)
// Signed distance from P to the infinite plane through B with normal N.
// Param 3 is unused. N is NOT mutated. outSigned must be non-null --
// binary stores unconditionally (no null guard).
// Asm-inspector misread the Dot PLT thunk as Normalise; corrected by re-RE.
bool PointOnLineSide(const _Vector3<float>* P, const _Vector3<float>* B, const _Vector3<float>* /*unused*/, const _Vector3<float>* N, float* outSigned) {
    float signedDist = N->Dot(*P) - N->Dot(*B);
    *outSigned = signedDist;
    return signedDist > 0.0f;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 binary @ 0x001b3324 (asm-inspector)
// DEAD CODE in shipping binary. Standard Catmull-Rom spline:
// 0.5 * (2*p1 + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t^2 + (-p0+3*p1-3*p2+p3)*t^3)
// Same constants {2,3,4,5,0.5}, port inlines the Vec3 helper calls; cosmetic.
_Vector3<float> CatmullRom(const _Vector3<float>& p0, const _Vector3<float>& p1, const _Vector3<float>& p2,
                           const _Vector3<float>& p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return (p1 * 2.0f
        + (p2 - p0) * t
        + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
        + (p0 * (-1.0f) + p1 * 3.0f - p2 * 3.0f + p3) * t3) * 0.5f;
}

} // namespace Math
