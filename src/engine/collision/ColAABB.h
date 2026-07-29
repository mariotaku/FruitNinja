#ifndef FN_ENGINE_COLLISION_COL_AABB_H
#define FN_ENGINE_COLLISION_COL_AABB_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"

// v1.6.1 sizeof = 0x80 (128B):
//   Col base 0x00..0x14 (20B): vptr@0, m_PrimaryPoint @+0x04..+0x0f, m_CollideFlag @+0x10.
//   m_HalfExtents Vec3 @ +0x14..+0x1f.
//   m_Corners float[24] (Vec3[8]) @ +0x20..+0x7f -- cached corner verts, rebuilt by UpdateVertices().
//
// IMPORTANT: the binary's ColAABB is a CENTER + HALF-EXTENTS box, NOT a min/max box.
//   m_PrimaryPoint @+0x04  == box centre (aliases Col::m_PrimaryPoint, no extra storage).
//   m_HalfExtents  @+0x14  == per-axis half-size (always positive).
// The 2-arg ctor stores arg0 -> centre, arg1 -> half-extents verbatim (v1.6.1 ctor @ 0x00275e2c).
// The no-arg ctor defaults m_HalfExtents to Vector3::One (v1.6.1 ctor @ 0x00275d7c) -- a fresh
// ColAABB is a unit box, not a degenerate zero-size box.
// All three Col-vs-Col tests (ColAABBAABB/Line/Sphere) read this representation.
class ColAABB : public Col {
public:
    _Vector3<float> m_HalfExtents; // +0x14 -- per-axis half-size (positive)
    float        m_Corners[24]; // +0x20..+0x7f -- 8 cached corner Vec3, rebuilt by UpdateVertices()

    // centre accessor -- returns the Col-base m_PrimaryPoint (binary reuse; no extra storage)
    _Vector3<float>& m_Center() { return m_PrimaryPoint; }
    const _Vector3<float>& m_Center() const { return m_PrimaryPoint; }

    // v1.6.1 ColAABB::ColAABB @ 0x00275d7c -- default ctor: m_HalfExtents = (1,1,1), UpdateVertices().
    ColAABB();
    // v1.6.1 ColAABB::ColAABB @ 0x00275e2c -- ctor(centre, halfExtents)
    ColAABB(_Vector3<float> center, _Vector3<float> halfExtents);

    virtual ~ColAABB() override {}

    // Binary slot 2 -- v1.6.1 ColAABB::GetType @ 0x002769ac
    virtual int GetType() override { return TYPE_AABB; }

    // Binary slot 3 -- v1.6.1 ColAABB::Collide @ 0x0027674c -- double-dispatch by other->GetType()
    virtual int Collide(Col* other, _Vector3<float>* outNormal) override;

    // Binary slot 4 -- v1.6.1 ColAABB::DrawDebug @ 0x00276020
    virtual void DrawDebug() override;

    // v1.6.1 ColAABB::UpdateVertices @ 0x00275ccc (duplicate body @ 0x00110320) --
    // rebuild the 8 cached corner verts from centre +- half-extents.
    void UpdateVertices();

    // ---- binary static helpers (this == box1; out receives penetration normal) ----
    // v1.6.1 ColAABB::ColAABBAABB @ 0x00275ecc -- AABB-vs-AABB overlap + min-penetration face normal into out.
    bool ColAABBAABB(ColAABB* box1, ColAABB* box2, _Vector3<float>* out);
    // v1.6.1 ColAABB::ColAABBLine @ 0x002763b4 -- AABB-vs-Line SAT test + separating-axis normal into out.
    bool ColAABBLine(ColAABB* box, ColLine* line, _Vector3<float>* out);
    // v1.6.1 ColAABB::ColAABBSphere @ 0x002760c4 -- AABB-vs-Sphere closest-point test + penetration normal into out.
    bool ColAABBSphere(ColAABB* box, ColSphere* sphere, _Vector3<float>* out);
};

#ifdef __bada__
static_assert(sizeof(ColAABB) == 128, "ColAABB binary size mismatch");
#endif

#endif
