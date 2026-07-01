// test_arcade_spawn -- Regression guard for arcade mode sustained spawning.
//
// Drives the REAL arcade game-start path (m_PauseAmount=0.0f + Reset(true)) and
// asserts that fruit spawns are sustained over time, not just the first wave.
//
// This test specifically exercises the scenario the user reported: "spawns 1
// kiwifruit then NO more fruit" in arcade mode. The existing gameplay_arcade
// test only checks >=1 spawn (passes with exactly 1) so it does NOT catch this.
//
// The test is designed to match the game's STATE_GAME_START path:
//   - game_work.gameMode = ARCADE
//   - game_work.m_PauseAmount = 0.0f  (camera-fade settled)
//   - WaveManager::Reset(true)   (fullReset = NewGame = PUM::Reset(true))
//   - bM_bPaused = 0, bM_Mode = false  (gameplay active)
//
// Setup notes:
//   - game_work.gameMode must be re-asserted each frame: the main screen's
//     state machine (MainScreen case-0 settle branch) resets it to 0 each
//     RunHeadless tick.
//   - Menu fruits from the 120-frame burn-in are killed with ClearUnspawned(true)
//     and swept via ActorManager::Update(0) directly (avoiding RunHeadless which
//     would clobber gameMode before Reset is called).
//
// Verifies: >= 3 wave transitions in 600 frames (10 simulated seconds).
//
// Run:
//   cd build/host && ctest --output-on-failure -R arcade_spawn_real
//   ./build/host/tests/Debug/test_arcade_spawn.exe

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/StartupEffects.h"
#include "game/PowerUpManager.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include <cstring>
#include <cstdio>

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "arcade_spawn");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    WaveManager* wm = WaveManager::GetInstance();
    int waveCountAtMode = (int)wm->m_WaveInfo[Mortar::GAME_MODE_ARCADE].size();
    if (waveCountAtMode < 1) {
        fprintf(stderr, "FAIL: no arcade waves loaded\n");
        return 1;
    }
    printf("[arcade_spawn] arcade wave count: %d\n", waveCountAtMode);

    // Set up arcade mode matching the game's STATE_GAME_START path.
    // bM_Mode=false: gameplay-active gate (GameUpdate canUpdate=true path)
    // so WaveManager::Update(dt) is called with real dt, not frozen 0.0f.
    game_work.gameMode   = (uint8_t)Mortar::GAME_MODE_ARCADE;
    game_work.m_PauseAmount   = 0.0f;
    game_work.bM_bPaused = 0;
    game_work.bM_Mode    = false;

    // Reset(true) mirrors STATE_GAME_START: full wave state reset + NewGame().
    WaveManager::GetInstance()->Reset(true);

    // Kill any leftover menu fruits from the 120-frame burn-in. Use clearAll=true
    // to kill ALL fruits (including active ones) without miss penalty, then sweep
    // via ActorManager::Update directly. We avoid h.RunHeadless() here because
    // GameTaskUpdate clobbers game_work.gameMode (MainScreen case-0 sets it to 0),
    // which would cause GetNextWave to prime a classic wave (wfe=1) instead of
    // arcade (wfe=0), stalling IsWaveProcessing on entities that never die.
    Fruit::ClearUnspawned(true);
    {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if (am) am->Update(0.0f);   // sweep ENT_KILLED entities into free pool
    }

    if (!wm->m_pCurrentWave[0]) {
        fprintf(stderr, "FAIL: m_pCurrentWave[0] null after Reset(true)\n");
        return 1;
    }
    printf("[arcade_spawn] Reset(true) primed wave=%p\n",
           (void*)wm->m_pCurrentWave[0]);

    // Count WAVE TRANSITIONS over 600 frames (~10s). A sustained arcade session
    // must cycle through multiple waves; if spawning stalls after the first wave,
    // waveTransitions stays at 0 or 1.
    //
    // We use wave-pointer changes rather than raw spawn counts because:
    // - Raw entity counts are confounded by existing menu fruits from burn-in.
    // - Wave transitions are a binary indicator that GetNextWave() fired.
    // The test PASSES if >= 3 wave transitions occur (implying >=3 wave cycles
    // fired, i.e., the spawn pump was not permanently stalled).
    int waveTransitions = 0;
    WAVE_INFO* prevWave = wm->m_pCurrentWave[0];

    for (int frame = 0; frame < 600; ++frame) {
        // Re-assert arcade mode each frame: the main screen state machine
        // (running via GameTaskUpdate inside RunHeadless) resets gameMode to 0
        // in its settle branch. Without this re-assertion, WaveManager::UpdateWave
        // sees gameMode=0 and uses classic waves (wfe=1, entity stall).
        game_work.gameMode   = (uint8_t)Mortar::GAME_MODE_ARCADE;
        game_work.bM_Mode    = false;
        game_work.bM_bPaused = 0;
        game_work.m_PauseAmount   = 0.0f;

        h.RunHeadless(1);

        if (wm->m_pCurrentWave[0] != prevWave) {
            ++waveTransitions;
            prevWave = wm->m_pCurrentWave[0];
        }
    }

    printf("[arcade_spawn] 600 frames done: waveTransitions=%d\n", waveTransitions);

    // Harden: if the spawn pump stalls after 1 fruit, waveTransitions stays
    // at 0 or 1. We require >= 3 to prove sustained multi-wave cycling.
    if (waveTransitions < 3) {
        fprintf(stderr,
            "FAIL: arcade_spawn_real: only %d wave transitions in 600 frames "
            "(expected >= 3).\n"
            "  -> BUG: spawn pump stalled; wave system not cycling past first wave.\n",
            waveTransitions);
        return 1;
    }

    if (h.IsScreenshot()) h.Screenshot();

    printf("PASS: arcade_spawn_real -- %d wave transitions in 600 frames\n",
           waveTransitions);
    return h.Shutdown();
}
