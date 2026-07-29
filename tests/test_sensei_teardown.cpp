// test_sensei_teardown -- reproduce the "sensei board lingers after quit" bug.
//
// GOAL: Drive a real Classic GameOverScreen to STATE_MAIN_DISPLAY (so the
// sensei textures / FruitFactClassicFactPage are created), then fire the
// quit callback and tick ~180 frames.  Per-frame logging shows exactly which
// gate is stuck.
//
// THEORY (from RE):
//   QuitCallback -> HitMenuBomb (sets m_BombHitTimer ~2.0) -> STATE_QUIT_WAIT (9).
//   STATE_QUIT_WAIT gates on GetNumEntities(0)==0 -> DoQuitToMenu() ->
//     bM_bPaused=1, SetIntroHoldTimer(0.5f), MainScreen->STATE_CAMERA_ZOOM.
//   STATE_FINAL_FADE (11): if(m_PauseAmount < 0.0f) SetTerminate().
//   m_PauseAmount goes negative only when MainScreen::STATE_CAMERA_ZOOM SETTLE branch
//   runs: HOLD branch fires while f0>0 OR m_BombHitTimer>1.45.
//   m_BombHitTimer decays in GameUpdate ACTIVE branch (active=!bM_Mode && pmState==0).
//
// RUN:
//   cmake --build build/host -j
//   ./build/host/tests/Debug/test_sensei_teardown.exe
//   ctest --test-dir build/host -R sensei_teardown --output-on-failure

#include "test_harness.h"
#include "screens/GameOverScreen.h"
#include "screens/MainScreen.h"
#include "screens/FruitFactClassicFactPage.h"
#include "screens/BaseScreen.h"
#include "hud/FruitFactControl.h"
#include "hud/HUDControl.h"
#include "hud/GenericHUDControl.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "entities/ActorManager.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/FruitSaveData.h"
#include "entities/Bomb.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <list>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Count HUD controls by GetType().
static int CountHudType(int type) {
    if (!game_work.mHud) return 0;
    int count = 0;
    std::list<HUDControl*>::iterator it;
    for (it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* c = *it;
        if (c && c->GetType() == type) ++count;
    }
    return count;
}

// Count controls that are registered in a BaseScreen's m_HUDControls list
// AND are also present in game_work.mHud->controls.
// Used to count the sensei GenericHUDControl children of FruitFactClassicFactPage,
// which live in the page's m_HUDControls and are pushed to the global HUD via
// BaseScreen::AddGenericControl.
// NOTE: GenericHUDControl::GetType() inherits HUDControl3d::GetType() -> returns 1
// (not 6); counting by type alone is not reliable since type 1 is shared with
// many other controls. This function counts by pointer identity instead.
// Requires FN_TEST (adds GetHUDControlsForTest() accessor to BaseScreen).
static int CountPageChildrenInHud(BaseScreen* page) {
    if (!page || !game_work.mHud) return 0;
    int count = 0;
    const std::list<GenericHUDControl*>& pageChildren = page->GetHUDControlsForTest();
    std::list<GenericHUDControl*>::const_iterator pit;
    for (pit = pageChildren.begin(); pit != pageChildren.end(); ++pit) {
        HUDControl* child = static_cast<HUDControl*>(*pit);
        if (!child) continue;
        std::list<HUDControl*>::iterator hit;
        for (hit = game_work.mHud->controls.begin();
             hit != game_work.mHud->controls.end(); ++hit) {
            if (*hit == child) { ++count; break; }
        }
    }
    return count;
}

// Count FruitFactControl (type 12) in HUD.
static int CountFruitFactControls() {
    return CountHudType(12);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "sensei_teardown");
    // 120 burn-in frames: lets GameInit run through splash->Game so HUD is live.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }
    if (!game_work.mMainScreen) {
        fprintf(stderr, "FAIL: game_work.mMainScreen null after boot\n");
        return 1;
    }

    // -----------------------------------------------------------------------
    // SETUP: Classic mode, no pause, m_PauseAmount=1.0 (fully faded in)
    // -----------------------------------------------------------------------
    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.bM_bPaused   = 0;
    game_work.bM_Mode      = 0;   // game-active (not paused/suspended)
    game_work.m_PauseAmount     = 1.0f;
    game_work.m_BombHitTimer = 0.0f;

    // Put MainScreen in a stable state so it doesn't fight the transition.
    game_work.mMainScreen->SetState(STATE_CAMERA_FADE);

    // Drain any stale fruit entities from the pool.
    {
        Mortar::ActorManager* am = h.game.actorManager;
        if (am) {
            am->DeactivateAllEntities(0);
            am->DeactivateAllEntities(1);
        }
        // One tick to process deactivations; do NOT run more yet (m_SaveData may change below).
        int rem0 = am ? am->GetNumEntities(0) : 0;
        int rem1 = am ? am->GetNumEntities(1) : 0;
        printf("[setup] entity drain: type0=%d type1=%d after DeactivateAllEntities\n", rem0, rem1);
    }

    // -----------------------------------------------------------------------
    // PHASE 1: Create GameOverScreen in STATE_MAIN_DISPLAY via fast path.
    //
    // Fast-path gate (Initialise): param2 > 5 && game_work.m_PauseAmount > 0.999f.
    // With param2=6 (STATE_MAIN_DISPLAY=6) and m_PauseAmount=1.0f this fires,
    // which internally calls Update(0.0f) -> creates m_pFruitFact + Classic page.
    // -----------------------------------------------------------------------
    // NOTE: FruitSaveData contains std::map members (m_Totals, m_SessionTotals).
    // Do NOT memset -- that destroys already-constructed map internals.
    // Instead, use the live game_work.m_SaveData (already valid from boot)
    // and just patch the fields we need.
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    if (game_work.m_SaveData) {
        // Seed a known highscore so GetCurrentModeHighscore() returns something
        // sensible. currentScore > hi/2 -> m_ExpressionIdx randomised to 2 or 3.
        game_work.m_SaveData->m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 100;
        game_work.m_SaveData->newBestThisGame = 0;
    }
    game_work.currentScore = 150;

    game_work.m_PauseAmount = 1.0f;
    GameOverScreen* gos = new GameOverScreen(
        "Classic",
        GameOverScreen::STATE_MAIN_DISPLAY,  // param2=6 -> fast path
        0.0f,                                // param3>=0 -> fast path applies
        1,                                   // expressionIdx
        1,                                   // bgPatternIdx
        0,                                   // tabIndex
        0);                                  // starCount

    gos->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT;
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);

    printf("[setup] GameOverScreen=%p created, m_State=%d\n",
           (void*)gos, gos->m_State);

    // -----------------------------------------------------------------------
    // PHASE 2: Settle to STATE_MAIN_DISPLAY and wait for FruitFact + sensei.
    //
    // Run real game frames (h.RunHeadless) so GameUpdate (ACTIVE branch)
    // ticks normally. The fast-path in Initialise already called Update(0.0f)
    // once, so m_pFruitFact should be non-null immediately.
    // -----------------------------------------------------------------------
    game_work.m_PauseAmount     = 1.0f;
    game_work.bM_bPaused   = 0;
    game_work.bM_Mode      = 0;
    game_work.m_BombHitTimer = 0.0f;

    // Run up to 120 frames to let FruitFactControl + ClassicFactPage settle.
    // Keep draining fruit entities so MenuButton fruits (spawned by buttons)
    // do not crowd the entity pool (we need type-0 count == 0 for QUIT_WAIT gate).
    int setupFrames = 0;
    for (int i = 0; i < 120; ++i) {
        game_work.m_PauseAmount = 1.0f;
        if (h.game.actorManager) {
            h.game.actorManager->DeactivateAllEntities(0);
            h.game.actorManager->DeactivateAllEntities(1);
        }
        h.RunHeadless(1);
        ++setupFrames;
        if (gos->m_pFruitFact != NULL && gos->m_State == GameOverScreen::STATE_MAIN_DISPLAY)
            break;
    }

    game_work.m_PauseAmount = 1.0f;   // restore after settle

    printf("[setup] after %d settle frames:\n", setupFrames);
    printf("  gos->m_State         = %d (expect %d=STATE_MAIN_DISPLAY)\n",
           gos->m_State, GameOverScreen::STATE_MAIN_DISPLAY);
    printf("  gos->m_pFruitFact    = %p\n", (void*)gos->m_pFruitFact);
    printf("  gos->m_pClassicPage  = %p\n", (void*)gos->m_pClassicFactPage);
    printf("  gos->m_pQuitBtn      = %p\n", (void*)gos->m_pQuitBtn);
    // CountPageChildrenInHud: counts the GenericHUDControl children from the classic
    // page that are still present in game_work.mHud->controls. These are the sensei
    // head+body controls created by FruitFactClassicFactPage::Init().
    // Note: GenericHUDControl::GetType() inherits HUDControl3d::GetType() -> 1, NOT 6.
    // Counting by type 6 would always return 0. We count by pointer identity instead.
    int senseiInHud = gos->m_pClassicFactPage
        ? CountPageChildrenInHud(gos->m_pClassicFactPage) : -1;
    printf("  FruitFactControls    = %d (expect 1)\n", CountFruitFactControls());
    printf("  SenseiControls(by ptr) = %d (expect 2 sensei head+body)\n", senseiInHud);
    printf("  entity count(0)      = %d (expect 0)\n",
           h.game.actorManager ? h.game.actorManager->GetNumEntities(0) : -1);
    printf("  m_BombHitTimer       = %.4f (expect 0)\n", game_work.m_BombHitTimer);
    printf("  bM_Mode              = %d   (expect 0)\n", (int)game_work.bM_Mode);
    printf("  bM_bPaused           = %d   (expect 0)\n", (int)game_work.bM_bPaused);

    // ASSERT: we must have FruitFactControl and the classic page.
    if (gos->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
        fprintf(stderr, "FAIL: GameOverScreen not in STATE_MAIN_DISPLAY after setup "
                "(state=%d) -- cannot reproduce sensei linger\n", gos->m_State);
        game_work.m_SaveData = prevSaveData;
        return 1;
    }
    if (!gos->m_pFruitFact) {
        fprintf(stderr, "FAIL: m_pFruitFact not created -- Classic page (and sensei "
                "controls) never built; cannot reproduce\n");
        game_work.m_SaveData = prevSaveData;
        return 1;
    }

    // Snapshot pre-quit state for the final assertion.
    // GenericHUDControl::GetType() inherits HUDControl3d::GetType() -> returns 1, NOT 6.
    // Counting by type-6 always returns 0. Instead, collect the child pointer addresses
    // from pClassicPage->m_HUDControls BEFORE the quit fires, so we can check after
    // teardown that those specific pointers are no longer in mHud->controls.
    int preQuitFruitFacts = CountFruitFactControls();
    FruitFactControl* pFruitFact = gos->m_pFruitFact;
    FruitFactClassicFactPage* pClassicPage = gos->m_pClassicFactPage;

    // Collect the exact HUDControl* pointers for the sensei children.
    // These are the 2 GenericHUDControl instances created by Init() and pushed
    // to the global HUD via BaseScreen::AddGenericControl.
    static const int MAX_SENSEI = 8;
    HUDControl* senseiPtrs[MAX_SENSEI];
    int numSensei = 0;
    if (pClassicPage) {
        const std::list<GenericHUDControl*>& children = pClassicPage->GetHUDControlsForTest();
        std::list<GenericHUDControl*>::const_iterator it;
        for (it = children.begin(); it != children.end() && numSensei < MAX_SENSEI; ++it) {
            senseiPtrs[numSensei++] = static_cast<HUDControl*>(*it);
        }
    }
    int preQuitSensei = CountPageChildrenInHud(pClassicPage);

    printf("[setup] PRE-QUIT HUD: fruitfacts=%d sensei_children=%d (ptr-captured=%d)\n",
           preQuitFruitFacts, preQuitSensei, numSensei);

    // -----------------------------------------------------------------------
    // PHASE 3: Ensure entities cleared (QUIT_WAIT gate needs count==0).
    // -----------------------------------------------------------------------
    {
        Mortar::ActorManager* am = h.game.actorManager;
        if (am) {
            am->DeactivateAllEntities(0);
            am->DeactivateAllEntities(1);
        }
        int remaining0 = am ? am->GetNumEntities(0) : 0;
        int remaining1 = am ? am->GetNumEntities(1) : 0;
        printf("[setup] entity drain for QUIT gate: type0=%d type1=%d\n",
               remaining0, remaining1);
    }

    // -----------------------------------------------------------------------
    // PHASE 4: Fire the quit path.
    //
    // The binary path: QuitCallback -> m_State=9, HitMenuBomb(Vec3(163,-96,0)).
    // HitMenuBomb sets m_BombHitTimer=2.0.
    // If the quit button is available, fire through m_ClickCallback (real path).
    // Otherwise call QuitCallback equivalent directly via gos->m_pQuitBtn.
    // -----------------------------------------------------------------------
    game_work.m_PauseAmount     = 1.0f;
    game_work.bM_bPaused   = 0;
    game_work.bM_Mode      = 0;

    printf("\n[quit] Firing quit callback...\n");
    if (gos->m_pQuitBtn) {
        // Real path: user taps the quit button.
        gos->m_pQuitBtn->m_ClickCallback();
        printf("[quit] fired via m_pQuitBtn->m_ClickCallback()\n");
    } else {
        // Fallback: the Quit button was not created yet (happens if settle
        // didn't reach STATE_MAIN_DISPLAY stable state); fire HitMenuBomb
        // directly and set state to QUIT_WAIT manually.
        printf("[quit] m_pQuitBtn==NULL, forcing STATE_QUIT_WAIT + HitMenuBomb\n");
        gos->m_State = GameOverScreen::STATE_QUIT_WAIT;
        HitMenuBomb(_Vector3<float>(163.0f, -96.0f, 0.0f));
    }

    printf("[quit] post-fire: gos->m_State=%d m_BombHitTimer=%.4f bM_Mode=%d bM_bPaused=%d\n",
           gos->m_State, game_work.m_BombHitTimer, (int)game_work.bM_Mode, (int)game_work.bM_bPaused);

    // -----------------------------------------------------------------------
    // PHASE 5: Tick 180 frames with per-frame trace.
    //
    // Each frame: do NOT drain entities (let normal decay happen; draining
    // would interfere with the QUIT_WAIT gate).
    // Log every 10 frames, plus the first 5 and last 5.
    // -----------------------------------------------------------------------
    printf("\n[trace] frame | bM_Mode bM_bPaused | m_BombHitTimer | m_PauseAmount"
           " | gos_state | ms_state | pGOS | fruitfacts | sensei\n");

    static const int TRACE_FRAMES = 180;
    static const int LOG_EVERY    = 5;  // print every N frames

    bool teardownOccurred = false;
    int  teardownFrame    = -1;
    int  stateAtTeardown  = -1;

    for (int f = 0; f < TRACE_FRAMES; ++f) {
        // Pre-tick snapshot.
        int   bm_mode    = (int)game_work.bM_Mode;
        int   bm_paused  = (int)game_work.bM_bPaused;
        float bomb_timer = game_work.m_BombHitTimer;
        float game_dt    = game_work.m_PauseAmount;
        int   gos_state  = gos ? gos->m_State : -1;
        int   ms_state   = game_work.mMainScreen ? game_work.mMainScreen->m_State : -1;
        void* pGOS       = (void*)game_work.pGameOverScreen;
        int   ff_count   = CountFruitFactControls();
        // Sensei count: pointer-identity scan of the classic page's m_HUDControls.
        // If the page pointer is still valid (gos not yet torn down), count directly.
        // After GOS teardown pClassicPage may be freed; use 0 in that case.
        int   sei_count  = (pClassicPage && game_work.pGameOverScreen)
                           ? CountPageChildrenInHud(pClassicPage) : 0;

        bool logThisFrame = (f < 10) || (f % LOG_EVERY == 0) || (f >= TRACE_FRAMES - 5);

        if (logThisFrame) {
            printf("[trace] %4d  | %d %d | %.4f | %.4f | %2d | %2d | %s | %d | %d\n",
                   f,
                   bm_mode, bm_paused,
                   bomb_timer,
                   game_dt,
                   gos_state,
                   ms_state,
                   pGOS ? "live" : "null",
                   ff_count,
                   sei_count);
        }

        // Tick one real game frame.
        h.RunHeadless(1);

        // Detect teardown: pGameOverScreen becomes null.
        if (!teardownOccurred && game_work.pGameOverScreen == NULL) {
            teardownOccurred = true;
            teardownFrame    = f;
            stateAtTeardown  = gos_state;
            printf("[trace] %4d  TEARDOWN: pGameOverScreen went null (was state=%d)\n",
                   f, stateAtTeardown);
            // Force log of post-teardown HUD state immediately after the teardown frame.
            // pClassicPage is now freed (deleted by GOS teardown); do NOT dereference it.
            // Scan by pre-captured sensei pointer set.
            int senseiInHudNow = 0;
            if (game_work.mHud) {
                for (int si = 0; si < numSensei; ++si) {
                    HUDControl* sp = senseiPtrs[si];
                    if (!sp) continue;
                    std::list<HUDControl*>::iterator hit;
                    for (hit = game_work.mHud->controls.begin();
                         hit != game_work.mHud->controls.end(); ++hit) {
                        if (*hit == sp) { ++senseiInHudNow; break; }
                    }
                }
            }
            printf("[trace] post-teardown (same frame): fruitfacts=%d sensei=%d"
                   " bm_mode=%d bm_paused=%d game_dt=%.4f\n",
                   CountFruitFactControls(), senseiInHudNow,
                   (int)game_work.bM_Mode, (int)game_work.bM_bPaused,
                   game_work.m_PauseAmount);

            // Run one more frame so HUD::Update can process m_bPendingRemoval on
            // any sensei children whose flags were set during the teardown delete chain.
            h.RunHeadless(1);
            senseiInHudNow = 0;
            if (game_work.mHud) {
                for (int si = 0; si < numSensei; ++si) {
                    HUDControl* sp = senseiPtrs[si];
                    if (!sp) continue;
                    std::list<HUDControl*>::iterator hit;
                    for (hit = game_work.mHud->controls.begin();
                         hit != game_work.mHud->controls.end(); ++hit) {
                        if (*hit == sp) { ++senseiInHudNow; break; }
                    }
                }
            }
            printf("[trace] post-teardown (+1 frame):  fruitfacts=%d sensei=%d"
                   " bm_mode=%d bm_paused=%d game_dt=%.4f\n",
                   CountFruitFactControls(), senseiInHudNow,
                   (int)game_work.bM_Mode, (int)game_work.bM_bPaused,
                   game_work.m_PauseAmount);
            break;
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 6: Final assertions.
    // -----------------------------------------------------------------------
    printf("\n[assert]\n");
    int failures = 0;

    // 1. pGameOverScreen must be null (GameOverScreen SetTerminate + HUD removal).
    if (game_work.pGameOverScreen != NULL) {
        fprintf(stderr,
            "FAIL: pGameOverScreen=%p still non-null after %d frames "
            "(GameOverScreen never terminated)\n",
            (void*)game_work.pGameOverScreen, TRACE_FRAMES);
        fprintf(stderr,
            "  STUCK GATE DIAGNOSIS:\n"
            "  -> gos->m_State=%d (expect STATE_FINAL_FADE=11 -> SetTerminate)\n"
            "  -> game_work.m_PauseAmount=%.4f (must be < 0 for STATE_FINAL_FADE to terminate)\n"
            "  -> game_work.m_BombHitTimer=%.4f (must be <= 1.45 for SETTLE branch in case-0)\n"
            "  -> game_work.bM_Mode=%d (must be 0 for ACTIVE branch / BombHitTimer decay)\n"
            "  -> game_work.bM_bPaused=%d\n"
            "  -> MainScreen->m_State=%d (must be STATE_CAMERA_ZOOM=0)\n",
            gos ? gos->m_State : -1,
            game_work.m_PauseAmount,
            game_work.m_BombHitTimer,
            (int)game_work.bM_Mode,
            (int)game_work.bM_bPaused,
            game_work.mMainScreen ? game_work.mMainScreen->m_State : -1);
        ++failures;
    } else {
        printf("PASS (1/2): pGameOverScreen is null (teardown at frame %d)\n",
               teardownFrame);
    }

    // 2. No FruitFactControl should remain in the HUD.
    int remainingFF = CountFruitFactControls();
    if (remainingFF != 0) {
        fprintf(stderr,
            "FAIL: HUD still contains FruitFactControl=%d after teardown"
            " (sensei board lingers)\n", remainingFF);
        ++failures;
    } else {
        printf("PASS (2/3): no FruitFactControl remains in HUD\n");
    }

    // 3. No captured sensei children should remain in the HUD.
    // We check by pointer identity (GenericHUDControl::GetType()==1 is shared with
    // many other controls; pointer check is the only reliable way).
    // pClassicPage has been deleted by GOS teardown -- do NOT dereference it.
    // Use the pre-captured senseiPtrs[] snapshot instead.
    int remainingSensei = 0;
    if (game_work.mHud) {
        for (int si = 0; si < numSensei; ++si) {
            HUDControl* sp = senseiPtrs[si];
            if (!sp) continue;
            std::list<HUDControl*>::iterator hit;
            for (hit = game_work.mHud->controls.begin();
                 hit != game_work.mHud->controls.end(); ++hit) {
                if (*hit == sp) { ++remainingSensei; break; }
            }
        }
    }
    if (remainingSensei != 0) {
        fprintf(stderr,
            "FAIL: %d sensei GenericHUDControl(s) still in HUD after teardown"
            " (sensei board lingers -- bug REPRODUCED)\n", remainingSensei);
        ++failures;
    } else if (numSensei > 0) {
        printf("PASS (3/3): all %d sensei controls removed from HUD after teardown\n",
               numSensei);
    } else {
        // numSensei==0 means Init() never populated m_HUDControls -- the sensei
        // controls were never created, so teardown couldn't linger them.
        printf("NOTE (3/3): numSensei==0 -- FruitFactClassicFactPage::Init() did not"
               " populate m_HUDControls; sensei controls were not tracked\n");
    }

    game_work.m_SaveData = prevSaveData;

    if (failures > 0) {
        fprintf(stderr,
            "\nFAIL: test_sensei_teardown -- BUG REPRODUCED (or setup error).\n"
            "  See per-frame [trace] above and assertion messages.\n"
            "  Stuck gates to check:\n"
            "    -> m_BombHitTimer not decaying: bM_Mode must be 0 for ACTIVE branch\n"
            "    -> m_PauseAmount never < 0: MainScreen STATE_CAMERA_ZOOM SETTLE branch requires\n"
            "       m_BombHitTimer <= 1.45 AND m_IntroHoldTimer <= 0\n"
            "    -> GOS STATE_FINAL_FADE (11) requires m_PauseAmount < 0 to call SetTerminate\n");
        h.Shutdown();
        return 1;
    }

    printf("\nPASS: test_sensei_teardown -- GameOverScreen + sensei cleaned up"
           " within %d frames.\n", TRACE_FRAMES);
    if (numSensei == 0) {
        printf("  NOTE: sensei controls (GenericHUDControl) not created in this run.\n"
               "  FruitFactClassicFactPage::Init() must be called and m_HUDControls\n"
               "  populated for the teardown check to be meaningful.\n");
    } else {
        printf("  Sensei-linger path: headless reaps cleanly.\n"
               "  If the real game shows lingering, the stall is display/input-timing\n"
               "  specific and does not manifest in the headless game loop.\n");
    }
    return h.Shutdown();
}
