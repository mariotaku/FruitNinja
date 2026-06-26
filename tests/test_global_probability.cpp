// test_global_probability -- GlobalProbabilityOveride (GPO) fire-path verification.
//
// Proves the super_pomegranate spawning pipeline from parse -> arm -> gate -> fire
// in a deterministic, end-to-end way without running a full game loop or accumulating
// score via real gameplay.
//
// Uses fn_add_game_test + TestHarness to boot the game once (needed so FruitInfo
// and WaveManager XML are loaded -- FruitType("super_pomegranate") requires the
// fruitlist.xml fruit array; globalprobabilities.xml is loaded by WaveManager::Init).
// After init the test manipulates save-data state directly and calls the GPO logic.
//
// Test cases:
//   CASE 1: Parse -- assert 1 PointBased entry loaded, TypeChances[0] == "super_pomegranate"
//   CASE 2: Arm   -- NewGameStarted seeds a score threshold in [fromMin=40, fromMax=70]
//   CASE 3: Below threshold -> NO fire (score = 0 -> n > 0 -> n <= score is false)
//   CASE 4: At/above threshold -> FIRES super_pomegranate (score >= 110 > everyMax)
//   CASE 5: Re-arm -- after firing, threshold advances so next call does NOT fire immediately
//
// Run:
//   build/host/test_global_probability
//   ctest -R global_probability

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/WaveStructs.h"
#include "game/GlobalProbabilityOveride.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "screens/MainScreen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns the first GlobalProbabilityOveridePointBased in wm's list, or null.
static GlobalProbabilityOveridePointBased* FindPointBased(WaveManager* wm) {
    for (size_t i = 0; i < wm->m_GlobalProbabilityOverride.size(); ++i) {
        GlobalProbabilityOveride* g = wm->m_GlobalProbabilityOverride[i];
        if (!g) continue;
        GlobalProbabilityOveridePointBased* pb =
            dynamic_cast<GlobalProbabilityOveridePointBased*>(g);
        if (pb) return pb;
    }
    return 0;
}

// Set the raw GPO score threshold directly (bypasses NewGameStarted randomness
// so CASE 3 and CASE 4 are 100% deterministic regardless of RNG state).
// Uses FruitSaveData::SetTotal to write save_key -> value.
static void SetThreshold(FruitSaveData* sd, GlobalProbabilityOveridePointBased* pb, int value) {
    sd->SetTotal(pb->m_SaveKey, value, false, false);
}

// Returns the current stored threshold for pb.
static int GetThreshold(FruitSaveData* sd, GlobalProbabilityOveridePointBased* pb) {
    return sd->GetTotal(pb->m_SaveSubId);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "global_probability");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    int failures = 0;

    // ------------------------------------------------------------------
    // SETUP: locate WaveManager singleton and FruitSaveData.
    // ------------------------------------------------------------------

    WaveManager* wm = WaveManager::GetInstance();
    if (!wm) {
        std::fprintf(stderr, "FAIL: WaveManager::GetInstance() null\n");
        return 1;
    }

    if (!game_work.m_SaveData) {
        std::fprintf(stderr, "FAIL: game_work.m_SaveData null after init\n");
        return 1;
    }
    FruitSaveData* sd = game_work.m_SaveData;

    // ------------------------------------------------------------------
    // CASE 1: Parse -- assert exactly 1 PointBased entry parsed, name == "super_pomegranate"
    // ------------------------------------------------------------------
    std::printf("\n[CASE 1] Parse\n");

    int pbCount = 0;
    for (size_t i = 0; i < wm->m_GlobalProbabilityOverride.size(); ++i) {
        GlobalProbabilityOveride* g = wm->m_GlobalProbabilityOverride[i];
        if (g && dynamic_cast<GlobalProbabilityOveridePointBased*>(g)) ++pbCount;
    }

    // globalprobabilities.xml probabilityFile0 has exactly one
    // <globalProbabilityPointBased> for Classic mode.
    std::printf("  PointBased entries loaded: %d (expected 1)\n", pbCount);
    if (pbCount != 1) {
        std::fprintf(stderr, "  FAIL: expected 1 PointBased entry, got %d\n", pbCount);
        ++failures;
    }

    GlobalProbabilityOveridePointBased* pb = FindPointBased(wm);
    if (!pb) {
        std::fprintf(stderr, "  FAIL: no PointBased entry found\n");
        return 1;  // Can't continue without pb
    }

    std::printf("  pb->m_SaveKey = '%s'\n", pb->m_SaveKey ? pb->m_SaveKey : "(null)");
    std::printf("  pb->TypeChances.size() = %d\n", (int)pb->m_TypeChances.size());

    if (pb->m_TypeChances.empty()) {
        std::fprintf(stderr, "  FAIL: m_TypeChances is empty\n");
        ++failures;
    } else {
        const char* typeName = pb->m_TypeChances[0].m_TypeName.c_str();
        std::printf("  pb->m_TypeChances[0].m_TypeName = '%s' (expected 'super_pomegranate')\n",
                    typeName);
        if (std::strcmp(typeName, "super_pomegranate") != 0) {
            std::fprintf(stderr, "  FAIL: expected 'super_pomegranate', got '%s'\n", typeName);
            ++failures;
        }
    }

    // Verify XML parse fields.
    std::printf("  pb->m_Every=%d m_EveryMax=%d m_From=%d m_FromMax=%d\n",
                pb->m_Every, pb->m_EveryMax, pb->m_From, pb->m_FromMax);
    // XML: everyMin="70" everyMax="110" fromMin="40" fromMax="70"
    if (pb->m_Every != 70 || pb->m_EveryMax != 110 || pb->m_From != 40 || pb->m_FromMax != 70) {
        std::fprintf(stderr,
            "  FAIL: parsed fields mismatch. Expected every=70 everyMax=110 from=40 fromMax=70\n");
        ++failures;
    }

    // mode mask: XML mode="CLASSIC" -> bit 0 set only.
    std::printf("  pb->m_ModeMask=0x%x (expected 0x1 for CLASSIC)\n", pb->m_ModeMask);
    if (pb->m_ModeMask != 0x1u) {
        std::fprintf(stderr, "  FAIL: m_ModeMask expected 0x1, got 0x%x\n", pb->m_ModeMask);
        ++failures;
    }

    std::printf("  CASE 1 %s\n", (failures == 0) ? "PASS" : "FAIL");

    // ------------------------------------------------------------------
    // Set game mode to CLASSIC so the mode-mask gate passes.
    // ------------------------------------------------------------------
    game_work.gameMode = (uint8_t)Mortar::GAME_MODE_CLASSIC;

    // ------------------------------------------------------------------
    // CASE 2: Arm -- NewGameStarted seeds threshold in [fromMin=40, fromMax=70].
    // ------------------------------------------------------------------
    std::printf("\n[CASE 2] Arm\n");

    // Clear any stale totals from the real game session.
    sd->ClearTotals();

    // Call NewGameStarted (slot4) which sets initial score threshold via
    // T_877(m_From=40, m_FromMax=70).
    pb->NewGameStarted();

    int threshold = GetThreshold(sd, pb);
    std::printf("  threshold after NewGameStarted = %d (expected [40, 70])\n", threshold);
    if (threshold < 40 || threshold > 70) {
        std::fprintf(stderr, "  FAIL: threshold %d out of expected [40, 70]\n", threshold);
        ++failures;
    }
    std::printf("  CASE 2 %s\n", (threshold >= 40 && threshold <= 70) ? "PASS" : "FAIL");

    // ------------------------------------------------------------------
    // CASE 3: Below threshold -> NO fire
    // Set score to 0 (below any threshold in [40,70]).
    // Use a known threshold (55) so the test is fully deterministic.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 3] Below threshold -> NO fire\n");

    // Force a known threshold of 55 for determinism.
    SetThreshold(sd, pb, 55);
    game_work.currentScore = 0;

    std::printf("  score=%d threshold=%d\n",
                game_work.currentScore, GetThreshold(sd, pb));

    // Call CheckForOverride directly on pb -- should NOT fire.
    int outType = -9999;
    bool fired = pb->CheckForOverride(outType);
    std::printf("  CheckForOverride fired=%d outType=%d (expected fired=0)\n",
                fired ? 1 : 0, outType);
    if (fired) {
        std::fprintf(stderr, "  FAIL: GPO fired when score (%d) < threshold (%d) -- inverted gate?\n",
                     game_work.currentScore, 55);
        ++failures;
    }
    // threshold should still be 55 (no rearm when not firing below threshold)
    int thresholdAfter3 = GetThreshold(sd, pb);
    std::printf("  threshold after CASE 3 call = %d (should be unchanged = 55)\n", thresholdAfter3);
    if (thresholdAfter3 != 55) {
        std::fprintf(stderr, "  FAIL: threshold unexpectedly changed to %d\n", thresholdAfter3);
        ++failures;
    }
    std::printf("  CASE 3 %s\n", (!fired && thresholdAfter3 == 55) ? "PASS" : "FAIL");

    // ------------------------------------------------------------------
    // CASE 4: At/above threshold -> FIRES super_pomegranate
    // Set score to 110 (above everyMax=110, so definitely > threshold 55).
    //
    // CanSpawn() faithful path: set m_pCurrentWave[0] to a WAVE_INFO with
    // m_GamesMin==0 so CanSpawn hits the early-return "if (wave && wave->m_GamesMin==0)
    // return true" branch.  This is the authentic Classic-gameplay path (most Classic
    // waves carry m_GamesMin==0) and is independent of the wave-count gate operand values.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 4] At/above threshold -> FIRES super_pomegranate\n");

    // Restore known threshold.
    SetThreshold(sd, pb, 55);
    game_work.currentScore = 110;

    std::printf("  score=%d threshold=%d\n",
                game_work.currentScore, GetThreshold(sd, pb));

    // Resolve what FruitType("super_pomegranate") gives us.
    int expectedType = Fruit::FruitType("super_pomegranate", false);
    std::printf("  Fruit::FruitType('super_pomegranate') = %d\n", expectedType);
    if (expectedType < 0) {
        std::fprintf(stderr,
            "  WARN: FruitType('super_pomegranate') = %d (not found in fruitlist.xml)\n",
            expectedType);
    }

    // Install a fake WAVE_INFO with m_GamesMin==0 so CanSpawn() returns true
    // via the early-return path ("wave && wave->m_GamesMin==0").
    WAVE_INFO fakeWave;
    fakeWave.m_GamesMin = 0;
    WAVE_INFO* savedWave = wm->m_pCurrentWave[0];
    wm->m_pCurrentWave[0] = &fakeWave;

    std::printf("  m_pCurrentWave[0]=%p fakeWave.m_GamesMin=%d -> CanSpawn early-return true\n",
                (void*)wm->m_pCurrentWave[0], fakeWave.m_GamesMin);

    int outType4 = -9999;
    bool fired4 = pb->CheckForOverride(outType4);

    // Restore wave pointer immediately.
    wm->m_pCurrentWave[0] = savedWave;

    std::printf("  CheckForOverride fired=%d outType=%d (expected fired=1 outType=%d)\n",
                fired4 ? 1 : 0, outType4, expectedType >= 0 ? expectedType : 0);

    if (!fired4) {
        std::fprintf(stderr, "  FAIL: GPO did NOT fire when score (%d) >= threshold (55)\n",
                     game_work.currentScore);

        // Diagnosis: trace which sub-gate blocked.
        std::printf("  DIAGNOSIS:\n");
        std::printf("    game_work.m_SaveData = %p\n", (void*)game_work.m_SaveData);
        uint32_t modeBit = GetModeBitMask((GAME_MODE)game_work.gameMode);
        std::printf("    GetModeBitMask(gameMode=%d) = 0x%x, m_ModeMask=0x%x, AND=0x%x\n",
                     (int)game_work.gameMode, modeBit, pb->m_ModeMask, modeBit & pb->m_ModeMask);
        int n = sd->GetTotal(pb->m_SaveSubId);
        std::printf("    n=GetTotal(m_SaveSubId) = %d, currentScore = %d, n<=score = %s\n",
                     n, game_work.currentScore, (n <= game_work.currentScore) ? "true" : "false");
        std::printf("    CanSpawn: fakeWave.m_GamesMin=%d -> early-return should have fired\n",
                     fakeWave.m_GamesMin);

        ++failures;
    } else {
        // Verify the type written is super_pomegranate (or at least valid).
        if (expectedType >= 0 && outType4 != expectedType) {
            std::fprintf(stderr,
                "  FAIL: GPO fired but wrote type %d instead of super_pomegranate type %d\n",
                outType4, expectedType);
            ++failures;
        } else {
            std::printf("  GPO FIRED: outType=%d", outType4);
            if (expectedType >= 0) {
                const char* typeName = Fruit::FruitTypeName(outType4);
                std::printf(" (%s)", typeName ? typeName : "?");
            }
            std::printf("\n");
        }
    }
    std::printf("  CASE 4 %s\n", (fired4 && (expectedType < 0 || outType4 == expectedType)) ? "PASS" : "FAIL");

    // ------------------------------------------------------------------
    // CASE 5: Re-arm -- after firing, CheckForOverride should NOT fire again
    // immediately at the same score (threshold advanced by T_877(every=70,everyMax=110)).
    // ------------------------------------------------------------------
    std::printf("\n[CASE 5] Re-arm\n");

    if (!fired4) {
        std::printf("  SKIP: CASE 4 did not fire, re-arm cannot be tested\n");
    } else {
        // After CASE 4 fired, threshold advanced by T_877(70, 110) -> new threshold
        // is at least 55 + 70 = 125, which is > currentScore (110).
        int thresholdAfterFire = GetThreshold(sd, pb);
        std::printf("  threshold after CASE 4 fire = %d (expected >= %d = score+every_min = %d)\n",
                    thresholdAfterFire, 110 + 70, 110 + 70);

        // Immediate call at same score (110) should NOT fire.
        int outType5 = -9999;
        bool fired5 = pb->CheckForOverride(outType5);
        std::printf("  CheckForOverride again at score=110 fired=%d (expected 0 = re-armed)\n",
                    fired5 ? 1 : 0);

        if (fired5) {
            std::fprintf(stderr, "  FAIL: GPO fires on every call (no re-arm). threshold=%d\n",
                         thresholdAfterFire);
            ++failures;
        }
        // Threshold should still be the post-fire value (no decrement when not firing).
        int thresholdAfter5 = GetThreshold(sd, pb);
        std::printf("  threshold unchanged after no-fire call = %d\n", thresholdAfter5);
        if (thresholdAfter5 != thresholdAfterFire) {
            std::fprintf(stderr,
                "  FAIL: threshold changed from %d to %d during a non-fire call\n",
                thresholdAfterFire, thresholdAfter5);
            ++failures;
        }

        std::printf("  CASE 5 %s\n",
                    (!fired5 && thresholdAfter5 == thresholdAfterFire) ? "PASS" : "FAIL");
    }

    // ------------------------------------------------------------------
    // SUMMARY
    // ------------------------------------------------------------------
    std::printf("\n=== SUMMARY ===\n");
    std::printf("failures = %d\n", failures);

    if (failures == 0) {
        std::printf("test_global_probability: PASS\n");
    } else {
        std::fprintf(stderr, "test_global_probability: FAIL (%d failures)\n", failures);
    }

    return (failures == 0) ? 0 : 1;
}
