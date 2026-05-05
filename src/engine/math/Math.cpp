// Analysed: 2026-05-04T00:00
#include "Math.h"
#include <cmath>

namespace Math {

// Binary @ 0x001b3248 — 2D foot-of-perpendicular in XY (Z=0)
// Horizontal (A.y == B.y) and vertical (A.x == B.x) segments are special-cased.
void ClosestPointOnLine(Vec3 A, Vec3 B, Vec3 P, Vec3& out) {
    if (A.x == B.x) {
        // Vertical segment — clamp to [min,max] y
        float minY = A.y < B.y ? A.y : B.y;
        float maxY = A.y > B.y ? A.y : B.y;
        float clamped = P.y < minY ? minY : (P.y > maxY ? maxY : P.y);
        out = Vec3(A.x, clamped, 0.0f);
        return;
    }
    if (A.y == B.y) {
        // Horizontal segment — clamp to [min,max] x
        float minX = A.x < B.x ? A.x : B.x;
        float maxX = A.x > B.x ? A.x : B.x;
        float clamped = P.x < minX ? minX : (P.x > maxX ? maxX : P.x);
        out = Vec3(clamped, A.y, 0.0f);
        return;
    }
    Vec3 AB = B - A;
    Vec3 AP = P - A;
    float t = AP.Dot(AB) / AB.Dot(AB);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    out = A + AB * t;
    out.z = 0.0f;
}

// Binary @ 0x001b30f8 — 2D segment-segment intersect in XY with AABB bounds check
bool LineIntersect(Vec3 A1, Vec3 A2, Vec3 B1, Vec3 B2, Vec3& out) {
    float dx1 = A2.x - A1.x;
    float dy1 = A2.y - A1.y;
    float dx2 = B2.x - B1.x;
    float dy2 = B2.y - B1.y;

    float denom = dx1 * dy2 - dy1 * dx2;
    if (denom == 0.0f) return false;

    float t = ((B1.x - A1.x) * dy2 - (B1.y - A1.y) * dx2) / denom;
    float u = ((B1.x - A1.x) * dy1 - (B1.y - A1.y) * dx1) / denom;

    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return false;

    out = Vec3(A1.x + t * dx1, A1.y + t * dy1, 0.0f);
    return true;
}

// Binary @ 0x001b32d8 — signed distance N*(P-B); returns true when signed dist > 0
// Third parameter is unused in the binary.
bool PointOnLineSide(const Vec3* P, const Vec3* B, const Vec3* /*unused*/, const Vec3* N, float* outSigned) {
    Vec3 diff = *P - *B;
    float d = N->Dot(diff);
    if (outSigned) *outSigned = d;
    return d > 0.0f;
}

// Binary @ 0x001b3324 — DEAD CODE in shipping binary
// Standard Catmull-Rom spline: 0.5 * (2*p1 + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t^2 + (-p0+3*p1-3*p2+p3)*t^3)
Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return (p1 * 2.0f
        + (p2 - p0) * t
        + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
        + (p0 * (-1.0f) + p1 * 3.0f - p2 * 3.0f + p3) * t3) * 0.5f;
}

} // namespace Math
