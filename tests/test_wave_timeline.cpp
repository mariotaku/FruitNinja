// test_wave_timeline -- fruit-spawn timeline diagnostic.
//
// Boots the game headless in the chosen mode, fires PrepareForLevelStart, then
// drives N sim-frames (default 3600 = 60s at 1/60) and logs what spawns each
// simulated second.
//
// NOT deterministic. The --seed value is applied to WaveManager::m_Random below,
// but PrepareForLevelStart -> WaveManager::Reset immediately reseeds m_Random
// from the wall-clock-seeded global stream, so every run draws a different
// timeline. Nothing here asserts on spawn content, so that is not a problem --
// but do not treat the log as a reproducible baseline.
//
// Output format:
//   MM:SS: <comma-separated spawn names this second>
//   --- Wave <idx> (<N> spawners) --- (on wave transitions)
//
// SUMMARY block at end shows total fruit, total bombs, total super-fruit, and an
// explicit "SUPER-FRUIT SPAWNED: <N>" line for grepping.
//
// The entire log + summary is printed to stdout and compared against nothing:
// this is a human-readable diagnostic, NOT a regression check.
//
// Returns 0 if the pipeline ran and at least 1 fruit spawned.
// Returns 1 if setup failed or zero fruit ever spawned (broken pipeline).
// That liveness check is the only assertion in the test.
//
// Usage:
//   test_wave_timeline                         -- classic, 3600 frames, seed 12345
//   test_wave_timeline --mode=arcade           -- arcade mode
//   test_wave_timeline --mode=zen              -- zen mode
//   test_wave_timeline --mode=combo            -- combo mode
//   test_wave_timeline --frames=7200           -- 120 seconds
//   test_wave_timeline --seed=99               -- (see note above: overwritten by Reset)
//   test_wave_timeline --interactive           -- visible window (watch it play)

#include "test_harness.h"
#include "game/WaveManager.h"
#include "game/WaveStructs.h"
#include "game/StartupEffects.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "entities/Bomb.h"
#include "entities/Entity.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "screens/MainScreen.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <vector>
#include <list>
#include <string>

// ---------------------------------------------------------------------------
// Arg parsing helpers
// ---------------------------------------------------------------------------

static int ParseGameMode(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--mode=", 7) == 0) {
            const char* m = argv[i] + 7;
            if (std::strcmp(m, "classic") == 0) return 0;
            if (std::strcmp(m, "combo")   == 0) return 1;
            if (std::strcmp(m, "arcade")  == 0) return 2;
            if (std::strcmp(m, "zen")     == 0) return 3;
            std::fprintf(stderr, "FAIL: unknown --mode=%s (use classic/combo/arcade/zen)\n", m);
            std::exit(1);
        }
    }
    return 0;  // classic default
}

static int ParseFrames(int argc, char** argv, int defaultVal) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--frames=", 9) == 0) {
            int n = std::atoi(argv[i] + 9);
            if (n > 0) return n;
        }
        if (i + 1 < argc && std::strcmp(argv[i], "--frames") == 0) {
            int n = std::atoi(argv[++i]);
            if (n > 0) return n;
        }
    }
    return defaultVal;
}

static int ParseSeed(int argc, char** argv, int defaultVal) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            return std::atoi(argv[i] + 7);
        }
        if (i + 1 < argc && std::strcmp(argv[i], "--seed") == 0) {
            return std::atoi(argv[++i]);
        }
    }
    return defaultVal;
}

static const char* ModeName(int gameMode) {
    switch (gameMode) {
        case 0: return "classic";
        case 1: return "combo";
        case 2: return "arcade";
        case 3: return "zen";
        default: return "?";
    }
}

// Returns the WAVE_INFO's m_WaveIndex or -1 if null.
static int WaveIdx(WAVE_INFO* w) {
    if (!w) return -1;
    return w->m_WaveIndex;
}

// ---------------------------------------------------------------------------
// Per-second spawn record
// ---------------------------------------------------------------------------

struct SpawnRecord {
    std::string name;  // e.g. "apple", "bomb", "watermelon [SUPER]"
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const int gameMode   = ParseGameMode(argc, argv);
    const int frameCount = ParseFrames(argc, argv, 3600);
    const int seed       = ParseSeed(argc, argv, 12345);

    char labelBuf[32];
    std::snprintf(labelBuf, sizeof(labelBuf), "wave_timeline_%s", ModeName(gameMode));

    fn::TestHarness h(argc, argv, labelBuf);
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // Verify HUD is live.
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after init\n");
        return 1;
    }

    WaveManager* wm = WaveManager::GetInstance();
    if (!wm) {
        std::fprintf(stderr, "FAIL: WaveManager null\n");
        return 1;
    }

    const int modeWaveCount = (int)wm->m_WaveInfo[gameMode].size();
    if (modeWaveCount == 0) {
        std::fprintf(stderr, "FAIL: %s wave list empty -- XML load failed?\n", ModeName(gameMode));
        return 1;
    }

    // Header.
    std::printf("=== WAVE TIMELINE: mode=%s seed=%d frames=%d (%ds) waveInfos=%d ===\n",
                ModeName(gameMode), seed, frameCount, frameCount / 60, modeWaveCount);

    // Seed the WaveManager RNG before the game-start call. NOTE: this does NOT
    // make the run reproducible -- PrepareForLevelStart below calls
    // WaveManager::Reset, which reseeds m_Random from Math::g_Random.Rand32(0)
    // (wall-clock-seeded), discarding this value. Kept because it is harmless
    // and mirrors the intent, but nothing downstream depends on it.
    wm->m_Random.Seed((unsigned int)seed);

    // Replicate test_spawn_lifecycle's setup: clear menu items, advance
    // MainScreen to STATE_CAMERA_FADE so it stops re-spawning menu bombs,
    // set game mode, then fire PrepareForLevelStart.
    ClearMenuItems();
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }
    game_work.gameMode = (uint8_t)gameMode;
    PrepareForLevelStart();
    game_work.bM_bPaused = 0;

    // Drain the physics of flung menu items before counting spawns.
    h.RunHeadless(120);

    WAVE_INFO* initialWave = wm->m_pCurrentWave[0];
    if (!initialWave) {
        std::fprintf(stderr, "FAIL: m_pCurrentWave[0] null after PrepareForLevelStart\n");
        return 1;
    }

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        std::fprintf(stderr, "FAIL: ActorManager null\n");
        return 1;
    }

    // Mark all currently-live entities as already-seen so we only log NEW spawns.
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

    // Log the initial wave.
    std::printf("00:00: --- Wave %d (%d spawners) ---\n",
                WaveIdx(initialWave),
                initialWave ? initialWave->m_SpawnerCount : 0);

    WAVE_INFO* prevWave = initialWave;

    // Per-second accumulator: spawns this second.
    std::vector<SpawnRecord> secondSpawns;
    int prevSecond = 0;

    // Totals for summary.
    int totalFruit = 0;
    int totalBombs = 0;
    int totalSuper = 0;

    // Everything printed by this loop is a stdout diagnostic for human reading;
    // it is not captured, not diffed against a baseline, and cannot fail the test.
    for (int frame = 1; frame <= frameCount; ++frame) {
        h.RunHeadless(1);

        const int currentSecond = frame / 60;

        // Wave-change detection.
        WAVE_INFO* curWave = wm->m_pCurrentWave[0];
        if (curWave != prevWave) {
            // Flush current second before printing wave banner.
            if (!secondSpawns.empty()) {
                std::printf("%02d:%02d:", prevSecond / 60, prevSecond % 60);
                for (int k = 0; k < (int)secondSpawns.size(); ++k) {
                    std::printf("%s%s", k == 0 ? " " : ", ", secondSpawns[k].name.c_str());
                }
                std::printf("\n");
                secondSpawns.clear();
                prevSecond = currentSecond;
            }
            std::printf("%02d:%02d: --- Wave %d (%d spawners) ---\n",
                        currentSecond / 60, currentSecond % 60,
                        WaveIdx(curWave),
                        curWave ? curWave->m_SpawnerCount : 0);
            prevWave = curWave;
        }

        // Flush accumulated spawns when second boundary crossed.
        if (currentSecond != prevSecond) {
            if (!secondSpawns.empty()) {
                std::printf("%02d:%02d:", prevSecond / 60, prevSecond % 60);
                for (int k = 0; k < (int)secondSpawns.size(); ++k) {
                    std::printf("%s%s", k == 0 ? " " : ", ", secondSpawns[k].name.c_str());
                }
                std::printf("\n");
                secondSpawns.clear();
            }
            prevSecond = currentSecond;
        }

        // Detect new fruits (type 0).
        {
            std::list<Mortar::Entity*>::iterator it;
            for (Mortar::Entity* e = am->GetEntityFirst(0, it); e; e = am->GetEntityNext(0, it)) {
                if (seenFruits.insert(e).second) {
                    // New fruit: read its type and check the super-fruit flag.
                    Fruit* f = static_cast<Fruit*>(e);
                    const char* typeName = Fruit::FruitTypeName((long)f->m_FruitType);
                    const FruitInfo* fi  = FruitInfo_Get((int)f->m_FruitType);
                    bool isSuper = (fi->m_bIsSuperFruit != 0);

                    SpawnRecord rec;
                    if (typeName && typeName[0] != '\0') {
                        rec.name = typeName;
                    } else {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "type%d", (int)f->m_FruitType);
                        rec.name = buf;
                    }
                    if (isSuper) {
                        rec.name += " [SUPER]";
                        ++totalSuper;
                    }
                    secondSpawns.push_back(rec);
                    ++totalFruit;
                }
            }
        }

        // Detect new bombs (type 1).
        {
            std::list<Mortar::Entity*>::iterator it;
            for (Mortar::Entity* e = am->GetEntityFirst(1, it); e; e = am->GetEntityNext(1, it)) {
                if (seenBombs.insert(e).second) {
                    SpawnRecord rec;
                    rec.name = "bomb";
                    secondSpawns.push_back(rec);
                    ++totalBombs;
                }
            }
        }

        // Prune dead entities from seen sets so recycled slots are detected again.
        {
            std::set<Mortar::Entity*>::iterator sit = seenFruits.begin();
            while (sit != seenFruits.end()) {
                if (!(*sit)->IsActive()) {
                    std::set<Mortar::Entity*>::iterator del = sit++;
                    seenFruits.erase(del);
                } else {
                    ++sit;
                }
            }
        }
        {
            std::set<Mortar::Entity*>::iterator sit = seenBombs.begin();
            while (sit != seenBombs.end()) {
                if (!(*sit)->IsActive()) {
                    std::set<Mortar::Entity*>::iterator del = sit++;
                    seenBombs.erase(del);
                } else {
                    ++sit;
                }
            }
        }
    }

    // Flush any remaining spawns from the last partial second.
    if (!secondSpawns.empty()) {
        std::printf("%02d:%02d:", prevSecond / 60, prevSecond % 60);
        for (int k = 0; k < (int)secondSpawns.size(); ++k) {
            std::printf("%s%s", k == 0 ? " " : ", ", secondSpawns[k].name.c_str());
        }
        std::printf("\n");
    }

    // Summary.
    std::printf("\n=== SUMMARY ===\n");
    std::printf("mode:              %s\n", ModeName(gameMode));
    std::printf("seed:              %d\n", seed);
    std::printf("frames:            %d\n", frameCount);
    std::printf("total fruit:       %d\n", totalFruit);
    std::printf("total bombs:       %d\n", totalBombs);
    std::printf("total super-fruit: %d\n", totalSuper);
    std::printf("SUPER-FRUIT SPAWNED: %d\n", totalSuper);

    // Return 1 (pipeline broken) if zero fruit spawned in the whole run.
    if (totalFruit == 0) {
        std::fprintf(stderr, "FAIL: zero fruit spawned -- spawn pipeline is broken\n");
        return 1;
    }

    return h.Shutdown();
}
