#ifndef MORTAR_COL_AABB_H
#define MORTAR_COL_AABB_H

#include "math/Vec3.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include <algorithm>

namespace Mortar {

struct ColAABB {
    Vec3 m_Min;
    Vec3 m_Max;

    ColAABB() : m_Min(), m_Max() {}
    ColAABB(const Vec3& min, const Vec3& max) : m_Min(min), m_Max(max) {}

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

    // AABB-AABB overlap test
    bool Intersects(const ColAABB& other) const {
        return m_Min.x <= other.m_Max.x && m_Max.x >= other.m_Min.x &&
               m_Min.y <= other.m_Max.y && m_Max.y >= other.m_Min.y &&
               m_Min.z <= other.m_Max.z && m_Max.z >= other.m_Min.z;
    }

    // AABB-Sphere overlap test
    bool IntersectsSphere(const ColSphere& sphere) const {
        // Clamp sphere center to AABB to find closest point
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

    // AABB-Line segment overlap test (slab method)
    bool IntersectsLine(const ColLine& line) const {
        Vec3 d = line.Direction();
        float tmin = 0.0f;
        float tmax = 1.0f;

        for (int i = 0; i < 3; i++) {
            float origin = (&line.a.x)[i];
            float dir = (&d.x)[i];
            float bmin = (&m_Min.x)[i];
            float bmax = (&m_Max.x)[i];

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
};

} // namespace Mortar

#endif
