// test_arcade_spawn -- per-mode spawn lifecycle diagnostic.
//
// Boots the game in the chosen mode (classic/arcade/zen), fires
// PrepareForLevelStart, and ticks N frames. Default duration matches
// the timed-mode game length + a 5s buffer so the run covers the real
// playable session:
//   classic  -> 30s + 5s   (no built-in timer; sanity duration)
//   arcade   -> 60s + 5s   (arcade countdown is 60s)
//   zen      -> 90s + 5s   (zen countdown is 90s)
// Frame count derived at ~60 Hz simulated.
//
// On every frame:
//
//   * Detect new fruit/bomb entities arriving in ActorManager (compared to
//     the previous frame's snapshot) and log their initial pos, vel,
//     gravity, fruitType, m_PlayerIdx, m_ChuckDelay, m_ZPosition.
//   * Detect wave transitions (m_pCurrentWave[0] changed) and dump the new
//     wave's spawner configuration: placement, gravity, m_VelXScale,
//     m_VelYScale, m_HorizMin/Max, m_SpawnMin/Max, m_Delay/m_DelayInc.
//
// This is a DIAGNOSTIC test, not pass/fail -- the goal is a lifecycle log
// for the user to read and confirm whether spawn parameters match what the
// binary should produce. Always returns 0 unless setup fails.
//
// Run:
//   ./build/tests/Debug/test_arcade_spawn.exe                   # arcade, 65s default
//   ./build/tests/Debug/test_arcade_spawn.exe --mode=classic    # classic, 35s default
//   ./build/tests/Debug/test_arcade_spawn.exe --mode=zen        # zen, 95s default
//   ./build/tests/Debug/test_arcade_spawn.exe --frames=600      # override frame count
//   ./build/tests/Debug/test_arcade_spawn.exe --trace=30        # log entity pos/vel every 30 frames
//   ./build/tests/Debug/test_arcade_spawn.exe --interactive     # visible window
//
// Lifecycle entries are tagged [SPAWN-N], [WAVE], [BLITZ], [TRACE], [KILL]
// so users can grep easily.
//
// Physics note: per ASM-verified Fruit::Update, velocity is in "units per
// 60fps frame", apex above launch = v^2/(2|g|) * 60 (not v^2/(2|g|)). With
// vel.y=9.27, g=-12, apex ≈ 215 above start. Fruits spawned at y=-160
// should reach the top of the visible area easily.

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/WaveStructs.h"
#include "game/StartupEffects.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/Entity.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"  // FN::ClearMenuItems
#include "screens/MainScreen.h"

#include <cstring>
#include <cstdlib>
#include <set>
#include <vector>
#include <list>

static const char* PlacementName(int p) {
    switch (p) {
    case 0: return "BOTTOM";
    case 1: return "BOTTOM_SLOW";
    case 2: return "LEFT";
    case 3: return "RIGHT";
    case 4: return "RANDOM_SIDE";
    default: return "?";
    }
}

// Returns the WAVE_INFO's m_WaveIndex (sequential XML position) or -1 if null.
static int WaveIdx(WAVE_INFO* w) {
    if (!w) return -1;
    return w->m_WaveIndex;
}

static void DumpSpawner(int idx, const SPAWNER_INFO& s) {
    printf("  spawner[%d]: place=%s grav=(%.2f,%.2f,%.2f) horiz=[%.3f,%.3f]"
           " velScale=(%.2f,%.2f) min/max=%.2f/%.2f delay=%.2f delayInc=%.3f"
           " timeScale=%.2f types=%d remain=%d\n",
           idx, PlacementName((int)s.m_SpawnType),
           s.m_Gravity_x, s.m_Gravity_y, s.m_Gravity_z,
           s.m_HorizMin, s.m_HorizMax,
           s.m_VelXScale, s.m_VelYScale,
           s.m_SpawnMin, s.m_SpawnMax,
           s.m_Delay, s.m_DelayInc,
           s.m_TimeScale,
           s.m_FruitTypeCount, s.m_RemainingCount);
}

static void DumpWave(int frame, WAVE_INFO* w) {
    if (!w) { printf("[WAVE @f=%d] (null)\n", frame); return; }
    printf("[WAVE @f=%d] idx=%d score=[%d..%d] dt=%.2f spawners=%d pool=%d"
           " specials=%zu chance=%d/%d revisit=%.1f\n",
           frame, w->m_WaveIndex,
           w->m_ScoreThreshold, w->m_EndScore,
           w->m_WaveDt, w->m_SpawnerCount,
           w->m_OverideProbabilityPool,
           w->m_SpecialFruits.size(),
           w->m_Chance, w->m_CurrentChance, w->field_0x34);
    for (int i = 0; i < w->m_SpawnerCount; ++i) {
        DumpSpawner(i, w->m_pSpawners[i]);
    }
}

static void LogNewFruit(int frame, int spawnIdx, Fruit* f) {
    printf("[SPAWN-FRUIT @f=%d #%d] type=%d entity=%p pos=(%.1f,%.1f,%.1f)"
           " vel=(%.2f,%.2f,%.2f) grav=(%.2f,%.2f,%.2f) playerIdx=%d"
           " chuckDelay=%.3f Z=%.1f timeScale=%.3f sliced=%d\n",
           frame, spawnIdx,
           (int)f->m_FruitType, (void*)f,
           f->pos.x, f->pos.y, f->pos.z,
           f->vel.x, f->vel.y, f->vel.z,
           f->m_Gravity.x, f->m_Gravity.y, f->m_Gravity.z,
           (int)f->m_PlayerIdx,
           f->m_ChuckDelay, f->m_ZPosition,
           f->m_TimeScale,
           (int)f->m_bSliced);
}

static void LogNewBomb(int frame, int spawnIdx, Bomb* b) {
    printf("[SPAWN-BOMB  @f=%d #%d] entity=%p pos=(%.1f,%.1f,%.1f)"
           " vel=(%.2f,%.2f,%.2f) Z=%.1f\n",
           frame, spawnIdx, (void*)b,
           b->pos.x, b->pos.y, b->pos.z,
           b->vel.x, b->vel.y, b->vel.z,
           b->m_ZPosition);
}

static int ParseFrames(int argc, char** argv, int defaultVal) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--frames=", 9) == 0) {
            int n = std::atoi(argv[i] + 9);
            if (n > 0) return n;
        }
    }
    return defaultVal;
}

static int ParseTraceInterval(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--trace=", 8) == 0) {
            int n = std::atoi(argv[i] + 8);
            if (n > 0) return n;
        }
    }
    return 0;  // 0 = trace off
}

// Returns 0=classic, 2=arcade, 3=zen. Defaults to arcade for back-compat
// with the test's original name. Aborts the test with usage info on
// unknown values rather than silently running a different mode.
static int ParseGameMode(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--mode=", 7) == 0) {
            const char* m = argv[i] + 7;
            if (std::strcmp(m, "classic") == 0) return 0;
            if (std::strcmp(m, "arcade")  == 0) return 2;
            if (std::strcmp(m, "zen")     == 0) return 3;
            fprintf(stderr, "FAIL: unknown --mode=%s (use classic/arcade/zen)\n", m);
            std::exit(1);
        }
    }
    return 2;  // arcade default
}

// Mode -> default frame count. Headless runs at ~60 Hz; the durations
// match real-game session length + 5s buffer so the menu-bomb that
// title-screen leaves in ActorManager has time to OOB-kill itself and
// the test doesn't surface that as a "stall".
static int DefaultFramesForMode(int gameMode) {
    switch (gameMode) {
        case 0: return 35 * 60;   // classic: 30s + 5s
        case 2: return 65 * 60;   // arcade: 60s + 5s
        case 3: return 95 * 60;   // zen: 90s + 5s
        default: return 360;
    }
}

static const char* ModeName(int gameMode) {
    switch (gameMode) {
        case 0: return "classic";
        case 2: return "arcade";
        case 3: return "zen";
        default: return "?";
    }
}

int main(int argc, char* argv[]) {
    const int gameMode      = ParseGameMode(argc, argv);
    const int frameCount    = ParseFrames(argc, argv, DefaultFramesForMode(gameMode));
    const int traceInterval = ParseTraceInterval(argc, argv);

    char labelBuf[32];
    std::snprintf(labelBuf, sizeof(labelBuf), "%s_spawn", ModeName(gameMode));
    fn::TestHarness h(argc, argv, labelBuf);
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud null after init\n");
        return 1;
    }

    WaveManager* wm = WaveManager::GetInstance();
    if (!wm) {
        fprintf(stderr, "FAIL: WaveManager null\n");
        return 1;
    }

    const int modeWaveCount = (int)wm->waveInfos[gameMode].size();
    printf("[BOOT] mode=%s gameMode=%d waveInfos[%d].size=%d frames=%d\n",
           ModeName(gameMode), gameMode, gameMode, modeWaveCount, frameCount);
    if (modeWaveCount == 0) {
        fprintf(stderr, "FAIL: %s wave list empty -- XML load failed?\n",
                ModeName(gameMode));
        return 1;
    }

    // In real gameplay, GameModeScreen's mode-click handler fires
    // FN::ClearMenuItems() (which flings/disables the menu-screen fruits
    // and bombs) before transitioning, then MainScreen advances to
    // STATE_CAMERA_FADE for gameplay (which DOES NOT lazy-recreate menu
    // buttons each frame).
    //
    // Our test bypasses GameModeScreen entirely, so we have to:
    //   (a) call ClearMenuItems to fling the menu fruits/bombs, AND
    //   (b) advance MainScreen to STATE_CAMERA_FADE so it stops lazy-
    //       creating new QUIT bombs each frame the title screen is
    //       active. Without (b) the QUIT MenuButton's m_pQuitBtn==null
    //       branch keeps respawning the bomb, locking IsWaveProcessing
    //       at true forever and surfacing a fake "wave stall" that
    //       doesn't exist in real arcade gameplay.
    FN::ClearMenuItems();
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }
    game_work.gameMode = (uint8_t)gameMode;
    FN::PrepareForLevelStart();
    game_work.m_LevelTransitionFlag = 0;  // bypass MainScreen camera-fade gate
    // Let the flung menu items physics-die before we start counting spawns.
    h.RunHeadless(120);

    WAVE_INFO* initialWave = wm->m_pCurrentWave[0];
    if (!initialWave) {
        fprintf(stderr, "FAIL: m_pCurrentWave[0] null after PrepareForLevelStart\n");
        return 1;
    }
    printf("[BOOT] initial wave primed\n");
    DumpWave(0, initialWave);

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "FAIL: ActorManager null\n");
        return 1;
    }

    // Snapshot the entities currently in ActorManager (typically 0 after a
    // fresh PrepareForLevelStart, but the harness may already have spawned
    // menu fruits during the 120 init frames -- treat them all as "seen"
    // so we only log NEW arrivals from arcade gameplay.
    std::set<Mortar::Entity*> seenFruits;
    std::set<Mortar::Entity*> seenBombs;
    {
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            seenFruits.insert(e);
        }
        for (Mortar::Entity* e = am->GetEntityFirst(1, it); e; e = am->GetEntityNext(1, it)) {
            seenBombs.insert(e);
        }
    }
    printf("[BOOT] pre-existing entities: fruits=%zu bombs=%zu (marked seen, not logged)\n",
           seenFruits.size(), seenBombs.size());

    WAVE_INFO* prevWave = initialWave;
    int spawnNo = 0;
    int prevBlitzCounter = 0;

    // Tracking fields on the WaveManager that drive the Arcade blitz state
    // machine -- expose via reflection-style direct access. WaveManager
    // member offsets/types are declared in the public header.
    // field_0x23d: global blitz counter
    // field_0x23e: phase counter
    // field_0x240: gate timer

    for (int frame = 1; frame <= frameCount; ++frame) {
        // Snapshot blitz counters BEFORE tick to detect changes.
        int blitzBefore = (int)wm->field_0x23d;
        int phaseBefore = (int)wm->field_0x23e;
        float gateBefore = wm->field_0x240;

        h.RunHeadless(1);

        // Wave-change detection.
        if (wm->m_pCurrentWave[0] != prevWave) {
            printf("[WAVE-CHANGE @f=%d] from idx=%d to idx=%d\n",
                   frame, WaveIdx(prevWave), WaveIdx(wm->m_pCurrentWave[0]));
            prevWave = wm->m_pCurrentWave[0];
            if (prevWave) DumpWave(frame, prevWave);
        }

        // Blitz state-machine event log (Arcade-only state).
        int blitzAfter = (int)wm->field_0x23d;
        int phaseAfter = (int)wm->field_0x23e;
        float gateAfter = wm->field_0x240;
        if (blitzAfter != blitzBefore || phaseAfter != phaseBefore || gateAfter != gateBefore) {
            printf("[BLITZ @f=%d] counter %d->%d phase %d->%d gate %.2f->%.2f\n",
                   frame, blitzBefore, blitzAfter,
                   phaseBefore, phaseAfter,
                   gateBefore, gateAfter);
        }
        (void)prevBlitzCounter;

        // Detect new fruits.
        std::list<Mortar::Entity*>::iterator it;
        for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
            if (seenFruits.insert(e).second) {
                LogNewFruit(frame, ++spawnNo, static_cast<Fruit*>(e));
            }
        }
        // Detect new bombs.
        for (Mortar::Entity* e = am->GetEntityFirst(1, it); e; e = am->GetEntityNext(1, it)) {
            if (seenBombs.insert(e).second) {
                LogNewBomb(frame, ++spawnNo, static_cast<Bomb*>(e));
            }
        }

        // Drop dead entities from our seen sets so the same allocator slot
        // can be re-flagged as "new" when ActorManager recycles it.
        for (std::set<Mortar::Entity*>::iterator sit = seenFruits.begin(); sit != seenFruits.end(); ) {
            Mortar::Entity* e = *sit;
            if (!e->IsActive()) {
                printf("[KILL-FRUIT @f=%d] entity=%p died\n", frame, (void*)e);
                std::set<Mortar::Entity*>::iterator del = sit++;
                seenFruits.erase(del);
            } else {
                ++sit;
            }
        }
        for (std::set<Mortar::Entity*>::iterator sit = seenBombs.begin(); sit != seenBombs.end(); ) {
            Mortar::Entity* e = *sit;
            if (!e->IsActive()) {
                printf("[KILL-BOMB  @f=%d] entity=%p died\n", frame, (void*)e);
                std::set<Mortar::Entity*>::iterator del = sit++;
                seenBombs.erase(del);
            } else {
                ++sit;
            }
        }

        // Periodic trace: dump live fruit/bomb pos+vel so we can see if
        // they're rising/falling and where they get stuck.
        if (traceInterval > 0 && (frame % traceInterval) == 0) {
            int procP0 = wm->IsWaveProcessing(0) ? 1 : 0;
            int fruitsAlive = (int)seenFruits.size();
            int bombsAlive  = (int)seenBombs.size();

            // Probe each sub-counter so we can see which one keeps
            // IsWaveProcessing latched true.
            int fruitsInactive = Fruit::GetNumActiveForPlayer(-1, false);  // counts !IsActive
            int bombsCountdown = Bomb::GetNumActiveForPlayer(-1, false);   // counts m_Countdown>0 && !m_bHit
            int amFruitsRaw    = am->GetNumEntities(0);
            int amBombsRaw     = am->GetNumEntities(1);

            // Walk the raw fruit list to break down flag states.
            int liveFruits = 0, inactiveFruits = 0, killedFruits = 0;
            {
                std::list<Mortar::Entity*>::iterator pit;
                for (Mortar::Entity* pe = am->GetEntityFirst(0, pit); pe; pe = am->GetEntityNext(0, pit)) {
                    if (!pe->IsActive()) {
                        if (pe->flags & 0x10) ++killedFruits;
                        else                   ++inactiveFruits;
                    } else {
                        ++liveFruits;
                    }
                }
            }
            WAVE_INFO* w = wm->m_pCurrentWave[0];
            printf("[TRACE @f=%d] waveProc=%d curWave=idx%d wfProc=%d wfEnt=%d"
                   " | amFruits=%d (live=%d inact=%d killed=%d) GnaInact=%d"
                   " | amBombs=%d GnaCountdown=%d\n",
                   frame, procP0, WaveIdx(w),
                   w ? (int)w->m_bWaitForProcessing : -1,
                   w ? (int)w->m_bWaitForEntities  : -1,
                   amFruitsRaw, liveFruits, inactiveFruits, killedFruits, fruitsInactive,
                   amBombsRaw, bombsCountdown);
            (void)fruitsAlive; (void)bombsAlive;
            for (std::set<Mortar::Entity*>::iterator sit = seenFruits.begin(); sit != seenFruits.end(); ++sit) {
                Fruit* f = static_cast<Fruit*>(*sit);
                printf("[TRACE @f=%d]   fruit=%p type=%d pos=(%.1f,%.1f) vel=(%.2f,%.2f) sliced=%d active=%d\n",
                       frame, (void*)f, (int)f->m_FruitType,
                       f->pos.x, f->pos.y, f->vel.x, f->vel.y,
                       (int)f->m_bSliced, (int)f->IsActive());
            }
            for (std::set<Mortar::Entity*>::iterator sit = seenBombs.begin(); sit != seenBombs.end(); ++sit) {
                Bomb* b = static_cast<Bomb*>(*sit);
                printf("[TRACE @f=%d]   bomb =%p pos=(%.1f,%.1f) vel=(%.2f,%.2f) active=%d\n",
                       frame, (void*)b,
                       b->pos.x, b->pos.y, b->vel.x, b->vel.y,
                       (int)b->IsActive());
            }
        }
    }

    printf("[DONE] frames=%d spawnsLogged=%d finalWave=idx%d\n",
           frameCount, spawnNo, WaveIdx(wm->m_pCurrentWave[0]));

    return h.Shutdown();
}
