// Analysed: 2026-05-04T00:00
#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include "collision/ColAABB.h"
#include "math/Math.h"
#include "math/MathUtil.h"  // Math::Sqrt

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
            // Clamp sphere centre into box span using center+halfExtents API.
            float bx = box->m_Center().x, hx = box->m_HalfExtents.x;
            float by = box->m_Center().y, hy = box->m_HalfExtents.y;
            float bz = box->m_Center().z, hz = box->m_HalfExtents.z;
            float cx = center().x < (bx - hx) ? (bx - hx) : (center().x > (bx + hx) ? (bx + hx) : center().x);
            float cy = center().y < (by - hy) ? (by - hy) : (center().y > (by + hy) ? (by + hy) : center().y);
            float cz = center().z < (bz - hz) ? (bz - hz) : (center().z > (bz + hz) ? (bz + hz) : center().z);
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

// Binary @ 0x0019fdec -- sphere (this) vs line (l) penetration.
//   closest = ClosestPointOnLine(l.a, l.b, this.center); delta = closest - center;
//   if MagnitudeSqr(delta) < radius^2: mag = Magnitude(delta); normalise(delta);
//   push = |radius - mag|; outVec = delta * push; return 1. Else return 0.
int ColSphere::ColSphereLine(ColSphere* self, ColLine* l, Vec3* outVec) {
    // Math::ClosestPointOnLine(A, B, P, out): A=line start, B=line end, P=sphere centre.
    Vec3 closest;
    Math::ClosestPointOnLine(l->a(), l->b, self->center(), closest);

    // Binary order: operator-(out, this=closest, param=center) -> closest - center.
    Vec3 delta = closest - self->center();
    float distSq = delta.MagnitudeSqr();
    if (distSq < self->radius * self->radius) {
        float mag = delta.Magnitude();   // computed before Normalise (binary calls both)
        delta.Normalise();
        float push = self->radius - mag;
        if (push < 0.0f) push = -push;   // vneg.mi -> fabs
        delta *= push;
        *outVec = delta;
        return 1;
    }
    return 0;
}

// Binary @ 0x0019fc90 -- sphere (this) vs sphere (other) penetration.
//   delta = this.center - other.center; outVec is pre-zeroed (Vec3::Zero).
//   radSum = this.radius + other.radius;
//   if MagnitudeSqr(delta) < radSum^2: d = Sqrt(distSq); push = d - radSum;
//     if d > 0 delta /= d; outVec = delta * push; return 1. Else (no hit) outVec stays zero, return 0.
int ColSphere::ColSphereSphere(ColSphere* self, ColSphere* other, Vec3* outVec) {
    Vec3 delta = self->center() - other->center();
    float distSq = delta.MagnitudeSqr();

    // Output initialised to zero before the penetration test (matches binary store).
    *outVec = Vec3::Zero();

    float radSum = self->radius + other->radius;
    if (distSq < radSum * radSum) {
        float d = Math::Sqrt(distSq);
        float push = d - radSum;
        if (d > 0.0f) {
            delta /= d;
        }
        *outVec = delta * push;
        return 1;
    }
    return 0;
}
