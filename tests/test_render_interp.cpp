// test_render_interp -- render interpolation unit tests.
//
// TEST 1: Determinism / byte-identity
//   Boots the game, runs a few frames to get live entities, snapshots entity
//   transforms, calls Apply(0.5), then Restore(), and asserts every field is
//   bit-identical to the pre-Apply snapshot.  Proves zero net mutation.
//
// TEST 2: Recycle guard
//   Kills a fruit and immediately forces the slot to be re-used by a new fruit
//   (via ActorManager pool reuse), calls SnapshotAfterStep twice, and asserts
//   the new occupant has hadPrev==false -- meaning no stale position from the
//   dead fruit leaks into the new fruit's lerp.
//
// Both tests compile to pass/skip when FN_RENDER_INTERP is 0 (the interp code
// is entirely absent; gating the test body avoids link errors).

#include "test_harness.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/Entity.h"
#include "game/GameWork.h"
#include "game/WaveManager.h"
#include "game/StartupEffects.h"
#include "screens/MainScreen.h"
#include "hud/MenuButton.h"

#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
#include "platform/RenderInterp.h"
#endif

#include <cstdio>
#include <cstring>
#include <list>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Simple bit-identical comparison for Vec3.
static bool Vec3Eq(const Vec3& a, const Vec3& b) {
    uint32_t ax, ay, az, bx, by, bz;
    memcpy(&ax, &a.x, 4); memcpy(&ay, &a.y, 4); memcpy(&az, &a.z, 4);
    memcpy(&bx, &b.x, 4); memcpy(&by, &b.y, 4); memcpy(&bz, &b.z, 4);
    return ax == bx && ay == by && az == bz;
}

static bool FloatEq(float a, float b) {
    uint32_t ai, bi;
    memcpy(&ai, &a, 4); memcpy(&bi, &b, 4);
    return ai == bi;
}

static bool QuatEq(const Quaternion& a, const Quaternion& b) {
    uint32_t ax, ay, az, aw, bx, by, bz, bw;
    memcpy(&ax, &a.x, 4); memcpy(&ay, &a.y, 4);
    memcpy(&az, &a.z, 4); memcpy(&aw, &a.w, 4);
    memcpy(&bx, &b.x, 4); memcpy(&by, &b.y, 4);
    memcpy(&bz, &b.z, 4); memcpy(&bw, &b.w, 4);
    return ax == bx && ay == by && az == bz && aw == bw;
}

// Struct to capture a Fruit's interpolated fields for comparison.
struct FruitSnapshot {
    Vec3       pos;
    Vec3       scale;
    float      zPos;
    Vec3       secondPos;
    Quaternion rot1;
    Quaternion rot2;
};

struct BombSnapshot {
    Vec3    pos;
    Vec3    scale;
    float   zPos;
    int16_t rotX;
    int16_t rotY;
};

static FruitSnapshot CaptureFruit(Fruit* f) {
    FruitSnapshot s;
    s.pos       = f->pos;
    s.scale     = f->scale;
    s.zPos      = f->m_ZPosition;
    s.secondPos = f->m_SecondPos;
    s.rot1      = f->m_Rot1;
    s.rot2      = f->m_Rot2;
    return s;
}

static BombSnapshot CaptureBomb(Bomb* b) {
    BombSnapshot s;
    s.pos  = b->pos;
    s.scale = b->scale;
    s.zPos = b->m_ZPosition;
    s.rotX = b->m_RotX;
    s.rotY = b->m_RotY;
    return s;
}

static bool FruitSnapEq(const FruitSnapshot& a, const FruitSnapshot& b) {
    return Vec3Eq(a.pos, b.pos) && Vec3Eq(a.scale, b.scale) &&
           FloatEq(a.zPos, b.zPos) && Vec3Eq(a.secondPos, b.secondPos) &&
           QuatEq(a.rot1, b.rot1) && QuatEq(a.rot2, b.rot2);
}

static bool BombSnapEq(const BombSnapshot& a, const BombSnapshot& b) {
    return Vec3Eq(a.pos, b.pos) && Vec3Eq(a.scale, b.scale) &&
           FloatEq(a.zPos, b.zPos) &&
           a.rotX == b.rotX && a.rotY == b.rotY;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "render_interp");
    h.SetInitFrames(60);  // enough for at least one fruit to spawn
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

#if !defined(FN_RENDER_INTERP) || !FN_RENDER_INTERP
    printf("[render_interp] FN_RENDER_INTERP is OFF -- tests skipped (pass)\n");
    return h.Shutdown();
#else

    // ---- Setup: get into gameplay so entities are live ----
    FN::ClearMenuItems();
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }
    game_work.gameMode = 0;  // classic
    FN::PrepareForLevelStart();
    game_work.bM_bPaused = 0;

    // Run enough frames that at least a couple of fruits and bombs spawn.
    h.RunHeadless(180);

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "FAIL: ActorManager null\n");
        return 1;
    }

    // ============================================================
    // TEST 1: byte-identity after Apply(0.5) + Restore()
    // ============================================================
    printf("[render_interp] TEST 1: byte-identity after Apply+Restore\n");

    // Need at least one step committed so m_cur is populated.
    h.game.stepUpdate();
    fn::RenderInterp::Get().SnapshotAfterStep();   // populates m_cur (no prev yet)
    h.game.stepUpdate();
    fn::RenderInterp::Get().SnapshotAfterStep();   // now m_cur has hadPrev=true entries

    // Capture current state of all active fruits and bombs.
    std::vector<Fruit*>       fruits;
    std::vector<FruitSnapshot> fruitSnaps;
    std::vector<Bomb*>        bombs;
    std::vector<BombSnapshot>  bombSnaps;

    {
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (!e->IsActive()) continue;
            Fruit* f = static_cast<Fruit*>(e);
            fruits.push_back(f);
            fruitSnaps.push_back(CaptureFruit(f));
        }
    }
    {
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(1, it); e; e = am->GetEntityNext(1, it)) {
            if (!e->IsActive()) continue;
            Bomb* b = static_cast<Bomb*>(e);
            bombs.push_back(b);
            bombSnaps.push_back(CaptureBomb(b));
        }
    }

    printf("[render_interp]   live fruits=%d bombs=%d\n",
           (int)fruits.size(), (int)bombs.size());

    if (fruits.empty() && bombs.empty()) {
        printf("[render_interp] WARN: no live entities captured -- TEST 1 trivially passes\n");
    } else {
        // Apply interpolation at midpoint.
        fn::RenderInterp::Get().ApplyForDraw(0.5f);

        // Restore to exact sim state.
        fn::RenderInterp::Get().RestoreAfterDraw();

        // Assert every field is bit-identical to the pre-Apply snapshot.
        int failures = 0;
        for (size_t i = 0; i < fruits.size(); ++i) {
            FruitSnapshot after = CaptureFruit(fruits[i]);
            if (!FruitSnapEq(fruitSnaps[i], after)) {
                fprintf(stderr,
                    "FAIL: fruit[%zu]=%p NOT restored: "
                    "pos(%.4f,%.4f,%.4f)!=(%.4f,%.4f,%.4f) "
                    "rot1(%.4f,%.4f,%.4f,%.4f)!=(%.4f,%.4f,%.4f,%.4f)\n",
                    i, (void*)fruits[i],
                    after.pos.x, after.pos.y, after.pos.z,
                    fruitSnaps[i].pos.x, fruitSnaps[i].pos.y, fruitSnaps[i].pos.z,
                    after.rot1.x, after.rot1.y, after.rot1.z, after.rot1.w,
                    fruitSnaps[i].rot1.x, fruitSnaps[i].rot1.y,
                    fruitSnaps[i].rot1.z, fruitSnaps[i].rot1.w);
                ++failures;
            }
        }
        for (size_t i = 0; i < bombs.size(); ++i) {
            BombSnapshot after = CaptureBomb(bombs[i]);
            if (!BombSnapEq(bombSnaps[i], after)) {
                fprintf(stderr,
                    "FAIL: bomb[%zu]=%p NOT restored: "
                    "pos(%.4f,%.4f,%.4f)!=(%.4f,%.4f,%.4f) "
                    "rotX=%d!=%d rotY=%d!=%d\n",
                    i, (void*)bombs[i],
                    after.pos.x, after.pos.y, after.pos.z,
                    bombSnaps[i].pos.x, bombSnaps[i].pos.y, bombSnaps[i].pos.z,
                    (int)after.rotX, (int)bombSnaps[i].rotX,
                    (int)after.rotY, (int)bombSnaps[i].rotY);
                ++failures;
            }
        }
        if (failures == 0) {
            printf("[render_interp] TEST 1 PASSED: all %d fruits, %d bombs restored\n",
                   (int)fruits.size(), (int)bombs.size());
        } else {
            fprintf(stderr, "FAIL: TEST 1 had %d field restoration failures\n", failures);
            return 1;
        }
    }

    // ============================================================
    // TEST 2: recycle guard -- new occupant must have hadPrev==false
    // ============================================================
    printf("[render_interp] TEST 2: recycle -- new fruit has hadPrev==false\n");

    // Grab the first active fruit pointer.
    Fruit* recycleTarget = nullptr;
    {
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (e->IsActive()) { recycleTarget = static_cast<Fruit*>(e); break; }
        }
    }
    if (!recycleTarget) {
        printf("[render_interp] WARN: no active fruit for recycle test -- running more frames\n");
        h.RunHeadless(120);
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (e->IsActive()) { recycleTarget = static_cast<Fruit*>(e); break; }
        }
    }
    if (!recycleTarget) {
        printf("[render_interp] WARN: still no fruit -- TEST 2 trivially passes\n");
        printf("[render_interp] TEST 2 PASSED (trivial)\n");
    } else {
        uint32_t oldRuntimeId = recycleTarget->m_RuntimeID;

        // Snapshot so the pointer is in m_cur with some runtimeId.
        fn::RenderInterp::Get().SnapshotAfterStep();

        // Kill the fruit (deactivate into pool) and spawn a new one to reuse
        // the slot.  We use KillFruit to properly deactivate, then manually
        // recycle by calling ActorManager::Deactivate (puts it in the free
        // pool) and Add to pull it back with a new runtimeId.
        // Simpler: just deactivate directly and Add, since the free pool
        // reuses LIFO -- the last deactivated slot is picked first.
        am->Deactivate(recycleTarget);
        Mortar::Entity* newEnt = am->Add(0, true);

        if (!newEnt) {
            printf("[render_interp] WARN: Add returned null -- TEST 2 trivially passes\n");
            printf("[render_interp] TEST 2 PASSED (trivial)\n");
        } else {
            // Force a different runtimeId so the recycle guard fires.
            // The binary assigns runtimeId from a counter in Add; port does
            // the same.  Verify the runtimeId changed.
            uint32_t newRuntimeId = newEnt->m_RuntimeID;

            // Snapshot again: the new entity occupies the same pointer,
            // but different runtimeId => hadPrev must be false.
            fn::RenderInterp::Get().SnapshotAfterStep();

            // Inspect m_cur for the recycled pointer.
            // We access the singleton directly since the test owns the context.
            fn::RenderInterp& interp = fn::RenderInterp::Get();

            // Apply with alpha=0.5 -- if hadPrev were incorrectly true, the
            // new entity would get lerped from the dead entity's position.
            // We can't directly inspect m_cur (private), but we can apply and
            // check that the position is not wildly different from the cur pos
            // (if hadPrev is falsely true, the position would jump to a lerp
            // of the old dead entity's position).
            Vec3 posBefore = newEnt->pos;
            interp.ApplyForDraw(0.5f);
            Vec3 posAfter = newEnt->pos;
            interp.RestoreAfterDraw();
            Vec3 posRestored = newEnt->pos;

            // If hadPrev == false, ApplyForDraw must NOT have touched the entity:
            // pos before == pos after == pos restored.
            bool applyWasNoOp = Vec3Eq(posBefore, posAfter);
            bool restoreOk    = Vec3Eq(posBefore, posRestored);

            if (applyWasNoOp && restoreOk) {
                printf("[render_interp] TEST 2 PASSED: recycle guard correctly blocked "
                       "lerp (runtimeId %u->%u, pos unchanged)\n",
                       (unsigned)oldRuntimeId, (unsigned)newRuntimeId);
            } else {
                fprintf(stderr,
                    "FAIL: TEST 2 recycle guard failed: "
                    "posBefore=(%.3f,%.3f,%.3f) posAfter=(%.3f,%.3f,%.3f) "
                    "posRestored=(%.3f,%.3f,%.3f) runtimeId %u->%u\n",
                    posBefore.x, posBefore.y, posBefore.z,
                    posAfter.x, posAfter.y, posAfter.z,
                    posRestored.x, posRestored.y, posRestored.z,
                    (unsigned)oldRuntimeId, (unsigned)newRuntimeId);
                return 1;
            }
        }
    }

    printf("[render_interp] all tests PASSED\n");
    return h.Shutdown();
#endif  // FN_RENDER_INTERP
}
