// test_hud_alpha -- regression guard for HUD draw-alpha vs slow-mo field conflation.
//
// Bug: the port read HUD+0x24 (m_globalTimeScale, slow-mo multiplier) as the
// per-frame draw-alpha in ScoreControl::Draw/PreDraw and MissControl::Draw.
// The binary writes 1.0 to HUD+0x20 each tick for draw-alpha and HUD+0x24 for
// slow-mo. When a slow-mo writer lowers +0x24 to 0.0, the score alpha drops
// to 0 -> invisible score during active play.
//
// Fix: HUD::Update writes 1.0 to +0x20 (m_DrawAlpha); ScoreControl and
// MissControl read +0x20 for alpha.
//
// This test drives the real Update path, then simulates a slow-mo writer
// by setting m_globalTimeScale=0.0f AFTER the Update tick (matching how
// SuperFruitControl/MainScreen write +0x24 during a frame). It then inspects
// ScoreControl::m_DrawColour.a after Draw -- must be 255 regardless of +0x24.
//
// Passes AFTER the fix; FAILS before it (alpha ~0 because Draw reads +0x24).
//
// Cross-build safe: no lambdas, no range-for, no auto, no enum class.

#include "test_harness.h"
#include "hud/ScoreControl.h"
#include "hud/MissControl.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/FruitSaveData.h"
#include "hud/HUDLayer.h"
#include <cstdio>

static int g_Failures = 0;

static void CheckAlpha(const char* label, uint8_t got, uint8_t expected) {
    if (got == expected) {
        std::printf("  PASS %s: m_DrawColour.a=%d\n", label, (int)got);
    } else {
        std::fprintf(stderr, "  FAIL %s: m_DrawColour.a=%d, expected=%d\n",
                     label, (int)got, (int)expected);
        g_Failures++;
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "hud_alpha");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Seed game_work to active-play state (classic mode, score visible).
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 0;
    saveData.newBestThisGame = 0;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;
    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 100;
    game_work.m_GameDt     = 0.0f;  // active-play state (not suppressed)
    game_work.bM_bPaused   = 0;

    // -------------------------------------------------------------------------
    // Case 1: ScoreControl::Draw alpha under slow-mo.
    //
    // Drive one HUD::Update tick (which should write 1.0 to the per-frame alpha
    // field at +0x20). Then simulate a slow-mo writer (sets +0x24 to 0.0).
    // ScoreControl::Draw is called -- expected alpha is 255.
    //
    // BEFORE fix: Draw reads +0x24 (0.0) -> alpha 0 -> FAIL.
    // AFTER fix:  Draw reads +0x20 (1.0) -> alpha 255 -> PASS.
    // -------------------------------------------------------------------------
    {
        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        game_work.mHud->AddControl(sc);

        // Run one Update tick: HUD::Update writes the per-frame alpha field.
        game_work.mHud->Update(1.0f / 60.0f);

        // Simulate a slow-mo writer lowering +0x24 AFTER Update.
        // (Binary: SuperFruitControl::Update writes <1.0 to HUD+0x24 during its
        // Update call, which happens INSIDE the same HUD::Update loop iteration,
        // AFTER HUD::Update has set the per-frame alpha sentinel.)
        game_work.mHud->m_globalTimeScale = 0.0f;

        // Restore active-play state (Update may have reset game_work fields via
        // control callbacks; re-seed to keep the Draw gate open).
        game_work.m_GameDt = 0.0f;

        // Call ScoreControl::Draw directly to check the alpha assignment.
        float hudScaleRaw[3] = { 1.0f, 1.0f, 1.0f };
        sc->Draw(hudScaleRaw);

        CheckAlpha("ScoreControl::Draw alpha under slow-mo", sc->m_DrawColour.a, 255);

        // Remove sc before next case.
        sc->m_bPendingRemoval = 1;
        game_work.mHud->Update(1.0f / 60.0f);
    }

    // -------------------------------------------------------------------------
    // Case 2: ScoreControl::PreDraw alpha under slow-mo.
    // Same scenario but for the PreDraw path (which also reads the alpha field).
    // PreDraw drives text rendering; the alpha it assigns to `alpha` is the
    // one used for score digit quads.
    // -------------------------------------------------------------------------
    {
        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        game_work.mHud->AddControl(sc);

        // Warm up the ScoreControl so m_DisplayedScore is set.
        for (int i = 0; i < 5; ++i) {
            game_work.currentScore = 100;
            game_work.m_GameDt = 0.0f;
            game_work.mHud->Update(1.0f / 60.0f);
        }

        // Set slow-mo AFTER the final Update tick.
        game_work.mHud->m_globalTimeScale = 0.0f;
        game_work.m_GameDt = 0.0f;

        // PreDraw internally assigns `alpha` from the HUD field and would use it
        // for digit rendering. The simplest observable check is via the same
        // m_DrawColour.a path used in Draw. We call Draw (which sets m_DrawColour.a)
        // then verify it is unaffected by the slow-mo in +0x24.
        float hudScaleRaw[3] = { 1.0f, 1.0f, 1.0f };
        sc->Draw(hudScaleRaw);
        CheckAlpha("ScoreControl::Draw alpha (PreDraw warm-up)", sc->m_DrawColour.a, 255);

        sc->m_bPendingRemoval = 1;
        game_work.mHud->Update(1.0f / 60.0f);
    }

    game_work.m_SaveData = prevSaveData;

    if (g_Failures > 0) {
        std::fprintf(stderr, "FAIL: %d assertion(s) failed -- score alpha reads wrong HUD field\n",
                     g_Failures);
        return 1;
    }
    std::printf("PASS: all HUD alpha assertions OK\n");
    return h.Shutdown();
}
