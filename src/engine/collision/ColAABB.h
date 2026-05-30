#ifndef FN_ENGINE_COLLISION_COL_AABB_H
#define FN_ENGINE_COLLISION_COL_AABB_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include <algorithm>

// Binary vtable @ 0x001eb638. sizeof = 0x80 (128B):
//   Col base 0x00..0x14 (20B): vptr@0, m_PrimaryPoint (min corner) @+0x04..+0x0f, m_CollideFlag @+0x10.
//   m_Max Vec3 @ +0x14..+0x1f.
//   m_Corners float[24] (Vec3[8]) @ +0x20..+0x7f -- cached corner verts, rebuilt by UpdateVertices().
// m_Min is NOT a separate member -- it aliases Col::m_PrimaryPoint @+0x04.
class ColAABB : public Col {
public:
    Vec3         m_Max;        // +0x14
    float        m_Corners[24]; // +0x20..+0x7f -- 8 cached corner Vec3, rebuilt by UpdateVertices()

    // m_Min accessor -- returns Col-base m_PrimaryPoint (binary reuse; no extra storage)
    Vec3& m_Min()             { return m_PrimaryPoint; }
    const Vec3& m_Min() const { return m_PrimaryPoint; }

    ColAABB();
    ColAABB(Vec3 min, Vec3 max);

    virtual ~ColAABB() override {}

    // Binary slot 2
    virtual int GetType() const override { return TYPE_AABB; }

    // Binary slot 3
    virtual int Collide(Col* other, Vec3* outNormal) override;

    // Binary slot 4
    virtual void DrawDebug() override;

    Vec3 Center() const {
        return Vec3(
            (m_PrimaryPoint.x + m_Max.x) * 0.5f,
            (m_PrimaryPoint.y + m_Max.y) * 0.5f,
            (m_PrimaryPoint.z + m_Max.z) * 0.5f
        );
    }

    bool Contains(const Vec3& p) const {
        return p.x >= m_PrimaryPoint.x && p.x <= m_Max.x &&
               p.y >= m_PrimaryPoint.y && p.y <= m_Max.y &&
               p.z >= m_PrimaryPoint.z && p.z <= m_Max.z;
    }

    bool Intersects(const ColAABB& other) const {
        return m_PrimaryPoint.x <= other.m_Max.x && m_Max.x >= other.m_PrimaryPoint.x &&
               m_PrimaryPoint.y <= other.m_Max.y && m_Max.y >= other.m_PrimaryPoint.y &&
               m_PrimaryPoint.z <= other.m_Max.z && m_Max.z >= other.m_PrimaryPoint.z;
    }

    bool IntersectsSphere(const ColSphere& sphere) const {
        float cx = sphere.center().x;
        float cy = sphere.center().y;
        float cz = sphere.center().z;
        if (cx < m_PrimaryPoint.x) cx = m_PrimaryPoint.x;
        else if (cx > m_Max.x) cx = m_Max.x;
        if (cy < m_PrimaryPoint.y) cy = m_PrimaryPoint.y;
        else if (cy > m_Max.y) cy = m_Max.y;
        if (cz < m_PrimaryPoint.z) cz = m_PrimaryPoint.z;
        else if (cz > m_Max.z) cz = m_Max.z;

        float dx = cx - sphere.center().x;
        float dy = cy - sphere.center().y;
        float dz = cz - sphere.center().z;
        return (dx*dx + dy*dy + dz*dz) <= sphere.radius * sphere.radius;
    }

    bool IntersectsLine(const ColLine& line) const {
        Vec3 d = line.Direction();
        float tmin = 0.0f;
        float tmax = 1.0f;

        for (int i = 0; i < 3; i++) {
            float origin = (&line.a().x)[i];
            float dir    = (&d.x)[i];
            float bmin   = (&m_PrimaryPoint.x)[i];
            float bmax   = (&m_Max.x)[i];

            if (dir < -1e-8f || dir > 1e-8f) {
                float t1 = (bmin - origin) / dir;
                float t2 = (bmax - origin) / dir;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            } else {
                if (origin < bmin || origin > bmax) return false;
            }
        }
        return true;
    }

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ColAABB::ColAABBAABB -- auto stub from binary missing-symbol set
    void ColAABBAABB(ColAABB*, ColAABB*, Vec3*);
    // STUB: ColAABB::ColAABBLine -- auto stub from binary missing-symbol set
    void ColAABBLine(ColAABB*, ColLine*, Vec3*);
    // STUB: ColAABB::ColAABBSphere -- auto stub from binary missing-symbol set
    void ColAABBSphere(ColAABB*, ColSphere*, Vec3*);
    // ---- end AUTO-STUB MERGE ----
};

#ifdef __bada__
static_assert(sizeof(ColAABB) == 128, "ColAABB binary size mismatch");
#endif

#endif
