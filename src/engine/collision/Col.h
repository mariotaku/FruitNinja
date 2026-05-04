#ifndef FN_ENGINE_COLLISION_COL_H
#define FN_ENGINE_COLLISION_COL_H

#include "math/Vec3.h"
#include <cstdint>

namespace Mortar {

// Mortar::Col -- polymorphic collision base. Binary @ ctor 0x0019fae8 / vtable 0x001eb5d0.
// sizeof = 0x14 (20B); 5 vtable slots: ~Col(D2), ~Col(D0), GetType, Collide, DrawDebug.
// Subclasses overlay m_PrimaryPoint as their natural data (Sphere::center / Line::a / AABB::min).
class Col {
public:
    enum Type { TYPE_AABB = 0, TYPE_SPHERE = 1, TYPE_LINE = 2 };

    // Binary @ 0x0019fae8 -- base ctor sets vptr, clears m_PrimaryPoint + m_CollideFlag
    Col();
    // Binary @ 0x0019fab8 (D2) / 0x0019fb0c (D0) -- virtual dtor enables polymorphic delete via slot 1
    virtual ~Col() {}

    // Binary slot 2 -- pure-virtual; subclasses return TYPE_AABB / SPHERE / LINE
    virtual int GetType() const = 0;

    // Binary slot 3 -- pure-virtual; double-dispatch entry point
    // Returns nonzero on hit; outNormal receives penetration normal pointing AT this from other.
    virtual int Collide(Col* other, Vec3* outNormal) = 0;

    // Binary slot 4 -- debug draw; pure on base
    virtual void DrawDebug() = 0;

    // Binary @ 0x0019fad4 -- non-virtual; called from Collide impls when hit
    void AddCollision() { m_CollideFlag = 1; }
    // Binary @ 0x0019fadc -- non-virtual; clears the per-frame flag
    void ClearCollideFlag() { m_CollideFlag = 0; }

protected:
    Vec3      m_PrimaryPoint;   // +0x04 -- Sphere::center / Line::a / AABB::min (derived owns naming)
    uint8_t   m_CollideFlag;    // +0x10 -- set when collided this frame
    // padding +0x11..+0x13 to reach sizeof = 0x14
};

}  // namespace Mortar

#endif
