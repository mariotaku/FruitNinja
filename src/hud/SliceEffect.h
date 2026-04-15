#ifndef FN_SLICE_EFFECT_H
#define FN_SLICE_EFFECT_H

//
// SliceEffect — the brief "slash line" visual that appears at the slice
// point when a fruit is cut. Spawned by AddSlice from
// Fruit::CollisionResponse (and twice from Fruit::Slice for critical
// hits), ticked + drawn each frame.
//
// Binary refs:
//   AddSlice        0x0016b480 (62 lines) — pool pop + list append + SFX
//   DrawSlices      0x00169ac8 (65 lines) — iterate + draw + remove
//   KeyframeInit    0x0016d0dc (_GLOBAL__I_GameTask.cpp static ctor)
//
// Analysed: 2026-04-15T15:00
//

#include "math/Vec3.h"
#include <cstdint>

namespace FN {

// Pool node layout (matches binary AddSlice stores @ 0x0016b480).
struct SliceEffect {
    // Default-construct to the free sentinel (timer < 0). MemoryPool::Create
    // uses `new T[N]` which only default-inits PODs — without this ctor the
    // backing slots would contain garbage and the Draw sweep would treat
    // them as live, OOB-indexing SLICE_KEYFRAMES.
    SliceEffect()
        : timer(-1.0f), impulse(0.0f), angleDeg(0.0f),
          pos(0, 0, 0), critical(0) {}

    // +0x00: timer — advances by dt * 40.0 * (0.75 if critical else 1.0)
    //        each frame. Entry removed when timer >= 6.0 (7 keyframes,
    //        6 integer steps). Matches DAT_00169c38 = 40.0.
    float  timer;

    // +0x04: impulse (display-space length scale). Binary stores the
    //        param_1.y at this offset — currently unused by the draw
    //        path but kept for layout parity.
    float  impulse;

    // +0x08: rotation angle in degrees (degrees-offset representation
    //        used throughout Mortar: Atan2Idx / 182.0 + base). Matches
    //        binary node+0x08 from AddSlice param_1.x.
    float  angleDeg;

    // +0x0c: world position (centred ortho).
    Vec3   pos;

    // +0x18: critical-hit flag. Selects slice_fx_crit.mmd model and the
    //        0.75× timer-rate multiplier.
    int    critical;
};

// Call once from GameInitialise. Allocates the slice-effect pool and
// loads the slice_fx[_crit].mmd models via MeshManager.
// Matches MemoryPool<SliceEffect::Node>::Create(32).
void SliceEffect_CreatePool(int capacity);
void SliceEffect_DestroyPool();

// Append a new slice line. Matches AddSlice (0x0016b480).
//   pos       — world position of the slice
//   angleDeg  — rotation angle in degrees (degrees-offset convention,
//               converted internally via 16-bit angle scale = 182.0)
//   impulse   — length-scale hint (unused by draw path, stored for parity)
//   critical  — selects the critical visual variant + 0.75× rate
void SliceEffect_Add(const Vec3& pos, float angleDeg, float impulse, bool critical);

// Tick + render every active slice. Matches DrawSlices (0x00169ac8).
// Called from GameDraw on the HUD 0x40 layer.
void SliceEffect_Draw(float dt);

} // namespace FN

#endif
