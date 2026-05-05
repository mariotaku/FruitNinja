#ifndef FN_ENGINE_COLLISION_COL_SPHERE_H
#define FN_ENGINE_COLLISION_COL_SPHERE_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include <cmath>

// Forward declarations for double-dispatch
class ColLine;
class ColAABB;

// Binary vtable @ 0x001eb5f8. sizeof = 0x18 (24B): base 0x14 + radius float at +0x14.
class ColSphere : public Col {
public:
    // center aliases m_PrimaryPoint (offset +0x04 from Col base, i.e. +0x08 from ColSphere*)
    Vec3&  center; // reference alias for m_PrimaryPoint
    float  radius; // +0x14

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

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ColSphere::ColSphereLine -- auto stub from binary missing-symbol set
    void ColSphereLine(ColSphere*, ColLine*, Vec3*);
    // STUB: ColSphere::ColSphereSphere -- auto stub from binary missing-symbol set
    void ColSphereSphere(ColSphere*, ColSphere*, Vec3*);
    // ---- end AUTO-STUB MERGE ----
};

#endif
