//
// SliceEffect — slash-line visual pool.
// See SliceEffect.h for binary refs.
//
// Analysed: 2026-04-15T15:00
//

#include "SliceEffect.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/Mesh.h"
#include "asset/MeshManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include "math/Matrix44.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "debug/Logger.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include "game/GameWork.h"

namespace FN {

// ---------------------------------------------------------------------
// Binary DrawSlices (0x00169ac8) constants
// ---------------------------------------------------------------------

// DAT_00169c38 = 0x42200000 = 40.0 — timer advance per-second base.
static const float SLICE_TIMER_RATE   = 40.0f;
// 0.75 multiplier for critical slices (dt * 40 * 0.75).
static const float SLICE_TIMER_CRIT   = 0.75f;
// DAT_00169c3c = 0x43360000 = 182.0 — degrees → 16-bit angle index.
static const float SLICE_ANGLE_SCALE  = 182.0f;
// 7 keyframes; lifetime = [0, 6.0] with integer frame index + fraction.
static const int   SLICE_NUM_FRAMES   = 7;
static const float SLICE_MAX_TIME     = 6.0f;

// Keyframe scale table — initialised by _GLOBAL__I_GameTask.cpp at
// 0x0016d0dc. Verified raw values:
//   DAT_0016d3ec = 0x3fd9999a = 1.700  (stretch X peak base)
//   DAT_0016d3f0 = 0x3e99999a = 0.300  (Y thin)
//   DAT_0016d3f4 = 0x3dcccccd = 0.100  (Y very thin / collapse)
// The slice line starts as a normal-sized blob, stretches to x=20 across
// the middle, then collapses.
static const Vec3 SLICE_KEYFRAMES[SLICE_NUM_FRAMES] = {
    Vec3( 1.0f, 1.0f, 1.0f),  // frame 0 — circle blob
    Vec3( 1.7f, 0.3f, 1.0f),  // frame 1 — beginning to stretch
    Vec3( 8.0f, 0.1f, 1.0f),  // frame 2 — thin line
    Vec3(20.0f, 0.1f, 1.0f),  // frame 3 — max stretch
    Vec3( 4.0f, 0.1f, 1.0f),  // frame 4 — snapping back
    Vec3( 0.1f, 0.1f, 0.1f),  // frame 5 — near-invisible
    Vec3( 0.1f, 0.1f, 0.1f),  // frame 6 — fully collapsed
};

// ---------------------------------------------------------------------
// Pool + loaded mesh content
// ---------------------------------------------------------------------

static Mortar::MemoryPool<SliceEffect> s_Pool;

// 3D slice-fx models loaded via MeshManager. Matches binary's
// g_sliceData + 0xbc / +0xc0 slots (Mortar::SmartPtr<Model>).
// Paths from 0x001bc93f / 0x001bc959:
//   "models/fruit/slice_fx.mmd"
//   "models/fruit/slice_fx_crit.mmd"
static Mortar::SmartPtr<Mortar::Model> s_SliceFxNormal;
static Mortar::SmartPtr<Mortar::Model> s_SliceFxCrit;

// ---------------------------------------------------------------------
// Pool / content
// ---------------------------------------------------------------------

void SliceEffect_CreatePool(int capacity) {
    s_Pool.Create(capacity);

    Game* game = Game::GetInstance();
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (game && meshMgr) {
        if (!s_SliceFxNormal.IsValid()) {
            // logical path; FileSystem_Direct prepends data_dir
            s_SliceFxNormal = meshMgr->Load("models/fruit/slice_fx.mmd");
            LOG_DEBUG("SliceEffect", "slice_fx.mmd valid=%d",
                      s_SliceFxNormal.IsValid());
        }
        if (!s_SliceFxCrit.IsValid()) {
            // logical path; FileSystem_Direct prepends data_dir
            s_SliceFxCrit = meshMgr->Load("models/fruit/slice_fx_crit.mmd");
            LOG_DEBUG("SliceEffect", "slice_fx_crit.mmd valid=%d",
                      s_SliceFxCrit.IsValid());
        }
    }
    LOG_DEBUG("SliceEffect", "CreatePool: capacity=%d", capacity);
}

void SliceEffect_DestroyPool() {
    s_Pool.Destroy();
    s_SliceFxNormal.SetNull();
    s_SliceFxCrit.SetNull();
}

// Binary: GameInit step 9 @ 0x0016c9a8..0x0016ca90.
// Allocates List<SliceEffect> + MemoryPool<Node>(capacity), stores in
// g_TaskState +0x64 / +0xc8. Port no-op: C-array pool above already serves.
// TODO: implement -- see docs/systems/gameinit-todos.md step 9.
void SliceEffect_CreateList(int /*capacity*/) {
    // TODO: implement SliceEffect_CreateList -- see docs/systems/gameinit-todos.md step 9.
}

// ---------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------

// Matches AddSlice (0x0016b480).
//   1. Pop a SliceEffect::Node from MemoryPool
//   2. memset first 0x20 bytes to 0 (timer = 0)
//   3. Fill angle, impulse, pos, critical flag
//   4. Append to the global List<SliceEffect>
//   5. (if impulse > 2.5 AND 1/3 RNG) play Clean-Slice-1/2/3 SFX
//      — port now calls through GameSound (no-op backend for now).
void SliceEffect_Add(const Vec3& pos, float angleDeg, float impulse, bool critical) {
    SliceEffect* s = s_Pool.Pop();
    if (!s) return;  // pool exhausted

    s->timer    = 0.0f;
    s->impulse  = impulse;     // +0x04 (param_1.y)
    s->angleDeg = angleDeg;    // +0x08 (param_1.x)
    s->pos      = pos;         // +0x0c (Vec3*)
    s->critical = critical ? 1 : 0;

    // Clean-Slice SFX gate from binary (0x0016b480):
    //   if (impulse > 2.5f && Rand32(3)==0 && Rand32(3)==0)
    //       name = (Rand32(2)==0) ? "Clean-Slice-1" : "Clean-Slice-3"
    //       GameSound::SFXPlay(name, 1.0, pitch, cb)
    // Compound gate gives ~1/9 actual rate. "Clean-Slice-2" is the binary's
    // fall-through path when one gate fails and is never played here.
    if (impulse > 2.5f && (rand() % 3) == 0 && (rand() % 3) == 0) {
        Game* g = Game::GetInstance();
        if (g && game_work.mGameSound) {
            const char* name = (rand() % 2 == 0) ? "Clean-Slice-1" : "Clean-Slice-3";
            game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------
// Tick + draw
// ---------------------------------------------------------------------

// Matches DrawSlices (0x00169ac8). Single pass:
//   - advance timer by dt * 40.0 * (0.75 if critical else 1.0)
//   - while timer < 6.0: compute (frame, frac) from timer, lerp between
//     keyframe[frame] and keyframe[frame+1], build Scale * RotZ *
//     Translate, draw the slice_fx[_crit].mmd model
//   - on timer >= 6.0: return the slot to the pool
void SliceEffect_Draw(float dt) {
    const int N = s_Pool.Capacity();

    for (int i = 0; i < N; ++i) {
        SliceEffect* s = s_Pool.SlotAt(i);
        if (!s) continue;

        // Sentinel sweep: negative timer = free slot.
        if (s->timer < 0.0f) continue;

        // Advance timer.
        const float rate = SLICE_TIMER_RATE *
                           (s->critical ? SLICE_TIMER_CRIT : 1.0f);
        s->timer += dt * rate;

        if (s->timer >= SLICE_MAX_TIME) {
            // Expired — mark as free sentinel and return to pool.
            s->timer = -1.0f;
            s_Pool.Push(s);
            continue;
        }

        // Keyframe interpolation (lerp from keyframe[frame] toward
        // keyframe[frame+1], matches binary DrawSlices inner loop).
        int frame = (int)s->timer;
        if (frame < 0) frame = 0;
        if (frame >= SLICE_NUM_FRAMES - 1) frame = SLICE_NUM_FRAMES - 2;
        const float frac = s->timer - (float)frame;
        const Vec3& kA = SLICE_KEYFRAMES[frame];
        const Vec3& kB = SLICE_KEYFRAMES[frame + 1];
        const Vec3 scale(
            kA.x + (kB.x - kA.x) * frac,
            kA.y + (kB.y - kA.y) * frac,
            kA.z + (kB.z - kA.z) * frac
        );

        // Pick model variant.
        Mortar::Model* model = s->critical ? s_SliceFxCrit.Get()
                                           : s_SliceFxNormal.Get();
        if (!model) continue;

        // Build the final transform: Scale → RotZ → Translate.
        // Binary (0x00169bd8): Scale44 → RotZ44(sin, cos) → Translate44.
        //   sin = SinIdx((uint16)(int)(182.0 * angleDeg))
        //   cos = CosIdx(...)
        const uint16_t angle16 = (uint16_t)(int)(SLICE_ANGLE_SCALE * s->angleDeg);
        const float rad = (float)(int16_t)angle16 * (6.2831853f / 65536.0f);
        const float sinA = sinf(rad);
        const float cosA = cosf(rad);

        Matrix44 mat = Matrix44::MakeScale(scale.x, scale.y, scale.z);
        mat.RotZ44(sinA, cosA);
        mat.GlobalTranslate44(s->pos);

        model->Draw(mat);
    }
}

} // namespace FN
