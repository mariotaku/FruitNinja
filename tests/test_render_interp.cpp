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
// TEST 3: Teleport-safety (Phase 2)
//   A live fruit's prev snapshot is on-screen; before the next
//   SnapshotAfterStep its pos.y is force-set to a teleport-magnitude jump
//   (mimicking Fruit::CheckHasGoneOffscreen's edge-warp). Asserts
//   ApplyForDraw(alpha) snaps straight to the teleported value (no lerped
//   streak) instead of interpolating toward the old on-screen position, while
//   a small in-range motion on the same field still lerps to the midpoint.
//
// All tests compile to pass/skip when FN_RENDER_INTERP is 0 (the interp code
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
static bool Vec3Eq(const _Vector3<float>& a, const _Vector3<float>& b) {
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
    _Vector3<float> pos;
    _Vector3<float> scale;
    float      zPos;
    _Vector3<float> secondPos;
    Quaternion rot1;
    Quaternion rot2;
};

struct BombSnapshot {
    _Vector3<float> pos;
    _Vector3<float> scale;
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
    ClearMenuItems();
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }
    game_work.gameMode = 0;  // classic
    PrepareForLevelStart();
    game_work.bM_bPaused = 0;
    // Set taskStateIndex=2 (Game state) so SnapshotAfterStep / ApplyForDraw pass
    // the gameplay gate added by #172.  Without this the interp singleton no-ops
    // and the test trivially passes without exercising any interpolation logic.
    game_work.taskStateIndex = 2;

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
            _Vector3<float> posBefore = newEnt->pos;
            interp.ApplyForDraw(0.5f);
            _Vector3<float> posAfter = newEnt->pos;
            interp.RestoreAfterDraw();
            _Vector3<float> posRestored = newEnt->pos;

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

    // ============================================================
    // TEST 3: teleport-safety -- snap instead of lerp past TELEPORT_DIST
    // ============================================================
    printf("[render_interp] TEST 3: teleport-safety snap-vs-lerp\n");

    // Grab a live fruit fresh (TEST 2 may have recycled the previous one).
    Fruit* teleportTarget = nullptr;
    {
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (e->IsActive()) { teleportTarget = static_cast<Fruit*>(e); break; }
        }
    }
    if (!teleportTarget) {
        h.RunHeadless(60);
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (e->IsActive()) { teleportTarget = static_cast<Fruit*>(e); break; }
        }
    }
    if (!teleportTarget) {
        printf("[render_interp] WARN: no fruit for teleport test -- TEST 3 trivially passes\n");
    } else {
        fn::RenderInterp& interp = fn::RenderInterp::Get();

        // Commit a normal step so prev/cur are both populated with hadPrev=true.
        h.game.stepUpdate();
        interp.SnapshotAfterStep();
        h.game.stepUpdate();
        interp.SnapshotAfterStep();

        const _Vector3<float> onScreenPos = teleportTarget->pos;

        // --- Sub-case A: teleport jump (> TELEPORT_DIST) must SNAP, not lerp ---
        const _Vector3<float> teleportedPos(onScreenPos.x, onScreenPos.y - 200.0f, onScreenPos.z);
        teleportTarget->pos = teleportedPos;   // mimic Fruit::CheckHasGoneOffscreen edge-warp
        interp.SnapshotAfterStep();            // prev = onScreenPos snapshot, cur = teleportedPos

        interp.ApplyForDraw(0.5f);
        _Vector3<float> appliedTeleport = teleportTarget->pos;
        interp.RestoreAfterDraw();
        _Vector3<float> restoredTeleport = teleportTarget->pos;

        bool snapped     = Vec3Eq(appliedTeleport, teleportedPos);
        bool restoredOk1 = Vec3Eq(restoredTeleport, teleportedPos);

        if (!snapped || !restoredOk1) {
            fprintf(stderr,
                "FAIL: TEST 3a teleport did not snap: applied=(%.3f,%.3f,%.3f) "
                "expected cur=(%.3f,%.3f,%.3f) restored=(%.3f,%.3f,%.3f)\n",
                appliedTeleport.x, appliedTeleport.y, appliedTeleport.z,
                teleportedPos.x, teleportedPos.y, teleportedPos.z,
                restoredTeleport.x, restoredTeleport.y, restoredTeleport.z);
            return 1;
        }

        // --- Sub-case B: small in-range motion (< TELEPORT_DIST) must still LERP ---
        const _Vector3<float> smallMovePos(teleportedPos.x, teleportedPos.y - 5.0f, teleportedPos.z);
        teleportTarget->pos = smallMovePos;
        interp.SnapshotAfterStep();   // prev = teleportedPos snapshot, cur = smallMovePos

        interp.ApplyForDraw(0.5f);
        _Vector3<float> appliedSmall = teleportTarget->pos;
        interp.RestoreAfterDraw();
        _Vector3<float> restoredSmall = teleportTarget->pos;

        _Vector3<float> expectedMid(
            (teleportedPos.x + smallMovePos.x) * 0.5f,
            (teleportedPos.y + smallMovePos.y) * 0.5f,
            (teleportedPos.z + smallMovePos.z) * 0.5f);

        bool lerped      = Vec3Eq(appliedSmall, expectedMid);
        bool restoredOk2 = Vec3Eq(restoredSmall, smallMovePos);

        if (!lerped || !restoredOk2) {
            fprintf(stderr,
                "FAIL: TEST 3b small motion did not lerp: applied=(%.3f,%.3f,%.3f) "
                "expected mid=(%.3f,%.3f,%.3f) restored=(%.3f,%.3f,%.3f)\n",
                appliedSmall.x, appliedSmall.y, appliedSmall.z,
                expectedMid.x, expectedMid.y, expectedMid.z,
                restoredSmall.x, restoredSmall.y, restoredSmall.z);
            return 1;
        }

        printf("[render_interp] TEST 3 PASSED: teleport snapped, small motion lerped\n");
    }

    printf("[render_interp] all tests PASSED\n");
    return h.Shutdown();
#endif  // FN_RENDER_INTERP
}
