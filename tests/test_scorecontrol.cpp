// test_scorecontrol -- standalone screenshot test for ScoreControl (in-game HUD score widget).
//
// Usage:
//   test_scorecontrol [--screenshot|--interactive|--headless]
//
// Renders ScoreControl in four cases and captures each to a PNG:
//   scorecontrol/default    -- m_PauseAmount=1.0f (fully faded/game-over state)
//   scorecontrol/active     -- m_PauseAmount=0.0f (active gameplay, score visible, no wordmark)
//   scorecontrol/suppressed -- m_PauseAmount=-1.0f (camera pulled back, score fully suppressed)
//   scorecontrol/arcade_x2  -- Arcade mode, GetScoreGainMultiplier()==2 -> "x2" badge
//                              (ScoreControl.cpp:548-558, PreDraw @0x001ace80)
//
// Isolation approach: InitComponent() (boot + 120 burn-in + HUD cleared),
// then a ScoreControl is constructed and added as the sole HUD control.
// game_work fields are seeded directly -- no game-loop state machine.
//
// State seeded:
//   - currentScore  = 266
//   - gameMode      = CLASSIC (0)
//   - m_SaveData    = local FruitSaveData with highscore 200 and newBestThisGame=1
//     (currentScore 266 > highscore 200 -> NEW BEST path, S6 popup fires)
//   - m_bDirty=1 (already set by ctor), so first Update snaps m_ScoreSmoothed to 266
//
// "default" (m_PauseAmount=1.0f): renders S1 (score.tex wordmark), S2 (animated score number),
//   S4 (BEST: banner), and S6 (NEW BEST! IngamePopup popup via m_BannerScaleTime=1.0 seeded
//   after Skip()).
//
// "active" (m_PauseAmount=0.0f): renders S2 (score number) + watermelon icon only.
//   The "SCORE" wordmark (Section D) is gated off (requires transTimer > 0.0).
//   Binary draw gate: Draw() passes (m_PauseAmount >= -1.0), PreDraw Section A fires.
//
// "suppressed" (m_PauseAmount=-1.0f): Draw() passes (m_PauseAmount == -1.0, not < -1.0),
//   PreDraw Section A fires (transTimer >= -1.0), but pos.x slides to -418 (fully
//   off-screen). Section D and E are gated off. Effectively a blank frame.
//
// "arcade_x2" (gameMode=ARCADE, m_PauseAmount=0.0f, GetScoreGainMultiplier()==2):
//   renders S2 (score number) + the Arcade-only "x2" badge in pFontBlue2 (blue
//   numbers), anchored at (pos.x-18, pos.y-52) = (-236, 86) -- left of and just
//   above the running score. Previously untested: only Classic-mode cases ran,
//   so this gate (gameMode==GAME_MODE_ARCADE, ScoreControl.cpp:548) never fired.

#include "test_harness.h"
#include "hud/ScoreControl.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/FruitSaveData.h"
#include "game/PowerUpManager.h"
#include "hud/HUDLayer.h"
#include <cstdio>

// Helper: print position fields for diagnostic logging.
static void LogScoreControlPos(const char* caseName, ScoreControl* sc) {
    std::printf("[scorecontrol/%s] pos=(%.2f, %.2f, %.2f)"
                " m_DrawPosX=%.2f m_DrawPosY=%.2f m_DrawPosZ=%.2f"
                " displayedScore=%d bannerScale=%.2f\n",
                caseName,
                sc->pos.x, sc->pos.y, sc->pos.z,
                sc->m_DrawPosX, sc->m_DrawPosY, sc->m_DrawPosZ,
                sc->m_DisplayedScore,
                sc->m_BannerScaleTime);
}

int main(int argc, char* argv[]) {
    // The harness label is used for the window title. Screenshots are written
    // per-case with explicit nameOverride strings.
    fn::TestHarness h(argc, argv, "scorecontrol/default");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Shared save data: highscore below currentScore so NEW BEST fires.
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 200; // 266 > 200 -> new best
    saveData.newBestThisGame = 1;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    // -------------------------------------------------------------------------
    // Case 1: "default" -- m_PauseAmount=1.0f (fully faded, game-over state)
    // Same setup as the original single-case test.
    // -------------------------------------------------------------------------
    {
        game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
        game_work.currentScore = 266;
        game_work.m_PauseAmount     = 1.0f;
        game_work.bM_bPaused   = 0;

        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        // Skip() forces m_BannerScaleTime=1.0f (NEW BEST path from m_SaveData).
        sc->Skip();
        game_work.mHud->AddControl(sc);

        for (int i = 0; i < 60; ++i) {
            game_work.m_PauseAmount     = 1.0f;
            game_work.currentScore = 266;
            h.RunComponentHeadless(1, 0x7FFFFFFF);
        }

        LogScoreControlPos("default", sc);

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("scorecontrol/default")) {
                game_work.m_SaveData = prevSaveData;
                return 3;
            }
        }

        // Remove sc from the HUD before the next case. Mark no-destructor so
        // HUD::Release doesn't double-free -- we let the next Reset re-add.
        sc->m_bPendingRemoval = 1;
        // Drive one frame to let HUD process the pending removal.
        h.RunComponentHeadless(1, 0x7FFFFFFF);
        std::printf("PASS: scorecontrol/default complete\n");
    }

    // -------------------------------------------------------------------------
    // Case 2: "active" -- m_PauseAmount=0.0f (active gameplay)
    // Score number + watermelon icon render; SCORE wordmark (Section D) is OFF.
    // Expected: m_DrawPosX = SCORE_BASE_POS_X + 24 = -218 + 24 = -194
    // -------------------------------------------------------------------------
    {
        game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
        game_work.currentScore = 266;
        game_work.m_PauseAmount     = 0.0f;
        game_work.bM_bPaused   = 0;

        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        // Do NOT call Skip() -- at m_PauseAmount=0.0 the banner is already gated off
        // (wantBanner requires waveTimer > SCORE_BANNER_TIMER_THRESH ~= 1.0).
        // m_BannerScaleTime starts at -2.0 from ctor, decays further, stays hidden.
        game_work.mHud->AddControl(sc);

        for (int i = 0; i < 60; ++i) {
            game_work.m_PauseAmount     = 0.0f;
            game_work.currentScore = 266;
            h.RunComponentHeadless(1, 0x7FFFFFFF);
        }

        LogScoreControlPos("active", sc);

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("scorecontrol/active")) {
                game_work.m_SaveData = prevSaveData;
                return 3;
            }
        }

        sc->m_bPendingRemoval = 1;
        h.RunComponentHeadless(1, 0x7FFFFFFF);
        std::printf("PASS: scorecontrol/active complete\n");
    }

    // -------------------------------------------------------------------------
    // Case 3: "suppressed" -- m_PauseAmount=-1.0f (camera pulled back / menu state)
    // Draw() gate: m_PauseAmount < -1.0f returns early -- at exactly -1.0f it does
    // NOT return early (strict less-than). However pos.x slides to
    //   SCORE_BASE_POS_X - SCORE_MP_X_STRIDE * abs(-1.0) = -218 - 200 = -418
    // which is fully off-screen (screen half-width = 240 in Y, 160 in X).
    // Section D (score.tex) is gated off (transTimer > 0 fails at -1.0).
    // Section E (banner) is also gated off (m_BannerScaleTime stays <= 0).
    // Expected: a blank/empty frame.
    // -------------------------------------------------------------------------
    {
        game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
        game_work.currentScore = 266;
        game_work.m_PauseAmount     = -1.0f;
        game_work.bM_bPaused   = 0;

        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        game_work.mHud->AddControl(sc);

        for (int i = 0; i < 60; ++i) {
            game_work.m_PauseAmount     = -1.0f;
            game_work.currentScore = 266;
            h.RunComponentHeadless(1, 0x7FFFFFFF);
        }

        LogScoreControlPos("suppressed", sc);

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("scorecontrol/suppressed")) {
                game_work.m_SaveData = prevSaveData;
                return 3;
            }
        }

        sc->m_bPendingRemoval = 1;
        h.RunComponentHeadless(1, 0x7FFFFFFF);
        std::printf("PASS: scorecontrol/suppressed complete\n");
    }

    // -------------------------------------------------------------------------
    // Case 4: "arcade_x2" -- Arcade mode, GetScoreGainMultiplier()==2
    // Exercises the Arcade-gated "x%d" badge block (ScoreControl.cpp:548-558,
    // PreDraw @0x001ace80), previously untested (badge is drawn only when
    // gameMode == GAME_MODE_ARCADE). Anchor: (pos.x - 18, pos.y - 52) with
    // pos = (-218, 138) -> badge center at roughly (-236, 86), left of and
    // just above the running score.
    // -------------------------------------------------------------------------
    {
        game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_ARCADE;
        game_work.currentScore = 266;
        game_work.m_PauseAmount     = 0.0f;
        game_work.bM_bPaused   = 0;

        // Force GetScoreGainMultiplier() == 2 ("x2" badge). SetDefaults (run
        // during boot) leaves m_ScoreGainMult == m_ScoreGainFactor == 1, so a
        // single *= 2 gives the multiplier == 2.
        PowerUpManager::GetInstance()->AddToScoreGainMultiply(2);

        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        game_work.mHud->AddControl(sc);

        for (int i = 0; i < 60; ++i) {
            game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_ARCADE;
            game_work.currentScore = 266;
            game_work.m_PauseAmount     = 0.0f;
            PowerUpManager::GetInstance()->m_ScoreGainMult = 2;
            h.RunComponentHeadless(1, 0x7FFFFFFF);
        }

        LogScoreControlPos("arcade_x2", sc);
        std::printf("[scorecontrol/arcade_x2] GetScoreGainMultiplier()=%d\n",
                    PowerUpManager::GetInstance()->GetScoreGainMultiplier());

        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("scorecontrol/arcade_x2")) {
                game_work.m_SaveData = prevSaveData;
                return 3;
            }
        }

        sc->m_bPendingRemoval = 1;
        h.RunComponentHeadless(1, 0x7FFFFFFF);
        std::printf("PASS: scorecontrol/arcade_x2 complete\n");
    }

    game_work.m_SaveData = prevSaveData;
    return h.Shutdown();
}
