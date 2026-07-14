// test_superfruit_finale_tick -- proves the super-fruit finale is actually
// ticked by HUD::Update after the SuperFruitControl -> HUDControl3d re-base.
//
// Before that re-base, SuperFruitControl derived from Mortar::Entity and was
// never added to game_work.mHud -- SuperFruitSliced() allocated the controller
// and put it in the SuperFruitControls lookup map, but nothing in the engine
// ever called its Update(). It was created-but-never-ticked dead code: the
// combo/explosion/finale state machine in SuperFruitControl::Update never ran
// a single frame. This test is the durable regression guard for that fix and
// the follow-on WaveManager::Resume rebuild path.
//
// Approach (mirrors tests/scenes/scene_special_fruit.cpp's boot + spawn, and
// the headless drive pattern used by test_bomb_spawn.cpp / test_fruit_pool_reuse.cpp):
//   1. Full game boot via fn::TestHarness (SDL+GL hidden window, GameInitialise
//      + GameInit both run during the burn-in frames -- same as test_bomb_spawn).
//      SoundManager SFX volume forced to 0 by TestHarness::Init() in headless mode.
//   2. Spawn a "super_pomegranate" Fruit via the real ActorManager pool (same
//      helper pattern as scene_special_fruit.cpp's SpawnFruit).
//   3. Slice it through the REAL path: Fruit::CollisionResponse(hitter, ...),
//      which fires Fruit::FruitWasSlicedEvent() -> SuperFruitControl::SuperFruitSliced
//      (subscribed once at boot by SuperFruitControl::LoadContent(), called from
//      GameInitialise). This is the exact call chain a real blade slice drives;
//      no direct call to SuperFruitSliced() is used.
//   4. Look up the freshly created SuperFruitControl in the SuperFruitControls
//      map (proves creation + HUD registration happened) and read its m_Timer.
//   5. Tick the REAL frame loop (Game::runFrames, which calls GameUpdate's
//      common tail -> game_work.mHud->Update(dt) every frame, ASM-verified at
//      v1.6.1 GameUpdate @0x001cf534) and assert m_Timer ADVANCES. Before the
//      HUDControl3d re-base this delta was always 0 -- the control was orphaned.
//   6. Continue ticking until the controller self-removes from game_work.mHud's
//      control list (the observable effect of Update() setting m_bPendingRemoval
//      once the finale timeline completes) and assert m_Timer crossed m_Lifetime
//      along the way. This proves the FULL phase ladder in Update() -- not just
//      the first tick -- runs to completion.
//
// Memory-safety note: once game_work.mHud->Update() sees m_bPendingRemoval, it
// deletes the control in the SAME call (HUD::Update, hud/HUD.cpp). The delete
// happens inside the very frame where the flag flips, so this test never reads
// ctrl->m_Timer / ctrl->m_bPendingRemoval on a frame after it has left
// game_work.mHud->controls -- ControlStillInHud() below is a raw pointer-VALUE
// membership check only (never a dereference of a possibly-freed ctrl), used to
// detect self-removal without touching freed memory.
//
// Headless only: no screenshot, no GPU pixel readback. This verifies tick LOGIC
// (Update() reaching the phase ladder), not the DrawOrder finale VFX (still an
// unported TODO -- see SuperFruitControl::DrawOrder).
//
// Run:
//   ctest -R superfruit_finale_tick --output-on-failure

#include "test_harness.h"
#include "entities/Entity.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "entities/SuperFruitControl.h"
#include "entities/ActorManager.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "math/_Vector3.h"
#include <cstdio>
#include <cstdint>
#include <list>
#include <map>

// Raw pointer-VALUE membership check against game_work.mHud->controls. Never
// dereferences ctrl -- safe to call even after HUD::Update has freed it.
static bool ControlStillInHud(HUDControl* ctrl) {
    if (!game_work.mHud) return false;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        if (*it == ctrl) return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "superfruit_finale_tick");
    // 120 burn-in frames: same budget test_bomb_spawn.cpp uses to reach the
    // point where the GameTaskState machine has run GameInit and game_work.mHud
    // is live.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        std::fprintf(stderr, "FAIL: actorManager null after boot\n");
        return 1;
    }

    // "super_pomegranate" has score="0" in fruitlist.xml -> FruitInfo::m_bIsSuperFruit
    // is set (FruitInfo.cpp: m_bIsSuperFruit = (m_Score == 0)).
    int fruitType = Fruit::FruitType("super_pomegranate", false);
    if (fruitType < 0) {
        std::fprintf(stderr, "FAIL: 'super_pomegranate' not found in FruitInfo\n");
        return 1;
    }

    Mortar::Entity* e = am->Add(0, true);
    if (!e) {
        std::fprintf(stderr, "FAIL: fruit pool exhausted\n");
        return 1;
    }
    Fruit* fruit = static_cast<Fruit*>(e);
    fruit->Init(NULL, (long)fruitType, NULL);
    fruit->pos = _Vector3<float>(0.0f, 0.0f, 0.0f);
    fruit->vel = _Vector3<float>(0.0f, 0.0f, 0.0f);
    fruit->flags &= ~(uint32_t)(0x01 | 0x10);  // clear ENT_INACTIVE | ENT_KILLED

    // Real hitter entity: GameInit spawns 16 SlashEntity finger-slots at boot
    // (GameInit.cpp step 13); g_pSlashEntities[0] is a live, real SlashEntity,
    // not a fabricated pointer. Fruit::CollisionResponse never dereferences the
    // hitter -- it is only forwarded to Fruit::FruitWasSlicedEvent() and (for a
    // super fruit) must be non-null for the event to fire at all.
    if (!g_pSlashEntities[0]) {
        std::fprintf(stderr, "FAIL: g_pSlashEntities[0] null after boot -- no hitter entity\n");
        return 1;
    }

    // Active-play state: the faithful SuperFruitSliced create-gate @0x001be630 skips
    // creation when (FailureEnabled(mode) && bM_bPaused). Booting the harness leaves
    // bM_bPaused at its GameInit default of 1 (no real round was started); a live slice
    // only ever happens in unpaused play, so reflect that here.
    game_work.bM_bPaused = 0;

    // Real slice path: Fruit::CollisionResponse -> Fruit::FruitWasSlicedEvent()
    // -> SuperFruitControl::SuperFruitSliced (subscribed by LoadContent() during
    // GameInitialise, which already ran as part of h.Init()).
    _Vector3<float> bladeVel(15.0f, 15.0f, 0.0f);
    fruit->CollisionResponse(g_pSlashEntities[0], 0, 0, &bladeVel);

    std::map<Fruit*, SuperFruitControl*>::iterator it =
        SuperFruitControl::SuperFruitControls.find(fruit);
    if (it == SuperFruitControl::SuperFruitControls.end() || !it->second) {
        std::fprintf(stderr,
            "FAIL: SuperFruitSliced did not create/register a SuperFruitControl "
            "for the sliced super fruit\n");
        return 1;
    }
    SuperFruitControl* ctrl = it->second;

    if (!SuperFruitControl::IsInSuperFruitState()) {
        std::fprintf(stderr, "FAIL: IsInSuperFruitState() false right after slice\n");
        return 1;
    }

    if (!ControlStillInHud(ctrl)) {
        std::fprintf(stderr,
            "FAIL: SuperFruitControl was created but never registered into "
            "game_work.mHud->controls -- HUD::Update cannot reach it "
            "(this is the exact dead-code condition this test guards against)\n");
        return 1;
    }

    const float timer0    = ctrl->m_Timer;
    const float lifetime  = ctrl->m_Lifetime;
    std::printf("[superfruit_finale_tick] ctrl=%p created; m_Timer=%.3f m_Lifetime=%.3f\n",
                static_cast<void*>(ctrl), timer0, lifetime);

    // ---- Core proof: HUD::Update reaches SuperFruitControl::Update ----
    // Tick 60 real frames (~1s at 60Hz, fixed dt). m_Lifetime is uniform[2,3)s,
    // so the control is guaranteed to still be alive and registered here --
    // ControlStillInHud() is re-checked anyway before every dereference.
    for (int i = 0; i < 60; ++i) h.game.runFrames(1);

    if (!ControlStillInHud(ctrl)) {
        std::fprintf(stderr,
            "FAIL: SuperFruitControl was removed from the HUD within the first "
            "60 frames (unexpected -- m_Lifetime=%.3f should keep it alive past 1s)\n",
            lifetime);
        return 1;
    }
    const float timer1 = ctrl->m_Timer;
    std::printf("[superfruit_finale_tick] after 60 frames: m_Timer=%.3f (delta=%.4f)\n",
                timer1, timer1 - timer0);

    // THE assertion that proves Update now runs: m_Timer must have advanced by
    // roughly 60 frames worth of dt (~1.0s at fixed 1/60 step). Before the
    // SuperFruitControl -> HUDControl3d re-base this delta was always exactly
    // 0.0 -- the controller was created but nothing in the engine ever called
    // its Update(), so m_Timer stayed frozen at whatever Sliced() set it to.
    if (!(timer1 - timer0 > 0.5f)) {
        std::fprintf(stderr,
            "FAIL: m_Timer did not advance (delta=%.4f, want >0.5 over 60 frames). "
            "HUD::Update is not reaching SuperFruitControl::Update -- the exact "
            "dead-code regression this test exists to catch.\n",
            timer1 - timer0);
        return 1;
    }
    std::printf("PASS: m_Timer advances under HUD::Update (delta=%.4f over 60 frames)\n",
                timer1 - timer0);

    // ---- Full timeline proof: the phase ladder runs to completion ----
    // Keep ticking until the controller self-removes from the HUD (the visible
    // effect of Update() setting m_bPendingRemoval once tEnd is crossed --
    // v1.6.1 SuperFruitControl::Update @0x001bca10, tEnd = m_Lifetime + 0.5 +
    // 0.35 + 0.55 + 0.65 + 0.25 + modeBias(0.5 default) + 0.15 ~= m_Lifetime + 2.95,
    // i.e. ~7.95s for the default m_Lifetime=5.0 -- budget generously below.
    bool crossedLifetime  = false;
    bool sawSelfRemoval   = false;
    float lastKnownTimer  = timer1;
    const int kMaxAdditionalFrames = 900;  // 15s of margin over the ~7.95s timeline
    for (int i = 0; i < kMaxAdditionalFrames; ++i) {
        if (!ControlStillInHud(ctrl)) {
            sawSelfRemoval = true;
            break;
        }
        if (ctrl->m_Timer >= lifetime) crossedLifetime = true;
        lastKnownTimer = ctrl->m_Timer;
        h.game.runFrames(1);
    }

    std::printf("[superfruit_finale_tick] settle done: lastKnownTimer=%.3f "
                "crossedLifetime=%d sawSelfRemoval=%d\n",
                lastKnownTimer, (int)crossedLifetime, (int)sawSelfRemoval);

    if (!crossedLifetime) {
        std::fprintf(stderr,
            "FAIL: m_Timer never reached m_Lifetime (%.3f) -- explosion phase never entered\n",
            lifetime);
        return 1;
    }
    if (!sawSelfRemoval) {
        std::fprintf(stderr,
            "FAIL: SuperFruitControl never self-removed from the HUD within %d "
            "additional frames -- the finale teardown branch (m_bPendingRemoval) "
            "never fired, i.e. the phase ladder did not run to completion\n",
            kMaxAdditionalFrames);
        return 1;
    }

    std::printf("PASS: %s -- SuperFruitControl finale ticks and completes under "
                "HUD::Update\n", h.label);
    return h.Shutdown();
}
