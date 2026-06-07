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

// TODO: 0x0019f4f0 -- line-segment vs line-segment closest-approach; compute the
//   two segment directions, MagnitudeSqr/Dot, clamp parameters, and write the
//   separating/penetration vector to the out Vec3 (impl @ 0x0019f4f0, thunk @ 0x000f8508).
void ColLine::ColLineLine(ColLine*, ColLine*, Vec3*) {}
