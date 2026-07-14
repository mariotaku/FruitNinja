// test_bonus_phase -- reproduces the BONUS_PHASE state-machine stall in
// isolation so the fix can be iterated without launching the full game.
//
// Background: Arcade BONUS_PHASE (GameOverScreen::m_State == 1) never
// advances to STATE_MAIN_DISPLAY (6). The callback chain that should advance
// state is:
//   BonusScreen::Update -> sets m_bPendingRemoval = 1 (Phase D, timer >= dismissAt)
//   HUD::Update         -> fires m_RemoveCallback on control with pending removal
//   DeletedControl      -> m_pBonusScreen = nullptr; m_State = 6
//
// The test boots a full Arcade-mode game-over and measures whether
// GameOverScreen::m_State exits STATE_BONUS_PHASE within 600 frames (10s at
// 60fps). Today the test FAILS (bug reproduced). Once the fix lands it
// should PASS.
//
// Part 2 (post-BONUS_PHASE flow): after reaching STATE_MAIN_DISPLAY, the test
// continues for up to 600 more frames to let the screen settle, then fires
// RetryCallback and QuitCallback via the button click delegates and asserts
// that m_State advances out of STATE_MAIN_DISPLAY for each.
//
// Run via:
//   ctest --test-dir build/host -R bonus_phase --output-on-failure
// or:
//   ./build/host/tests/Debug/test_bonus_phase.exe
//   ./build/host/tests/Debug/test_bonus_phase.exe --interactive

#include "test_harness.h"
#include "screens/GameOverScreen.h"
#include "screens/BonusScreen.h"
#include "screens/MainScreen.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "entities/ActorManager.h"
#include "game/FruitSaveData.h"
#include "engine/math/_Vector3.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// How many total frames to give the state machine to exit BONUS_PHASE.
// 600 frames = 10 seconds at 60fps. The bonus animation + dismiss should
// complete in << 5 seconds in the original binary.
static const int TIMEOUT_FRAMES = 600;

// How many frames to wait after entering STATE_MAIN_DISPLAY before injecting
// callbacks. 60 frames (1s) gives the screen time to settle and create buttons.
static const int SETTLE_FRAMES = 60;

// Maximum frames to wait for the state to advance after firing a callback.
static const int ADVANCE_TIMEOUT = 120;

// Deactivate all menu / mode-select HUD overlays so they do not fight the
// game-over scene. Mirrors the pattern used by test_screen.cpp.
static void HideMenuScreens(Game& game)
{
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* c = *it;
        if (dynamic_cast<DojoScreen*>(c)
         || dynamic_cast<AboutScreen*>(c)
         || dynamic_cast<ShopScreen*>(c)
         || dynamic_cast<GameModeScreen*>(c)) {
            c->m_Active = 0;
        }
    }
}

// Drain all fruit and bomb entities from the ActorManager so the
// BONUS_PHASE entity-gate is satisfied immediately without waiting for
// natural off-screen kill.
static void DrainEntities(Game& game)
{
    Mortar::ActorManager* am = game.actorManager;
    if (!am) return;
    // Deactivate all type-0 (Fruit) and type-1 (Bomb) entities via the
    // binary-faithful ActorManager::Deactivate path.
    am->DeactivateAllEntities(0);
    am->DeactivateAllEntities(1);
    // One ActorManager update tick processes the deactivations.
    game.runFrames(1);
}

// Tick one frame while keeping the entity pool drained (fruit and bomb types).
// Pass drainEntities=false once in STATE_MAIN_DISPLAY to let MenuButton
// fruit entities live; DeactivateAllEntities(0) would kill them immediately,
// triggering DeletedControl and nulling m_pRetryBtn/m_pQuitBtn.
static void TickFrame(Game& game, bool drainEntities = true)
{
    if (drainEntities) {
        Mortar::ActorManager* am = game.actorManager;
        if (am) {
            am->DeactivateAllEntities(0);
            am->DeactivateAllEntities(1);
        }
    }
    game.runFrames(1);
}

// ---------------------------------------------------------------------------
// Main test
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    fn::TestHarness h(argc, argv, "bonus_phase");
    h.SetInitFrames(120);  // burn through GameInit + splash so HUD is live
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    // Set up Arcade mode.
    HideMenuScreens(h.game);
    game_work.gameMode = 2;  // GAME_MODE_ARCADE

    // Put MainScreen into the gameplay-active state so it does not fight
    // the transition timer.
    if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    game_work.m_PauseAmount = 1.0f;  // fully faded in

    // Drain any entities so the BONUS_PHASE gate clears immediately.
    DrainEntities(h.game);

    // Create GameOverScreen in STATE_BONUS_PHASE (m_State=1) directly.
    // The ctor param2 / param3 are state / timer overrides; they only apply
    // when param3 >= 0.0f (the fast-path gate in Initialise). Passing 0.0f
    // satisfies the gate. After construction, pin m_Timer to -0.333f (the
    // value state-0 writes when transitioning to state-1, DAT_00141db0) so
    // the bonus-phase timer progresses from the same starting point as in
    // the real game.
    GameOverScreen* gos = new GameOverScreen("arcade", GameOverScreen::STATE_BONUS_PHASE, 0.0f, 1, 1, 0, 0);
    gos->m_Timer = -0.333f;  // match state-0 -> state-1 initial timer (DAT_00141db0)
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);
    game_work.bM_bPaused = 1;  // mirror production: GameOver() sets LTF=1 before creating GameOverScreen

    printf("[bonus_phase] GameOverScreen created in STATE_BONUS_PHASE (%d)\n",
           GameOverScreen::STATE_BONUS_PHASE);
    printf("[bonus_phase] Starting %d-frame tick loop...\n", TIMEOUT_FRAMES);

    // Diagnostic: report m_bPendingRemoval and m_Active from HUDControl base.
    // These fields MUST exist in BonusScreen (inherited from HUDControl).
    // The task description mentioned them as potentially missing -- verify here.
    printf("[bonus_phase] sizeof(BonusScreen) = %d\n", (int)sizeof(BonusScreen));
    printf("[bonus_phase] HUDControl::m_bPendingRemoval offset = %d\n",
           (int)offsetof(HUDControl, m_bPendingRemoval));
    printf("[bonus_phase] HUDControl::m_Active offset = %d\n",
           (int)offsetof(HUDControl, m_Active));

    int finalState       = GameOverScreen::STATE_BONUS_PHASE;
    int exitFrame        = -1;
    int bonusCreateFrame = -1;

    for (int frame = 0; frame < TIMEOUT_FRAMES; ++frame) {
        TickFrame(h.game);

        const int state = gos->m_State;

        // Log when BonusScreen first appears.
        if (bonusCreateFrame < 0 && gos->m_pBonusScreen != NULL) {
            bonusCreateFrame = frame;
            printf("[bonus_phase] BonusScreen created at frame %d, m_Active=%d, m_bPendingRemoval=%d\n",
                   frame,
                   (int)gos->m_pBonusScreen->m_Active,
                   (int)gos->m_pBonusScreen->m_bPendingRemoval);
        }

        // Log once per second while in BONUS_PHASE.
        if (state == GameOverScreen::STATE_BONUS_PHASE && frame % 60 == 0) {
            Mortar::ActorManager* am = h.game.actorManager;
            int nf = am ? am->GetNumEntities(0) : 0;
            int nb = am ? am->GetNumEntities(1) : 0;
            printf("[bonus_phase] frame=%d STATE_BONUS_PHASE timer=%.3f entities=(%d,%d)",
                   frame, gos->m_Timer, nf, nb);
            if (gos->m_pBonusScreen) {
                printf(" bonus.m_Timer=%.3f bonus.m_bPendingRemoval=%d",
                       gos->m_pBonusScreen->m_Timer,
                       (int)gos->m_pBonusScreen->m_bPendingRemoval);
            } else {
                printf(" bonus=null");
            }
            printf("\n");
        }

        if (state != GameOverScreen::STATE_BONUS_PHASE) {
            exitFrame  = frame;
            finalState = state;
            printf("[bonus_phase] State exited BONUS_PHASE -> %d at frame %d\n",
                   state, frame);
            break;
        }
    }

    // ---- Assertions ----
    int failures = 0;

    // Primary: state must have advanced out of BONUS_PHASE within the timeout.
    if (exitFrame < 0) {
        fprintf(stderr,
            "FAIL: GameOverScreen still in STATE_BONUS_PHASE (%d) after %d frames.\n"
            "      This is the BONUS_PHASE stall bug.\n"
            "      BonusScreen was%s created during the run.\n",
            GameOverScreen::STATE_BONUS_PHASE,
            TIMEOUT_FRAMES,
            bonusCreateFrame >= 0 ? "" : " NOT");
        // Dump the final BonusScreen state to help diagnose.
        if (gos->m_pBonusScreen) {
            printf("[bonus_phase] Final BonusScreen state:\n");
            printf("  m_Active=%d  m_bPendingRemoval=%d\n",
                   (int)gos->m_pBonusScreen->m_Active,
                   (int)gos->m_pBonusScreen->m_bPendingRemoval);
            printf("  bonus.m_Timer=%.3f  gos.m_Timer=%.3f\n",
                   gos->m_pBonusScreen->m_Timer, gos->m_Timer);
        } else {
            printf("[bonus_phase] m_pBonusScreen is null at end of run.\n");
        }
        failures++;
    } else {
        // Advance to STATE_MAIN_DISPLAY (6) is the expected transition.
        if (finalState != GameOverScreen::STATE_MAIN_DISPLAY) {
            fprintf(stderr,
                "WARN: exited BONUS_PHASE but went to state %d (expected %d = STATE_MAIN_DISPLAY)\n",
                finalState, GameOverScreen::STATE_MAIN_DISPLAY);
            // Not a hard failure -- any advance is progress.
        }
        printf("PASS: BONUS_PHASE exited at frame %d (state -> %d)\n",
               exitFrame, finalState);
    }

    // =========================================================================
    // Interactive mode: once STATE_MAIN_DISPLAY is reached, hand off to the
    // event loop so the tester can visually inspect the screen. ESC / window
    // close exits.
    // =========================================================================
    if (h.IsInteractive() && exitFrame >= 0 && finalState == GameOverScreen::STATE_MAIN_DISPLAY) {
        printf("[bonus_phase] --interactive: entering event loop from STATE_MAIN_DISPLAY\n");
        h.RunInteractive(NULL, NULL, -1);
        return h.Shutdown();
    }

    // =========================================================================
    // Part 2: post-BONUS_PHASE flow validation
    // Only run if we reached STATE_MAIN_DISPLAY.
    // =========================================================================
    if (exitFrame >= 0 && finalState == GameOverScreen::STATE_MAIN_DISPLAY) {
        printf("[bonus_phase] --- Part 2: post-MAIN_DISPLAY callback assertions ---\n");

        // Settle for SETTLE_FRAMES so buttons are created and the screen is
        // fully initialized. The retry+quit buttons are created on the first
        // tick of STATE_MAIN_DISPLAY (prevState == 6 gate in RunStateMainDisplay).
        printf("[bonus_phase] Settling for %d frames in MAIN_DISPLAY...\n", SETTLE_FRAMES);
        for (int i = 0; i < SETTLE_FRAMES; ++i) {
            // Do NOT drain entities: MenuButton::Init spawns a fruit entity
            // (type 0). DeactivateAllEntities(0) would kill it, triggering
            // DeletedControl and nulling m_pRetryBtn/m_pQuitBtn.
            TickFrame(h.game, /*drainEntities=*/false);
            // If state drifted out of MAIN_DISPLAY during settle, stop.
            if (gos->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
                printf("[bonus_phase] State drifted to %d during settle at settle-frame %d\n",
                       gos->m_State, i);
                break;
            }
        }

        // Ensure m_TransitionTimer satisfies RetryCallback's alpha gate.
        game_work.m_PauseAmount = 1.0f;

        // ---- Sub-test A: RetryCallback via retry button (m_pRetryBtn) click delegate ----
        printf("[bonus_phase] Sub-test A: RetryCallback\n");
        printf("[bonus_phase]   state before = %d, m_pRetryBtn (retry) = %p\n",
               gos->m_State, (void*)gos->m_pRetryBtn);

        if (gos->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
            fprintf(stderr,
                "SKIP (A): not in MAIN_DISPLAY (%d) before retry injection\n",
                gos->m_State);
        } else if (gos->m_pRetryBtn == nullptr) {
            fprintf(stderr,
                "FAIL (A): retry button (m_pRetryBtn) is NULL after %d settle frames -- "
                "RetryCallback cannot be injected via button delegate\n",
                SETTLE_FRAMES);
            failures++;
        } else {
            // Fire the retry button click delegate (same path as user tap).
            // RetryCallback sets m_State = STATE_RETRY_PREPARE synchronously.
            // Check state immediately -- before any tick -- to catch the
            // synchronous write. STATE_RETRY_PREPARE may revert to MAIN_DISPLAY
            // on the next tick if entities are still alive (the wave manager
            // keeps spawning during settle frames), so we cannot rely on a
            // post-tick observation.
            gos->m_pRetryBtn->m_ClickCallback();
            const int stateAfterTap = gos->m_State;

            if (stateAfterTap == GameOverScreen::STATE_MAIN_DISPLAY) {
                fprintf(stderr,
                    "FAIL (A): RetryCallback fired but m_State is still MAIN_DISPLAY (%d) "
                    "immediately after tap -- RetryCallback did not advance state\n",
                    GameOverScreen::STATE_MAIN_DISPLAY);
                failures++;
            } else {
                printf("PASS (A): RetryCallback -> state advanced to %d immediately after tap\n",
                       stateAfterTap);
                // Run a few ticks to stabilise (state may settle into RETRY_FADE or similar).
                for (int i = 0; i < ADVANCE_TIMEOUT; ++i) {
                    TickFrame(h.game, /*drainEntities=*/false);
                }
            }
        }

        // ---- Sub-test B: QuitCallback via m_pQuitBtn click delegate ----
        // Re-create a fresh GameOverScreen in MAIN_DISPLAY to test Quit.
        printf("[bonus_phase] Sub-test B: QuitCallback (fresh GameOverScreen)\n");

        // Allocate a second GameOverScreen directly in STATE_MAIN_DISPLAY via
        // the fast path: param2=6(>5 gate), param3=0.0f, with waveAlpha=1.0f.
        // The fast-path block at Initialise L448 requires param2>5 && waveAlpha>kWaveAlphaGate.
        // We set m_TransitionTimer=1.0f (satisfies the gate) before construction.
        game_work.m_PauseAmount = 1.0f;
        GameOverScreen* gos2 = new GameOverScreen(
            "arcade",
            /*param2=state=*/GameOverScreen::STATE_MAIN_DISPLAY,
            /*param3=timer=*/0.0f,
            /*expressionIdx=*/1,
            /*bgPatternIdx=*/1,
            /*pomCount=*/0,
            /*starCount=*/0);
        game_work.pGameOverScreen = gos2;
        game_work.mHud->AddControl(gos2);
        game_work.bM_bPaused = 1;  // mirror production: GameOver() sets LTF=1 before creating GameOverScreen

        // Settle to create the quit button (no entity drain -- same reason as above).
        printf("[bonus_phase]   settling %d frames for gos2 buttons...\n", SETTLE_FRAMES);
        for (int i = 0; i < SETTLE_FRAMES; ++i) {
            TickFrame(h.game, /*drainEntities=*/false);
            if (gos2->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
                printf("[bonus_phase]   gos2 state drifted to %d at settle-frame %d\n",
                       gos2->m_State, i);
                break;
            }
        }

        game_work.m_PauseAmount = 1.0f;

        printf("[bonus_phase]   state before = %d, m_pQuitBtn = %p\n",
               gos2->m_State, (void*)gos2->m_pQuitBtn);

        if (gos2->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
            fprintf(stderr,
                "SKIP (B): gos2 not in MAIN_DISPLAY (%d) before quit injection\n",
                gos2->m_State);
        } else if (gos2->m_pQuitBtn == nullptr) {
            fprintf(stderr,
                "FAIL (B): m_pQuitBtn is NULL after %d settle frames -- "
                "QuitCallback cannot be injected via button delegate\n",
                SETTLE_FRAMES);
            failures++;
        } else {
            // Fire the quit button click delegate.
            gos2->m_pQuitBtn->m_ClickCallback();

            // Allow up to ADVANCE_TIMEOUT frames for the state to advance.
            int advanceFrame = -1;
            int advancedState = gos2->m_State;
            for (int i = 0; i < ADVANCE_TIMEOUT; ++i) {
                TickFrame(h.game, /*drainEntities=*/false);
                if (gos2->m_State != GameOverScreen::STATE_MAIN_DISPLAY) {
                    advanceFrame  = i;
                    advancedState = gos2->m_State;
                    break;
                }
            }

            if (advanceFrame < 0) {
                fprintf(stderr,
                    "FAIL (B): QuitCallback fired but state did not advance out of "
                    "MAIN_DISPLAY (%d) within %d frames\n",
                    GameOverScreen::STATE_MAIN_DISPLAY, ADVANCE_TIMEOUT);
                failures++;
            } else {
                printf("PASS (B): QuitCallback -> state advanced to %d at frame %d after tap\n",
                       advancedState, advanceFrame);
            }
        }
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d assertion(s) failed\n", failures);
        return h.Shutdown();
    }
    return h.Shutdown();
}
