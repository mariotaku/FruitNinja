// test_bomb_spawn — spawns bombs directly via WaveManager::SpawnBomb,
// ticks 200 frames per variant, and asserts that all bombs have been
// cleaned up by OOB-kill (no entity leak).
//
// Three variants mirror common <Spawn type="bomb" .../> patterns from
// originalwavelist.xml and combowavelist.xml.
//
// Build with -DFRUITNINJA_BOMB_TRACE=ON to see per-frame physics output
// from Bomb::Update. Without that flag the test still runs and checks
// the no-leak postcondition.
//
// Run via:
//   ctest --test-dir build -R bomb_spawn --output-on-failure
//
// Or with trace enabled:
//   cmake -DFRUITNINJA_BOMB_TRACE=ON -B build && cmake --build build
//   ctest --test-dir build -R bomb_spawn --output-on-failure

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "game/WaveManager.h"
#include "game/WaveStructs.h"
#include "entities/ActorManager.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_bomb_spawn [A|B|C|all|visual]\n"
        "  A      bare bottom bomb (ctor defaults, count=1)\n"
        "  B      left-side spawn\n"
        "  C      right-side spawn\n"
        "  all    run all variants (default; headless, fast)\n"
        "  visual run all variants with a visible 60Hz window;\n"
        "         keeps the window open after the runs until you close it\n");
    return 1;
}

// Global -- only true in `visual` mode. Drives window-visibility, vsync,
// per-variant frame count, and post-run idle loop.
static bool g_visual = false;

// Returns the number of live bombs (entity type 1).
static int BombCount(Game& game) {
    Mortar::ActorManager* am = game.actorManager;
    return am ? am->GetNumEntities(1) : 0;
}

// Initialise a full game context. Returns false on failure.
static bool GameSetup(SDL_Window** outWindow, SDL_GLContext* outGl, Game* game) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
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

    Uint32 winFlags = SDL_WINDOW_OPENGL;
    if (!g_visual) winFlags |= SDL_WINDOW_HIDDEN;

    *outWindow = SDL_CreateWindow(
        "fruit-ninja-bomb-spawn-test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        960, 640,
        winFlags);
    if (!*outWindow) {
        fprintf(stderr, "Window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    *outGl = SDL_GL_CreateContext(*outWindow);
    if (!*outGl) {
        fprintf(stderr, "GL ctx failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(*outWindow);
        SDL_Quit();
        return false;
    }
    // Visual mode: vsync on for ~60Hz watchable pacing. Headless mode: no
    // sync so ctest finishes fast.
    SDL_GL_SetSwapInterval(g_visual ? 1 : 0);

    if (!gl_load_functions()) {
        fprintf(stderr, "gl_load_functions failed\n");
        return false;
    }

    if (!game->init(*outWindow, *outGl)) {
        fprintf(stderr, "game.init failed\n");
        return false;
    }

    // Burn through GameInit + splash frames.
    game->runFrames(120);
    if (!game->hud) {
        fprintf(stderr, "FAIL: game.hud null after 120 frames\n");
        return false;
    }
    return true;
}

// Spawn a single bomb using the given SPAWNER_INFO, tick 200 frames,
// return true if no bombs remain (OOB-killed as expected).
static bool RunVariant(Game& game, const char* name, SPAWNER_INFO& spawner) {
    printf("=== Variant %s ===\n", name);

    // Clear any leftover entities from a previous variant.
    game.runFrames(5);

    const int bombsBefore = BombCount(game);

    // Bypass the wave pump; call SpawnBomb directly.
    WaveManager* wm = WaveManager::GetInstance();
    // Ensure physics runs (PrepareForLevelStart sets pauseFlag=1).
    game.pauseFlag = 0;

    wm->SpawnBomb(1, 1, &spawner, 0);

    printf("[test_bomb_spawn] %s: spawned, bombs_before=%d bombs_now=%d\n",
           name, bombsBefore, BombCount(game));

    // Tick frames. 200 is enough for a bomb arc + OOB kill at 60Hz;
    // visual mode runs 360 (~6s) so the user can watch the full flight
    // including a pause after the OOB kill before the next variant.
    game.pauseFlag = 0;
    game.runFrames(g_visual ? 360 : 200);

    const int bombsAfter = BombCount(game);
    printf("[test_bomb_spawn] %s: after 200 frames bombs=%d\n",
           name, bombsAfter);

    if (bombsAfter > bombsBefore) {
        fprintf(stderr,
            "FAIL: variant %s leaked %d bomb(s) after 200 frames\n",
            name, bombsAfter - bombsBefore);
        return false;
    }
    printf("PASS: variant %s -- no bomb leak\n", name);
    return true;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char* variant = "all";
    if (argc >= 2) {
        variant = argv[1];
        if (strcmp(variant, "A") != 0 &&
            strcmp(variant, "B") != 0 &&
            strcmp(variant, "C") != 0 &&
            strcmp(variant, "all") != 0 &&
            strcmp(variant, "visual") != 0) {
            return FailUsage();
        }
        if (strcmp(variant, "visual") == 0) {
            g_visual = true;
            variant = "all";
        }
    }

    SDL_Window*   window = NULL;
    SDL_GLContext gl     = NULL;
    Game game;
    if (!GameSetup(&window, &gl, &game)) return 1;

    // Force classic mode so WaveManager has a valid wave list; we won't
    // actually use the wave pump (SpawnBomb bypasses it).
    game.gameMode = 0;
    game.pauseFlag = 0;

    int failures = 0;

    // Variant A: bare bottom bomb -- ctor defaults, count=1, type=1.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "A") == 0) {
        SPAWNER_INFO spawnerA;
        // All ctor defaults: PLACEMENT_BOTTOM, gravity (0,-1,0), horiz full range.
        if (!RunVariant(game, "A: bare bottom bomb", spawnerA)) ++failures;
    }

    // Variant B: left-side spawn.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "B") == 0) {
        SPAWNER_INFO spawnerB;
        spawnerB.m_SpawnType  = PLACEMENT_LEFT;
        spawnerB.m_Gravity_y  = -0.5f;
        spawnerB.m_HorizMin   = -0.25f;
        spawnerB.m_HorizMax   =  0.5f;
        if (!RunVariant(game, "B: left-side spawn", spawnerB)) ++failures;
    }

    // Variant C: right-side spawn.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "C") == 0) {
        SPAWNER_INFO spawnerC;
        spawnerC.m_SpawnType  = PLACEMENT_RIGHT;
        spawnerC.m_Gravity_y  = -0.5f;
        spawnerC.m_HorizMin   = -0.25f;
        spawnerC.m_HorizMax   =  0.5f;
        if (!RunVariant(game, "C: right-side spawn", spawnerC)) ++failures;
    }

    if (g_visual) {
        printf("=== visual mode: variants done. Close the window to exit. ===\n");
        // Keep rendering until the user closes the window (Game::runFrames
        // sets game.running = false on SDL_QUIT). Use a big frame budget;
        // the loop exits naturally on close.
        for (int i = 0; i < 60 * 60 * 5 && game.running; ++i) {
            game.runFrames(1);
        }
    }

    game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d variant(s) failed\n", failures);
        return 1;
    }
    printf("PASS: all bomb_spawn variants OK\n");
    return 0;
}
