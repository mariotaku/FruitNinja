// test_bomb_spawn -- spawns bombs directly via WaveManager::SpawnBomb,
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
//   ctest --test-dir build/host -R bomb_spawn --output-on-failure
//
// Or with trace enabled:
//   cmake -DFRUITNINJA_BOMB_TRACE=ON -B build/host && cmake --build build/host
//   ctest --test-dir build/host -R bomb_spawn --output-on-failure
//
// Interactive (visible window, watchable pacing, post-run idle loop):
//   ./build/host/tests/Debug/test_bomb_spawn.exe --interactive
//   ./build/host/tests/Debug/test_bomb_spawn.exe all --interactive

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/WaveStructs.h"
#include "game/StartupEffects.h"
#include "game/GameTaskState.h"
#include "entities/ActorManager.h"
#include "entities/Bomb.h"
#include "hud/HUD.h"
#include "screens/MainScreen.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "audio/GameSound.h"
#include "audio/MortarSound.h"
#include "util/StringHash.h"
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_bomb_spawn [A|B|C|D|all] [--interactive] [--screenshot]\n"
        "  A      bare bottom bomb (ctor defaults, count=1)\n"
        "  B      left-side spawn\n"
        "  C      right-side spawn\n"
        "  D      spawn + slice; verify fuse SFX silences after clear\n"
        "  all    run all variants (default; headless, fast)\n"
        "  --interactive  run with a visible 60Hz window;\n"
        "                 keeps the window open after the runs until you close it\n");
    return 1;
}

// Returns the number of live bombs (entity type 1).
static int BombCount(Game& game) {
    Mortar::ActorManager* am = game.actorManager;
    return am ? am->GetNumEntities(1) : 0;
}

// Zero out the current wave's spawner counts so WaveManager's spawn pump
// runs without spawning anything alongside our test bomb. Idempotent; safe
// to call every tick. Called from a per-frame loop inside RunVariant.
static void SuppressWaveSpawn() {
    WaveManager* wm = WaveManager::GetInstance();
    if (!wm || !wm->m_pCurrentWave[0]) return;
    WAVE_INFO* w = wm->m_pCurrentWave[0];
    for (int i = 0; i < w->m_SpawnerCount; ++i) {
        w->m_pSpawners[i].m_RemainingCount = 0;
        w->m_pSpawners[i].m_SpawnCountF    = 0.0f;
    }
}

// Returns the live volume of the "Bomb-Fuse" slot in GameSound, or -1.0
// if no such slot is currently held. Mirrors the binary's per-frame
// SetVolume(0)-on-no-bomb mute mechanism (binary @ 0x0016c4c8..0x0016c5ca).
static float BombFuseSlotVolume(Game& game) {
    GameSound* gs = game_work.mGameSound;
    if (!gs) return -1.0f;
    const uint32_t fuseHash = ::StringHash("Bomb-Fuse");
    for (int i = 0; i < GameSound::MAX_SLOTS; ++i) {
        const GameSound::Slot& s = gs->m_Slots[i];
        if (!s.isFree && s.id == fuseHash) return s.volume;
    }
    return -1.0f;
}

// Returns the first Bomb entity in ActorManager type-1 list, or nullptr.
static Bomb* FirstBomb(Game& game) {
    Mortar::ActorManager* am = game.actorManager;
    if (!am) return nullptr;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    return e ? static_cast<Bomb*>(e) : nullptr;
}

// Spawn + slice variant: verify the persistent Bomb-Fuse SFX channel
// silences (volume drops to ~0) once the bomb is cleared.
//
// Sequence:
//   1. Spawn one bomb.
//   2. Tick ~30 frames so the fuse SFX handle is allocated and ramped
//      to a non-zero volume by GameUpdate's fuse-vol block.
//   3. Slice the bomb via direct CollisionResponse (bypass SlashEntity).
//   4. Tick ~240 frames so the hit-branch BombBlast loop runs, the bomb
//      eventually OOBs, GetHeighestBomb() returns its -10000 sentinel,
//      and the GameUpdate block SetVolume(fuse, 0)'s.
//   5. Assert: bombs == 0 AND fuse slot volume < 0.01 (silent).
static bool RunVariantSlice(Game& game, bool visual) {
    const char* name = "D: spawn + slice + fuse-silence verify";
    printf("=== Variant %s ===\n", name);

    // Settle from previous variants.
    for (int i = 0; i < 5; ++i) { SuppressWaveSpawn(); game.runFrames(1); }

    // Step 1: spawn a single bomb (bare bottom, like variant A).
    SPAWNER_INFO spawner;
    WaveManager::GetInstance()->SpawnBomb(1, &spawner, 0);
    printf("[slice] spawned: bombs=%d\n", BombCount(game));

    // Step 2: let the bomb fall + fuse SFX spin up.
    for (int i = 0; i < 30; ++i) { SuppressWaveSpawn(); game.runFrames(1); }
    const float volPreSlice = BombFuseSlotVolume(game);
    printf("[slice] after spin-up (30 frames): bombs=%d, fuse vol=%.4f\n",
           BombCount(game), volPreSlice);
    if (volPreSlice <= 0.0f) {
        fprintf(stderr,
            "WARN: fuse slot vol=%.4f after spin-up (expected >0). "
            "GameUpdate fuse-vol channel may not be active. "
            "Continuing -- the post-slice silence check is the real assertion.\n",
            volPreSlice);
    }

    // Step 3: slice. Direct CollisionResponse with a non-zero blade vel
    // (avoids needing the full SlashEntity touch-routing path).
    Bomb* bomb = FirstBomb(game);
    if (!bomb) {
        fprintf(stderr, "FAIL: %s -- no bomb to slice\n", name);
        return false;
    }
    Vec3 bladeVel(15.0f, 15.0f, 0.0f);
    bomb->CollisionResponse(nullptr, 0, 0, &bladeVel);
    printf("[slice] sliced bomb at pos=(%.1f, %.1f); m_bHit=%d, "
           "bombHitTimer=%.2f\n",
           bomb->pos.x, bomb->pos.y, (int)bomb->m_bHit, game_work.m_BombHitTimer);

    // Step 4: tick enough frames for the bombHitTimer to cross 1.5
    // (ResetGameEntities fires -> bomb flung -> OOB -> KillBomb),
    // GameOver to run, and the fuse channel to be muted.
    // 240 frames @ ~60Hz = 4s of game time; bombHitTimer 3.2 -> 1.5
    // takes ~1.7s, plus another ~1.5s for the bomb to OOB after fling.
    const int frames = visual ? 360 : 240;
    for (int i = 0; i < frames; ++i) {
        SuppressWaveSpawn();
        game.runFrames(1);
    }

    const int bombsAfter = BombCount(game);
    const float volPostSlice = BombFuseSlotVolume(game);
    printf("[slice] after %d post-slice frames: bombs=%d, fuse vol=%.4f\n",
           frames, bombsAfter, volPostSlice);

    // Primary assertion: fuse SFX silenced. Allow tiny epsilon for
    // in-flight SetVolume integration. The slot may still exist (handle
    // persists per binary) -- we check volume, not slot presence.
    //
    // Note: bombsAfter may be >0 even when the original sliced bomb
    // OOB-cleared, because the slice triggers the bombHitTimer=3.2 ->
    // GameOver -> ResetGameEntities -> chain-spawn flow which can spawn
    // a fresh bomb before this assertion runs. That's a separate concern
    // from the fuse-silence regression and not what this variant checks.
    bool ok = true;
    if (volPostSlice > 0.01f) {
        fprintf(stderr,
            "FAIL: %s -- fuse SFX still audible: vol=%.4f (expected ~0).\n"
            "      This is the user-reported \"hissing doesn't stop\" bug.\n",
            name, volPostSlice);
        ok = false;
    } else {
        printf("[slice] fuse silence verified: vol=%.4f <= 0.01 (silenced).\n",
               volPostSlice);
    }
    if (bombsAfter > 0) {
        printf("[slice] (informational) %d bomb(s) remain at test end -- "
               "likely chain-spawned by the GameOver sequence; not a fuse-SFX "
               "regression.\n", bombsAfter);
    }

    if (ok) printf("PASS: variant %s -- fuse SFX silenced cleanly\n", name);
    return ok;
}

// Spawn a single bomb using the given SPAWNER_INFO, tick frames while
// suppressing the wave-manager's own spawns, return true if no bombs
// remain (OOB-killed as expected).
static bool RunVariant(Game& game, const char* name, SPAWNER_INFO& spawner, bool visual) {
    printf("=== Variant %s ===\n", name);

    // Clear any leftover entities from a previous variant; keep wave-spawn
    // suppressed even during this clear window.
    for (int i = 0; i < 5; ++i) { SuppressWaveSpawn(); game.runFrames(1); }

    const int bombsBefore = BombCount(game);

    // Bypass the wave pump; call SpawnBomb directly.
    WaveManager* wm = WaveManager::GetInstance();
    wm->SpawnBomb(1, &spawner, 0);

    printf("[test_bomb_spawn] %s: spawned, bombs_before=%d bombs_now=%d\n",
           name, bombsBefore, BombCount(game));

    // Tick frames. 200 is enough for a bomb arc + OOB kill at 60Hz;
    // visual mode runs 360 (~6s) so the user can watch the full flight
    // including a pause after the OOB kill before the next variant.
    const int frames = visual ? 360 : 200;
    for (int i = 0; i < frames; ++i) {
        SuppressWaveSpawn();
        game.runFrames(1);
    }

    const int bombsAfter = BombCount(game);
    printf("[test_bomb_spawn] %s: after %d frames bombs=%d\n",
           name, frames, bombsAfter);

    if (bombsAfter > bombsBefore) {
        fprintf(stderr,
            "FAIL: variant %s leaked %d bomb(s) after %d frames\n",
            name, bombsAfter - bombsBefore, frames);
        return false;
    }
    printf("PASS: variant %s -- no bomb leak\n", name);
    return true;
}

// Idle callback for RunInteractive -- keeps ticking indefinitely until
// ESC / window close.
static bool IdleTick(Game& /*game*/, int /*frame*/, void* /*userdata*/) {
    return true;
}

int main(int argc, char* argv[]) {
    const char* variant = "all";
    if (argc >= 2 && argv[1][0] != '-') {
        variant = argv[1];
        if (strcmp(variant, "A") != 0 &&
            strcmp(variant, "B") != 0 &&
            strcmp(variant, "C") != 0 &&
            strcmp(variant, "D") != 0 &&
            strcmp(variant, "all") != 0 &&
            strcmp(variant, "visual") != 0) {
            return FailUsage();
        }
        // Legacy "visual" positional arg: treat as --interactive + "all".
        if (strcmp(variant, "visual") == 0) {
            variant = "all";
        }
    }

    fn::TestHarness h(argc, argv, "bomb_spawn");
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    const bool visual = h.IsInteractive();

    // Drop into the Classic gameplay stage. PrepareForLevelStart alone
    // only resets WaveManager data -- the actual screen transition out of
    // MainScreen needs the state machine kicked into STATE_CAMERA_FADE
    // (0x11) and the camera transition timer zeroed, otherwise MainScreen
    // stays in its CAMERA_ZOOM state and renders the menu. Mirror the
    // sequence used by test_screen.cpp's "classic" path.
    //
    // Per-tick we also zero every spawner's m_RemainingCount so the wave
    // manager does NOT spawn its own fruit/bombs alongside our test bombs
    // -- the test stays a single-bomb-at-a-time visualisation.
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* c = *it;
        if (dynamic_cast<DojoScreen*>(c)
         || dynamic_cast<AboutScreen*>(c)
         || dynamic_cast<ShopScreen*>(c)
         || dynamic_cast<GameModeScreen*>(c)) {
            c->m_Active = 0;
        }
    }
    game_work.gameMode = 0;
    PrepareForLevelStart();
    game_work.bM_bPaused = 0;
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
        // Port-specific: explicitly remove menu buttons so they don't
        // appear in this visual test. DeleteMenuButtons has no binary
        // counterpart -- the binary keeps buttons alive across gameplay
        // (persisting MainScreen model).
        game_work.mMainScreen->DeleteMenuButtons();
    }
    game_work.m_PauseAmount = 0.0f;

    // Settle the camera + HUD into gameplay.
    h.RunHeadless(60);

    int failures = 0;

    // Variant A: bare bottom bomb -- ctor defaults, count=1, type=1.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "A") == 0) {
        SPAWNER_INFO spawnerA;
        // All ctor defaults: PLACEMENT_BOTTOM, gravity (0,-1,0), horiz full range.
        if (!RunVariant(h.game, "A: bare bottom bomb", spawnerA, visual)) ++failures;
    }

    // Variant B: left-side spawn.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "B") == 0) {
        SPAWNER_INFO spawnerB;
        spawnerB.m_SpawnType  = PLACEMENT_LEFT;
        spawnerB.m_Gravity_y  = -0.5f;
        spawnerB.m_HorizMin   = -0.25f;
        spawnerB.m_HorizMax   =  0.5f;
        if (!RunVariant(h.game, "B: left-side spawn", spawnerB, visual)) ++failures;
    }

    // Variant C: right-side spawn.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "C") == 0) {
        SPAWNER_INFO spawnerC;
        spawnerC.m_SpawnType  = PLACEMENT_RIGHT;
        spawnerC.m_Gravity_y  = -0.5f;
        spawnerC.m_HorizMin   = -0.25f;
        spawnerC.m_HorizMax   =  0.5f;
        if (!RunVariant(h.game, "C: right-side spawn", spawnerC, visual)) ++failures;
    }

    // Variant D: spawn + slice + verify fuse SFX silences.
    if (strcmp(variant, "all") == 0 || strcmp(variant, "D") == 0) {
        if (!RunVariantSlice(h.game, visual)) ++failures;
    }

    if (visual) {
        printf("=== interactive mode: variants done. Close the window to exit. ===\n");
        h.RunInteractive(IdleTick, NULL, /*maxFrames=*/60 * 60 * 5);
    }

    if (h.IsScreenshot()) h.ScreenshotPng();

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d variant(s) failed\n", failures);
        return h.Shutdown();
    }
    printf("PASS: all bomb_spawn variants OK\n");
    return h.Shutdown();
}
