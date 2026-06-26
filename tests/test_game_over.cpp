// test_game_over -- AddToCurrentScore clamp regression guard (#222 fix).
//
// Regression: before the #222 fix, AddToCurrentScore did NOT clamp currentScore
// to 0. Applying a bomb penalty larger than the current score produced a
// negative score (e.g. currentScore=3, penalty=-10 -> -7). The fix added a
// clamp-to-zero after the delta is applied. This test pins that invariant.
//
// Test cases:
//   1. Normal gain: score 0 -> AddToCurrentScore(3,...) -> 3
//   2. Penalty clamped: score 3 -> AddToCurrentScore(-10,...) -> 0 (NOT -7)
//   3. Penalty from zero: score 0 -> AddToCurrentScore(-5,...) -> 0
//   4. Large gain: score 0 -> AddToCurrentScore(50,...) -> 50
//
// Uses TestHarness (fn_add_game_test) because AddToCurrentScore calls
// Game::GetInstance() + PowerUpManager + g_ScoreDelegate.
//
// Run via:
//   ctest --test-dir build/host -R game_over --output-on-failure
//   ./build/host/tests/Debug/test_game_over.exe

#include "test_harness.h"
#include "game/GameOver.h"
#include "game/GameWork.h"
#include "game/ScoreDelegate.h"
#include "game/PowerUpManager.h"

static int g_Failures = 0;

static void Expect(const char* label, int got, int expected) {
    if (got == expected) {
        std::printf("  PASS %s: got %d\n", label, got);
    } else {
        std::fprintf(stderr, "  FAIL %s: got %d, expected %d\n", label, got, expected);
        g_Failures++;
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "game_over");
    // 120 burn-in frames: GameInit -> splash -> game state running, HUD live,
    // PowerUpManager initialised with defaults.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    // Confirm default score multipliers are 1 so face-value asserts hold.
    // GetScoreGainMultiplier = m_ScoreGainMult(1) * m_ScoreGainFactor(1) = 1.
    // GetScoreLossMultiplier = m_ScoreLossMult(1) * m_ScoreLossFactor(1) = 1.
    // AddToCurrentScore multiplies by GetScoreGainMultiplier BEFORE calling the
    // delegate, so: delta = DefaultScoreDelegate(points * gainMult).
    // DefaultScoreDelegate(n): n>0 -> n * gainMult; n<=0 -> n * lossMult.
    // With all defaults=1: delta == points.
    {
        PowerUpManager* pum = PowerUpManager::GetInstance();
        int gainMult = pum->GetScoreGainMultiplier();
        int lossMult = pum->GetScoreLossMultiplier();
        std::printf("[game_over] PowerUp defaults: gainMult=%d lossMult=%d\n",
                    gainMult, lossMult);
        if (gainMult != 1 || lossMult != 1) {
            std::fprintf(stderr,
                "FAIL: unexpected default multipliers (gainMult=%d lossMult=%d) -- "
                "a power-up may have activated during boot; test precondition broken\n",
                gainMult, lossMult);
            return 1;
        }
    }

    // Ensure default score delegate is installed (no modifier active).
    SetDefaultScoreDelegate();

    std::printf("[game_over] --- AddToCurrentScore clamp regression guard ---\n");

    // Case 1: Normal fruit gain, score 0 -> 3.
    game_work.currentScore = 0;
    AddToCurrentScore(3, 0, false, false);
    Expect("case1: +3 from 0", game_work.currentScore, 3);

    // Case 2: Bomb penalty larger than current score -> clamped to 0, NOT -7.
    // This is the #222 regression: without the clamp, result is 3 + (-10) = -7.
    game_work.currentScore = 3;
    AddToCurrentScore(-10, 0, false, false);
    Expect("case2: -10 from 3 (clamp to 0, not -7)", game_work.currentScore, 0);

    // Case 3: Penalty from zero -> stays 0.
    game_work.currentScore = 0;
    AddToCurrentScore(-5, 0, false, false);
    Expect("case3: -5 from 0 (stays 0)", game_work.currentScore, 0);

    // Case 4: Large gain from zero.
    game_work.currentScore = 0;
    AddToCurrentScore(50, 0, false, false);
    Expect("case4: +50 from 0", game_work.currentScore, 50);

    if (g_Failures > 0) {
        std::fprintf(stderr, "FAIL: %d assertion(s) failed\n", g_Failures);
        return 1;
    }

    std::printf("PASS: all AddToCurrentScore clamp cases OK\n");
    return h.Shutdown();
}
