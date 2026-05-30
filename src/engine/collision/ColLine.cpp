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
    if (t == TYPE_SPHERE) {
        ColSphere* s = static_cast<ColSphere*>(other);
        hit = s->IntersectsLine(*this) ? 1 : 0;
        // TODO: outNormal from SphereLine -- compute penetration vector, then negate
        //   per binary convention (binary @ 0x... points INTO sphere from line). Not yet ported.
    } else if (t == TYPE_LINE) {
        // TODO: LineLine collision helper not ported
        hit = 0;
    } else if (t == TYPE_AABB) {
        ColAABB* box = static_cast<ColAABB*>(other);
        hit = box->IntersectsLine(*this) ? 1 : 0;
        // TODO: outNormal
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

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: ColLine::ColLineLine -- auto stub
void ColLine::ColLineLine(ColLine*, ColLine*, Vec3*) {}
// ---- end AUTO-STUB MERGE ----
