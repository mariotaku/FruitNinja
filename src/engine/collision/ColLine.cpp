// Analysed: 2026-05-04T00:00
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include "collision/ColAABB.h"

ColLine::ColLine() : Col(), b() {}

ColLine::ColLine(Vec3 start, Vec3 end) : Col(), b(end) {
    m_PrimaryPoint = start;
}

// Binary slot 3 -- double-dispatch by other->GetType()
int ColLine::Collide(Col* other, Vec3* outNormal) {
    int t = other->GetType();
    int hit = 0;
    // ASM-spec v1.6.1 ColLine::Collide @ 0x0025ccf0: TYPE_SPHERE calls ColSphereLine
    // directly (no negate); TYPE_AABB calls ColAABBLine then unconditionally negates
    // and stores outNormal regardless of hit.
    if (t == TYPE_SPHERE) {
        ColSphere* s = static_cast<ColSphere*>(other);
        Vec3 norm;
        hit = ColSphere::ColSphereLine(s, this, &norm);
        // Binary 0x0025cd60-cd7c: calls ColSphereLine(sphere, line, outNormal)
        // directly -- no post-negate (matches ColSphereLine's own hit-only
        // write convention).
        if (hit && outNormal) *outNormal = norm;
    } else if (t == TYPE_LINE) {
        // Binary @ 0x0019f8ae: type-2 branch calls ColLineLine and uses its
        // return as the hit flag; the helper writes outNormal itself.
        ColLine* line = static_cast<ColLine*>(other);
        hit = ColLineLine(this, line, outNormal);
    } else if (t == TYPE_AABB) {
        ColAABB* box = static_cast<ColAABB*>(other);
        Vec3 norm;
        hit = box->ColAABBLine(box, this, &norm) ? 1 : 0;
        // Binary 0x0025cd2c-cd5c: calls ColAABBLine(box, line, outNormal) then
        // unconditionally negates the result and stores it (store happens even
        // when hit==0) -- matches the negate convention already ported in
        // ColAABB::Collide's SPHERE/LINE branches (ColAABB.cpp:246-250).
        if (outNormal) *outNormal = -norm;
    } else {
        return other->Collide(this, outNormal);
    }
    if (hit) { AddCollision(); other->AddCollision(); }
    return hit;
}

// Binary slot 4
void ColLine::DrawDebug() {
    // TODO: DrawLine helper not ported
}

// Binary @ 0x0019f4f0 -- segment-segment closest-approach (Ericson form).
// d1/d2 are the two segment directions; r is the offset between the two
// start points. denom = (d1.d1)(d2.d2) - (d1.d2)^2. When denom is below
// the parallel epsilon (1e-6, DAT_0019f6f8) the binary fixes s = 0 and
// solves t directly; otherwise it solves both parameters. A hit is reported
// only when the squared closest-approach distance is below 1e-5
// (DAT_0019f700) AND both clamped params lie in [0,1]. The out vector is the
// along-d1 displacement from the nearer endpoint of segment a.
int ColLine::ColLineLine(ColLine* a, ColLine* b, Vec3* out) {
    Vec3 d1 = a->b - a->a();   // a->Direction()
    Vec3 d2 = b->b - b->a();   // b->Direction()
    Vec3 r  = a->a() - b->a();

    float dd1 = d1.Dot(d1);    // s19
    float dd  = d1.Dot(d2);    // s16
    float dd2 = d2.Dot(d2);    // s17
    float c   = d1.Dot(r);     // s18
    float f   = d2.Dot(r);     // s0

    float denom = dd1 * dd2 - dd * dd;  // a*e - b*b

    float s;
    float t;
    if (denom < 1e-6f) {
        // Parallel / degenerate: pin s, solve t from the larger projection.
        s = 0.0f;
        if (dd > dd2) {
            t = c / dd;
        } else {
            t = f / dd2;
        }
    } else {
        s = (dd * f - dd2 * c) / denom;
        t = (dd1 * f - dd * c) / denom;
    }

    // diff = (a.a + s*d1) - (b.a + t*d2) = closest-on-a minus closest-on-b.
    Vec3 diff = (r + d1 * s) - d2 * t;

    if (diff.MagnitudeSqr() < 1e-5f &&
        s >= 0.0f && s <= 1.0f &&
        t >= 0.0f && t <= 1.0f) {
        Vec3 v;
        if (s < 0.5f) {
            v = d1 * s;
        } else {
            v = -d1 * (1.0f - s);
        }
        *out = v;
        return 1;
    }
    return 0;
}
