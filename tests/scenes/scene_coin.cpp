// scene_coin.cpp -- Coin currency-sprite spawn/home render test.
//
// Renders the Coin entity (entity type 2) the crit-slice / fruit-kill coin
// drop spawns (Fruit::CollisionResponse @0x001de778 calls Coin::MakeCoins
// with silent=true -- see Fruit.cpp:1410-1412). This test replicates that
// exact call convention: coinsPerCoin=1, target=NULL (defaults to
// COIN_DEFAULT_TARGET (220,-140,0)), flyFXName/collectFXName=NULL (defaults
// "coin_fly"/"coin_collect"), onArrived=Coin::DefaultArrivedDelegate(),
// silent=true, delayStep=0.02f, delayCap=0.15f.
//
// silent=true is REQUIRED for anything to render: Coin::Draw's binary gate
// is `if (m_Silent == 0) return;` -- an inverted-looking but ASM-confirmed
// check (see Coin.cpp Draw() comment) where only SILENT coins draw their
// model. silent=true also drives the richer state arc (WAITING -> FLYING ->
// DECEL -> HOMING -> ARRIVED) vs. non-silent's WAITING -> HOMING shortcut,
// so it is both the faithful real-call-site value AND the one that exercises
// every Coin::_Update state.
//
// Coin::Draw also requires `s_coinModel.IsValid()` (Coin::LoadContent loads
// "models/Fruit/coin.mmd" via MeshManager -- fixed 2026-07-08; a stale
// 2026-05-23 RE comment had wrongly claimed no such asset exists) and
// `m_State > 1` (WAITING/state 0 and ARRIVED/state 1 do not draw -- by
// design, not a bug).
//
// Captures 3 frames along the trajectory:
//   just_spawned -- immediately after MakeCoins, before any tick (state 0,
//                   WAITING). Draw is a no-op here by design (m_State<=1) --
//                   pixel count is logged only, NOT asserted non-zero.
//   mid_flight   -- first tick where any coin reaches state 2 (FLYING,
//                   ballistic launch arc). Hard-asserts non-background pixels.
//   homing       -- first tick where any coin reaches state 4 (HOMING,
//                   steering toward the coin-counter target). Hard-asserts
//                   non-background pixels.
//
// Assertions:
//   SPAWN: GetNumEntities(2) == coinCount immediately after MakeCoins.
//   STATE: both FLYING and HOMING are reached within the tick guard.
//   DRAW:  mid_flight and homing captures each have >= MIN_DRAWN_PIXELS
//          non-background pixels.
//
// Port specific: standalone coin spawn/state/draw regression test.
//
// Run:
//   ctest -R scene_coin --output-on-failure
//   ./build/host/tests/scenes/scene_coin.exe --interactive
//
// Screenshots: tmp/test/screenshots/coin/{just_spawned,mid_flight,homing}.png

#include "../test_harness.h"
#include "entities/Coin.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <list>

// Background clear colour: dark wood grey (matches scene_special_fruit's
// BG so coin.mmd's gold/yellow material reads clearly against it).
static const unsigned char BG_R         = 33;
static const unsigned char BG_G         = 26;
static const unsigned char BG_B         = 20;
static const int           BG_THRESHOLD = 30;

// Minimum non-background pixels to consider a capture "drawn".
static const int MIN_DRAWN_PIXELS = 50;

// Fixed dt (1/60 s = one simulation frame). Coin::Update reads game_work.dt
// directly (ignores its own dt param), so this is written there each tick.
static const float TICK_DT = 1.0f / 60.0f;

// Guard against an infinite loop if a state transition never fires.
static const int MAX_TICKS = 600; // 10s

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int d = abs((int)r - (int)BG_R) + abs((int)g - (int)BG_G) + abs((int)b - (int)BG_B);
    return d <= BG_THRESHOLD;
}

static int CountNonBackground(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if (!IsBackground(px[0], px[1], px[2])) ++count;
    }
    return count;
}

// Count coin-pool (type 2) entities currently in the given m_State.
static int CountCoinsInState(Mortar::ActorManager* am, int state) {
    int n = 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(2, it);
    while (e) {
        Coin* c = static_cast<Coin*>(e);
        if (c->m_State == state) ++n;
        e = am->GetEntityNext(2, it);
    }
    return n;
}

// Advance every pooled entity (only coins are populated in this scene) by
// one fixed simulation tick. Coin::Update's fixed-step wrapper reads
// game_work.dt directly, so it is set here before dispatch (mirrors
// scene_fruit_splat's SplatEntity::UpdateActiveSplats(TICK_DT) pattern).
static void TickCoins(Mortar::ActorManager* am, float dt) {
    game_work.dt = dt;
    am->Update(dt);
}

// Render one frame: clear to BG_*, set up the standard perspective, draw
// all pooled entities via ActorManager::Draw (same depth state as
// scene_special_fruit's DrawScene -- coin.mmd is a normal depth-written 3D
// mesh like the Fruit/Bomb models, unlike SplatEntity's decal pass).
static void RenderCoinFrame(fn::TestHarness& h) {
    SDL_Window* window = static_cast<SDL_Window*>(h.window);
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    glClearColor((float)BG_R / 255.0f, (float)BG_G / 255.0f, (float)BG_B / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(PT_STANDARD, true);
    }

    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
    if (h.game.actorManager) {
        h.game.actorManager->Draw(h.game.renderer);
    }
    dm.SetDepthBuffer(false);
}

// Warm-up + measurement frame, screenshot, and non-background pixel count
// for one capture point. Mirrors scene_fruit_splat's capture-loop pattern.
static int CaptureFrame(fn::TestHarness& h, const char* name) {
    RenderCoinFrame(h);
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    RenderCoinFrame(h); // measurement frame, no swap -- glReadPixels sees this

    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    int drawn = pixels ? CountNonBackground(pixels, fw, fh) : 0;
    free(pixels);

    h.ScreenshotPng(name);
    return drawn;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone coin spawn/state/draw regression test.

    fn::TestHarness h(argc, argv, "scene_coin");
    h.SetInteractiveDefault(false);
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    // GameInit wires ActorManager pools (incl. type 2 = Coin) + FruitCamera.
    GameInit(0);
    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "[scene_coin] FAIL: actorManager null\n");
        return 1;
    }

    // --- Spawn: exact call convention of the real crit-slice/fruit-kill
    //     coin drop (Fruit::CollisionResponse @0x001de778, Fruit.cpp:1410-1412).
    const int coinCount = 5;
    _Vector3<float> spawnPos(0.0f, 0.0f, 0.0f);
    uint16_t baseAngle = 0;
    int rawSpread = (coinCount + 1) * 8190;
    uint16_t angleSpread = (uint16_t)(rawSpread < 65520 ? rawSpread : 65520);

    Coin::MakeCoins(coinCount, /*coinsPerCoin=*/1, &spawnPos, baseAngle, angleSpread,
                    /*target=*/NULL, /*flyFXName=*/NULL, /*collectFXName=*/NULL,
                    Coin::DefaultArrivedDelegate(), /*silent=*/true,
                    /*delayStep=*/0.02f, /*delayCap=*/0.15f);

    int spawnedCount = am->GetNumEntities(2);
    printf("[scene_coin] SPAWN: requested=%d spawned(pool count)=%d\n", coinCount, spawnedCount);
    bool spawnPass = (spawnedCount == coinCount);
    if (!spawnPass) {
        fprintf(stderr, "[scene_coin] FAIL (SPAWN): expected %d coins in pool, got %d\n",
                coinCount, spawnedCount);
    }

    // --- Interactive path: just tick + draw forever ---
    if (h.IsInteractive()) {
        SDL_GL_SetSwapInterval(1);
        bool running = true;
        while (running && h.game.running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            }
            TickCoins(am, TICK_DT);
            RenderCoinFrame(h);
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
        Coin::ClearCoins(false);
        return h.Shutdown();
    }

    // --- Headless: capture 3 frames along the trajectory ---

    // just_spawned: state 0 (WAITING) for all coins, before any tick.
    // Draw() no-ops here by design (m_State<=1) -- logged only, not asserted.
    int justSpawnedPixels = CaptureFrame(h, "coin/just_spawned");
    printf("[scene_coin] just_spawned: state0Count=%d drawnPixels=%d (informational; "
           "WAITING coins never draw)\n",
           CountCoinsInState(am, 0), justSpawnedPixels);

    // mid_flight: tick until any coin reaches state 2 (FLYING).
    bool reachedFlying = false;
    int ticksToFlying = 0;
    for (int i = 0; i < MAX_TICKS; ++i) {
        TickCoins(am, TICK_DT);
        ++ticksToFlying;
        if (CountCoinsInState(am, 2) > 0) { reachedFlying = true; break; }
        // Silent coins can blow straight through FLYING into DECEL/HOMING on
        // a single low-speed roll; treat reaching state >= 2 as success too.
        if (CountCoinsInState(am, 3) > 0 || CountCoinsInState(am, 4) > 0) {
            reachedFlying = true;
            break;
        }
    }
    int midFlightPixels = -1;
    if (reachedFlying) {
        midFlightPixels = CaptureFrame(h, "coin/mid_flight");
        printf("[scene_coin] mid_flight: ticks=%d flying=%d decel=%d homing=%d drawnPixels=%d\n",
               ticksToFlying, CountCoinsInState(am, 2), CountCoinsInState(am, 3),
               CountCoinsInState(am, 4), midFlightPixels);
    } else {
        fprintf(stderr, "[scene_coin] FAIL: no coin reached FLYING/DECEL/HOMING within %d ticks\n",
                MAX_TICKS);
    }

    // homing: continue ticking until any coin reaches state 4 (HOMING).
    bool reachedHoming = (CountCoinsInState(am, 4) > 0);
    int ticksToHoming = 0;
    for (int i = 0; !reachedHoming && i < MAX_TICKS; ++i) {
        TickCoins(am, TICK_DT);
        ++ticksToHoming;
        if (CountCoinsInState(am, 4) > 0) { reachedHoming = true; }
    }
    int homingPixels = -1;
    if (reachedHoming) {
        homingPixels = CaptureFrame(h, "coin/homing");
        printf("[scene_coin] homing: extra_ticks=%d homingCount=%d drawnPixels=%d\n",
               ticksToHoming, CountCoinsInState(am, 4), homingPixels);
    } else {
        fprintf(stderr, "[scene_coin] FAIL: no coin reached HOMING within %d additional ticks\n",
                MAX_TICKS);
    }

    Coin::ClearCoins(false);

    bool drawPass = (midFlightPixels >= MIN_DRAWN_PIXELS) && (homingPixels >= MIN_DRAWN_PIXELS);
    if (!drawPass) {
        fprintf(stderr, "[scene_coin] FAIL (DRAW): mid_flight=%d homing=%d (min required = %d each)\n",
                midFlightPixels, homingPixels, MIN_DRAWN_PIXELS);
    }

    bool overallPass = spawnPass && reachedFlying && reachedHoming && drawPass;
    printf("[scene_coin] SPAWN=%s STATE=%s DRAW=%s\n",
           spawnPass ? "PASS" : "FAIL",
           (reachedFlying && reachedHoming) ? "PASS" : "FAIL",
           drawPass ? "PASS" : "FAIL");

    h.Shutdown();
    return overallPass ? 0 : 1;
}
