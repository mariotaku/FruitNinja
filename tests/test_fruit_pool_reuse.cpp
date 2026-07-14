// test_fruit_pool_reuse -- #346 regression guard.
//
// v1.6.1 Fruit::Init @0x001e2898 clears m_OnSliced/m_OnKilled/m_OnExpired at
// the top of Init (m_OnSliced.Clear()/m_OnKilled.Clear()/m_OnExpired.Clear())
// so a recycled pool entity never fires a delegate bound to whatever object
// subscribed to the *previous* occupant. Before the fix, Init left the event
// lists untouched, so a stale subscriber from a freed object could fire on
// the next pool user.
//
// Drives the REAL pool-reuse path (not an isolated Event<> object):
//   1. Boot the full game (game.actorManager, WaveManager, FruitInfo all live).
//   2. Obtain a Fruit via ActorManager::Add(0, true) (same call MenuButton /
//      WaveManager use), subscribe a dummy delegate to each of the three
//      events, assert none are Empty().
//   3. ActorManager::Deactivate(f) + Add(0, true) again -- the real
//      "pool reuse" recycle path (ActorManager.cpp reverse-scans m_FreePool
//      for a matching entityType and hands the SAME Entity* back without
//      touching its fields).
//   4. Call Fruit::Init(nullptr, fruitType, nullptr) on the recycled entity
//      (matches MenuButton::CreateFruit / WaveManager spawn call shape) and
//      assert all three events are now Empty().
//
// If the three Clear() calls at the top of Fruit::Init were removed, step 4's
// asserts fail because the delegate subscribed in step 2 is still in the list.
//
// Boots via test_harness.h (fn_add_game_test): hidden SDL/GL window, SFX
// volume forced to 0. No screenshot, no audio played, deterministic.

#include "test_harness.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"

#include <cstdio>

namespace {

// Dummy subscriber for the three Fruit events. Exists only to prove
// (un)subscription state via call counts are irrelevant here -- we only
// care whether the delegate is still present in the list after Init().
struct DummyReceiver {
    int slicedCalls;
    int killedCalls;
    int expiredCalls;

    DummyReceiver() : slicedCalls(0), killedCalls(0), expiredCalls(0) {}

    void OnSliced(Fruit* /*f*/, int /*a*/, Mortar::Entity* /*hitter*/) { ++slicedCalls; }
    void OnKilled(Fruit* /*f*/) { ++killedCalls; }
    void OnExpired(Fruit* /*f*/) { ++expiredCalls; }
};

} // namespace

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "fruit_pool_reuse");
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    int failures = 0;
    Game& game = h.game;

    Mortar::ActorManager* am = game.actorManager;
    if (!am) {
        std::fprintf(stderr, "FAIL: game.actorManager is null\n");
        h.Shutdown();
        return 1;
    }

    // --- Step 2: obtain a fruit and subscribe to all three events ---
    Mortar::Entity* e = am->Add(0, true);
    if (!e) {
        std::fprintf(stderr, "FAIL: ActorManager::Add(0, true) returned null\n");
        h.Shutdown();
        return 1;
    }
    Fruit* f = static_cast<Fruit*>(e);

    DummyReceiver recv;
    Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*> dSliced =
        Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(&recv, &DummyReceiver::OnSliced);
    Mortar::Delegate1<void, Fruit*> dKilled =
        Mortar::Delegate1<void, Fruit*>::Make(&recv, &DummyReceiver::OnKilled);
    Mortar::Delegate1<void, Fruit*> dExpired =
        Mortar::Delegate1<void, Fruit*>::Make(&recv, &DummyReceiver::OnExpired);

    f->m_OnSliced  += dSliced;
    f->m_OnKilled  += dKilled;
    f->m_OnExpired += dExpired;

    if (f->m_OnSliced.Empty() || f->m_OnKilled.Empty() || f->m_OnExpired.Empty()) {
        std::fprintf(stderr, "FAIL: event lists empty right after subscribe (test setup bug)\n");
        ++failures;
    } else {
        std::printf("[PASS] subscribed dummy delegates to m_OnSliced/m_OnKilled/m_OnExpired\n");
    }

    // --- Step 3: real pool-reuse recycle path ---
    // ActorManager::Deactivate pushes the entity onto m_FreePool without
    // touching its fields (no Release, no field reset). The next Add(0, true)
    // reverse-scans the free pool for a matching entityType and hands the
    // pooled Entity* straight back -- this is the "recycled fruit still
    // carries the old subscriber" scenario #346 fixes.
    am->Deactivate(f);
    Mortar::Entity* e2 = am->Add(0, true);
    if (!e2) {
        std::fprintf(stderr, "FAIL: recycle ActorManager::Add(0, true) returned null\n");
        h.Shutdown();
        return 1;
    }
    Fruit* f2 = static_cast<Fruit*>(e2);

    // --- Step 4: Init() must clear all three event lists on the recycled fruit ---
    f2->pos = _Vector3<float>(0.0f, 0.0f, 0.0f);
    f2->Init(nullptr, 0, nullptr);

    if (!f2->m_OnSliced.Empty()) {
        std::fprintf(stderr, "FAIL: m_OnSliced NOT cleared by Fruit::Init on pool reuse (#346 regression)\n");
        ++failures;
    }
    if (!f2->m_OnKilled.Empty()) {
        std::fprintf(stderr, "FAIL: m_OnKilled NOT cleared by Fruit::Init on pool reuse (#346 regression)\n");
        ++failures;
    }
    if (!f2->m_OnExpired.Empty()) {
        std::fprintf(stderr, "FAIL: m_OnExpired NOT cleared by Fruit::Init on pool reuse (#346 regression)\n");
        ++failures;
    }

    if (failures == 0) {
        std::printf("[PASS] fruit_pool_reuse: Fruit::Init cleared all 3 event lists on pool reuse\n");
    }

    h.Shutdown();
    return failures;
}
