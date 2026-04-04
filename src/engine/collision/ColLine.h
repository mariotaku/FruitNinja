#ifndef MORTAR_COL_LINE_H
#define MORTAR_COL_LINE_H

#include "math/Vec3.h"

namespace Mortar {

// Line segment collision primitive
struct ColLine {
    Vec3 a; // start point
    Vec3 b; // end point

    ColLine() : a(), b() {}
    ColLine(const Vec3& start, const Vec3& end) : a(start), b(end) {}

    Vec3 Direction() const { return b - a; }
    float LengthSq() const { Vec3 d = Direction(); return d.x*d.x + d.y*d.y + d.z*d.z; }
};

// Closest point on line segment ab to point p, returns parameter t in [0,1]
inline float ColLineClosestParam(const ColLine& line, const Vec3& p) {
    Vec3 ab = line.b - line.a;
    Vec3 ap = p - line.a;
    float abLenSq = ab.x*ab.x + ab.y*ab.y + ab.z*ab.z;
    if (abLenSq < 1e-8f) return 0.0f;
    float t = (ap.x*ab.x + ap.y*ab.y + ap.z*ab.z) / abLenSq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

// Closest point on line segment to point p
inline Vec3 ColLineClosestPoint(const ColLine& line, const Vec3& p) {
    float t = ColLineClosestParam(line, p);
    return Vec3(
        line.a.x + t * (line.b.x - line.a.x),
        line.a.y + t * (line.b.y - line.a.y),
        line.a.z + t * (line.b.z - line.a.z)
    );
}

} // namespace Mortar

#endif
