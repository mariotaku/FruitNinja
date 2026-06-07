// Analysed: 2026-05-04T00:00
#include "collision/ColAABB.h"
#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include <cstring>

ColAABB::ColAABB() : Col(), m_Max() {
    memset(m_Corners, 0, sizeof(m_Corners));
}

ColAABB::ColAABB(Vec3 min, Vec3 max) : Col(), m_Max(max) {
    m_PrimaryPoint = min;
    memset(m_Corners, 0, sizeof(m_Corners));
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

// TODO: 0x001b594c — ColAABBAABB: AABB-vs-AABB overlap test + penetration normal into Vec3*
void ColAABB::ColAABBAABB(ColAABB*, ColAABB*, Vec3*) {}
// TODO: 0x001b5ca8 — ColAABBLine: AABB-vs-Line slab intersection test + slab normal into Vec3*
void ColAABB::ColAABBLine(ColAABB*, ColLine*, Vec3*) {}
// TODO: 0x001b6224 — ColAABBSphere: AABB-vs-Sphere closest-point test + penetration normal into Vec3*
void ColAABB::ColAABBSphere(ColAABB*, ColSphere*, Vec3*) {}
