#ifndef FN_ENGINE_COLLISION_COL_LINE_H
#define FN_ENGINE_COLLISION_COL_LINE_H

#include "collision/Col.h"

// Binary vtable @ 0x001eb618. sizeof = 0x20 (32B): base 0x14, b Vec3 at +0x14.
class ColLine : public Col {
public:
    // a aliases m_PrimaryPoint (+0x04 in Col base)
    Vec3&  a; // reference alias for m_PrimaryPoint (start point)
    Vec3   b; // +0x14 (end point)

    ColLine();
    ColLine(Vec3 start, Vec3 end);

    virtual ~ColLine() override {}

    // Binary slot 2
    virtual int GetType() const override { return TYPE_LINE; }

    // Binary slot 3
    virtual int Collide(Col* other, Vec3* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

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

#endif
