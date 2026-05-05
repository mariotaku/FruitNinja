#ifndef FN_ENGINE_MATH_MATH_H
#define FN_ENGINE_MATH_MATH_H

#include "Vec3.h"

namespace Math {

// Binary @ 0x001b3248 — 2D foot-of-perpendicular in XY (Z=0); horiz/vert special-cased
void ClosestPointOnLine(Vec3 A, Vec3 B, Vec3 P, Vec3& out);

// Binary @ 0x001b30f8 — 2D segment-segment intersect in XY with AABB bounds check
bool LineIntersect(Vec3 A1, Vec3 A2, Vec3 B1, Vec3 B2, Vec3& out);

// Binary @ 0x001b32d8 — signed distance N*(P-B); returns true when > 0; param 3 unused
bool PointOnLineSide(const Vec3* P, const Vec3* B, const Vec3* /*unused*/, const Vec3* N, float* outSigned);

// Binary @ 0x001b3324 — DEAD CODE in shipping binary; standard Catmull-Rom spline
Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t);

} // namespace Math

#endif
