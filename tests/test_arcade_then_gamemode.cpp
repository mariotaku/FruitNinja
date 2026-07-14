// test_arcade_then_gamemode -- ASAN regression driver for a UAF on the SHARED
// red_ring texture (game_work.m_RingTex[16]) surfacing on the SECOND
// GameModeScreen::CreateControls call, after a full arcade round.
//
// MOTIVATION: a real ASAN run reported heap-use-after-free at
//   GameModeScreen::CreateControls -> SmartPtr<Texture>::assign ("index out
//   of bounds")
// The existing tests/test_mainscreen_to_gamemode.cpp (fresh boot -> slice
// NEW GAME -> GameModeScreen) PASSES because red_ring.tex is still valid on
// a fresh boot -- there is no prior state to over-release it. The crash
// needs an arcade round in between: game_work.m_RingTex[16] is shared by
// MULTIPLE screens along this test's path --
//   MainScreen::CreateButtons quit ring   (MainScreen.cpp:909)
//   GameModeScreen::CreateControls (1st)  back button (GameModeScreen.cpp:326, m_RingTex[0x10]==16)
//   GameOverScreen::CreateQuitButton      quit ring    (GameOverScreen.cpp:865)
//   GameModeScreen::CreateControls (2nd)  back button  <- the reported crash site
// If any one of those over-releases the shared SmartPtr<Texture> on
// teardown, the 2nd CreateControls call copies a dead SmartPtr and AddRef's
// freed memory. This test drives all four sites in sequence under ASAN
// (build/asan) so the free site and the use site both show up in the report.
//
// REAL TRIGGERS USED (not fabricated):
//   - TriggerNewGameSlice(): same MenuButton::Update velocity-slice gate as
//     test_mainscreen_to_gamemode.cpp / test_menu_fruit_roundtrip.cpp.
//   - GameModeScreen::ArcadeModeCallback(): the real public method bound as
//     the arcade ring's m_ClickCallback in GameModeScreen::CreateControls
//     (GameModeScreen.cpp:407-416, GameModeScreen.h:162). Calling it directly
//     is exactly what MenuButton::Update's slice gate would invoke.
//   - WaveManager::SpawnFruit(count, fruitType, spawner, playerIdx): the real
//     production spawn path (WaveManager.h:320; already called this way by
//     SuperFruitControl.cpp and SpawnModifier.cpp) -- used to force special
//     /power fruit coverage (frenzy/fourth_banana/freeze/scorex2/dragon/
//     super_dragon/super_pomegranate, same names as
//     tests/scenes/scene_special_fruit.cpp) instead of waiting on
//     WaveManager::CriticalMode's RNG within a bounded frame budget.
//   - Fruit::CollisionResponse(nullptr, 0, 0, &bladeVel): the real slice
//     entry point (vtable slot 9), same technique as
//     test_bomb_spawn.cpp's RunVariantSlice for Bomb.
//   - FN::GameOver(-1, -1.0f, -1): the real free function used by every
//     arcade-timeout / miss-out site (hud/TimeControl.cpp:197,
//     entities/Fruit.cpp:960/2574, game/WaveManager.cpp:2409).
//
// ONE DELIBERATE SHORTCUT: GameOverScreen::QuitCallback() is PRIVATE
// (screens/GameOverScreen.h:182), so this test cannot call it directly.
// Instead -- mirroring the already-established technique in
// test_menu_fruit_roundtrip.cpp Phase 4 -- it kills all live type-0
// entities directly (Fruit::KillFruit(false), a public real method) and
// pins gos->m_State = GameOverScreen::STATE_QUIT_WAIT (a public field).
// STATE_QUIT_WAIT's own gate (GameOverScreen.cpp:1282-1288) is purely
// "GetNumEntities(0)==0 -> DoQuitToMenu()"; killing the entities directly
// satisfies that gate exactly as QuitCallback's HitMenuBomb()-driven fling
// would, just without the ~1.5s fling animation.
//
// Run:
//   ctest --test-dir build/host -R arcade_then_gamemode --output-on-failure
//   ./build/host/tests/Debug/test_arcade_then_gamemode.exe --verbose
//
// ASAN: build/asan build of the same target is the actual point of this
// test -- run it there to catch the UAF (free site + use site both report).

#include "test_harness.h"
#include "screens/MainScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/GameOverScreen.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"
#include "game/WaveManager.h"
#include "game/GameOver.h"
#include "game/GameWork.h"

#include <cstdio>
#include <cstring>
#include <list>

// Scan game_work.mHud->controls for a live GameModeScreen (dynamic_cast,
// mirrors test_mainscreen_to_gamemode.cpp / test_gamemode_destructor.cpp).
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
    printf("[arcade_then_gamemode] TriggerNewGameSlice: btn=%p fruit=%p "
           "m_bSliced set, vel=(5,2,0) secondVel=(-5,-2,0)\n",
           (void*)btn, (void*)f);
}

// Special / power fruit names -- same set as tests/scenes/scene_special_fruit.cpp
// (fruitlist.xml). Forced via WaveManager::SpawnFruit so the #326 special-fruit
// path is exercised deterministically instead of depending on
// WaveManager::CriticalMode's RNG landing within our frame budget.
static const char* kSpecialFruitNames[] = {
    "frenzy", "fourth_banana", "freeze", "scorex2",
    "dragon", "super_dragon", "super_pomegranate",
};
static const int kSpecialFruitCount =
    (int)(sizeof(kSpecialFruitNames) / sizeof(kSpecialFruitNames[0]));

static void ForceSpawnSpecialFruit(int idx) {
    const char* name = kSpecialFruitNames[idx % kSpecialFruitCount];
    int ft = Fruit::FruitType(name, false);
    if (ft < 0) {
        printf("[arcade_then_gamemode] WARN: special fruit '%s' not in FruitInfo -- skip\n", name);
        return;
    }
    Mortar::Entity* e = WaveManager::GetInstance()->SpawnFruit(1, ft, NULL, 0);
    printf("[arcade_then_gamemode] force-spawned special fruit '%s' (type=%d) entity=%p\n",
           name, ft, (void*)e);
}

// Real-slice every currently active, not-yet-sliced type-0 (fruit) entity via
// the vtable-9 CollisionResponse entry point -- same technique as
// test_bomb_spawn.cpp's RunVariantSlice. Returns the number sliced this call.
static int SliceAllActiveFruits(Mortar::ActorManager* am) {
    if (!am) return 0;
    int sliced = 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e) {
        Mortar::Entity* next_e = am->GetEntityNext(0, it);
        Fruit* f = static_cast<Fruit*>(e);
        if (f->IsActive() && !f->Sliced()) {
            _Vector3<float> bladeVel(15.0f, 15.0f, 0.0f);
            f->CollisionResponse(nullptr, 0, 0, &bladeVel);
            ++sliced;
        }
        e = next_e;
    }
    return sliced;
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    }
    (void)verbose;

    fn::TestHarness h(argc, argv, "arcade_then_gamemode");
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

    // ==========================================================================
    // Phase 1: MainScreen -> slice NEW GAME -> GameModeScreen (1st CreateControls)
    // ==========================================================================
    int warmup = 0;
    while (warmup < 600) {
        bool ready = ms->m_pGameModeButton && ms->m_pGameModeButton->m_pTrackedFruit;
        if (ready) break;
        h.RunHeadless(10);
        warmup += 10;
    }
    if (!ms->m_pGameModeButton || !ms->m_pGameModeButton->m_pTrackedFruit) {
        fprintf(stderr, "FAIL: NEW GAME ring not ready after %d warmup frames\n", warmup);
        return 1;
    }
    printf("[arcade_then_gamemode] NEW GAME ring ready after %d warmup frames\n", warmup);

    TriggerNewGameSlice(ms->m_pGameModeButton);

    GameModeScreen* gms = NULL;
    for (int frame = 0; frame < 120; ++frame) {
        h.game.runFrames(1);
        if (!gms) gms = FindGameModeScreen();
    }
    if (!gms) {
        fprintf(stderr, "FAIL: GameModeScreen (1st) never created after 120 frames "
                        "(MainScreen state=%d)\n", ms->m_State);
        h.Shutdown();
        return 1;
    }
    if (!gms->m_pArcadeButton) {
        fprintf(stderr, "FAIL: GameModeScreen (1st) has no m_pArcadeButton "
                        "(CreateControls did not run)\n");
        h.Shutdown();
        return 1;
    }
    printf("[arcade_then_gamemode] GameModeScreen (1st) live: gms=%p m_pArcadeButton=%p "
           "m_pBackButton=%p\n", (void*)gms, (void*)gms->m_pArcadeButton, (void*)gms->m_pBackButton);

    // ==========================================================================
    // Phase 2: pick ARCADE via the REAL ArcadeModeCallback -- same call the
    // arcade ring's MenuButton::m_ClickCallback delegate makes.
    // ==========================================================================
    gms->ArcadeModeCallback();
    // NOTE: gms->m_State is inherited from BaseScreen as protected (BaseScreen.h:108) --
    // GameModeScreen doesn't expose it publicly, so it can't be logged from here.
    printf("[arcade_then_gamemode] ArcadeModeCallback() fired: gameMode=%d\n",
           (int)game_work.gameMode);

    // Case 3-6 block: alpha/camera decay -> SetupLevel() (PrepareForLevelStart) once
    // camT crosses -0.9 -> WaveManager::NewGame() + MainScreen->STATE_CAMERA_FADE
    // once |camT|<0.001. ~24-30 frames per the 0.75 decay derivation (see
    // GameModeScreen.cpp case 3-6 comments); 120 frames gives generous margin.
    bool reachedGameplay = false;
    for (int frame = 0; frame < 120; ++frame) {
        h.game.runFrames(1);
        if (ms->m_State == STATE_CAMERA_FADE) { reachedGameplay = true; break; }
    }
    if (!reachedGameplay) {
        fprintf(stderr, "FAIL: MainScreen never reached STATE_CAMERA_FADE after "
                        "ArcadeModeCallback (state=%d) -- arcade entry stalled\n", ms->m_State);
        h.Shutdown();
        return 1;
    }
    printf("[arcade_then_gamemode] Arcade entry OK: MainScreen state=%d gameMode=%d "
           "bM_bPaused=%d\n", ms->m_State, (int)game_work.gameMode, (int)game_work.bM_bPaused);

    // ==========================================================================
    // Phase 3: play arcade -- spawn (organic + forced specials) and slice.
    // ==========================================================================
    const int kArcadePlayFrames = 500;
    int totalSliced   = 0;
    int specialSpawns = 0;
    for (int frame = 0; frame < kArcadePlayFrames; ++frame) {
        h.game.runFrames(1);

        // Force one special/power fruit spawn every 50 frames (10 total across
        // 500 frames -- covers the 7-name list at least once).
        if ((frame % 50) == 0) {
            ForceSpawnSpecialFruit(specialSpawns);
            ++specialSpawns;
        }

        totalSliced += SliceAllActiveFruits(h.game.actorManager);

        if ((frame % 100) == 0) {
            printf("[arcade_then_gamemode] play f=%d gameMode=%d slicedSoFar=%d score=%d\n",
                   frame, (int)game_work.gameMode, totalSliced, game_work.currentScore);
        }
    }
    printf("[arcade_then_gamemode] arcade play done: %d frames, %d slices, %d forced "
           "special spawns\n", kArcadePlayFrames, totalSliced, specialSpawns);

    // ==========================================================================
    // Phase 4: end the round via the REAL FN::GameOver() free function, then
    // reach STATE_QUIT_WAIT (GameOverScreen::QuitCallback is private -- see
    // file header for the equivalent public-API technique used here).
    // ==========================================================================
    GameOver(-1, -1.0f, -1);
    GameOverScreen* gos = game_work.pGameOverScreen;
    if (!gos) {
        fprintf(stderr, "FAIL: GameOver(-1,-1.0f,-1) did not create game_work.pGameOverScreen "
                        "(bM_bPaused=%d re-entry guard?)\n", (int)game_work.bM_bPaused);
        h.Shutdown();
        return 1;
    }
    printf("[arcade_then_gamemode] GameOverScreen=%p created, state=%d\n",
           (void*)gos, gos->m_State);

    // Kill all live type-0 entities so STATE_QUIT_WAIT's entity-count gate
    // (GetNumEntities(0)==0) passes immediately -- equivalent end-state to
    // QuitCallback's HitMenuBomb()-driven fling+reap, without the ~1.5s wait.
    {
        Mortar::ActorManager* am = h.game.actorManager;
        if (am) {
            std::list<Mortar::Entity*>::iterator it;
            Mortar::Entity* e = am->GetEntityFirst(0, it);
            while (e) {
                Mortar::Entity* next_e = am->GetEntityNext(0, it);
                static_cast<Fruit*>(e)->KillFruit(false);
                e = next_e;
            }
            am->Update(0.0f);
            printf("[arcade_then_gamemode] cleared type-0 entities; count now=%d\n",
                   am->GetNumEntities(0));
        }
    }
    gos->m_State = GameOverScreen::STATE_QUIT_WAIT;

    int returnFrames = 0;
    int stateBeforeReturn = ms->m_State;
    while (returnFrames < 300 && ms->m_State != STATE_CAMERA_ZOOM) {
        h.RunHeadless(10);
        returnFrames += 10;
    }
    printf("[arcade_then_gamemode] MainScreen reached state=%d after %d frames "
           "(was %d)\n", ms->m_State, returnFrames, stateBeforeReturn);
    if (ms->m_State != STATE_CAMERA_ZOOM) {
        fprintf(stderr, "FAIL: MainScreen never reached STATE_CAMERA_ZOOM after "
                        "DoQuitToMenu (state=%d)\n", ms->m_State);
        h.Shutdown();
        return 1;
    }

    // MainScreen persists (same pointer, see project lifecycle-pause-model note);
    // re-fetch defensively in case that model is ever revisited.
    if (game_work.mMainScreen != ms) {
        printf("[arcade_then_gamemode] NOTE: MainScreen pointer changed %p -> %p\n",
               (void*)ms, (void*)game_work.mMainScreen);
        ms = game_work.mMainScreen;
    }

    // ==========================================================================
    // Phase 5: back at the menu -- wait for NEW GAME ring to be recreated, then
    // slice it a SECOND time -> GameModeScreen (2nd) -> CreateControls (2nd).
    // This is the crash window under test: m_pBackButton->m_Texture =
    // game_work.m_RingTex[0x10] copies (and AddRefs) whatever is left of the
    // shared red_ring Texture after the arcade round above.
    // ==========================================================================
    int settleFrames = 0;
    while (settleFrames < 400) {
        bool ready = ms->m_pGameModeButton && ms->m_pGameModeButton->m_pTrackedFruit;
        if (ready) break;
        h.RunHeadless(10);
        settleFrames += 10;
    }
    if (!ms->m_pGameModeButton || !ms->m_pGameModeButton->m_pTrackedFruit) {
        fprintf(stderr, "FAIL: NEW GAME ring not recreated after %d settle frames "
                        "(MainScreen state=%d)\n", settleFrames, ms->m_State);
        h.Shutdown();
        return 1;
    }
    printf("[arcade_then_gamemode] NEW GAME ring recreated after %d settle frames\n",
           settleFrames);

    TriggerNewGameSlice(ms->m_pGameModeButton);

    GameModeScreen* gms2 = NULL;
    int gms2Frame = -1;
    for (int frame = 0; frame < 120; ++frame) {
        h.game.runFrames(1);
        if (!gms2) {
            GameModeScreen* cur = FindGameModeScreen();
            if (cur) { gms2 = cur; gms2Frame = frame; }
        }
        if ((frame % 20) == 0) {
            printf("[arcade_then_gamemode] 2nd-transition f=%d MainScreen state=%d\n",
                   frame, ms->m_State);
        }
    }

    int failures = 0;
    if (!gms2) {
        fprintf(stderr, "FAIL: GameModeScreen (2nd) was never created after 120 frames "
                        "(MainScreen state=%d) -- 2nd NEW GAME transition did not fire\n",
                ms->m_State);
        ++failures;
    } else {
        printf("[arcade_then_gamemode] PASS: GameModeScreen (2nd) created at frame %d "
               "(gms2=%p)\n", gms2Frame, (void*)gms2);
    }

    GameModeScreen* finalGms2 = FindGameModeScreen();
    if (!finalGms2) {
        fprintf(stderr, "FAIL: GameModeScreen (2nd) not present at end of settle window\n");
        ++failures;
    } else {
        printf("[arcade_then_gamemode] GameModeScreen (2nd) still live: m_pBackButton=%p "
               "m_pClassicButton=%p m_pZenButton=%p m_pArcadeButton=%p\n",
               (void*)finalGms2->m_pBackButton, (void*)finalGms2->m_pClassicButton,
               (void*)finalGms2->m_pZenButton, (void*)finalGms2->m_pArcadeButton);
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: arcade_then_gamemode -- MainScreen -> GameModeScreen -> Arcade -> "
           "GameOver -> menu -> GameModeScreen (2nd CreateControls) all completed "
           "(the real payload is running this under ASAN: build/asan)\n");
    return h.Shutdown();
}
