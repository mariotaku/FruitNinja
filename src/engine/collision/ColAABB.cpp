// Analysed: 2026-05-04T00:00
#include "collision/ColAABB.h"
#include "collision/ColSphere.h"
#include "collision/ColLine.h"

ColAABB::ColAABB() : Col(), m_Max() {}

ColAABB::ColAABB(Vec3 min, Vec3 max) : Col(), m_Max(max) {
    m_PrimaryPoint = min;
}

// Binary slot 3 -- double-dispatch by other->GetType(); normal-sign flip on SPHERE/LINE
int ColAABB::Collide(Col* other, Vec3* outNormal) {
    int t = other->GetType();
    int hit = 0;
    if (t == TYPE_SPHERE) {
        ColSphere* s = static_cast<ColSphere*>(other);
        hit = IntersectsSphere(*s) ? 1 : 0;
        // TODO: outNormal from AABBSphere -- compute closest-point penetration vector,
        //   then negate (binary points from sphere INTO AABB). Normal computation not
        //   yet ported; outNormal left unwritten until full penetration math is RE'd.
    } else if (t == TYPE_LINE) {
        ColLine* l = static_cast<ColLine*>(other);
        hit = IntersectsLine(*l) ? 1 : 0;
        // TODO: outNormal from AABBLine -- compute slab-intersection normal, then
        //   negate per binary convention. Not yet ported.
    } else if (t == TYPE_AABB) {
        ColAABB* box = static_cast<ColAABB*>(other);
        hit = Intersects(*box) ? 1 : 0;
        // TODO: outNormal from AABBAABB penetration
    } else {
        return other->Collide(this, outNormal);
    }
    if (hit) { AddCollision(); other->AddCollision(); }
    return hit;
}

// Binary slot 4
void ColAABB::DrawDebug() {
    // TODO: Mesh::DrawCube helper not ported
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: ColAABB::ColAABBAABB -- auto stub
void ColAABB::ColAABBAABB(ColAABB*, ColAABB*, Vec3*) {}
// STUB: ColAABB::ColAABBLine -- auto stub
void ColAABB::ColAABBLine(ColAABB*, ColLine*, Vec3*) {}
// STUB: ColAABB::ColAABBSphere -- auto stub
void ColAABB::ColAABBSphere(ColAABB*, ColSphere*, Vec3*) {}
// ---- end AUTO-STUB MERGE ----
