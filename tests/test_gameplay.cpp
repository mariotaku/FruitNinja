// Per-mode gameplay smoke test. Boots the game, sets game_work.gameMode and
// fires the SetupLevel pipeline, ticks ~3 seconds of frames, and verifies
// (a) the wave manager loads the expected wave count for that mode,
// (b) m_pCurrentWave[0] becomes non-null after SetupLevel,
// (c) at least one fruit spawns within the run window.
//
// Run via:
//   cd build/host && ctest --output-on-failure -R gameplay_
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
#include "screens/MainScreen.h"
#include <cstring>
#include <list>
#include <map>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_gameplay <classic|arcade|zen|combo> [--interactive] [--screenshot]\n");
    return 1;
}

// Interactive tick callback: re-asserts the requested game mode each tick
// as a defensive guard (see the SetCameraTransition/SetState fix in main() for
// the real root-cause fix) and keeps ticking until ESC / window close.
static bool GameplayTick(Game& /*game*/, int /*frame*/, void* userdata) {
    game_work.gameMode   = *(const uint8_t*)userdata;
    game_work.bM_bPaused = 0;
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
    int waveCountAtMode = (int)wm->m_WaveInfo[gameMode].size();
    if (waveCountAtMode < expectedMinWaves) {
        fprintf(stderr, "FAIL: mode %d only loaded %d waves (want >= %d)\n",
                gameMode, waveCountAtMode, expectedMinWaves);
        return 1;
    }
    printf("[test_gameplay] mode %s (%d): loaded %d waves OK\n",
           modeName, gameMode, waveCountAtMode);

    // Force into gameMode + fire SetupLevel as if the user clicked.
    game_work.gameMode = (uint8_t)gameMode;
    PrepareForLevelStart();

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
    game_work.bM_bPaused = 0;

    // Root-cause fix (#37): the REAL "mode picked" transition -- v1.6.1
    // GameModeScreen::Update @0x1829e4 cases 3-6 (GameModeScreen.cpp:508-535) --
    // does THREE things once the camera-out fade crosses its threshold, not just
    // PrepareForLevelStart()/SetupLevel():
    //   1. MainScreen::SetCameraTransition(0.0f)  -- i.e. game_work.m_PauseAmount = 0.0f
    //   2. game_work.bM_bPaused = 0                (already done above)
    //   3. MainScreen::SetState(STATE_CAMERA_FADE)
    // This test only ever did PrepareForLevelStart() + bM_bPaused=0, so
    // m_PauseAmount was left at GameInit's -1.0f for the entire run (MainScreen
    // idles in STATE_CREATE_BUTTONS once the intro zoom settles -- it never
    // reaches case STATE_CAMERA_ZOOM/STATE_GAME_START again, and neither of
    // those write m_PauseAmount toward 0 on their own; STATE_GAME_START is in
    // fact dead code in v1.6.1 -- zero inbound xrefs, see MainScreen.cpp:959).
    // arcade_spawn_real avoids this by forcing m_PauseAmount=0.0f directly
    // every frame; mirror the binary's real call sequence here instead of
    // re-deriving the same fix ad hoc.
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetCameraTransition(0.0f);
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }

    uint8_t gameModeByte = (uint8_t)gameMode;
    if (h.IsInteractive()) {
        // After SetupLevel, keep ticking until ESC so the tester can watch
        // gameplay for the chosen mode.
        h.RunInteractive(GameplayTick, &gameModeByte, /*maxFrames=*/-1);
        return h.Shutdown();
    }

    // Tick ~3 seconds of frames (180 @ 60Hz) and look for spawn activity.
    //
    // Spawn detection: per-tick snapshot of live type-0 entities keyed by
    // pointer, storing m_SpawnDelay. A spawn this tick is either
    //   (a) a pointer not live before the tick, or
    //   (b) a live pointer whose m_SpawnDelay INCREASED (ActorManager::Add
    //       recycled a freed slot and WaveManager::SpawnFruit re-Chucked it --
    //       m_SpawnDelay only decreases after launch, so an increase proves a
    //       fresh Chuck; v1.6.1 Fruit::Chuck @0x001db5f0).
    // The previous detector compared net GetNumEntities(0) counts, which is
    // masked whenever a fruit dies in the same tick another spawns (flung
    // menu fruits from the burn-in die throughout the first seconds). That
    // made the test flaky, and reliably 0 for arcade once the v1.6.1-faithful
    // SpawnFruit (@0x00124298) launch offset (pos += throwDir*(scale.y*100),
    // @0x00124714) lengthened fruit flight times and shifted the death/spawn
    // overlap.
    // Arcade's first wave is "BlankWave" (waveNo=0, Spawn min=0 max=0 -> spawns
    // NOTHING, NextWaveDelay delay=0.85) per the WaveManager::Init @0x00129934
    // "until" auto-derivation: waves without an explicit until end at (next
    // higher waveNo)-1, so waveNo=0 BlankWave is the only waveCount==0 match.
    // The first REAL fruit comes from the waveNo=1 waves after GetNextWave
    // fires (~frame 55+).
    //
    // Belt-and-suspenders: re-assert gameMode/bM_bPaused/bM_Mode/m_PauseAmount
    // every frame, mirroring arcade_spawn_real's per-frame reassertion. Nothing
    // in the current call graph should overwrite these mid-run once the
    // SetCameraTransition/SetState fix above has fired, but MainScreen's
    // STATE_CAMERA_ZOOM/STATE_GAME_START handlers (dead in this run, since
    // MainScreen already idles in STATE_CREATE_BUTTONS/STATE_CAMERA_FADE
    // outside of this test) do write gameMode=0 / m_PauseAmount if ever
    // re-entered -- guard against that class of regression cheaply.
    int spawnCount = 0;
    int waveTransitions = 0;
    WAVE_INFO* prevWave = wm->m_pCurrentWave[0];
    std::map<Mortar::Entity*, float> liveBefore;
    for (int i = 0; i < 180; ++i) {
        game_work.gameMode      = (uint8_t)gameMode;
        game_work.bM_bPaused    = 0;
        game_work.bM_Mode       = false;
        game_work.m_PauseAmount = 0.0f;

        Mortar::ActorManager* am = h.game.actorManager;
        liveBefore.clear();
        if (am) {
            std::list<Mortar::Entity*>::iterator it;
            for (Mortar::Entity* e = am->GetEntityFirst(0, it); e;
                 e = am->GetEntityNext(0, it)) {
                liveBefore[e] = static_cast<Fruit*>(e)->m_SpawnDelay;
            }
        }

        h.RunHeadless(1);

        if (am) {
            std::list<Mortar::Entity*>::iterator it;
            for (Mortar::Entity* e = am->GetEntityFirst(0, it); e;
                 e = am->GetEntityNext(0, it)) {
                std::map<Mortar::Entity*, float>::iterator prev = liveBefore.find(e);
                if (prev == liveBefore.end() ||
                    static_cast<Fruit*>(e)->m_SpawnDelay > prev->second) {
                    ++spawnCount;
                }
            }
        }
        if (wm->m_pCurrentWave[0] != prevWave) {
            waveTransitions++;
            prevWave = wm->m_pCurrentWave[0];
            // Diagnostic (cheap; stays in for future regressions): confirm the
            // post-transition wave is a real, non-empty arcade wave, not an
            // empty/classic-list pick.
            WAVE_INFO* w = prevWave;
            printf("[test_gameplay] frame %d: wave transition -> waveNo=%d spawners=%d "
                   "wfe=%d gameMode=%d m_PauseAmount=%.3f bM_Mode=%d\n",
                   i, w ? w->m_WaveNumber : -999, w ? w->m_SpawnerCount : -1,
                   w ? (int)w->m_bWaitForEntities : -1,
                   (int)game_work.gameMode, game_work.m_PauseAmount, (int)game_work.bM_Mode);
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

    if (h.IsScreenshot()) h.ScreenshotPng();

    printf("PASS: gameplay mode '%s' OK (%d spawns, %d wave transitions)\n",
           modeName, spawnCount, waveTransitions);
    return h.Shutdown();
}
