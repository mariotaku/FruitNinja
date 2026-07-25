// test_versus_hud_render.cpp -- isolated ZenVersusControl render test.
//
// Visually verifies the revived online-VERSUS HUD (DIFFERS: Bada v1.6.1
// stripped, revived from iOS 1.6.1 ZenVersusControl @0x000882c4): the
// balance-slider fill tint/position, per-player score readouts (pFontNumbers,
// pulse-scaled), and player-name labels (pFontMain, blue P0 / violet P1).
//
// Usage: test_versus_hud_render [--screenshot] [--interactive]
//
// Default (no flags): headless assertions (state/texture wiring checks).
// Passes via ctest -E screenshot.
// --screenshot: renders 3 states and writes:
//   tmp/test/screenshots/versus_hud/p1_leading.png -- scores (150,90), slider biased P0/blue
//   tmp/test/screenshots/versus_hud/p2_leading.png -- scores (60,200), slider biased P1/violet
//   tmp/test/screenshots/versus_hud/tied.png        -- scores (100,100), slider centred
//
// Each state sets scores via SetScoresForTest (game_work.currentScore +
// NetworkManager opponent score) and player names via GameWork::SetPlayerName,
// settles 30 Update(1/60) frames so the eased score/pulse/intro/wobble
// animation state converges, then renders one HUD::Draw(all layers) pass --
// HUD::Draw's PreDrawOrder/DrawOrder dispatch drives ZenVersusControl exactly
// as GameDraw would (see HUD::Draw @0x0018bfc4 in hud/HUD.cpp).

#include "test_harness.h"
#include "hud/ZenVersusControl.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "game/GameWork.h"
#include "engine/network/NetworkManager.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL.h>

// Minimum non-black pixels for the screenshot "something drew" check.
static const int MIN_DRAWN_PIXELS = 200;

static int CountNonBlack(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if ((int)px[0] + (int)px[1] + (int)px[2] > 30) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// FramePass -- one isolated-HUD frame: clear, ortho, HUD Update + BeginDraw +
// Draw(all layers). Does NOT SwapWindow; caller swaps (so ScreenshotPng can
// read the back buffer between render and swap). Mirrors
// test_menubutton_render.cpp's FramePass shape.
// ---------------------------------------------------------------------------
static void FramePass(fn::TestHarness& h) {
    static const float kDt = 1.0f / 60.0f;

    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    if (game_work.mHud) {
        game_work.mHud->Update(kDt);

        // Mirror GameDraw's scale reset so quads aren't tinted by stale
        // ScreenEffect scales from earlier frames.
        game_work.mHud->scales[0] = 1.0f;
        game_work.mHud->scales[1] = 1.0f;
        game_work.mHud->scales[2] = 1.0f;

        game_work.mHud->BeginDraw(kDt);
        game_work.mHud->Draw(0x7FFFFFFF);
    }
}

// Sets scores/names for the given control (SetScoresForTest snaps the eased
// score state to the exact target -- see ZenVersusControl.h), then drives n
// full-HUD FramePass frames so the non-score animation state (m_IntroTimer,
// m_WobbleAngle) settles. Each frame is swapped (visible in --interactive).
static void SettleState(fn::TestHarness& h, ZenVersusControl* ctrl,
                        int p0Score, int p1Score,
                        const char* p0Name, const char* p1Name, int frames) {
    game_work.SetPlayerName(0, p0Name);
    game_work.SetPlayerName(1, p1Name);
    // Port-only test helper (ZenVersusControl.h) -- sets game_work.currentScore
    // (P0) and NetworkManager's opponent-score slot (P1).
    ctrl->SetScoresForTest(p0Score, p1Score);

    for (int i = 0; i < frames; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) break;
        }
        FramePass(h);
        SDL_GL_SwapWindow(h.window);
    }
}

int main(int argc, char* argv[]) {
    // Port specific: standalone ZenVersusControl state render test.

    fn::TestHarness h(argc, argv, "versus_hud/p1_leading");
    // 60 burn-in frames: warms fonts (pFontMain, pFontNumbers) and textures
    // during game.init() before InitComponent() strips the HUD.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation: clears the HUD so only our control renders.
    if (!h.InitComponent()) return 1;

    int failures = 0;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after init\n");
        return 1;
    }

    // --- Build the control under test (HUD takes ownership on AddControl) ---
    // ZenVersusControl::LoadContent() is idempotent and also called by the
    // ctor; calling it explicitly here documents the real construction order
    // (LoadContent then construct) per the class's header contract.
    ZenVersusControl::LoadContent();
    ZenVersusControl* ctrl = new ZenVersusControl();
    game_work.mHud->AddControl(ctrl);

    // --- Preconditions: shared assets the control depends on ---
    if (!game_work.pFontNumbers.IsValid()) {
        std::fprintf(stderr, "FAIL: game_work.pFontNumbers not loaded\n");
        ++failures;
    }
    if (!game_work.pFontMain.IsValid()) {
        std::fprintf(stderr, "FAIL: game_work.pFontMain not loaded\n");
        ++failures;
    }
    if (failures > 0) {
        h.Shutdown();
        return 1;
    }

    // --- Headless assertions (state wiring) ---
    // SetScoresForTest snaps the eased state to the exact target (see
    // ZenVersusControl.h) -- a render test wants the exact set scores shown,
    // not a mid-ease asymptotic value, so no settle loop is needed here.
    ctrl->SetScoresForTest(150, 90);
    if (ctrl->m_ScoreInt[0] != 150 || ctrl->m_ScoreInt[1] != 90) {
        std::fprintf(stderr, "FAIL: snapped score != exact target: P0=%d P1=%d (want 150/90)\n",
                     ctrl->m_ScoreInt[0], ctrl->m_ScoreInt[1]);
        ++failures;
    }
    if (ctrl->m_SliderBias >= 0.0f) {
        std::fprintf(stderr, "FAIL: m_SliderBias=%.3f should be negative (P0 leading)\n",
                     ctrl->m_SliderBias);
        ++failures;
    }
    if (std::strcmp(ctrl->m_ScoreStr0, "150") != 0 || std::strcmp(ctrl->m_ScoreStr1, "90") != 0) {
        std::fprintf(stderr, "FAIL: score strings '%s'/'%s' != '150'/'90'\n",
                     ctrl->m_ScoreStr0, ctrl->m_ScoreStr1);
        ++failures;
    }

    // --- Screenshots: 3 states, each settled then captured ---
    if (h.IsScreenshot()) {
        struct State {
            const char* name;
            int p0, p1;
            const char* n0;
            const char* n1;
        };
        static const State kStates[] = {
            { "versus_hud/p1_leading", 150, 90,  "ALICE", "BOB" },
            { "versus_hud/p2_leading", 60,  200, "ALICE", "BOB" },
            { "versus_hud/tied",       100, 100, "ALICE", "BOB" },
        };
        for (int s = 0; s < (int)(sizeof(kStates) / sizeof(kStates[0])); ++s) {
            const State& st = kStates[s];
            // Scores are exact from frame 0 (SetScoresForTest snaps them);
            // a few frames just let the intro/wobble animation settle.
            SettleState(h, ctrl, st.p0, st.p1, st.n0, st.n1, 3);

            // One more pass, captured before swap.
            FramePass(h);

            if (ctrl->m_ScoreInt[0] != st.p0 || ctrl->m_ScoreInt[1] != st.p1 ||
                std::atoi(ctrl->m_ScoreStr0) != st.p0 || std::atoi(ctrl->m_ScoreStr1) != st.p1) {
                std::fprintf(stderr, "FAIL: %s: rendered scores P0=%s P1=%s != exact %d/%d\n",
                             st.name, ctrl->m_ScoreStr0, ctrl->m_ScoreStr1, st.p0, st.p1);
                ++failures;
            }

            int fw = 0, fh = 0;
            unsigned char* pixels = h.ReadPixels(&fw, &fh);
            int drawn = pixels ? CountNonBlack(pixels, fw, fh) : 0;
            std::free(pixels);
            if (drawn < MIN_DRAWN_PIXELS) {
                std::fprintf(stderr, "FAIL: %s: only %d non-black pixels (< %d) -- nothing drew\n",
                             st.name, drawn, MIN_DRAWN_PIXELS);
                ++failures;
            } else {
                std::printf("[versus_hud_render] %s drawnPixels=%d\n", st.name, drawn);
            }

            if (!h.ScreenshotPng(st.name)) {
                std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", st.name);
                ++failures;
            }
            SDL_GL_SwapWindow(h.window);
        }
    }

    // --- Interactive mode: cycle through the 3 states, ESC or close to exit ---
    if (h.IsInteractive()) {
        std::printf("[versus_hud_render] entering interactive mode -- ESC to exit\n");
        struct State {
            const char* n0;
            const char* n1;
            int p0, p1;
        };
        static const State kStates[] = {
            { "ALICE", "BOB", 150, 90 },
            { "ALICE", "BOB", 60,  200 },
            { "ALICE", "BOB", 100, 100 },
        };
        bool running = true;
        int stateIdx = 0;
        while (running) {
            const State& st = kStates[stateIdx % 3];
            SettleState(h, ctrl, st.p0, st.p1, st.n0, st.n1, 60);
            if (!h.window) break;

            for (int hold = 0; hold < 90 && running; ++hold) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { running = false; break; }
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                }
                if (!running) break;
                FramePass(h);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
            ++stateIdx;
        }
        std::printf("[versus_hud_render] interactive exit\n");
    }

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: versus_hud_render OK\n");
    return h.Shutdown();
}
