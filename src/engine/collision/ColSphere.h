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
    _Vector3<float>& center() { return m_PrimaryPoint; }
    const _Vector3<float>& center() const { return m_PrimaryPoint; }

    // ASM-spec v1.6.1 ColSphere::ColSphere() @ 0x0025cfd0
    ColSphere();
    // ASM-spec v1.6.1 ColSphere::ColSphere(Vec3, float) @ 0x0025d024
    ColSphere(_Vector3<float> c, float r);

    virtual ~ColSphere() override {}

    // Binary slot 2
    virtual int GetType() override { return TYPE_SPHERE; }

    // Binary slot 3 -- double-dispatch by other->GetType()
    virtual int Collide(Col* other, _Vector3<float>* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

    // Port-side intersection helpers (pre-hierarchy port, kept for call sites)
    bool Intersects(const ColSphere& other) const;
    bool Contains(const _Vector3<float>& p) const;

    // ASM-verified: 2026-06-26 v1.6.1 ColSphere::ColSphereLine @ 0x0025d114 (asm-inspector) -- sphere-vs-line penetration; returns 1 on hit.
    static int ColSphereLine(ColSphere*, ColLine*, _Vector3<float>*);
    // ASM-spec v1.6.1 ColSphere::ColSphereSphere @ 0x0025d228 -- sphere-vs-sphere penetration; returns 1 on hit.
    static int ColSphereSphere(ColSphere*, ColSphere*, _Vector3<float>*);
};

#ifdef __bada__
static_assert(sizeof(ColSphere) == 24, "ColSphere binary size mismatch");
#endif

#endif
