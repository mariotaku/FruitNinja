#ifndef FN_PLATFORM_RENDERINTERP_H
#define FN_PLATFORM_RENDERINTERP_H

// render interpolation -- lerp entity transforms between
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
//   - Teleport-safety (Phase 2): mid-life edge-warp teleports (a live entity's
//     position field jumps far in one committed step -- e.g.
//     Fruit::CheckHasGoneOffscreen warping one sliced half off-screen while
//     the other half stays live) are NOT caught by the pool-reuse guard above
//     (same pointer, same runtimeId, still alive). Each interpolated POSITION
//     field is lerped only when |cur-prev| <= TELEPORT_DIST; above that it
//     snaps straight to cur. See TELEPORT_DIST's comment for the threshold
//     derivation. Applies to Fruit::pos, Fruit::m_SecondPos, Bomb::pos, and
//     Jiblet::pos; rotation/scale/zPos fields are unaffected (the audited
//     teleport sites only ever move position).
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
#include "math/_Vector3.h"
#include "math/Quaternion.h"
#include <unordered_map>
#include <cstdint>

namespace fn {

// Per-entity transform snapshot.  All fields for all types stored in
// one union-like struct to keep the map value-type simple.
struct EntitySnap {
    // Common (all types)
    _Vector3<float> pos;
    _Vector3<float> scale;

    // Fruit-specific
    float    zPosFruit;
    _Vector3<float> secondPos;
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
    // Two snapshot maps keyed by entity pointer.
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

    static _Vector3<float> LerpVec3(const _Vector3<float>& a, const _Vector3<float>& b, float t)
    {
        return _Vector3<float>(Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t), Lerpf(a.z, b.z, t));
    }
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);

    // Port specific: teleport-safety (Phase 2). Fruit::CheckHasGoneOffscreen
    // (Fruit.cpp edge-warps, ~1075-1155) snap a sliced half's pos/m_SecondPos
    // to a screen-far coordinate (jumps of ~110-570 units) as an offscreen-kill
    // detector; the other half sometimes stays live+onscreen the same frame, so
    // the warped field survives to the next SnapshotAfterStep and ApplyForDraw
    // would lerp prev(onscreen) -> cur(warped) into a 1-frame streak. Max
    // legitimate per-60Hz-step position delta is the fruit launch speed
    // (WaveManager::SpawnFruit: speed 9.5..11.0 * 1.075 boost =~ 11.8/step,
    // roughly doubling over a few seconds of sliced-fruit gravity growth --
    // still well under 30) vs. the smallest teleport jump (~110, the X edge
    // warp). TELEPORT_DIST=60 sits in that gap: any single-field delta this
    // large this step can only be a warp, never real motion, so snap straight
    // to `cur` instead of lerping.
    static const float TELEPORT_DIST;

    // Per-field snap-or-lerp: |cur-prev| > TELEPORT_DIST -> cur, else lerp.
    // Position fields only (pos / m_SecondPos / Jiblet pos) -- rotation/scale
    // are never teleported by the audited call sites.
    static _Vector3<float> LerpOrSnapVec3(const _Vector3<float>& a, const _Vector3<float>& b, float t)
    {
        _Vector3<float> d(b.x - a.x, b.y - a.y, b.z - a.z);
        if (d.Magnitude() > TELEPORT_DIST) return b;
        return LerpVec3(a, b, t);
    }
};

} // namespace fn

#endif // FN_RENDER_INTERP

#endif // FN_PLATFORM_RENDERINTERP_H
