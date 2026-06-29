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

    // Binary @ 0x0019f4f0 (thunk @ 0x000f8508). Line-segment vs line-segment
    // closest-approach test; returns 1 if the segments approach within the
    // distance epsilon (with both clamped params in [0,1]) and writes the
    // along-A separation vector to the out Vec3, else returns 0.
    // a = first segment (binary `this`), b = second segment (binary param_1).
    int ColLineLine(ColLine* a, ColLine* b, Vec3* out);
};

#ifdef __bada__
static_assert(sizeof(ColLine) == 32, "ColLine binary size mismatch");
#endif


#endif
