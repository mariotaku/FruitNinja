// Per-screen smoke test. Boots the full game with a hidden SDL+GL
// context, pushes the requested screen onto the HUD, ticks a few
// frames, and verifies no crash + Font::DrawString hits the screen
// when expected. Catches regressions like the Font V-flip + maxWidth=0
// divide-by-zero that the home-screen run never exercised.
//
// Run via:
//   cd build/host && ctest --output-on-failure -R screen_
//
// Each ctest entry passes a different screen name.

#include "test_harness.h"
#include <climits>
#include <set>

#include "render/Renderer.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/GameOverScreen.h"
#include "screens/MainScreen.h"
#include "game/StartupEffects.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "hud/ScoreControl.h"
#include "hud/MissControl.h"
#include "hud/FruitFactControl.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"

static int FailUsage() {
    fprintf(stderr,
        "usage: test_screen <main|dojo|about|shop|gamemode|gameover|gameover-transition|classic> [flags]\n"
        "  --interactive: show the window and run the normal main loop\n"
        "                 instead of ticking 30 frames headless. ESC quits.\n"
        "  --screenshot:  dump the framebuffer to tmp/test/screenshots/.\n"
        "  --no-miss:     Classic only -- pin game_work.missCount to a very large\n"
        "                 negative value so Fruit miss penalties never reach\n"
        "                 the > 2 game-over trigger. Use with --interactive\n"
        "                 to leave the gameplay scene running for visual test.\n");
    return 1;
}

// Userdata for the classic --no-miss interactive callback.
struct ClassicNoMissData {
    std::set<Mortar::Entity*> seen;
    int frameIdx;
    Game* game;
};

static bool ClassicNoMissTick(Game& game, int frame, void* userdata) {
    ClassicNoMissData* d = (ClassicNoMissData*)userdata;
    game_work.missCount = 0;

    Mortar::ActorManager* am = game.actorManager;
    if (am) {
        std::set<Mortar::Entity*> current;
        for (int type = 0; type <= 1; ++type) {
            std::list<Mortar::Entity*>::iterator it;
            Mortar::Entity* e = am->GetEntityFirst(type, it);
            while (e) {
                current.insert(e);
                if (d->seen.find(e) == d->seen.end()) {
                    printf("[SPAWN] f=%d type=%d ent=%p pos=(%6.1f,%6.1f) vel=(%6.2f,%6.2f)\n",
                           frame, type, (void*)e,
                           e->pos.x, e->pos.y, e->vel.x, e->vel.y);
                } else if (frame % 10 == 0) {
                    printf("[POS]   f=%d type=%d ent=%p pos=(%6.1f,%6.1f) vel=(%6.2f,%6.2f)\n",
                           frame, type, (void*)e,
                           e->pos.x, e->pos.y, e->vel.x, e->vel.y);
                }
                e = am->GetEntityNext(type, it);
            }
        }
        for (std::set<Mortar::Entity*>::iterator sit = d->seen.begin();
             sit != d->seen.end(); ++sit) {
            if (current.find(*sit) == current.end()) {
                printf("[DESPAWN] f=%d ent=%p\n", frame, (void*)*sit);
            }
        }
        d->seen.swap(current);
    }

    return true;  // keep running; harness caps at maxFrames
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* screenName = argv[1];

    bool noMiss = false;
    for (int i = 2; i < argc; i++) {
        if      (strcmp(argv[i], "--interactive") == 0) {}  // handled by harness
        else if (strcmp(argv[i], "--screenshot")  == 0) {}  // handled by harness
        else if (strcmp(argv[i], "--no-miss")     == 0) noMiss = true;
        else return FailUsage();
    }

    fn::TestHarness h(argc, argv, screenName);
    h.SetInitFrames(5);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after runFrames(5)\n");
        return 1;
    }

    // Push the requested screen and hide all pre-existing HUD controls
    // (MainScreen, BG, etc.) so the screenshot/visual capture isolates
    // only the requested screen. Without this, parent menus draw under
    // the target screen and clutter the output.
    auto hideAllExisting = [&]() {
        for (auto it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
            (*it)->m_Active = 0;
        }
    };

    if (strcmp(screenName, "main") == 0) {
        // already there — leave MainScreen active
    } else if (strcmp(screenName, "dojo") == 0) {
        hideAllExisting();
        DojoScreen* s = new DojoScreen();
        game_work.mHud->AddControl(s);
    } else if (strcmp(screenName, "about") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen();
        dojo->m_Active = 0;  // dojo is just AboutScreen's parent for back-nav
        game_work.mHud->AddControl(dojo);
        AboutScreen* s = new AboutScreen(dojo);
        s->Init();
        game_work.mHud->AddControl(s);
    } else if (strcmp(screenName, "shop") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen();
        dojo->m_Active = 0;
        game_work.mHud->AddControl(dojo);
        ShopScreen* s = new ShopScreen(dojo);
        game_work.mHud->AddControl(s, false);
        s->Init();
    } else if (strcmp(screenName, "gamemode") == 0) {
        hideAllExisting();
        GameModeScreen* s = new GameModeScreen(false);
        game_work.mHud->AddControl(s);
    } else if (strcmp(screenName, "classic") == 0) {
        // Active Classic-mode gameplay HUD: ScoreControl + 3x MissControl
        // widgets. Both are created in GameInit (already done by runFrames
        // above) and remain live throughout state 2. We just need to:
        //   - hide MainScreen so the gameplay layer is unobstructed
        //   - select gameMode = 0 (Classic)
        //   - fire PrepareForLevelStart -> WaveManager::Reset to load wave 0
        //   - clear levelTransitionFlag (binary path: GameModeScreen::Update camera-settle)
        // so the spawn pump runs and the score / miss widgets see real game
        // state. Verifies the HUD widgets are wired and rendering during
        // actual gameplay.
        // hideAllExisting() would also kill ScoreControl + MissControl +
        // MainScreen (m_bActive=0), which is exactly what we DON'T want for
        // a gameplay-active test. Only deactivate menu/screen overlays.
        for (auto it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
            HUDControl* c = *it;
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_Active = 0;
            }
        }
        game_work.gameMode = 0;
        PrepareForLevelStart();
        game_work.bM_bPaused = 0;
        // Simulate the GameModeScreen state-6 snap (binary @ 0x0013f2b0):
        //   game_work.m_PauseAmount = 0.0f
        //   mainScreen.m_State    = STATE_CAMERA_FADE (0x11)
        // Without the state push, MainScreen stays in STATE_CAMERA_ZOOM
        // (initial state) and lerps the timer toward -1 every frame, which
        // makes ScoreControl Stage 6 compute pos.x = -218 - 200*|-1| = -418
        // (off-screen left). STATE_CAMERA_FADE is the gameplay-active state
        // and its update only writes timer when timer < 0.
        if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
        game_work.m_PauseAmount = 0.0f;
        // Sanity: confirm at least one ScoreControl + one MissControl exist
        // in HUD (created by GameInit step 3/4). If they're missing, the
        // test will print a diagnostic but not fail the smoke pass.
        int scoreCount = 0;
        int missCount  = 0;
        for (auto it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
            if (dynamic_cast<ScoreControl*>(*it)) scoreCount++;
            if (dynamic_cast<MissControl*>(*it))  missCount++;
        }
        printf("[test_screen classic] ScoreControl in HUD: %d; MissControl in HUD: %d\n",
               scoreCount, missCount);
        if (scoreCount < 1) {
            fprintf(stderr, "FAIL: no ScoreControl in HUD during Classic gameplay\n");
            return 1;
        }
        if (missCount < 3) {
            fprintf(stderr, "FAIL: expected 3 MissControl widgets in HUD, found %d\n", missCount);
            return 1;
        }
        if (noMiss) {
            // Fruit::CollisionResponse (src/entities/Fruit.cpp:~536) does
            //   if (game_work.missCount > 2) FN::GameOver(...);
            // game_work.missCount is uint8_t so a large negative initial value
            // wraps and doesn't help -- the per-frame reset loop below
            // (interactive path) keeps it pinned at 0 instead.
            game_work.missCount = 0;
            printf("[test_screen classic --no-miss] per-frame missCount reset enabled\n");
        }
    } else if (strcmp(screenName, "gameover") == 0) {
        // Force a Classic-mode game-over scene so the texture loads
        // (classic-game-over-bg.tex / retry-button.tex / quit-button.tex /
        // game-over.tex / fruitfact-panel.tex / fruitfact-backplate.tex)
        // exercise the LoadLocalisedTexture path. Score is set to a value
        // that triggers the highscore branch so save->newBestThisGame is
        // also exercised.
        // Don't deactivate ScoreControl / MissControl / MainScreen — they're
        // alive during game-over (ScoreControl renders the final banner).
        for (auto it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
            HUDControl* c = *it;
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_Active = 0;
            }
        }
        game_work.gameMode = 0;     // Classic
        game_work.currentScore = 1234;
        // MainScreen menu-idle states lerp m_TransitionTimer toward -1 every
        // frame. Pin it to STATE_CAMERA_FADE so it doesn't fight the
        // explicit 0.5 we set below (binary path: GameModeScreen state-6
        // snap leaves MainScreen in STATE_CAMERA_FADE then GameOverScreen
        // ramps timer toward +1).
        if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
        game_work.m_PauseAmount = 0.5f;  // mid-transition; not the fast-path
        // ctor args: (modeName, initialState, initialTimer,
        //             expressionIdx, bgPatternIdx, pomCount, starCount).
        // initialState=6 + initialTimer=0.0 puts us straight into the
        // main display state; runFrames below ticks score-submission.
        GameOverScreen* s = new GameOverScreen("classic", 6, 0.0f, 1, 1, 0, 0);
        game_work.pGameOverScreen = s;
        game_work.mHud->AddControl(s);
    } else if (strcmp(screenName, "gameover-transition") == 0) {
        // Full state-0 -> state-6 transition test with runtime assertions.
        // Verifies the fixes from b477592 (title slide-out gate),
        // a5db4b8 (sensei index defaults), 86bd3ff (highscore label),
        // fc098ee (Fruit::GetFact clamp + DrawOrder),
        // 25d7733 (MainScreen unconditional m_TransitionTimer mirror removal).
        //
        // Boots into state 0 (entry animation) and runs frames to drive the
        // full transition: title zooms in (state 0, 1.9s), title slides
        // down (state 6, ~1s), sensei + fact + retry/quit appear.
        //
        // IMPORTANT: do NOT hide MainScreen here. The real-game path has
        // MainScreen alive in the HUD list alongside GameOverScreen.
        // MainScreen::Update writes Game+0x0c (m_TransitionTimer) only from
        // within specific state-case bodies in the binary; an earlier
        // port bug had an UNCONDITIONAL mirror that stomped GameOverScreen's
        // alpha ramp to 0 every frame. Hiding MainScreen would mask such a
        // regression. Just deactivate the other screens (Dojo, Shop, etc)
        // so only MainScreen + GameOverScreen are live, matching the real
        // game-over scenario.
        for (auto it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
            HUDControl* c = *it;
            // Keep MainScreen + gameplay-HUD controls (ScoreControl /
            // MissControl); hide the menu/screen controls.
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_Active = 0;
            }
        }
        // Real game has MainScreen in STATE_CAMERA_FADE during gameplay;
        // that state only writes game_work.m_PauseAmount when it's < 0
        // (gate matches binary @ 0x0014c19a). Initial state in test is
        // STATE_CAMERA_ZOOM which lerps timer toward -1 every frame --
        // would fight GameOverScreen's ramp toward +1.
        if (game_work.mMainScreen) game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
        game_work.gameMode = 0;       // Classic
        game_work.currentScore = 1234;
        game_work.m_PauseAmount = 0.0f;
        // Seed a saved highscore so the layer-0x80 highscore-text block runs
        if (game_work.m_SaveData) game_work.m_SaveData->m_highscore = 5000;

        // Start at state 0 (entry animation) -- this is the realistic path
        // from gameplay -> game over.
        GameOverScreen* s = new GameOverScreen("classic", 0, 0.0f, 1, 1, 0, 0);
        game_work.pGameOverScreen = s;
        game_work.mHud->AddControl(s);

        // 1.9s state-0 + ~1s state-6 alpha ramp = ~180 frames at 60fps, plus
        // 60 extra idle frames so retry/quit creation (after m_ProgressCounter
        // == 10) and slide-in animations fully settle.
        //
        // STATE_ENTRY_ANIM's m_Timer is advanced by UpdateRealtime(dtSeconds)
        // in the port build (see GameOverScreen::UpdateRealtime, #ifndef
        // __bada__) rather than by the 60Hz Update() -- the real game loop
        // pumps both per presented frame (Game::run() calls tickRealtimeUi()
        // alongside stepUpdate(), see GameSDL.cpp). h.RunHeadless only drives
        // Update() via game.runFrames(), so without also calling
        // tickRealtimeUi() here the entry timer never advances past 0 and the
        // screen never leaves state 0. Mirrors the same fix in
        // test_scrollingmenu_updaterealtime.cpp / test_ring_texture_lifecycle.cpp.
        for (int i = 0; i < 240; ++i) {
            h.RunHeadless(1);
            h.game.tickRealtimeUi(1.0f / 60.0f);
        }

        // ---- Assertions ----
        int failures = 0;

        // 1. State machine: should have advanced from 0 -> 6.
        if (s->m_State != 6) {
            fprintf(stderr,
                "FAIL: m_State should be 6 after transition, got %d\n",
                s->m_State);
            failures++;
        }

        // 2. m_TransitionTimer should have ramped to ~1.0.
        if (game_work.m_PauseAmount < 0.99f) {
            fprintf(stderr,
                "FAIL: m_TransitionTimer should reach ~1.0, got %f\n",
                game_work.m_PauseAmount);
            failures++;
        }

        // 3. Title (HUDControl3d pos.y) should have slid down off-screen.
        //    Per binary: pos.y = 224 * alpha, gated on pos.y < 212.8.
        //    Expected final pos.y ~ 212.8 (just below the gate cutoff).
        if (s->pos.y < 100.0f) {
            fprintf(stderr,
                "FAIL: title should slide down, pos.y = %f (expected > 100)\n",
                s->pos.y);
            failures++;
        }

        // 4. FruitFactControl created during state 6.
        if (!s->m_pFruitFact) {
            fprintf(stderr, "FAIL: m_pFruitFact should be created in state 6\n");
            failures++;
        } else {
            // 4a. m_FactText set after Init.
            // (FruitFactControl uses m_FactText @+0x7C instead of old m_pCurFactString)
            if (!s->m_pFruitFact->m_FactText) {
                fprintf(stderr,
                    "FAIL: FruitFactControl::m_FactText should be non-null after Init\n");
                failures++;
            }

            // 4b. FruitFactControl pos animated in from off-screen.
            //     Final pos.x = m_OffsetPos.x + 183.0 = -18 + 183 = 165.
            if (s->m_pFruitFact->pos.x > 200.0f) {
                fprintf(stderr,
                    "FAIL: FruitFactControl should slide in (pos.x = %f, expected ~165)\n",
                    s->m_pFruitFact->pos.x);
                failures++;
            }
        }

        // 5. m_OffsetPos.x should have animated from 368 -> ~-18.
        if (s->m_OffsetPos.x > 50.0f) {
            fprintf(stderr,
                "FAIL: m_OffsetPos.x should animate to ~-18, got %f\n",
                s->m_OffsetPos.x);
            failures++;
        }

        // 6. Sensei body/head indices should be valid (binary > 0 gate).
        if (s->m_BgPatternIdx <= 0 || s->m_BgPatternIdx > 3) {
            fprintf(stderr,
                "FAIL: m_BgPatternIdx out of range [1..3], got %d\n",
                s->m_BgPatternIdx);
            failures++;
        }
        if (s->m_ExpressionIdx <= 0 || s->m_ExpressionIdx > 3) {
            fprintf(stderr,
                "FAIL: m_ExpressionIdx out of range [1..3], got %d\n",
                s->m_ExpressionIdx);
            failures++;
        }

        // 7. Retry/Quit buttons should spawn after m_StarCount == 10.
        if (!s->m_pRetryBtn) {
            fprintf(stderr, "FAIL: retry button (m_pRetryBtn) should be spawned in state 6\n");
            failures++;
        }
        if (!s->m_pQuitBtn) {
            fprintf(stderr, "FAIL: m_pQuitBtn should be spawned in state 6\n");
            failures++;
        }

        // 8. ScoreControl should have slid off-screen via wave-mode shift.
        //    pos.x = -218 - 200 * |waveTimer|. Sliding LEFT past the base
        //    -218 confirms the wave-mode shift is running (the actual final
        //    pos depends on per-frame Update order; we just want < -218).
        for (auto it = game_work.mHud->controls.begin();
             it != game_work.mHud->controls.end(); ++it) {
            ScoreControl* sc = dynamic_cast<ScoreControl*>(*it);
            if (sc) {
                if (sc->pos.x >= -218.0f) {
                    fprintf(stderr,
                        "FAIL: ScoreControl should slide left past -218, pos.x = %f\n",
                        sc->pos.x);
                    failures++;
                }
                break;
            }
        }

        if (failures > 0) {
            fprintf(stderr,
                "gameover-transition test FAILED with %d assertion(s)\n",
                failures);
            h.Shutdown();
            return 1;
        }
        fprintf(stdout, "PASS: gameover-transition all assertions ok "
                        "(state=%d, alpha=%f, fact=%s, retry=%s, quit=%s)\n",
                s->m_State, game_work.m_PauseAmount,
                (s->m_pFruitFact && s->m_pFruitFact->m_FactText) ? "ok" : "MISSING",
                s->m_pRetryBtn ? "ok" : "MISSING",
                s->m_pQuitBtn ? "ok" : "MISSING");
        return h.Shutdown();
    } else {
        fprintf(stderr, "unknown screen '%s'\n", screenName);
        return FailUsage();
    }

    if (h.IsInteractive()) {
        if (strcmp(screenName, "classic") == 0 && noMiss) {
            // Custom loop that mirrors Game::run() pacing but resets
            // game_work.missCount=0 every tick so misses never reach the > 2
            // game-over trigger in Fruit::CollisionResponse. game.run()
            // can't be intercepted mid-loop, so we use runFrames(1) +
            // reset and rely on Game::runFrames for event pump / vsync.
            // ESC / window close still flips game.running -> false.
            //
            // While we're here -- track every type-0 (fruit) and type-1
            // (bomb) entity each tick so we can verify visually whether
            // spawn happens off-screen or pops into the viewport. SPAWN
            // logs every new pointer, DESPAWN logs removal, POS logs the
            // trail every 10 frames. Runtime capped at 60s (~6000 frames
            // at ~100fps Game::run pacing) so a CI capture is bounded.
            const int kMaxFrames = 60 * 100;  // 60s at ~100fps
            ClassicNoMissData d;
            d.frameIdx = 0;
            d.game = &h.game;
            h.RunInteractive(ClassicNoMissTick, &d, kMaxFrames);
            printf("[test_screen classic --no-miss] exit after interactive run (%s)\n",
                   h.game.running ? "60s timeout" : "window closed");
        } else {
            // Interactive: hand off to the normal main loop. ESC / window
            // close exits. No automatic timeout.
            h.RunInteractive(NULL, NULL, -1);
        }
    } else {
        // Headless: drive enough frames to finish the in-transition
        // and reach the screen's idle/visible state.
        //
        // Screens use multiplicative lerp (alpha += (1-alpha)*0.125 etc.)
        // with thresholds around 0.999. With dt=1/60 that's ~55 frames
        // to settle one stage; About/Shop have an extra fade-in then a
        // back-button spawn at alpha>0.999. 180 frames (~3s) covers all
        // observed in-transitions with margin.
        //
        // Run in two passes so we hit Draw in the steady state explicitly:
        //   pass 1: drive the in-transition to completion
        //   pass 2: 30 more frames of "idle Draw" — this is what catches
        //           rendering bugs that only fire post-transition (e.g.
        //           glyph emission once the screen is fully alpha=1).
        h.RunHeadless(180);
        h.RunHeadless(30);

        if (h.IsScreenshot()) {
            char screenshotName[64];
            snprintf(screenshotName, sizeof(screenshotName), "screen_%s", screenName);
            h.ScreenshotPng(screenshotName);
        }
    }

    if (!h.IsInteractive()) {
        fprintf(stdout,
            "PASS: screen '%s' transition + 30 idle frames clean\n",
            screenName);
    }
    return h.Shutdown();
}
