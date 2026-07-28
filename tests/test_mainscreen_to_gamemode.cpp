// test_mainscreen_to_gamemode -- ASAN regression driver for the MainScreen ->
// GameModeScreen transition (slicing the NEW GAME ring).
//
// MOTIVATION: this exact transition crashes the WEB (wasm32) build with a
// UAF/bad-call. Native host ctests pass silently (host malloc/heap tolerates
// the bad access). This test drives the REAL in-process trigger path so an
// ASAN host build (build/asan) exercises the same crash window on every
// frame of the transition, instead of skipping over it with a single
// batched runFrames() call.
//
// REAL TRIGGER PATH (not fabricated):
//   MenuButton::Update (v1.6.1 @0x0019a860) drives the NEW GAME ring's
//   tracked Fruit entity. Its slice gate is VELOCITY-based: when
//   |vel - m_SecondVel|^2 > 0.001 (SLICE_EPS) on a Fruit with m_bSliced=1,
//   it fires m_ClickCallback -- which for m_pGameModeButton is
//   MainScreen::GameModeCallback (v1.6.1 @0x0014b068), wired at
//   MainScreen::CreateButtons (src/screens/MainScreen.cpp:848).
//   See MenuButton.cpp:622-639 for the exact gate.
//
//   GameModeCallback sets m_State=STATE_MODE_SELECT, m_Timer2=1.0f.
//   MainScreen::Update's STATE_MODE_SELECT case (MainScreen.cpp:439-457)
//   decays m_Timer2 by STATE_0E_DECAY=0.85 per frame; when it crosses
//   STATE_0E_THRESHOLD=0.25 (~9 frames after the callback fires), it
//   constructs GameModeScreen and adds it to the HUD:
//     GameModeScreen* gms = new GameModeScreen(false);
//     game_work.mHud->AddControl(gms);
//   This construction + HUD insertion is the crash window under test.
//
// This test does NOT fabricate a synthetic transition call -- it replicates
// exactly what MenuButton::Update does when a real touch-slice diverges the
// fruit's velocity (same technique as test_menu_fruit_roundtrip.cpp's
// TriggerButtonSlice(), reused here for a narrower, ASAN-focused check).
//
// Run:
//   ctest --test-dir build/host -R mainscreen_to_gamemode --output-on-failure
//   ./build/host/tests/Debug/test_mainscreen_to_gamemode.exe --verbose
//
// ASAN: build/asan build of the same target exercises heap instrumentation
// across every frame of the transition (see loop below); no GL/offscreen
// context beyond the standard hidden SDL window TestHarness::Init() creates
// is required -- this mirrors test_menu_fruit_roundtrip.cpp / test_dojo_navigation.cpp,
// neither of which need a visible window or FBO.

#include "test_harness.h"
#include "screens/MainScreen.h"
#include "screens/GameModeScreen.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "entities/Fruit.h"
#include "game/GameWork.h"

#include <cstdio>
#include <cstring>
#include <list>

// Scan game_work.mHud->controls for a live GameModeScreen (dynamic_cast,
// mirrors test_gamemode_destructor.cpp's FindGameModeScreen()).
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
// Fruit::Slice() produces on a real touch-slice (vel=halfVelB,
// m_SecondVel=halfVelA). See MenuButton.cpp:622-639 for the consuming gate.
static void TriggerNewGameSlice(MenuButton* btn) {
    if (!btn || !btn->m_pTrackedFruit) return;
    Fruit* f = btn->m_pTrackedFruit;
    f->m_bSliced   = 1;
    f->vel         = _Vector3<float>(5.0f, 2.0f, 0.0f);
    f->m_SecondVel = _Vector3<float>(-5.0f, -2.0f, 0.0f);
    f->m_bDrawWhole = 1;
    printf("[mainscreen_to_gamemode] TriggerNewGameSlice: btn=%p fruit=%p "
           "m_bSliced set, vel=(5,2,0) secondVel=(-5,-2,0)\n",
           (void*)btn, (void*)f);
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    }
    (void)verbose;

    fn::TestHarness h(argc, argv, "mainscreen_to_gamemode");
    // 300 burn-in frames: MainScreen boots in STATE_CAMERA_ZOOM, creates
    // buttons + tracked fruits in the first few frames. Matches
    // test_menu_fruit_roundtrip.cpp's init budget.
    h.SetInitFrames(300);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    MainScreen* ms = game_work.mMainScreen;
    if (!ms) {
        fprintf(stderr, "FAIL: game_work.mMainScreen is null after init\n");
        return 1;
    }
    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    printf("[mainscreen_to_gamemode] MainScreen state after %d frames: %d\n",
           h.initFrames, ms->m_State);

    // Pump extra frames until the NEW GAME ring button + its tracked fruit
    // are both alive (mirrors test_menu_fruit_roundtrip.cpp warmup loop).
    int warmup = 0;
    while (warmup < 600) {
        bool ready = ms->m_pGameModeButton && ms->m_pGameModeButton->m_pTrackedFruit;
        if (ready) break;
        h.RunHeadless(10);
        warmup += 10;
    }
    if (!ms->m_pGameModeButton) {
        fprintf(stderr, "FAIL: m_pGameModeButton never created (MainScreen state=%d)\n",
                ms->m_State);
        return 1;
    }
    if (!ms->m_pGameModeButton->m_pTrackedFruit) {
        fprintf(stderr, "FAIL: m_pGameModeButton->m_pTrackedFruit is null after %d warmup frames\n",
                warmup);
        return 1;
    }
    printf("[mainscreen_to_gamemode] NEW GAME ring ready after %d warmup frames "
           "(btn=%p fruit=%p)\n",
           warmup, (void*)ms->m_pGameModeButton, (void*)ms->m_pGameModeButton->m_pTrackedFruit);

    if (FindGameModeScreen() != NULL) {
        fprintf(stderr, "FAIL: GameModeScreen already present before the slice is fired\n");
        return 1;
    }

    // ---- Fire the real slice -> GameModeCallback -> STATE_MODE_SELECT -> ----
    // ---- GameModeScreen construction + HUD insertion (the crash window). ----
    TriggerNewGameSlice(ms->m_pGameModeButton);

    // Step the frame loop ONE FRAME AT A TIME through the entire transition so
    // the mid-transition crash window (GameModeScreen ctor + AddControl, then
    // its state-0->2 alpha lerp settle) is exercised on every frame, not
    // skipped over by a single batched runFrames(N) call.
    //
    // Timeline (see header comment above for the derivation):
    //   ~1-5 frames:   MenuButton::Update detects the slice, fires GameModeCallback.
    //   ~9 frames after that: m_Timer2 decays past STATE_0E_THRESHOLD (0.25),
    //                  GameModeScreen is constructed + added to the HUD.
    //   ~30-60 frames after that: GameModeScreen's alpha lerp settles to idle
    //                  (state 2, mode buttons created via CreateControls).
    // 120 frames total gives generous margin past all of the above.
    const int kTransitionFrames = 120;
    bool gmsFound = false;
    int  gmsFrame = -1;
    for (int frame = 0; frame < kTransitionFrames; ++frame) {
        h.game.runFrames(1);

        if (!gmsFound) {
            GameModeScreen* gms = FindGameModeScreen();
            if (gms) {
                gmsFound = true;
                gmsFrame = frame;
                printf("[mainscreen_to_gamemode] GameModeScreen created at frame %d (gms=%p)\n",
                       frame, (void*)gms);
            }
        }

        if ((frame % 20) == 0) {
            printf("[mainscreen_to_gamemode] f=%d MainScreen state=%d m_Timer2=%.4f\n",
                   frame, ms->m_State, ms->m_Timer2);
        }
    }

    int failures = 0;

    if (!gmsFound) {
        fprintf(stderr,
            "FAIL: GameModeScreen was never created after %d frames "
            "(MainScreen state=%d, m_Timer2=%.4f) -- NEW GAME transition did not fire\n",
            kTransitionFrames, ms->m_State, ms->m_Timer2);
        ++failures;
    } else {
        printf("[mainscreen_to_gamemode] PASS: transition fired at frame %d\n", gmsFrame);
    }

    GameModeScreen* finalGms = FindGameModeScreen();
    if (!finalGms) {
        fprintf(stderr,
            "FAIL: GameModeScreen not present at end of %d-frame settle window "
            "(created then reaped early, or construction failed silently)\n",
            kTransitionFrames);
        ++failures;
    } else {
        printf("[mainscreen_to_gamemode] GameModeScreen still live at frame %d: "
               "m_pBackButton=%p m_pClassicButton=%p m_pZenButton=%p m_pArcadeButton=%p\n",
               kTransitionFrames,
               (void*)finalGms->m_pBackButton, (void*)finalGms->m_pClassicButton,
               (void*)finalGms->m_pZenButton,  (void*)finalGms->m_pArcadeButton);
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: mainscreen_to_gamemode -- MainScreen -> GameModeScreen transition OK "
           "(the real payload is running this under ASAN: build/asan)\n");
    return h.Shutdown();
}
