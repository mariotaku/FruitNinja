// Analysed: 2026-05-04T00:00 / 2026-06-07 (Col-vs-Col helpers + UpdateVertices)
#include "collision/ColAABB.h"
#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include "math/Math.h"
#include "math/MathUtil.h"
#include <cmath>

// Binary @ 0x00275d7c -- default ctor sets m_HalfExtents = Vector3::One (1,1,1), not zero,
// then rebuilds the cached corner verts -- a fresh ColAABB is a unit box, not a degenerate
// zero-size box. No live port-side callers of this no-arg ctor currently exist; fixed for
// fidelity regardless since a future caller should get the binary's actual init.
ColAABB::ColAABB() : Col(), m_HalfExtents(1.0f, 1.0f, 1.0f) {
    UpdateVertices();
}

// Binary @ 0x00275e2c -- ctor(centre, halfExtents): stores arg0->centre, arg1->half-extents,
// clears collide flag, then rebuilds cached corner verts.
ColAABB::ColAABB(_Vector3<float> center, _Vector3<float> halfExtents) : Col(), m_HalfExtents(halfExtents) {
    m_PrimaryPoint = center;
    ClearCollideFlag();
    UpdateVertices();
}

// Binary @ 0x00275ccc (duplicate body @ 0x00110320, called by the no-arg ctor) --
// rebuild the 8 cached corner verts from centre +- half-extents.
// Corner ordering preserves the binary's field-write pattern (see header layout).
void ColAABB::UpdateVertices() {
    float cx = m_PrimaryPoint.x, cy = m_PrimaryPoint.y, cz = m_PrimaryPoint.z;
    float hx = m_HalfExtents.x, hy = m_HalfExtents.y, hz = m_HalfExtents.z;
    float minX = cx - hx, maxX = cx + hx;
    float minY = cy - hy, maxY = cy + hy;
    float minZ = cz - hz, maxZ = cz + hz;

    // Corner i -> m_Corners[3i .. 3i+2]
    // C0 (+0x20): maxX maxY maxZ
    m_Corners[0]  = maxX; m_Corners[1]  = maxY; m_Corners[2]  = maxZ;
    // C1 (+0x2c): maxX minY maxZ
    m_Corners[3]  = maxX; m_Corners[4]  = minY; m_Corners[5]  = maxZ;
    // C2 (+0x38): minX minY maxZ
    m_Corners[6]  = minX; m_Corners[7]  = minY; m_Corners[8]  = maxZ;
    // C3 (+0x44): minX maxY maxZ
    m_Corners[9]  = minX; m_Corners[10] = maxY; m_Corners[11] = maxZ;
    // C4 (+0x50): maxX maxY minZ
    m_Corners[12] = maxX; m_Corners[13] = maxY; m_Corners[14] = minZ;
    // C5 (+0x5c): maxX minY minZ
    m_Corners[15] = maxX; m_Corners[16] = minY; m_Corners[17] = minZ;
    // C6 (+0x68): minX minY minZ
    m_Corners[18] = minX; m_Corners[19] = minY; m_Corners[20] = minZ;
    // C7 (+0x74): minX maxY minZ
    m_Corners[21] = minX; m_Corners[22] = maxY; m_Corners[23] = minZ;
}

// Binary @ 0x00275ecc -- AABB-vs-AABB overlap test + min-penetration face normal.
// d = box1.centre - box2.centre; per-axis overlap = |d.axis| - (h1.axis + h2.axis).
// Any axis with overlap >= 0 => separated. Otherwise pick the axis of smallest
// penetration depth and emit out.axis = -(depth * sign(d.axis)); other axes 0.
bool ColAABB::ColAABBAABB(ColAABB* box1, ColAABB* box2, _Vector3<float>* out) {
    _Vector3<float> d = box1->m_Center() - box2->m_Center();

    float ox = std::fabs(d.x) - (box1->m_HalfExtents.x + box2->m_HalfExtents.x);
    if (ox >= 0.0f) return false;
    float oy = std::fabs(d.y) - (box1->m_HalfExtents.y + box2->m_HalfExtents.y);
    if (oy >= 0.0f) return false;
    float oz = std::fabs(d.z) - (box1->m_HalfExtents.z + box2->m_HalfExtents.z);
    if (oz >= 0.0f) return false;

    if (out == 0) return true;

    *out = _Vector3<float>();                 // zero out first (binary loads _Vector3::Zero)
    ox = std::fabs(ox);
    oy = std::fabs(oy);
    oz = std::fabs(oz);

    if (ox < oy) {
        if (ox < oz) {
            float s = (d.x < 0.0f) ? -1.0f : 1.0f;
            out->x = -(ox * s);
            return true;
        }
    } else if (oy < oz) {
        float s = (d.y < 0.0f) ? -1.0f : 1.0f;
        out->y = -(oy * s);
        return true;
    }
    {
        float s = (d.z < 0.0f) ? -1.0f : 1.0f;
        out->z = -(oz * s);
    }
    return true;
}

// Binary @ 0x002760c4 -- AABB-vs-Sphere closest-point test + penetration normal.
// box == this, sphere->m_Center() is the sphere centre, sphere->radius the radius.
bool ColAABB::ColAABBSphere(ColAABB* box, ColSphere* sphere, _Vector3<float>* out) {
    _Vector3<float> d = box->m_Center() - sphere->center();
    float r = sphere->radius;

    if (out) *out = _Vector3<float>();        // zero out first

    float ox = std::fabs(d.x) - (box->m_HalfExtents.x + r);
    if (ox >= 0.0f) return false;
    float oy = std::fabs(d.y) - (box->m_HalfExtents.y + r);
    if (oy >= 0.0f) return false;
    float oz = std::fabs(d.z) - (box->m_HalfExtents.z + r);
    if (oz >= 0.0f) return false;

    // Closest point on the box to the sphere centre (clamp sphere centre into box span).
    _Vector3<float> closest;
    float cx = box->m_Center().x, hx = box->m_HalfExtents.x, sx = sphere->center().x;
    if (sx > cx + hx || sx < cx - hx) closest.x = (sx <= cx) ? (cx - hx) : (cx + hx);
    else                              closest.x = sx;
    float cy = box->m_Center().y, hy = box->m_HalfExtents.y, sy = sphere->center().y;
    if (sy > cy + hy || sy < cy - hy) closest.y = (sy <= cy) ? (cy - hy) : (cy + hy);
    else                              closest.y = sy;
    float cz = box->m_Center().z, hz = box->m_HalfExtents.z, sz = sphere->center().z;
    if (sz > cz + hz || sz < cz - hz) closest.z = (sz <= cz) ? (cz - hz) : (cz + hz);
    else                              closest.z = sz;

    if (closest == sphere->center()) {
        // Sphere centre is inside the box: emit min-penetration face normal.
        // (penetration depths ox/oy/oz are negative here; binary compares |.| )
        if (std::fabs(ox) < std::fabs(oy)) {
            if (std::fabs(oz) < std::fabs(ox)) {
                float s = (d.z < 0.0f) ? -1.0f : 1.0f;
                if (out) out->z = -(oz * s);
            } else {
                float s = (d.x < 0.0f) ? -1.0f : 1.0f;
                if (out) out->x = -(ox * s);
            }
        } else if (std::fabs(oz) < std::fabs(oy)) {
            float s = (d.z < 0.0f) ? -1.0f : 1.0f;
            if (out) out->z = -(oz * s);
        } else {
            float s = (d.y < 0.0f) ? -1.0f : 1.0f;
            if (out) out->y = -(oy * s);
        }
        return true;
    }

    // Closest point on box surface; push out along (closest - sphere centre).
    _Vector3<float> delta = closest - sphere->center();
    if (delta.MagnitudeSqr() - r * r >= 0.0f) return false;
    float dist = delta.Magnitude();
    delta.Normalise();
    delta *= std::fabs(dist - r);
    if (out) *out = delta;
    return true;
}

// Binary @ 0x002763b4 -- AABB-vs-Line (segment) separating-axis test + penetration normal.
// box == this, param line. Treats the segment as a degenerate box (midpoint + half-direction)
// for the broadphase, then tests one perpendicular SAT axis derived from the segment.
bool ColAABB::ColAABBLine(ColAABB* box, ColLine* line, _Vector3<float>* out) {
    // Degenerate box guard (binary: half-extents == Zero => no collision).
    if (box->m_HalfExtents == _Vector3<float>()) return false;

    _Vector3<float> lb = line->b;
    _Vector3<float> la = line->a();
    _Vector3<float> dir = lb - la;                 // segment direction
    _Vector3<float> halfDir = dir * 0.5f;
    _Vector3<float> mid = la + halfDir;            // segment midpoint
    _Vector3<float> lineHalf(std::fabs(halfDir.x), std::fabs(halfDir.y), std::fabs(halfDir.z));

    _Vector3<float> sep = box->m_Center() - mid;   // box.centre - segment midpoint
    float ox = std::fabs(sep.x) - (box->m_HalfExtents.x + lineHalf.x);
    if (ox > 0.0f) return false;
    float oy = std::fabs(sep.y) - (box->m_HalfExtents.y + lineHalf.y);
    if (oy > 0.0f) return false;
    float oz = std::fabs(sep.z) - (box->m_HalfExtents.z + lineHalf.z);
    if (oz > 0.0f) return false;

    // Build the segment's SAT axis (perpendicular candidate) and orient it.
    _Vector3<float> axis(dir.x, dir.y, -dir.z);
    float side;
    if (Math::PointOnLineSide(&box->m_Center(), &la, &lb, &axis, &side)) {
        axis = _Vector3<float>(-dir.x, dir.y, dir.z);
    }
    axis.Normalise();

    // Project all 8 box corners onto the axis, track [boxMin, boxMax].
    float cx = box->m_Center().x, cy = box->m_Center().y, cz = box->m_Center().z;
    float hx = box->m_HalfExtents.x, hy = box->m_HalfExtents.y, hz = box->m_HalfExtents.z;
    _Vector3<float> corner;
    float boxMin, boxMax, p;

    corner = _Vector3<float>(cx + hx, cy + hy, cz + hz); boxMin = boxMax = axis.Dot(corner);
    corner = _Vector3<float>(cx + hx, cy + hy, cz - hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx + hx, cy - hy, cz + hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx + hx, cy - hy, cz - hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx - hx, cy + hy, cz + hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx - hx, cy + hy, cz - hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx - hx, cy - hy, cz + hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);
    corner = _Vector3<float>(cx - hx, cy - hy, cz - hz); p = axis.Dot(corner); boxMin = Math::Min<float>(boxMin, p); boxMax = Math::Max<float>(boxMax, p);

    // Project the two segment endpoints; lineMin is the relevant separation bound.
    float pa = axis.Dot(la);
    float pb = axis.Dot(lb);
    float lineMin = Math::Min<float>(pa, pb);

    float overlap = lineMin - boxMax;
    if (overlap > 0.0f) return false;

    if (out == 0) return true;         // binary null-checks the out pointer here

    *out = _Vector3<float>();
    float aox = std::fabs(ox);
    float aoy = std::fabs(oy);
    float aoverlap = std::fabs(overlap);

    if (aox < aoy) {
        if (aoverlap < aox) {
            *out = axis * overlap;              // SAT-normal push
        } else {
            float s = (sep.x < 0.0f) ? -1.0f : 1.0f;
            out->x = s * aox;
        }
    } else {
        if (aoverlap < aoy) {
            *out = axis * overlap;              // SAT-normal push
        } else {
            float s = (sep.y < 0.0f) ? -1.0f : 1.0f;
            out->y = s * aoy;
        }
    }
    return true;
}

// Binary slot 3 @ 0x0027674c -- double-dispatch by other->GetType().
// SPHERE/LINE results are NEGATED before storing (binary points from sphere/line
// INTO the AABB); AABB-vs-AABB is stored as-is. On hit, both collide flags are set.
int ColAABB::Collide(Col* other, _Vector3<float>* outNormal) {
    int t = other->GetType();
    bool hit;

    if (t == TYPE_SPHERE) {
        // Binary writes outNormal, then negates it in place (sphere -> AABB direction).
        hit = ColAABBSphere(this, static_cast<ColSphere*>(other), outNormal);
        if (outNormal) *outNormal = -(*outNormal);
    } else if (t == TYPE_LINE) {
        hit = ColAABBLine(this, static_cast<ColLine*>(other), outNormal);
        if (outNormal) *outNormal = -(*outNormal);
    } else if (t == TYPE_AABB) {
        hit = ColAABBAABB(this, static_cast<ColAABB*>(other), outNormal);
    } else {
        return other->Collide(this, outNormal);
    }

    if (hit) { AddCollision(); other->AddCollision(); }
    return hit ? 1 : 0;
}

// Binary slot 4 @ 0x00276020
void ColAABB::DrawDebug() {
    // Port specific: debug wireframe draw uses GL fixed-function (Mesh::DrawCube);
    // no gameplay effect and no GLES2 debug-draw path is wired. No-op.
}
