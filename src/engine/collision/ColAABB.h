#ifndef FN_ENGINE_COLLISION_COL_AABB_H
#define FN_ENGINE_COLLISION_COL_AABB_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"

// Binary vtable @ 0x001eb638. sizeof = 0x80 (128B):
//   Col base 0x00..0x14 (20B): vptr@0, m_PrimaryPoint @+0x04..+0x0f, m_CollideFlag @+0x10.
//   m_HalfExtents Vec3 @ +0x14..+0x1f.
//   m_Corners float[24] (Vec3[8]) @ +0x20..+0x7f -- cached corner verts, rebuilt by UpdateVertices().
//
// IMPORTANT: the binary's ColAABB is a CENTER + HALF-EXTENTS box, NOT a min/max box.
//   m_PrimaryPoint @+0x04  == box centre (aliases Col::m_PrimaryPoint, no extra storage).
//   m_HalfExtents  @+0x14  == per-axis half-size (always positive).
// The 2-arg ctor stores arg0 -> centre, arg1 -> half-extents verbatim (ctor @ 0x001b5a88).
// All three Col-vs-Col tests (ColAABBAABB/Line/Sphere) read this representation.
class ColAABB : public Col {
public:
    Vec3         m_HalfExtents; // +0x14 -- per-axis half-size (positive)
    float        m_Corners[24]; // +0x20..+0x7f -- 8 cached corner Vec3, rebuilt by UpdateVertices()

    // centre accessor -- returns the Col-base m_PrimaryPoint (binary reuse; no extra storage)
    Vec3& m_Center()             { return m_PrimaryPoint; }
    const Vec3& m_Center() const { return m_PrimaryPoint; }

    ColAABB();
    // Binary @ 0x001b5a88 -- ctor(centre, halfExtents)
    ColAABB(Vec3 center, Vec3 halfExtents);

    virtual ~ColAABB() override {}

    // Binary slot 2
    virtual int GetType() const override { return TYPE_AABB; }

    // Binary slot 3 -- double-dispatch by other->GetType()
    virtual int Collide(Col* other, Vec3* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

    // Binary @ 0x001b58b8 -- rebuild the 8 cached corner verts from centre +- half-extents.
    void UpdateVertices();

    // ---- binary static helpers (this == box1; out receives penetration normal) ----
    // Binary @ 0x001b594c -- AABB-vs-AABB overlap + min-penetration face normal into out.
    bool ColAABBAABB(ColAABB* box1, ColAABB* box2, Vec3* out);
    // Binary @ 0x001b5ca8 -- AABB-vs-Line SAT test + separating-axis normal into out.
    bool ColAABBLine(ColAABB* box, ColLine* line, Vec3* out);
    // Binary @ 0x001b6224 -- AABB-vs-Sphere closest-point test + penetration normal into out.
    bool ColAABBSphere(ColAABB* box, ColSphere* sphere, Vec3* out);

    // ---- convenience wrappers (no separate binary entry; delegate to static helpers) ----
    // TODO: ColAABB::IntersectsSphere binary address unknown -- thin wrapper over ColAABBSphere.
    bool IntersectsSphere(const ColSphere& sphere);
    // TODO: ColAABB::IntersectsLine binary address unknown -- thin wrapper over ColAABBLine.
    bool IntersectsLine(const ColLine& line);
};

#ifdef __bada__
static_assert(sizeof(ColAABB) == 128, "ColAABB binary size mismatch");
#endif

#endif
