// Analysed: 2026-05-04T00:00
#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include "collision/ColAABB.h"

// Binary @ 0x0019fc20
ColSphere::ColSphere() : Col(), radius(0.0f) {}

// Binary @ 0x0019fc50
ColSphere::ColSphere(Vec3 c, float r) : Col(), radius(r) {
    m_PrimaryPoint = c;
}

// Binary @ 0x0019feac -- vtable slot 3 (Collide); double-dispatch by other->GetType()
int ColSphere::Collide(Col* other, Vec3* outNormal) {
    int t = other->GetType();
    int hit = 0;
    if (t == TYPE_SPHERE) {
        ColSphere* s = static_cast<ColSphere*>(other);
        hit = Intersects(*s) ? 1 : 0;
        if (hit && outNormal) {
            Vec3 n(center().x - s->center().x, center().y - s->center().y, center().z - s->center().z);
            n.Normalise();
            *outNormal = n;
        }
    } else if (t == TYPE_LINE) {
        ColLine* l = static_cast<ColLine*>(other);
        hit = IntersectsLine(*l) ? 1 : 0;
        if (hit && outNormal) {
            Vec3 closest = ColLineClosestPoint(*l, center());
            Vec3 n(center().x - closest.x, center().y - closest.y, center().z - closest.z);
            n.Normalise();
            *outNormal = n;
        }
    } else if (t == TYPE_AABB) {
        ColAABB* box = static_cast<ColAABB*>(other);
        hit = box->IntersectsSphere(*this) ? 1 : 0;
        if (hit && outNormal) {
            float cx = center().x < box->m_Min().x ? box->m_Min().x : (center().x > box->m_Max.x ? box->m_Max.x : center().x);
            float cy = center().y < box->m_Min().y ? box->m_Min().y : (center().y > box->m_Max.y ? box->m_Max.y : center().y);
            float cz = center().z < box->m_Min().z ? box->m_Min().z : (center().z > box->m_Max.z ? box->m_Max.z : center().z);
            Vec3 n(cx - center().x, cy - center().y, cz - center().z);
            n.Normalise();
            *outNormal = n;
        }
    } else {
        // Unknown type -- recursive double-dispatch to other's slot 3
        return other->Collide(this, outNormal);
    }
    if (hit) { AddCollision(); other->AddCollision(); }
    return hit;
}

// Binary @ 0x0019fd70 -- DrawDebug; reset matrix, scale by radius, translate by center, draw sphere
void ColSphere::DrawDebug() {
    // TODO: 0x0019fd70 -- Mesh::DrawSphere(1.0f, colour, NULL) on unknown Mesh instance;
    //   matrix reset/scale/translate call sequence not yet RE'd. Mesh::DrawSphere exists but
    //   is also a binary BX LR stub, so this whole function is a no-op in the original.
}

bool ColSphere::Intersects(const ColSphere& other) const {
    Vec3 d(center().x - other.center().x, center().y - other.center().y, center().z - other.center().z);
    float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
    float radSum = radius + other.radius;
    return distSq <= radSum * radSum;
}

bool ColSphere::IntersectsLine(const ColLine& line) const {
    Vec3 closest = ColLineClosestPoint(line, center());
    Vec3 d(closest.x - center().x, closest.y - center().y, closest.z - center().z);
    float distSq = d.x*d.x + d.y*d.y + d.z*d.z;
    return distSq <= radius * radius;
}

bool ColSphere::Contains(const Vec3& p) const {
    Vec3 d(p.x - center().x, p.y - center().y, p.z - center().z);
    return (d.x*d.x + d.y*d.y + d.z*d.z) <= radius * radius;
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: ColSphere::ColSphereLine -- auto stub
void ColSphere::ColSphereLine(ColSphere*, ColLine*, Vec3*) {}
// STUB: ColSphere::ColSphereSphere -- auto stub
void ColSphere::ColSphereSphere(ColSphere*, ColSphere*, Vec3*) {}
// ---- end AUTO-STUB MERGE ----
