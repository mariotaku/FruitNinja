#ifndef FN_PLATFORM_RENDERINTERP_H
#define FN_PLATFORM_RENDERINTERP_H

// Port specific: render interpolation -- lerp entity transforms between
// the previous and current simulation steps so motion is smooth at any
// refresh rate.  ALL code in this file is gated behind FN_RENDER_INTERP
// and is never compiled under __bada__ (cross-build / asm-verify).
//
// Design:
//   - Platform-side only: no entity/GameWork fields added, no asm-verified
//     Update or Draw functions modified.
//   - Snapshot the transform state of interpolated entity types after each
//     committed simulation step; apply a lerp before GameTaskDraw; restore
//     the exact sim-step values afterwards so the next Update sees clean state.
//   - Keyed on Entity* (stable pool pointer) + m_RuntimeID + entityType to
//     detect pool-slot reuse (prevents teleport-lerp to a dead entity's position).
//
// Interpolated types and fields:
//   Fruit (type 0):  pos.xyz, scale.xyz, m_ZPosition(+0x9C),
//                    m_SecondPos(+0xC8), m_Rot1(+0xE0), m_Rot2(+0xF0) [slerp]
//   Bomb  (type 1):  pos.xyz, scale.xyz, m_ZPosition(+0x6C),
//                    m_RotX(+0x74), m_RotY(+0x76) [wrap-aware int16 lerp]
//   Jiblet(type 5):  pos.xyz only
//
// Not interpolated: camera/HUD (discrete), BombBlast/SplatEntity (batched),
//   Coin (optional later), blade trail (input-rate driven).

#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP

#include "entities/Entity.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/Jiblet.h"
#include "math/Vec3.h"
#include "math/Quaternion.h"
#include <unordered_map>
#include <cstdint>

namespace fn {

// Per-entity transform snapshot.  All fields for all types stored in
// one union-like struct to keep the map value-type simple.
struct EntitySnap {
    // Common (all types)
    Vec3     pos;
    Vec3     scale;

    // Fruit-specific
    float    zPosFruit;
    Vec3     secondPos;
    Quaternion rot1;
    Quaternion rot2;

    // Bomb-specific
    float    zPosBomb;
    int16_t  rotX;
    int16_t  rotY;

    // Validity guard: detect pool-slot reuse.
    uint32_t runtimeId;
    int      entityType;
    bool     hadPrev;
};

class RenderInterp {
public:
    static RenderInterp& Get();

    // Called after each committed stepUpdate().
    // Rotates cur -> prev, then re-populates cur from live entities.
    void SnapshotAfterStep();

    // Called before GameTaskDraw(). Writes lerped values into live entity
    // fields.  No-op when alpha <= 0.
    void ApplyForDraw(float alpha);

    // Called after GameTaskDraw(). Restores exact cur values so the next
    // stepUpdate() sees clean state.
    void RestoreAfterDraw();

private:
    // Port specific: two snapshot maps keyed by entity pointer.
    // m_prev = snapshot from the step before the current one.
    // m_cur  = snapshot from the most-recently committed step.
    std::unordered_map<Mortar::Entity*, EntitySnap> m_prev;
    std::unordered_map<Mortar::Entity*, EntitySnap> m_cur;

    // Capture a single entity of a known type into `dest`.
    void CaptureFruit (Mortar::Entity* e, EntitySnap& dest);
    void CaptureBomb  (Mortar::Entity* e, EntitySnap& dest);
    void CaptureJiblet(Mortar::Entity* e, EntitySnap& dest);

    // Write lerped values into a live entity.
    void ApplyFruit (Mortar::Entity* e, const EntitySnap& prev, const EntitySnap& cur, float alpha);
    void ApplyBomb  (Mortar::Entity* e, const EntitySnap& prev, const EntitySnap& cur, float alpha);
    void ApplyJiblet(Mortar::Entity* e, const EntitySnap& prev, const EntitySnap& cur, float alpha);

    // Restore exact cur values.
    void RestoreFruit (Mortar::Entity* e, const EntitySnap& cur);
    void RestoreBomb  (Mortar::Entity* e, const EntitySnap& cur);
    void RestoreJiblet(Mortar::Entity* e, const EntitySnap& cur);

    // Minimal helpers -- no existing lerp utilities in scope here.
    static float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
    static Vec3  LerpVec3(const Vec3& a, const Vec3& b, float t) {
        return Vec3(Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t), Lerpf(a.z, b.z, t));
    }
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
};

} // namespace fn

#endif // FN_RENDER_INTERP

#endif // FN_PLATFORM_RENDERINTERP_H
