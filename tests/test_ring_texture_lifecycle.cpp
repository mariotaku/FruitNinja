// test_ring_texture_lifecycle -- ASAN regression driver for a refcount
// imbalance on the SHARED ring textures game_work.m_RingTex[16] (red_ring,
// used by MainScreen's quit ring, GameModeScreen's back button AND
// DojoScreen's back button) and game_work.m_RingTex[13] (GameModeScreen's
// arcade-mode ring).
//
// MOTIVATION: on the real device (wasm), m_RingTex[16] / [13] are valid at
// boot but read NULL / over-freed by the time GameModeScreen/DojoScreen
// create their buttons, well after boot. #348 already fixed ONE cause
// (MenuButton::Release's m_pTrackedFruit-vs-m_pEntity fallback writing
// through a dangling pointer into freed heap that could alias a Texture --
// see src/hud/MenuButton.cpp:346-350). This test is a broader regression
// guard: it round-trips GameModeScreen creation/teardown N=6 times and
// asserts the SmartPtr<Texture> strong refcount on both rings returns to
// its baseline after EVERY round trip (not just survives one). A single
// transition passing (as test_mainscreen_to_gamemode.cpp / test_dojoscreen.cpp
// already show) does not catch a slow per-visit drift; only repetition does.
//
// REFCOUNT INSTRUMENTATION: SmartPtr<T>::DebugRefCount() (test/debug-only,
// src/engine/util/SmartPtr.h) exposes the pointee's existing
// Mortar::ReferenceCounter::GetRefCount(). Neither method has any production
// call site; both are read-only.
//
// REAL TRIGGER PATH (mirrors test_mainscreen_to_gamemode.cpp / test_arcade_then_gamemode.cpp):
//   TriggerNewGameSlice(): same MenuButton::Update velocity-slice gate used by
//     both sibling tests to fire MainScreen::GameModeCallback -> GameModeScreen
//     construction + CreateControls (creates m_pBackButton with m_Texture =
//     m_RingTex[16] and m_pArcadeButton with m_Texture = m_RingTex[13]).
//
// TEARDOWN PATH -- SIMPLIFICATION, see note below:
//   GameModeScreen::QuitCallback() (real, public, v1.6.1 @0x0013F5E0) is
//   called to fire the back button's real back-out semantics (SFX, fling
//   velocity on the bomb entity, m_State=0xf, tutorial reset). This alone
//   would only reap m_pBackButton (via MenuButton::Update's shrink-out gate,
//   once the flung bomb becomes disabled) -- classic/zen/arcade stay live
//   because the real binary's QuitCallback does NOT call ClearMenuItems()
//   (see GameModeScreen.cpp:720-723) and their fruits were never sliced.
//   SIMPLIFICATION: this test additionally calls GameModeScreen::RemoveButtons()
//   (real, public method already in the class; GameModeScreen.cpp:433-438)
//   right after QuitCallback() to force ALL FOUR buttons to HUD-reap
//   deterministically in the same frame window, without needing to RE +
//   replicate the classic/zen/arcade slice-cascade (which would fire their
//   mode-select callbacks and enter full gameplay -- out of scope for a
//   ring-refcount check). The port's own comments already establish that
//   GameModeScreen's real destructor/QuitCallback path does NOT call
//   RemoveButtons() (GameModeScreen.cpp:298-299) -- calling it directly here
//   is a test-only shortcut, not a claim about binary behaviour. It still
//   exercises the REAL MenuButton::Release()->m_Texture.SetNull() code once
//   HUD::Update reaps each pending-removal button (HUD.cpp:100-115), which is
//   exactly the code path under test.
//
// Run:
//   ctest --test-dir build/host -R ring_texture_lifecycle --output-on-failure
//   ./build/host/tests/Debug/test_ring_texture_lifecycle.exe --verbose
//
// ASAN: build/asan build of the same target is the actual point of this test
// -- a refcount imbalance eventually frees the Texture out from under
// game_work.m_RingTex[16]/[13] while they still hold a SmartPtr to it; ASAN
// catches the subsequent heap-use-after-free the moment any screen re-reads
// the ring (GameModeScreen::CreateControls / DojoScreen::CreateButtons).

#include "test_harness.h"
#include "screens/MainScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/DojoScreen.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "entities/Fruit.h"
#include "game/GameWork.h"

#include <cstdio>
#include <cstring>
#include <list>

static const int kRingRed        = 0x10; // 16 -- red_ring: MainScreen quit / GameModeScreen back / DojoScreen back
static const int kRingArcade     = 0xd;  // 13 -- GameModeScreen arcade-mode ring
static const int kRoundTrips     = 6;

// Scan game_work.mHud->controls for a live GameModeScreen (dynamic_cast,
// mirrors test_mainscreen_to_gamemode.cpp / test_arcade_then_gamemode.cpp).
static GameModeScreen* FindGameModeScreen() {
    if (!game_work.mHud) return NULL;
    std::list<HUDControl*>::iterator it;
    for (it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        GameModeScreen* gms = dynamic_cast<GameModeScreen*>(*it);
        if (gms) return gms;
    }
    return NULL;
}

// Fire the REAL MenuButton::Update slice gate on the NEW GAME ring's tracked
// fruit: m_bSliced=1 plus a vel/m_SecondVel divergence, matching what
// Fruit::Slice() produces on a real touch-slice. See
// test_mainscreen_to_gamemode.cpp for the full derivation; MenuButton.cpp:622-639
// for the consuming gate.
static void TriggerNewGameSlice(MenuButton* btn) {
    if (!btn || !btn->m_pTrackedFruit) return;
    Fruit* f = btn->m_pTrackedFruit;
    f->m_bSliced   = 1;
    f->vel         = _Vector3<float>(5.0f, 2.0f, 0.0f);
    f->m_SecondVel = _Vector3<float>(-5.0f, -2.0f, 0.0f);
    f->m_bDrawWhole = 1;
}

static void LogRingState(int iter, int redCount, bool redValid,
                          int arcCount, bool arcValid) {
    printf("[ring_texture_lifecycle] iter=%d red_ring: valid=%d count=%d  "
           "arcade_ring: valid=%d count=%d\n",
           iter, (int)redValid, redCount, (int)arcValid, arcCount);
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    }
    (void)verbose;

    fn::TestHarness h(argc, argv, "ring_texture_lifecycle");
    h.SetInitFrames(300);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // ==========================================================================
    // Step 1: PreloadRings has run by now (called from GameInitialise, which
    // game.init() drives before the 300-frame burn-in even starts). Assert both
    // target rings are valid.
    // ==========================================================================
    if (!game_work.m_RingTex[kRingRed].IsValid()) {
        fprintf(stderr, "FAIL: game_work.m_RingTex[0x%x] (red_ring) not valid after boot\n",
                kRingRed);
        h.Shutdown();
        return 1;
    }
    if (!game_work.m_RingTex[kRingArcade].IsValid()) {
        fprintf(stderr, "FAIL: game_work.m_RingTex[0x%x] (arcade ring) not valid after boot\n",
                kRingArcade);
        h.Shutdown();
        return 1;
    }

    MainScreen* ms = game_work.mMainScreen;
    if (!ms) {
        fprintf(stderr, "FAIL: game_work.mMainScreen is null after init\n");
        h.Shutdown();
        return 1;
    }
    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        h.Shutdown();
        return 1;
    }

    // ==========================================================================
    // Step 2: baseline refcount. Captured AFTER MainScreen's own boot-time
    // buttons exist (its quit ring already holds a permanent ref on
    // m_RingTex[16] -- MainScreen.cpp:909), BEFORE any GameModeScreen visit.
    // Every round trip below must return to this same count.
    // ==========================================================================
    const int baselineRed = game_work.m_RingTex[kRingRed].DebugRefCount();
    const int baselineArc = game_work.m_RingTex[kRingArcade].DebugRefCount();
    printf("[ring_texture_lifecycle] baseline: red_ring count=%d  arcade_ring count=%d\n",
           baselineRed, baselineArc);
    if (baselineRed < 1 || baselineArc < 1) {
        fprintf(stderr, "FAIL: baseline refcount(s) unexpectedly < 1 (red=%d arcade=%d)\n",
                baselineRed, baselineArc);
        h.Shutdown();
        return 1;
    }

    int failures = 0;

    for (int iter = 0; iter < kRoundTrips; ++iter) {
        // ---- Wait for the NEW GAME ring to be ready (fresh on iter 0; ----
        // ---- recreated by MainScreen after each round trip thereafter). ----
        int warmup = 0;
        while (warmup < 600) {
            bool ready = ms->m_pGameModeButton && ms->m_pGameModeButton->m_pTrackedFruit;
            if (ready) break;
            for (int f = 0; f < 10; ++f) {
                h.game.runFrames(1);
                h.game.tickRealtimeUi(1.0f / 60.0f);
            }
            warmup += 10;
        }
        if (!ms->m_pGameModeButton || !ms->m_pGameModeButton->m_pTrackedFruit) {
            fprintf(stderr, "FAIL: iter=%d NEW GAME ring not ready after %d warmup frames "
                            "(MainScreen state=%d)\n", iter, warmup, ms->m_State);
            ++failures;
            break;
        }

        TriggerNewGameSlice(ms->m_pGameModeButton);

        // ---- Step frames until GameModeScreen exists with its controls. ----
        GameModeScreen* gms = NULL;
        for (int frame = 0; frame < 120; ++frame) {
            h.game.runFrames(1);
            h.game.tickRealtimeUi(1.0f / 60.0f);
            if (!gms) gms = FindGameModeScreen();
        }
        if (!gms) {
            fprintf(stderr, "FAIL: iter=%d GameModeScreen never created after 120 frames "
                            "(MainScreen state=%d)\n", iter, ms->m_State);
            ++failures;
            break;
        }
        if (!gms->m_pBackButton || !gms->m_pArcadeButton) {
            fprintf(stderr, "FAIL: iter=%d GameModeScreen CreateControls incomplete "
                            "(m_pBackButton=%p m_pArcadeButton=%p)\n",
                    iter, (void*)gms->m_pBackButton, (void*)gms->m_pArcadeButton);
            ++failures;
            break;
        }

        // ---- Real teardown: press Back (QuitCallback), then force all four ----
        // ---- menu buttons to HUD-reap (RemoveButtons() -- see file header ----
        // ---- SIMPLIFICATION note). ----
        gms->QuitCallback();
        gms->RemoveButtons();

        // Step frames so HUD::Update reaps the pending-removal buttons (each
        // delete -> ~MenuButton -> Release() -> m_Texture.SetNull()), and so
        // GameModeScreen's own state-0xf transitionAlpha decay crosses
        // ALPHA_OUT_DONE, setting its own m_bPendingRemoval and letting HUD
        // reap the screen too (~GameModeScreen -> Release() + delete boxes).
        // Also lets MainScreen's SetState(STATE_SLIDE_IN) -> ... -> back to
        // STATE_CAMERA_ZOOM settle so the NEW GAME ring is recreated for the
        // next iteration.
        //
        // The state-0xf decay (and its m_bPendingRemoval threshold crossing)
        // moved to a port-only UpdateRealtime() in ca427a6c so the back-out
        // fade tracks display refresh; it is NOT advanced by runFrames()'s
        // 60Hz Update() alone (see GameModeScreen::Update's #else case 0xf,
        // which is a no-op under the port build). Must also call
        // tickRealtimeUi() here -- the real game loop pumps both per
        // presented frame (GameSDL.cpp) -- or GameModeScreen (and its two
        // buttons holding refs on m_RingTex[16]/[13]) never gets reaped and
        // the round-trip refcount never returns to baseline.
        for (int frame = 0; frame < 120; ++frame) {
            h.game.runFrames(1);
            h.game.tickRealtimeUi(1.0f / 60.0f);
        }

        const int redCount = game_work.m_RingTex[kRingRed].DebugRefCount();
        const bool redValid = game_work.m_RingTex[kRingRed].IsValid();
        const int arcCount = game_work.m_RingTex[kRingArcade].DebugRefCount();
        const bool arcValid = game_work.m_RingTex[kRingArcade].IsValid();
        LogRingState(iter, redCount, redValid, arcCount, arcValid);

        if (!redValid) {
            fprintf(stderr, "FAIL: iter=%d red_ring (m_RingTex[0x%x]) went invalid\n",
                    iter, kRingRed);
            ++failures;
        }
        if (!arcValid) {
            fprintf(stderr, "FAIL: iter=%d arcade_ring (m_RingTex[0x%x]) went invalid\n",
                    iter, kRingArcade);
            ++failures;
        }
        if (redValid && redCount != baselineRed) {
            fprintf(stderr, "FAIL: iter=%d red_ring refcount drifted: baseline=%d now=%d\n",
                    iter, baselineRed, redCount);
            ++failures;
        }
        if (arcValid && arcCount != baselineArc) {
            fprintf(stderr, "FAIL: iter=%d arcade_ring refcount drifted: baseline=%d now=%d\n",
                    iter, baselineArc, arcCount);
            ++failures;
        }

        if (failures > 0) break; // stop early once a ring is gone/invalid -- further
                                  // iterations would just chain UAFs.
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed across %d round trips\n",
                failures, kRoundTrips);
        h.Shutdown();
        return 1;
    }

    // ==========================================================================
    // Step 3: DojoScreen entry -- the exact crash site named in the bug report:
    // `m_pBackButton->m_Texture = game_work.m_RingTex[16]` (DojoScreen.cpp:291).
    // Assert red_ring is still valid immediately before DojoScreen::Init()
    // (which calls Reset() -> CreateButtons(), performing that exact assign).
    // ==========================================================================
    if (!game_work.m_RingTex[kRingRed].IsValid()) {
        fprintf(stderr, "FAIL: red_ring invalid before DojoScreen entry\n");
        h.Shutdown();
        return 1;
    }
    printf("[ring_texture_lifecycle] pre-DojoScreen: red_ring valid=1 count=%d\n",
           game_work.m_RingTex[kRingRed].DebugRefCount());

    DojoScreen* dojo = new DojoScreen();
    dojo->Init(); // Init() -> Reset() -> CreateButtons() -- the crash site.
    game_work.mHud->AddControl(dojo);

    for (int frame = 0; frame < 60; ++frame) {
        h.game.runFrames(1);
    }

    // (DojoScreen::CreateButtons assigns m_RingTex[16] into its back button; the
    // button pointer itself is private, so we validate via the shared ring slot.)
    if (!game_work.m_RingTex[kRingRed].IsValid()) {
        fprintf(stderr, "FAIL: red_ring went invalid after DojoScreen::CreateButtons\n");
        h.Shutdown();
        return 1;
    }
    printf("[ring_texture_lifecycle] PASS: DojoScreen entry OK, red_ring valid=1 count=%d\n",
           game_work.m_RingTex[kRingRed].DebugRefCount());

    printf("PASS: ring_texture_lifecycle -- %d GameModeScreen round trips + DojoScreen entry, "
           "red_ring/arcade_ring refcounts stable (the real payload is running this under "
           "ASAN: build/asan)\n", kRoundTrips);
    return h.Shutdown();
}
