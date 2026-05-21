// Per-mode gameplay smoke test. Boots the game, sets game_work.gameMode and
// fires the SetupLevel pipeline, ticks ~3 seconds of frames, and verifies
// (a) the wave manager loads the expected wave count for that mode,
// (b) m_pCurrentWave[0] becomes non-null after SetupLevel,
// (c) at least one fruit spawns within the run window.
//
// Run via:
//   cd build && ctest --output-on-failure -R gameplay_
//
// Modes:
//   gameplay_classic  -> mode 0, expects classic XML waves to spawn
//   gameplay_arcade   -> mode 2, expects arcade XML waves to spawn
//   gameplay_zen      -> mode 3, expects zen XML waves to spawn
//   gameplay_combo    -> mode 1, expects combo XML waves to spawn

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/StartupEffects.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "hud/HUD.h"
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_gameplay <classic|arcade|zen|combo> [--interactive] [--screenshot]\n");
    return 1;
}

// Interactive tick callback: just keeps ticking. Returns true always so the
// loop runs until ESC / window close.
static bool GameplayTick(Game& /*game*/, int /*frame*/, void* /*userdata*/) {
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* modeName = argv[1];

    // argv[1] must be the mode name, not a flag.
    if (modeName[0] == '-') return FailUsage();

    int gameMode = -1;
    int expectedMinWaves = 1;
    if      (strcmp(modeName, "classic") == 0) { gameMode = 0; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "combo")   == 0) { gameMode = 1; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "arcade")  == 0) { gameMode = 2; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "zen")     == 0) { gameMode = 3; expectedMinWaves = 5;  }
    else return FailUsage();

    fn::TestHarness h(argc, argv, "gameplay");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    // Verify WaveManager loaded the per-mode wave list.
    WaveManager* wm = WaveManager::GetInstance();
    int waveCountAtMode = (int)wm->waveInfos[gameMode].size();
    if (waveCountAtMode < expectedMinWaves) {
        fprintf(stderr, "FAIL: mode %d only loaded %d waves (want >= %d)\n",
                gameMode, waveCountAtMode, expectedMinWaves);
        return 1;
    }
    printf("[test_gameplay] mode %s (%d): loaded %d waves OK\n",
           modeName, gameMode, waveCountAtMode);

    // Force into gameMode + fire SetupLevel as if the user clicked.
    game_work.gameMode = (uint8_t)gameMode;
    FN::PrepareForLevelStart();

    if (!wm->m_pCurrentWave[0]) {
        fprintf(stderr, "FAIL: m_pCurrentWave[0] still null after PrepareForLevelStart\n");
        return 1;
    }
    printf("[test_gameplay] PrepareForLevelStart primed wave=%p\n",
           (void*)wm->m_pCurrentWave[0]);

    // PrepareForLevelStart sets levelTransitionFlag = 1 (binary @ 0x169a9c). The
    // binary clears it from MainScreen::Update case 2-end / case 0x11
    // once the camera has zoomed into gameplay; the unit test bypasses
    // that state machine, so clear it manually to enable the spawn pump.
    game_work.m_LevelTransitionFlag = 0;

    if (h.IsInteractive()) {
        // After SetupLevel, keep ticking until ESC so the tester can watch
        // gameplay for the chosen mode.
        h.RunInteractive(GameplayTick, NULL, /*maxFrames=*/-1);
        return h.Shutdown();
    }

    // Tick ~3 seconds of frames (180 @ 60Hz) and look for spawn activity.
    // We can't easily count fruit (ActorManager::Add returns recycled
    // entities; counting active fruits via flag bits is the right path).
    int spawnCount = 0;
    int waveTransitions = 0;
    WAVE_INFO* prevWave = wm->m_pCurrentWave[0];
    for (int i = 0; i < 180; ++i) {
        // Take a snapshot of (wave, active-fruit-count) before the tick;
        // any change in active count between ticks proves spawn activity.
        Mortar::ActorManager* am = h.game.actorManager;
        int fruitsBefore = am ? am->GetNumEntities(0) : 0;

        h.RunHeadless(1);

        int fruitsAfter = am ? am->GetNumEntities(0) : 0;
        if (fruitsAfter > fruitsBefore) {
            spawnCount += (fruitsAfter - fruitsBefore);
        }
        if (wm->m_pCurrentWave[0] != prevWave) {
            waveTransitions++;
            prevWave = wm->m_pCurrentWave[0];
        }
    }

    printf("[test_gameplay] %s: spawnCount=%d waveTransitions=%d after 180 frames\n",
           modeName, spawnCount, waveTransitions);

    if (spawnCount == 0) {
        fprintf(stderr, "FAIL: no fruit spawns observed in mode %s\n", modeName);
        return 1;
    }
    if (waveTransitions == 0) {
        // Not strictly fatal -- first wave could still be running -- but
        // useful as a soft signal.
        printf("[test_gameplay] WARN: no wave transitions in 3s; first wave still active.\n");
    }

    if (h.IsScreenshot()) h.Screenshot();

    printf("PASS: gameplay mode '%s' OK (%d spawns, %d wave transitions)\n",
           modeName, spawnCount, waveTransitions);
    return h.Shutdown();
}
