#ifndef FN_ENGINE_COLLISION_COL_SPHERE_H
#define FN_ENGINE_COLLISION_COL_SPHERE_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include <cmath>

// Forward declarations for double-dispatch
class ColLine;
class ColAABB;

// Binary vtable @ 0x001eb5f8. sizeof = 0x18 (24B):
//   Col base 0x00..0x14 (20B): vptr@0, m_PrimaryPoint (center) @+0x04..+0x0f, m_CollideFlag @+0x10.
//   radius float @ +0x14.
// center is NOT a separate member -- it aliases Col::m_PrimaryPoint @+0x04.
class ColSphere : public Col {
public:
    float  radius; // +0x14

    // center accessor -- returns the Col-base m_PrimaryPoint (binary reuse; no extra storage)
    Vec3& center()             { return m_PrimaryPoint; }
    const Vec3& center() const { return m_PrimaryPoint; }

    // Binary @ 0x0019fc20 -- default ctor
    ColSphere();
    // Binary @ 0x0019fc50 -- parameterized ctor
    ColSphere(Vec3 c, float r);

    virtual ~ColSphere() override {}

    // Binary slot 2
    virtual int GetType() const override { return TYPE_SPHERE; }

    // Binary slot 3 -- double-dispatch by other->GetType()
    virtual int Collide(Col* other, Vec3* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

    // Port-side intersection helpers (pre-hierarchy port, kept for call sites)
    bool Intersects(const ColSphere& other) const;
    bool IntersectsLine(const ColLine& line) const;
    bool Contains(const Vec3& p) const;

    // Binary @ 0x0019fdec -- sphere-vs-line penetration; returns 1 on hit.
    static int ColSphereLine(ColSphere*, ColLine*, Vec3*);
    // Binary @ 0x0019fc90 -- sphere-vs-sphere penetration; returns 1 on hit.
    static int ColSphereSphere(ColSphere*, ColSphere*, Vec3*);
};

#ifdef __bada__
static_assert(sizeof(ColSphere) == 24, "ColSphere binary size mismatch");
#endif

#endif
