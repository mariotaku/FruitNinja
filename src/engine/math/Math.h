#ifndef FN_ENGINE_MATH_MATH_H
#define FN_ENGINE_MATH_MATH_H

#include "_Vector3.h"

namespace Math {

// Binary @ 0x001b3248 — 2D foot-of-perpendicular in XY (Z=0); horiz/vert special-cased
// Params are non-const refs to match the binary's mangled ABI; A/B/P are read-only inside.
void ClosestPointOnLine(_Vector3<float>& A, _Vector3<float>& B, _Vector3<float>& P, _Vector3<float>& out);

// Binary @ 0x001b30f8 — 2D segment-segment intersect in XY with AABB bounds check
// Params are non-const refs to match the binary's mangled ABI; all four are read-only inside.
bool LineIntersect(_Vector3<float>& A1, _Vector3<float>& A2, _Vector3<float>& B1, _Vector3<float>& B2, _Vector3<float>& out);

// Binary @ 0x001b32d8 — signed distance N*(P-B); returns true when > 0; param 3 unused
bool PointOnLineSide(const _Vector3<float>* P, const _Vector3<float>* B, const _Vector3<float>* /*unused*/, const _Vector3<float>* N, float* outSigned);

// Binary @ 0x001b3324 — DEAD CODE in shipping binary; standard Catmull-Rom spline
_Vector3<float> CatmullRom(const _Vector3<float>& p0, const _Vector3<float>& p1, const _Vector3<float>& p2,
                           const _Vector3<float>& p3, float t);

} // namespace Math

#endif
