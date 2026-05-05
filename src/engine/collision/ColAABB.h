#ifndef FN_ENGINE_COLLISION_COL_AABB_H
#define FN_ENGINE_COLLISION_COL_AABB_H

#include "collision/Col.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include <algorithm>

// Binary vtable @ 0x001eb638. sizeof = 0x80 (128B):
//   base Col 0x14, m_Max Vec3 at +0x14, 8 cached corners Vec3 at +0x20..+0x7F.
class ColAABB : public Col {
public:
    // m_Min aliases m_PrimaryPoint (+0x04 in Col base)
    Vec3&  m_Min; // reference alias for m_PrimaryPoint
    Vec3   m_Max; // +0x14
    // TODO: +0x20..+0x7F -- 8 cached corner Vec3; UpdateVertices() recomputes from m_Min/m_Max
    // Vec3 m_Corners[8]; // +0x20

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
            (m_Min.x + m_Max.x) * 0.5f,
            (m_Min.y + m_Max.y) * 0.5f,
            (m_Min.z + m_Max.z) * 0.5f
        );
    }

    bool Contains(const Vec3& p) const {
        return p.x >= m_Min.x && p.x <= m_Max.x &&
               p.y >= m_Min.y && p.y <= m_Max.y &&
               p.z >= m_Min.z && p.z <= m_Max.z;
    }

    bool Intersects(const ColAABB& other) const {
        return m_Min.x <= other.m_Max.x && m_Max.x >= other.m_Min.x &&
               m_Min.y <= other.m_Max.y && m_Max.y >= other.m_Min.y &&
               m_Min.z <= other.m_Max.z && m_Max.z >= other.m_Min.z;
    }

    bool IntersectsSphere(const ColSphere& sphere) const {
        float cx = sphere.center.x;
        float cy = sphere.center.y;
        float cz = sphere.center.z;
        if (cx < m_Min.x) cx = m_Min.x; else if (cx > m_Max.x) cx = m_Max.x;
        if (cy < m_Min.y) cy = m_Min.y; else if (cy > m_Max.y) cy = m_Max.y;
        if (cz < m_Min.z) cz = m_Min.z; else if (cz > m_Max.z) cz = m_Max.z;

        float dx = cx - sphere.center.x;
        float dy = cy - sphere.center.y;
        float dz = cz - sphere.center.z;
        return (dx*dx + dy*dy + dz*dz) <= sphere.radius * sphere.radius;
    }

    bool IntersectsLine(const ColLine& line) const {
        Vec3 d = line.Direction();
        float tmin = 0.0f;
        float tmax = 1.0f;

        for (int i = 0; i < 3; i++) {
            float origin = (&line.a.x)[i];
            float dir    = (&d.x)[i];
            float bmin   = (&m_Min.x)[i];
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
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ColAABB::ColAABBAABB -- auto stub from binary missing-symbol set
    void ColAABBAABB(ColAABB*, ColAABB*, Vec3*);
    // STUB: ColAABB::ColAABBLine -- auto stub from binary missing-symbol set
    void ColAABBLine(ColAABB*, ColLine*, Vec3*);
    // STUB: ColAABB::ColAABBSphere -- auto stub from binary missing-symbol set
    void ColAABBSphere(ColAABB*, ColSphere*, Vec3*);
    // ---- end AUTO-STUB MERGE ----
};

#endif
