#ifndef FN_ENGINE_COLLISION_COL_LINE_H
#define FN_ENGINE_COLLISION_COL_LINE_H

#include "collision/Col.h"

// Binary vtable @ 0x001eb618. sizeof = 0x20 (32B):
//   Col base 0x00..0x14 (20B): vptr@0, m_PrimaryPoint (a endpoint) @+0x04..+0x0f, m_CollideFlag @+0x10.
//   b Vec3 @ +0x14..+0x1f.
// a is NOT a separate member -- it aliases Col::m_PrimaryPoint @+0x04.
class ColLine : public Col {
public:
    Vec3   b; // +0x14 (end point)

    // a accessor -- returns Col-base m_PrimaryPoint (binary reuse; no extra storage)
    Vec3& a()             { return m_PrimaryPoint; }
    const Vec3& a() const { return m_PrimaryPoint; }

    ColLine();
    ColLine(Vec3 start, Vec3 end);

    virtual ~ColLine() override {}

    // Binary slot 2
    virtual int GetType() const override { return TYPE_LINE; }

    // Binary slot 3
    virtual int Collide(Col* other, Vec3* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

    Vec3 Direction() const { return b - a(); }
    float LengthSq() const { Vec3 d = Direction(); return d.x*d.x + d.y*d.y + d.z*d.z; }

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ColLine::ColLineLine -- auto stub from binary missing-symbol set
    void ColLineLine(ColLine*, ColLine*, Vec3*);
    // ---- end AUTO-STUB MERGE ----
};

#ifdef __bada__
static_assert(sizeof(ColLine) == 32, "ColLine binary size mismatch");
#endif

// Closest point on line segment ab to point p, returns parameter t in [0,1]
inline float ColLineClosestParam(const ColLine& line, const Vec3& p) {
    Vec3 ab = line.b - line.a();
    Vec3 ap = p - line.a();
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
        line.a().x + t * (line.b.x - line.a().x),
        line.a().y + t * (line.b.y - line.a().y),
        line.a().z + t * (line.b.z - line.a().z)
    );
}

#endif
