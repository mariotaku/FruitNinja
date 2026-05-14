// Per-screen smoke test. Boots the full game with a hidden SDL+GL
// context, pushes the requested screen onto the HUD, ticks a few
// frames, and verifies no crash + Font::DrawString hits the screen
// when expected. Catches regressions like the Font V-flip + maxWidth=0
// divide-by-zero that the home-screen run never exercised.
//
// Run via:
//   cd build && ctest --output-on-failure -R screen_
//
// Each ctest entry passes a different screen name.

#include <SDL.h>
#include "render/gl_funcs.h"
#include <cstdlib>

// Test-only: glReadPixels isn't in the project's thin gl_funcs.h
// wrapper. Load it dynamically via SDL_GL_GetProcAddress so the test
// doesn't need a static link to opengl32 / libGL.
typedef int     GLint_t;
typedef unsigned int GLsizei_t;
typedef unsigned int GLenum_t;
typedef void    GLvoid_t;
typedef void (*PFN_glReadPixels)(GLint_t, GLint_t, GLsizei_t, GLsizei_t,
                                 GLenum_t, GLenum_t, GLvoid_t*);
static PFN_glReadPixels g_glReadPixels = nullptr;
#include "Game.h"
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
#include "hud/ScoreControl.h"
#include "hud/MissControl.h"
#include "hud/FruitFactControl.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_screen <main|dojo|about|shop|gamemode|gameover|gameover-transition|classic> [--interactive]\n"
        "  --interactive: show the window and run the normal main loop\n"
        "                 instead of ticking 30 frames headless. ESC quits.\n");
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* screenName = argv[1];
    bool interactive = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--interactive") == 0) interactive = true;
        else if (strcmp(argv[i], "--screenshot") == 0) {} // handled later
        else return FailUsage();
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(FRUIT_GL_API_ES1)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    // Hidden in headless mode (FB still rendered); shown in interactive.
    Uint32 winFlags = SDL_WINDOW_OPENGL | (interactive ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    SDL_Window* window = SDL_CreateWindow(
        "fruit-ninja-test",
        interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
        interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
        960, 640, winFlags);
    if (!window) { fprintf(stderr, "Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { fprintf(stderr, "GL ctx failed: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(interactive ? 1 : 0);

    if (!gl_load_functions()) { fprintf(stderr, "gl_load_functions failed\n"); return 1; }

    Game game;
    if (!game.init(window, gl)) { fprintf(stderr, "game.init failed\n"); return 1; }

    // Drive a few frames first so the splash -> game transition
    // initialises HUD + MainScreen.
    game.runFrames(5);
    if (!game.hud) {
        fprintf(stderr, "FAIL: game.hud is null after runFrames(5)\n");
        return 1;
    }

    // Push the requested screen and hide all pre-existing HUD controls
    // (MainScreen, BG, etc.) so the screenshot/visual capture isolates
    // only the requested screen. Without this, parent menus draw under
    // the target screen and clutter the output.
    auto hideAllExisting = [&]() {
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
            (*it)->m_bActive = 0;
        }
    };

    if (strcmp(screenName, "main") == 0) {
        // already there — leave MainScreen active
    } else if (strcmp(screenName, "dojo") == 0) {
        hideAllExisting();
        DojoScreen* s = new DojoScreen(game);
        game.hud->AddControl(s);
    } else if (strcmp(screenName, "about") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen(game);
        dojo->m_bActive = 0;  // dojo is just AboutScreen's parent for back-nav
        game.hud->AddControl(dojo);
        AboutScreen* s = new AboutScreen(game, dojo);
        s->Init();
        game.hud->AddControl(s);
    } else if (strcmp(screenName, "shop") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen(game);
        dojo->m_bActive = 0;
        game.hud->AddControl(dojo);
        ShopScreen* s = new ShopScreen(game, dojo);
        game.hud->AddControl(s, false);
        s->Init();
    } else if (strcmp(screenName, "gamemode") == 0) {
        hideAllExisting();
        GameModeScreen* s = new GameModeScreen(game, false);
        game.hud->AddControl(s);
    } else if (strcmp(screenName, "classic") == 0) {
        // Active Classic-mode gameplay HUD: ScoreControl + 3x MissControl
        // widgets. Both are created in GameInit (already done by runFrames
        // above) and remain live throughout state 2. We just need to:
        //   - hide MainScreen so the gameplay layer is unobstructed
        //   - select gameMode = 0 (Classic)
        //   - fire PrepareForLevelStart -> WaveManager::Reset to load wave 0
        //   - clear pauseFlag (binary path: GameModeScreen::Update camera-settle)
        // so the spawn pump runs and the score / miss widgets see real game
        // state. Verifies the HUD widgets are wired and rendering during
        // actual gameplay.
        // hideAllExisting() would also kill ScoreControl + MissControl +
        // MainScreen (m_bActive=0), which is exactly what we DON'T want for
        // a gameplay-active test. Only deactivate menu/screen overlays.
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
            HUDControl* c = *it;
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_bActive = 0;
            }
        }
        game.gameMode = 0;
        FN::PrepareForLevelStart();
        game.pauseFlag = 0;
        // Simulate the GameModeScreen state-6 snap (binary @ 0x0013f2b0):
        //   game.m_TransitionTimer = 0.0f
        //   mainScreen.m_State    = STATE_CAMERA_FADE (0x11)
        // Without the state push, MainScreen stays in STATE_CAMERA_ZOOM
        // (initial state) and lerps the timer toward -1 every frame, which
        // makes ScoreControl Stage 6 compute pos.x = -218 - 200*|-1| = -418
        // (off-screen left). STATE_CAMERA_FADE is the gameplay-active state
        // and its update only writes timer when timer < 0.
        if (game.mainScreen) game.mainScreen->SetState(STATE_CAMERA_FADE);
        game.m_TransitionTimer = 0.0f;
        // Sanity: confirm at least one ScoreControl + one MissControl exist
        // in HUD (created by GameInit step 3/4). If they're missing, the
        // test will print a diagnostic but not fail the smoke pass.
        int scoreCount = 0;
        int missCount  = 0;
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
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
    } else if (strcmp(screenName, "gameover") == 0) {
        // Force a Classic-mode game-over scene so the texture loads
        // (classic-game-over-bg.tex / retry-button.tex / quit-button.tex /
        // game-over.tex / fruitfact-panel.tex / fruitfact-backplate.tex)
        // exercise the LoadLocalisedTexture path. Score is set to a value
        // that triggers the highscore branch so save->newBestThisGame is
        // also exercised.
        // Don't deactivate ScoreControl / MissControl / MainScreen — they're
        // alive during game-over (ScoreControl renders the final banner).
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
            HUDControl* c = *it;
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_bActive = 0;
            }
        }
        game.gameMode = 0;     // Classic
        game.currentScore = 1234;
        // MainScreen menu-idle states lerp m_TransitionTimer toward -1 every
        // frame. Pin it to STATE_CAMERA_FADE so it doesn't fight the
        // explicit 0.5 we set below (binary path: GameModeScreen state-6
        // snap leaves MainScreen in STATE_CAMERA_FADE then GameOverScreen
        // ramps timer toward +1).
        if (game.mainScreen) game.mainScreen->SetState(STATE_CAMERA_FADE);
        game.m_TransitionTimer = 0.5f;  // mid-transition; not the fast-path
        // ctor args: (modeName, initialState, initialTimer,
        //             expressionIdx, bgPatternIdx, pomCount, starCount).
        // initialState=6 + initialTimer=0.0 puts us straight into the
        // main display state; runFrames below ticks score-submission.
        GameOverScreen* s = new GameOverScreen("classic", 6, 0.0f, 1, 1, 0, 0);
        game.pGameOverScreen = s;
        game.hud->AddControl(s);
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
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
            HUDControl* c = *it;
            // Keep MainScreen + gameplay-HUD controls (ScoreControl /
            // MissControl); hide the menu/screen controls.
            if (dynamic_cast<DojoScreen*>(c)
             || dynamic_cast<AboutScreen*>(c)
             || dynamic_cast<ShopScreen*>(c)
             || dynamic_cast<GameModeScreen*>(c)) {
                c->m_bActive = 0;
            }
        }
        // Real game has MainScreen in STATE_CAMERA_FADE during gameplay;
        // that state only writes game.m_TransitionTimer when it's < 0
        // (gate matches binary @ 0x0014c19a). Initial state in test is
        // STATE_CAMERA_ZOOM which lerps timer toward -1 every frame --
        // would fight GameOverScreen's ramp toward +1.
        if (game.mainScreen) game.mainScreen->SetState(STATE_CAMERA_FADE);
        game.gameMode = 0;       // Classic
        game.currentScore = 1234;
        game.m_TransitionTimer = 0.0f;
        // Seed a saved highscore so the layer-0x80 highscore-text block runs
        if (game.pSaveData) game.pSaveData->m_highscore = 5000;

        // Start at state 0 (entry animation) -- this is the realistic path
        // from gameplay -> game over.
        GameOverScreen* s = new GameOverScreen("classic", 0, 0.0f, 1, 1, 0, 0);
        game.pGameOverScreen = s;
        game.hud->AddControl(s);

        // 1.9s state-0 + ~1s state-6 alpha ramp = ~180 frames at 60fps.
        game.runFrames(180);
        // Extra idle frames so retry/quit creation (after m_ProgressCounter == 10)
        // and slide-in animations fully settle.
        game.runFrames(60);

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
        if (game.m_TransitionTimer < 0.99f) {
            fprintf(stderr,
                "FAIL: m_TransitionTimer should reach ~1.0, got %f\n",
                game.m_TransitionTimer);
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
            // 4a. GetFact returned a valid string (Fruit::GetFact clamp fix).
            if (!s->m_pFruitFact->m_pCurFactString) {
                fprintf(stderr,
                    "FAIL: FruitFactControl::m_pCurFactString should be non-null\n");
                failures++;
            }

            // 4b. FruitFactControl pos animated in from off-screen.
            //     Final pos.x = m_OffsetPosX + 183.0 = -18 + 183 = 165.
            if (s->m_pFruitFact->pos.x > 200.0f) {
                fprintf(stderr,
                    "FAIL: FruitFactControl should slide in (pos.x = %f, expected ~165)\n",
                    s->m_pFruitFact->pos.x);
                failures++;
            }
        }

        // 5. m_OffsetPosX should have animated from 368 -> ~-18.
        if (s->m_OffsetPosX > 50.0f) {
            fprintf(stderr,
                "FAIL: m_OffsetPosX should animate to ~-18, got %f\n",
                s->m_OffsetPosX);
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

        // 7. Retry/Quit buttons should spawn after m_ProgressCounter == 10.
        if (!s->m_pRetryBtn) {
            fprintf(stderr, "FAIL: m_pRetryBtn should be spawned in state 6\n");
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
        for (auto it = game.hud->controls.begin();
             it != game.hud->controls.end(); ++it) {
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
            game.shutdown();
            SDL_GL_DeleteContext(gl);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        fprintf(stdout, "PASS: gameover-transition all assertions ok "
                        "(state=%d, alpha=%f, fact=%s, retry=%s, quit=%s)\n",
                s->m_State, game.m_TransitionTimer,
                (s->m_pFruitFact && s->m_pFruitFact->m_pCurFactString) ? "ok" : "MISSING",
                s->m_pRetryBtn ? "ok" : "MISSING",
                s->m_pQuitBtn ? "ok" : "MISSING");
        game.shutdown();
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } else {
        fprintf(stderr, "unknown screen '%s'\n", screenName);
        return FailUsage();
    }

    bool screenshot = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) screenshot = true;
    }

    if (interactive) {
        // Interactive: hand off to the normal main loop. ESC / window
        // close exits. No automatic timeout.
        game.run();
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
        game.runFrames(180);
        game.runFrames(30);

        // --screenshot: dump the framebuffer to a PPM next to the exe so
        // remote testers can inspect what the screen rendered without a
        // visible window.
        if (screenshot) {
            int ww = 0, wh = 0;
            SDL_GL_GetDrawableSize(window, &ww, &wh);
            unsigned char* px = (unsigned char*)malloc((size_t)ww * wh * 3);
            if (!g_glReadPixels)
                g_glReadPixels = (PFN_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
            if (px && g_glReadPixels) {
                g_glReadPixels(0, 0, ww, wh, GL_RGB, GL_UNSIGNED_BYTE, px);
                char path[256];
                snprintf(path, sizeof(path), "screen_%s.ppm", screenName);
                FILE* f = fopen(path, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", ww, wh);
                    // glReadPixels gives bottom-up; flip to top-down so
                    // viewers (most expect P6 top-down) show correctly.
                    for (int y = wh - 1; y >= 0; y--) {
                        fwrite(px + (size_t)y * ww * 3, 1, (size_t)ww * 3, f);
                    }
                    fclose(f);
                    fprintf(stdout, "wrote %s (%dx%d)\n", path, ww, wh);
                }
                free(px);
            }
        }
    }

    game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!interactive) {
        fprintf(stdout,
            "PASS: screen '%s' transition + 30 idle frames clean\n",
            screenName);
    }
    return 0;
}
