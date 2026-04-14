//
// SliceEffect — slash-line visual pool.
// See SliceEffect.h for binary refs.
//
// Analysed: 2026-04-14T01:00
//

#include "SliceEffect.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Mesh.h"
#include "asset/MeshManager.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include "math/Matrix44.h"
#include "Game.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace FN {

// Binary DrawSlices (0x169ac8) timing constants, resolved from
// DAT_00169c38 = 40.0 and DAT_00169c3c = 182.0.
//
//   timer += dt * 40.0 * (0.75 if critical else 1.0)
//   alive while timer < 6.0  (6 keyframes)
//   frame     = (int)timer   ∈ [0, 5]
//   frameU    = timer - frame (interpolation fraction)
//   angle_rad = m_SliceAngle * 182.0 → 16-bit index
//
// The 6-frame scale animation lerps between 6 Vec3 keyframes stored
// in BSS (runtime-initialised — I couldn't locate the init function
// statically). Port uses the approximation below which models a
// quick expand → hold → shrink curve reconstructed from the visual
// shape of slice_fx.mmd: starts near zero, ramps to full at frame 2,
// holds through frame 4, shrinks by frame 5.
static const float SLICE_TIME_RATE    = 40.0f;   // DAT_00169c38
static const float SLICE_TIME_CRIT    = 0.75f;   // crit rate multiplier
static const float SLICE_MAX_FRAMES   = 6.0f;    // lifetime in keyframes
static const float SLICE_ANGLE_SCALE  = 182.0f;  // DAT_00169c3c

// Approximated keyframes — replaces the binary's BSS table until I
// track down its runtime initialiser. Six Vec3 (scale.x, scale.y, scale.z).
// Sized for the slice_fx.mmd model in centred ortho space.
static const Vec3 SLICE_KEYFRAMES[6] = {
    Vec3(0.10f, 0.05f, 1.0f),  // frame 0: barely visible
    Vec3(0.60f, 0.30f, 1.0f),  // frame 1: quick stretch
    Vec3(1.00f, 0.50f, 1.0f),  // frame 2: full length
    Vec3(1.00f, 0.50f, 1.0f),  // frame 3: hold
    Vec3(0.80f, 0.40f, 1.0f),  // frame 4: begin shrink
    Vec3(0.30f, 0.15f, 1.0f),  // frame 5: collapse
};

static Mortar::MemoryPool<SliceEffect> s_Pool;

// 3D slice-fx models loaded via MeshManager. Matches binary's
// models[isCritical * 4 + 0xbc] array — port uses two distinct
// slots. Loaded lazily on first draw if MeshManager is ready.
static SmartPtr<Mortar::Model> s_SliceFxNormal;
static SmartPtr<Mortar::Model> s_SliceFxCrit;

// --- Pool / content ----------------------------------------------------

void SliceEffect_CreatePool(int capacity) {
    s_Pool.Create(capacity);

    // Load slice_fx.mmd + slice_fx_crit.mmd via MeshManager. Binary
    // paths verified from `find FruitNinjaBada/Data/models/effects/`:
    //   slice_fx.mmd          → normal slash line mesh
    //   slice_fx_crit.mmd     → critical slash line mesh
    Game* game = Game::GetInstance();
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (game && meshMgr) {
        if (!s_SliceFxNormal.IsValid()) {
            std::string path = game->data_dir + "/models/effects/slice_fx.mmd";
            s_SliceFxNormal = meshMgr->Load(path.c_str());
            printf("[SliceEffect] slice_fx.mmd valid=%d\n",
                   s_SliceFxNormal.IsValid());
        }
        if (!s_SliceFxCrit.IsValid()) {
            std::string path = game->data_dir + "/models/effects/slice_fx_crit.mmd";
            s_SliceFxCrit = meshMgr->Load(path.c_str());
            printf("[SliceEffect] slice_fx_crit.mmd valid=%d\n",
                   s_SliceFxCrit.IsValid());
        }
    }
    printf("[SliceEffect] CreatePool: capacity=%d\n", capacity);
}

void SliceEffect_DestroyPool() {
    s_Pool.Destroy();
    s_SliceFxNormal.Clear();
    s_SliceFxCrit.Clear();
}

// --- Spawn -------------------------------------------------------------

// Matches AddSlice (0x16b480), port simplified. Binary:
//   1. Pop a SliceEffect::Node from the MemoryPool
//   2. Fill timer=0, length, pos, angle, critical flag
//   3. Append to the global List<SliceEffect>
//   4. (if impulse > 2.5) random whoosh SFX — skipped in port
void SliceEffect_Add(const Vec3& pos, uint16_t angle, float impulse, bool critical) {
    SliceEffect* s = s_Pool.Pop();
    if (!s) return;  // pool exhausted

    s->timer     = 0.0f;
    s->_reserved = 0.0f;
    s->impulse   = impulse;
    s->pos       = pos;
    s->critical  = critical ? 1 : 0;
    s->angleRaw  = angle;

    // TODO: whoosh SFX via GameSound::SFXPlay when audio is wired.
}

// --- Tick + draw -------------------------------------------------------

// Matches DrawSlices (0x169ac8). Port flattened into one pass that:
//   - advances each slice timer by dt * 40.0 * (0.75 if crit)
//   - while timer < 6.0, computes keyframe lerp (frame, u) and draws
//     slice_fx[_crit].mmd scaled by the lerp + rotated by slice angle
//   - pushes expired slices back to the pool
void SliceEffect_Draw(float dt) {
    const int N = s_Pool.Capacity();

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    for (int i = 0; i < N; ++i) {
        SliceEffect* s = s_Pool.SlotAt(i);
        if (!s) continue;

        // Sentinel sweep: negative timer = free slot.
        if (s->timer < 0.0f) continue;

        // Advance timer.
        const float rate = SLICE_TIME_RATE *
                           (s->critical ? SLICE_TIME_CRIT : 1.0f);
        s->timer += dt * rate;

        if (s->timer >= SLICE_MAX_FRAMES) {
            // Expired — return to pool.
            s->timer = -1.0f;
            s_Pool.Push(s);
            continue;
        }

        // Pick model variant. Fall back to nothing (no draw) if the
        // mesh didn't load — SFX-less passthrough.
        Mortar::Model* model = s->critical ? s_SliceFxCrit.Get()
                                            : s_SliceFxNormal.Get();
        if (!model) continue;

        // Keyframe lerp from the 6-entry SLICE_KEYFRAMES table.
        const int   frame0 = (int)s->timer;   // 0..5
        const int   frame1 = frame0 + 1;
        const float u      = s->timer - (float)frame0;
        const Vec3& k0 = SLICE_KEYFRAMES[frame0 < 5 ? frame0 : 5];
        const Vec3& k1 = SLICE_KEYFRAMES[frame1 < 6 ? frame1 : 5];
        Vec3 kScale(
            k0.x + (k1.x - k0.x) * u,
            k0.y + (k1.y - k0.y) * u,
            k0.z + (k1.z - k0.z) * u
        );

        // Binary writes `RotZ44(Sin(angle*182), Cos(angle*182))` —
        // multiplying m_SliceAngle (deg-offset) by 182 gives the
        // 16-bit index. We already store angle as 16-bit raw, so
        // skip the ×182 and use it directly.
        const float rad = (float)s->angleRaw * (6.2831853f / 65536.0f);

        // Build the final transform: scale → rotZ → translate to pos.
        Matrix44 mat = Matrix44::MakeScale(kScale.x, kScale.y, kScale.z);
        mat.RotZ44(sinf(rad), cosf(rad));
        mat.GlobalTranslate44(Vec3(s->pos.x, s->pos.y, s->pos.z));

        // Draw through Model::Draw which handles texture + MVP upload.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        model->Draw(mat);
    }
}

} // namespace FN
