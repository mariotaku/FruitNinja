#ifndef FN_SLICE_EFFECT_H
#define FN_SLICE_EFFECT_H

//
// SliceEffect — the brief white "slash line" visual that appears at
// the slice point when a fruit is cut. Spawned by AddSlice from
// Fruit::CollisionResponse (and twice from Fruit::Slice for critical
// hits), ticked + drawn each frame.
//
// Binary refs:
//   AddSlice        0x0016b480 (62 lines) — pool pop + list append + SFX
//   DrawSlices      0x00169ac8 (65 lines) — iterate + draw + remove
//
// Binary diverges from the port in one important way: the binary
// renders each slice as a scaled + rotated 3D mesh (slice_fx.mmd /
// slice_fx_crit.mmd) with a 6-frame scale keyframe animation. For the
// port's first pass we use a flat textured quad that fades over the
// same ~0.4s window. The mesh path is a TODO once LoadFruitModels
// lands in the port.
//
// Analysed: 2026-04-14T01:00
//

#include "math/Vec3.h"
#include <cstdint>

namespace FN {

struct SliceEffect {
    // Default-construct to the free sentinel (timer < 0). MemoryPool::Create
    // uses `new T[N]` which only default-inits PODs — without this ctor the
    // backing slots would contain garbage and the Draw sweep would treat
    // them as live, OOB-indexing SLICE_KEYFRAMES.
    SliceEffect()
        : timer(-1.0f), _reserved(0.0f), impulse(0.0f),
          pos(0, 0, 0), critical(0), angleRaw(0) {}

    // +0x00: timer — binary advances this by `dt * 60 * (0.75 if crit)`
    //        each frame; entry removed when timer >= 6.0 (6 keyframes).
    //        Port: use seconds, fade over 0.4s.
    float  timer;

    // Reserved (binary +0x04 is 0 at spawn, unused elsewhere).
    float  _reserved;

    // +0x08: impulse magnitude (display-space length). Used by binary
    //        for the scale keyframe lookup; port ignores.
    float  impulse;

    // +0x0c: world position (pos of the slice in centred ortho).
    Vec3   pos;

    // +0x18: critical-hit flag. Binary swaps to slice_fx_crit.mmd and
    //        uses a different time-rate. Port tints red for v1.
    int    critical;

    // +0x1c: 16-bit angle. Port stores the rotation angle directly
    //        (converted from the binary's degree-offset representation).
    uint16_t angleRaw;
};

// Call once from GameInitialise. Allocates the slice-effect pool and
// loads the port's slice texture. Matches binary's
// MemoryPool<SliceEffect::Node>::Create(32) call.
void SliceEffect_CreatePool(int capacity);
void SliceEffect_DestroyPool();

// Append a new slice line. Matches AddSlice (0x16b480). `angleDir` is
// the blade direction vector used to compute the rotation; `impulse`
// is the blade speed used for sizing (not used in port v1); `pos` is
// the world position; `critical` selects the visual variant.
// SFX dispatch (whoosh) is skipped until GameSound is ported.
void SliceEffect_Add(const Vec3& pos, uint16_t angle, float impulse, bool critical);

// Tick + render every active slice. Matches DrawSlices (0x169ac8).
// Called from GameDraw on the HUD 0x40 layer.
void SliceEffect_Draw(float dt);

} // namespace FN

#endif
