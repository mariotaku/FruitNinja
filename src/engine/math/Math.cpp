// Analysed: 2026-05-04T00:00
#include "Math.h"
#include <cmath>

namespace Math {

// ASM-verified: 2026-06-26 v1.6.1 Math::ClosestPointOnLine @ 0x0027542c (asm-inspector)
// Foot-of-perpendicular on the INFINITE line through A,B (not the segment --
// despite the name there is no t-clamp). Special cases write only x,y; out.z
// is left untouched (general path writes z=0). Branch order: A.y==B.y first.
void ClosestPointOnLine(Vec3 A, Vec3 B, Vec3 P, Vec3& out) {
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

// ASM-verified: 2026-05-06T15:30 v1.6.1 binary @ 0x001b30f8 (re-analyst)
// 2D segment-segment intersection via determinant + AABB containment.
// Returns false if denom==0 OR the intersection point falls outside either
// segment's XY bounding box. On success writes only out.x and out.y -- out.z
// is intentionally untouched (matches binary). DEAD CODE in shipping binary
// (no callers).
bool LineIntersect(Vec3 A1, Vec3 A2, Vec3 B1, Vec3 B2, Vec3& out) {
    float dxA = A2.x - A1.x;
    float dyA = A2.y - A1.y;
    float dxB = B2.x - B1.x;
    float dyB = B2.y - B1.y;
    // Binary's sign convention: denom = dyA*dxB + dyB*dxA (with one factor
    // entered as -dxA = A1.x-A2.x); algebraically dxA*dyB - dyA*dxB.
    float denom = dyA * dxB + dyB * (-(A1.x - A2.x));
    if (denom == 0.0f) return false;

    float S_A = dyA * A1.x - dxA * A1.y;
    float S_B = dxB * B1.y + dyB * B2.x;
    float X   = (dxA * S_B - dxB * S_A) / denom;

    float minAx = A1.x < A2.x ? A1.x : A2.x;
    float maxAx = A1.x > A2.x ? A1.x : A2.x;
    if (X < minAx || X > maxAx) return false;

    float Y = (dyB * S_A - dyA * S_B) / denom;
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
bool PointOnLineSide(const Vec3* P, const Vec3* B, const Vec3* /*unused*/, const Vec3* N, float* outSigned) {
    float signedDist = N->Dot(*P) - N->Dot(*B);
    *outSigned = signedDist;
    return signedDist > 0.0f;
}

// ASM-verified: 2026-05-06T15:30 v1.6.1 binary @ 0x001b3324 (asm-inspector)
// DEAD CODE in shipping binary. Standard Catmull-Rom spline:
// 0.5 * (2*p1 + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t^2 + (-p0+3*p1-3*p2+p3)*t^3)
// Same constants {2,3,4,5,0.5}, port inlines the Vec3 helper calls; cosmetic.
Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return (p1 * 2.0f
        + (p2 - p0) * t
        + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
        + (p0 * (-1.0f) + p1 * 3.0f - p2 * 3.0f + p3) * t3) * 0.5f;
}

} // namespace Math
