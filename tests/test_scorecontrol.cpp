// test_scorecontrol -- standalone screenshot test for ScoreControl (in-game HUD score widget).
//
// Usage:
//   test_scorecontrol [--screenshot|--interactive|--headless]
//
// Renders ScoreControl in component-isolation mode on a clean background.
// Output (--screenshot): tmp/test/screenshots/scorecontrol/default.png
//
// Isolation approach: InitComponent() (boot + 120 burn-in + HUD cleared),
// then a ScoreControl is constructed and added as the sole HUD control.
// game_work fields are seeded directly -- no game-loop state machine.
//
// State seeded:
//   - currentScore  = 266
//   - gameMode      = CLASSIC (0)
//   - m_GameDt      = 1.0f   (fully faded / settled -- causes SCORE wordmark + live banner path)
//   - m_SaveData    = local FruitSaveData with highscore 200 and newBestThisGame=1
//     (currentScore 266 > highscore 200 -> NEW BEST path, S6 popup fires)
//   - m_bDirty=1 (already set by ctor), so first Update snaps m_ScoreSmoothed to 266
//
// Renders S1 (score.tex wordmark), S2 (animated score number), S4 (BEST: banner),
// and S6 (NEW BEST! IngamePopup popup via m_BannerScaleTime=1.0 seeded after Skip()).

#include "test_harness.h"
#include "hud/ScoreControl.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/FruitSaveData.h"
#include "hud/HUDLayer.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "scorecontrol/default");
    // 120 burn-in: lets fonts/textures load and IngamePopup build (BuildAllPopups is
    // called from PreloadRings during GameInitialise, after the 120-frame burn-in start).
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Seed game_work state so ScoreControl renders all visible sub-elements.
    // Classic mode with a score that beats the saved highscore.
    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 266;
    // m_GameDt=1.0f: fully faded in (transTimer > 0 path in Update/PreDraw).
    // Section D score.tex wordmark is guarded by transTimer > 0.
    game_work.m_GameDt     = 1.0f;
    // bM_bPaused=0 so the highscore tracking branch fires each Update.
    game_work.bM_bPaused   = 0;

    // FruitSaveData with a highscore below currentScore so NEW BEST fires.
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 200; // 266 > 200 -> new best
    saveData.newBestThisGame = 1;   // triggers S6 NEW BEST! IngamePopup in PreDraw
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    // Construct ScoreControl. Ctor loads score.tex, new_best_score.tex, hud_fruit.tex,
    // and creates m_pScoreBox (SCORE wordmark baked string box).
    ScoreControl* sc = new ScoreControl();

    // Layer 0x01 (HUD_LAYER_DEFAULT) -- the steady-state layer for m_PlayerIdx=0
    // (set by Reset() -> m_LayerFlags = 1 << m_PlayerIdx = 1).
    // RunComponentHeadless uses layerMask=0x7FFFFFFF so all layers draw.
    sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;

    // Call Skip() to force m_BannerScaleTime=1.0f (NEW BEST path from m_SaveData).
    // Skip() is the binary's end-of-game fast-path that bypasses the gradual banner
    // animation ramp and immediately sets the scale to 1.0f.
    sc->Skip();

    // Add as the only control in the isolated HUD.
    game_work.mHud->AddControl(sc);

    if (h.IsInteractive()) {
        // Interactive: run indefinitely so the tester can watch the score animate.
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        game_work.m_SaveData = prevSaveData;
        return h.Shutdown();
    }

    // Headless: drive 60 frames to let Update animate score-easing, pulse decay,
    // banner animation, and digit-alpha cascade to their settled states.
    // Per-frame: keep m_GameDt=1.0f (settles the SCORE wordmark / banner path)
    //            and reset m_bPendingRemoval so the control isn't removed
    //            (ScoreControl removes itself only for P2 in non-multiplayer; P1 is safe).
    for (int i = 0; i < 60; ++i) {
        game_work.m_GameDt = 1.0f;
        // Keep currentScore constant so the smooth-lerp settles to 266.
        game_work.currentScore = 266;
        h.RunComponentHeadless(1, 0x7FFFFFFF);
    }

    std::printf("[scorecontrol/default] stable state reached"
                " (displayedScore=%d, bannerScale=%.2f, highscore=%d)\n",
                sc->m_DisplayedScore,
                sc->m_BannerScaleTime,
                saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC]);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) {
            game_work.m_SaveData = prevSaveData;
            return 3;
        }
    }

    game_work.m_SaveData = prevSaveData;
    std::printf("PASS: scorecontrol/default screenshot complete\n");
    return h.Shutdown();
}
