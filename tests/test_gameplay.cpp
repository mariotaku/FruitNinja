// Per-mode gameplay smoke test. Boots the game, sets game.gameMode and
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

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "game/WaveManager.h"
#include "game/StartupEffects.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_gameplay <classic|arcade|zen|combo>\n");
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* modeName = argv[1];

    int gameMode = -1;
    int expectedMinWaves = 1;
    if      (strcmp(modeName, "classic") == 0) { gameMode = 0; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "combo")   == 0) { gameMode = 1; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "arcade")  == 0) { gameMode = 2; expectedMinWaves = 5;  }
    else if (strcmp(modeName, "zen")     == 0) { gameMode = 3; expectedMinWaves = 5;  }
    else return FailUsage();

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

    SDL_Window* window = SDL_CreateWindow(
        "fruit-ninja-gameplay-test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { fprintf(stderr, "Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { fprintf(stderr, "GL ctx failed: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(0);

    if (!gl_load_functions()) { fprintf(stderr, "gl_load_functions failed\n"); return 1; }

    Game game;
    if (!game.init(window, gl)) { fprintf(stderr, "game.init failed\n"); return 1; }

    // Burn through GameInit + splash.
    game.runFrames(120);
    if (!game.hud) {
        fprintf(stderr, "FAIL: game.hud null after 120 frames\n");
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
    game.gameMode = (uint8_t)gameMode;
    FN::PrepareForLevelStart();

    if (!wm->m_pCurrentWave[0]) {
        fprintf(stderr, "FAIL: m_pCurrentWave[0] still null after PrepareForLevelStart\n");
        return 1;
    }
    printf("[test_gameplay] PrepareForLevelStart primed wave=%p\n",
           (void*)wm->m_pCurrentWave[0]);

    // Tick ~3 seconds of frames (180 @ 60Hz) and look for spawn activity.
    // We can't easily count fruit (ActorManager::Add returns recycled
    // entities; counting active fruits via flag bits is the right path).
    int spawnCount = 0;
    int waveTransitions = 0;
    WAVE_INFO* prevWave = wm->m_pCurrentWave[0];
    for (int i = 0; i < 180; ++i) {
        // Take a snapshot of (wave, active-fruit-count) before the tick;
        // any change in active count between ticks proves spawn activity.
        Mortar::ActorManager* am = game.actorManager;
        int fruitsBefore = am ? am->GetNumEntities(0) : 0;

        game.runFrames(1);

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
        // Not strictly fatal — first wave could still be running — but
        // useful as a soft signal.
        printf("[test_gameplay] WARN: no wave transitions in 3s; first wave still active.\n");
    }

    game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("PASS: gameplay mode '%s' OK (%d spawns, %d wave transitions)\n",
           modeName, spawnCount, waveTransitions);
    return 0;
}
