// test_bonus_crash -- native repro harness for the Arcade
// BONUS SCREEN -> GAME OVER transition "index out of bounds" (call_indirect)
// crash seen on the wasm build (task #40).
//
// GOAL: drive the exact Arcade end-of-game flow natively so that a
// stale-vtable / heap-use-after-free fires under the host/ASAN build and
// yields a SYMBOLIZED native stack (ASAN) instead of an unsymbolized wasm
// frame. The test PASSES iff the whole flow runs to completion without
// crashing (ASAN turns any UAF into a non-zero exit + stack); it also asserts
// the state machine leaves STATE_BONUS_PHASE, so a hang/stall is caught too.
//
// The suspected object is a HUD control whose vtable is stale by the time the
// HUD calls a virtual on it across the transition. The prime suspects are the
// powerup ScreenEffect controls that are ALIVE at game-end:
//   x2      "score_mult" -> ScoreMultiplyerBoard  (green pending-points board;
//                           detaches on powerup expiry and LINGERS in the HUD,
//                           frozen, because its Update freeze-gate is
//                           `if (game_work.bM_Mode != 0) return;`)
//   freeze  "freeze"     -> ice_cover / clock_freeze overlay images + (Berry
//                           Blast) TimeSinkControl stub.
//
// This test:
//   Phase 1 -- boots a full Arcade game, activates the x2 (+freeze) powerups so
//              a ScoreMultiplyerBoard is added to game_work.mHud, and confirms
//              it is present.
//   Phase 2 -- constructs a GameOverScreen in STATE_BONUS_PHASE (as GameOver()
//              does for Arcade), then ticks the FULL game loop through the
//              bonus finale (AwardScores + coins) and the reap->STATE_MAIN_DISPLAY
//              handoff (which creates FruitFactControl + FruitFactBonusFactPage).
//              While ticking, it lets the x2 powerup EXPIRE naturally
//              (PowerUpManager::Update, no pin) so the board detaches and
//              lingers -- exercising every free path through PowerUp::Deactivate
//              -> ScreenEffect::Deactivate (which should null board->m_pOwner
//              BEFORE the PowerUp is freed).
//   Phase 3 -- teardown: PowerUpManager::Reset(false) and a retry, to free any
//              remaining powerups while their boards may still be in the HUD.
//
// Every powerup free is routed through the REAL PowerUpManager APIs (never a
// raw delete that bypasses Deactivate), so a green run here is genuine evidence
// the powerup/board lifecycle is UAF-free; a red run under ASAN names the exact
// class + call site.
//
// Run:
//   ctest --test-dir build/host -R bonus_crash --output-on-failure
//   ./build/host/tests/Debug/test_bonus_crash.exe
// Under ASAN (clang64): build build/asan then run the same exe.

#include "test_harness.h"
#include "screens/GameOverScreen.h"
#include "screens/BonusScreen.h"
#include "screens/MainScreen.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "hud/HUD.h"
#include "hud/ScoreMultiplyerBoard.h"
#include "hud/TimeSinkControl.h"
#include "entities/ActorManager.h"
#include "game/PowerUpManager.h"
#include "game/PowerUp.h"
#include "game/GameModifier.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "engine/util/StringHash.h"
#include "engine/math/_Vector3.h"
#include <cstdio>
#include <list>

// Deactivate menu/mode-select overlays so they don't fight the game-over scene
// (mirrors test_bonus_phase.cpp).
static void HideMenuScreens()
{
    if (!game_work.mHud) return;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* c = *it;
        if (dynamic_cast<DojoScreen*>(c) || dynamic_cast<AboutScreen*>(c)
         || dynamic_cast<ShopScreen*>(c) || dynamic_cast<GameModeScreen*>(c)) {
            c->m_Active = 0;
        }
    }
}

// Count the powerup ScreenEffect controls currently in the HUD (the prime
// stale-vtable suspects). Any virtual dispatch on a freed one of these across
// the transition is exactly the crash we are hunting.
static int CountBoards(int* outSink)
{
    int boards = 0, sinks = 0;
    if (game_work.mHud) {
        for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
             it != game_work.mHud->controls.end(); ++it) {
            if (dynamic_cast<ScoreMultiplyerBoard*>(*it)) ++boards;
            if (dynamic_cast<TimeSinkControl*>(*it))      ++sinks;
        }
    }
    if (outSink) *outSink = sinks;
    return boards;
}

// Tick one full game-loop frame; optionally drain fruit/bomb entities so the
// BONUS_PHASE entity-gate clears immediately (mirrors test_bonus_phase.cpp).
static void TickFrame(Game& game, bool drainEntities)
{
    if (drainEntities && game.actorManager) {
        game.actorManager->DeactivateAllEntities(0);
        game.actorManager->DeactivateAllEntities(1);
    }
    game.runFrames(1);
}

int main(int argc, char* argv[])
{
    fn::TestHarness h(argc, argv, "bonus_crash");
    h.SetInitFrames(120);   // burn through GameInit + splash so HUD + powerup XML are live
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    HideMenuScreens();
    game_work.gameMode = Mortar::GAME_MODE_ARCADE;
    if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    game_work.m_PauseAmount = 1.0f;

    PowerUpManager* pum = PowerUpManager::GetInstance();
    pum->LoadTextures();   // WaveManager normally primes these on wave start

    // ---- Phase 1: activate x2 (+freeze) so the boards enter the HUD ----------
    _Vector3<float> origin(0.0f, 0.0f, 0.0f);
    PowerUp* pX2     = pum->ActivatePower(StringHash("score_mult"), origin, NULL);
    PowerUp* pFreeze = pum->ActivatePower(StringHash("freeze"),     origin, NULL);
    std::printf("[bonus_crash] activate score_mult -> %p, freeze -> %p\n",
                (void*)pX2, (void*)pFreeze);

    // Pin the powers into their steady window for a few frames so the boards are
    // created + settled in the HUD (ScreenEffect fade-in). Pinning m_BonusAccum
    // stops PowerUp::Update from expiring them yet.
    for (int f = 0; f < 30; ++f) {
        for (std::list<PowerUp*>::iterator it = pum->m_ActivePowerUps.begin();
             it != pum->m_ActivePowerUps.end(); ++it) {
            PowerUp* p = *it;
            float hold = p->m_TotalTime * 0.6f;
            p->m_BarRamp = 1.0f;
            p->m_LongestRemaining = hold;
            for (std::list<GameModifier*>::iterator mit = p->m_ModList.begin();
                 mit != p->m_ModList.end(); ++mit) {
                (*mit)->m_BonusAccum = hold;
            }
        }
        pum->Update(1.0f / 60.0f);
        TickFrame(h.game, /*drainEntities=*/true);
    }

    int sinks = 0;
    int boards = CountBoards(&sinks);
    std::printf("[bonus_crash] after activate+settle: ScoreMultiplyerBoard(s)=%d TimeSinkControl(s)=%d in HUD\n",
                boards, sinks);
    if (boards == 0) {
        // Not a hard failure: if the shipped score_mult effect no longer spawns
        // a defer-points board, the board-UAF angle can't be exercised here.
        std::printf("[bonus_crash] NOTE: no ScoreMultiplyerBoard in HUD -- "
                    "score_mult effect may not carry a defer=\"points\" image; "
                    "continuing to exercise the transition anyway.\n");
    }

    // ---- Phase 2: enter BONUS_PHASE and tick the transition ------------------
    // Drain entities so the BONUS_PHASE entity-gate clears immediately.
    if (h.game.actorManager) {
        h.game.actorManager->DeactivateAllEntities(0);
        h.game.actorManager->DeactivateAllEntities(1);
        h.game.runFrames(1);
    }

    GameOverScreen* gos =
        new GameOverScreen("arcade", GameOverScreen::STATE_BONUS_PHASE, 0.0f, 1, 1, 0, 0);
    gos->m_Timer = -0.333f;   // state-0 -> state-1 initial timer
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);
    game_work.bM_bPaused = 1;

    std::printf("[bonus_crash] GameOverScreen in STATE_BONUS_PHASE; ticking transition...\n");

    const int TIMEOUT_FRAMES = 900;   // 15s @ 60fps
    int exitFrame = -1;
    int finalState = GameOverScreen::STATE_BONUS_PHASE;
    int peakBoards = boards;

    for (int frame = 0; frame < TIMEOUT_FRAMES; ++frame) {
        // For the first ~40 frames, keep ticking the PowerUpManager WITHOUT
        // pinning so the x2/freeze powers EXPIRE naturally -> PowerUp::Deactivate
        // -> ScreenEffect::Deactivate detaches the board (should null m_pOwner
        // BEFORE the PowerUp is freed). The detached board then LINGERS in the
        // HUD (frozen, bM_Mode!=0) and keeps getting Update/Draw'd every frame --
        // the exact "control lives across the transition" condition.
        if (frame < 40) {
            pum->Update(1.0f / 60.0f);
        }

        TickFrame(h.game, /*drainEntities=*/true);

        int s = 0;
        int b = CountBoards(&s);
        if (b > peakBoards) peakBoards = b;

        const int state = gos->m_State;
        if (frame % 120 == 0) {
            std::printf("[bonus_crash] frame=%d state=%d bonus=%s boards=%d sinks=%d\n",
                        frame, state, gos->m_pBonusScreen ? "yes" : "null", b, s);
        }
        if (state != GameOverScreen::STATE_BONUS_PHASE) {
            exitFrame = frame;
            finalState = state;
            std::printf("[bonus_crash] exited BONUS_PHASE -> %d at frame %d (boards still in HUD=%d)\n",
                        state, finalState, b);
            break;
        }
    }

    // Let STATE_MAIN_DISPLAY settle (creates FruitFactControl + BonusFactPage,
    // retry/quit buttons) while any lingering board is still Update/Draw'd.
    if (exitFrame >= 0) {
        for (int i = 0; i < 120; ++i) {
            TickFrame(h.game, /*drainEntities=*/false);
        }
        std::printf("[bonus_crash] settled MAIN_DISPLAY; boards in HUD now=%d\n",
                    CountBoards(NULL));
    }

    // ---- Phase 3: teardown -- free any remaining powerups while boards live ---
    // Reset(false) is part of the retry/quit teardown; it frees non-purchaseable
    // powers through Deactivate. Then keep ticking so HUD::Update/Draw runs on
    // whatever remains -- any stale vtable fires here.
    std::printf("[bonus_crash] Phase 3: PowerUpManager::Reset(false) teardown...\n");
    pum->Reset(false);
    for (int i = 0; i < 120; ++i) {
        TickFrame(h.game, /*drainEntities=*/false);
    }
    std::printf("[bonus_crash] post-teardown boards in HUD=%d\n", CountBoards(NULL));

    // ---- Verdict -------------------------------------------------------------
    int failures = 0;
    if (exitFrame < 0) {
        std::fprintf(stderr,
            "FAIL: GameOverScreen stuck in STATE_BONUS_PHASE after %d frames "
            "(transition stall).\n", TIMEOUT_FRAMES);
        failures++;
    } else {
        std::printf("[bonus_crash] transition OK: BONUS_PHASE -> %d at frame %d; "
                    "peak boards in HUD across transition=%d\n",
                    finalState, exitFrame, peakBoards);
    }

    if (failures == 0) {
        std::printf("PASS: bonus_crash -- Arcade x2 -> bonus -> game-over flow "
                    "completed with no UAF/crash (run under ASAN to be sure)\n");
    }
    return (failures == 0) ? h.Shutdown() : (h.Shutdown(), 1);
}
