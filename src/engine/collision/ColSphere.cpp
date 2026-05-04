// Analysed: 2026-05-04T00:00
#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include "collision/ColAABB.h"

namespace Mortar {

// Binary @ 0x0019fc20
ColSphere::ColSphere() : Col(), center(m_PrimaryPoint), radius(0.0f) {}

// Binary @ 0x0019fc50
ColSphere::ColSphere(const Vec3& c, float r) : Col(), center(m_PrimaryPoint), radius(r) {
    m_PrimaryPoint = c;
}

// Binary @ 0x0019feac -- vtable slot 3 (Collide); double-dispatch by other->GetType()
int ColSphere::Collide(Col* other, Vec3* outNormal) {
    int t = other->GetType();
    int hit = 0;
    if (t == TYPE_SPHERE) {
        ColSphere* s = static_cast<ColSphere*>(other);
        hit = Intersects(*s) ? 1 : 0;
        // TODO: 0x0019feac -- outNormal from SphereSphere penetration vector
    } else if (t == TYPE_LINE) {
        ColLine* l = static_cast<ColLine*>(other);
        hit = IntersectsLine(*l) ? 1 : 0;
        // TODO: 0x0019feac -- outNormal from SphereLine penetration vector
    } else if (t == TYPE_AABB) {
        // TODO: 0x0019feac -- wire SphereAABB once helpers exist; flip normal sign per binary
        hit = 0;
    } else {
        // Unknown type -- recursive double-dispatch to other's slot 3
        return other->Collide(this, outNormal);
    }
    if (hit) { AddCollision(); other->AddCollision(); }
    return hit;
}

// Binary @ 0x0019fd70 -- DrawDebug; reset matrix, scale by radius, translate by center, draw sphere
void ColSphere::DrawDebug() {
    // TODO: 0x0019fd70 -- Mesh::DrawSphere(1.0f, colour, NULL); needs Mesh::DrawSphere helper
}

bool ColSphere::Intersects(const ColSphere& other) const {
    Vec3 d(center.x - other.center.x, center.y - other.center.y, center.z - other.center.z);
    float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
    float radSum = radius + other.radius;
    return distSq <= radSum * radSum;
}

bool ColSphere::IntersectsLine(const ColLine& line) const {
    Vec3 closest = ColLineClosestPoint(line, center);
    Vec3 d(closest.x - center.x, closest.y - center.y, closest.z - center.z);
    float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
    return distSq <= radius * radius;
}

bool ColSphere::Contains(const Vec3& p) const {
    Vec3 d(p.x - center.x, p.y - center.y, p.z - center.z);
    return (d.x*d.x + d.y*d.y + d.z*d.z) <= radius * radius;
}

}  // namespace Mortar
