#ifndef MORTAR_COL_SPHERE_H
#define MORTAR_COL_SPHERE_H

#include "math/Vec3.h"
#include "collision/ColLine.h"
#include <cmath>

namespace Mortar {

struct ColSphere {
    Vec3 center;
    float radius;

    ColSphere() : center(), radius(0.0f) {}
    ColSphere(const Vec3& c, float r) : center(c), radius(r) {}

    // Sphere-sphere intersection
    bool Intersects(const ColSphere& other) const {
        Vec3 d(center.x - other.center.x, center.y - other.center.y, center.z - other.center.z);
        float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
        float radSum = radius + other.radius;
        return distSq <= radSum * radSum;
    }

    // Sphere-line segment intersection (blade vs fruit collision)
    bool IntersectsLine(const ColLine& line) const {
        Vec3 closest = ColLineClosestPoint(line, center);
        Vec3 d(closest.x - center.x, closest.y - center.y, closest.z - center.z);
        float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
        return distSq <= radius * radius;
    }

    // Test if point is inside sphere
    bool Contains(const Vec3& p) const {
        Vec3 d(p.x - center.x, p.y - center.y, p.z - center.z);
        return (d.x*d.x + d.y*d.y + d.z*d.z) <= radius * radius;
    }
};

} // namespace Mortar

#endif
