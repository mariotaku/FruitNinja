// scene_coin.cpp -- Coin currency-drop spawn/state/particle test.
//
// Renders the Coin entity (entity type 2) the crit-slice / fruit-kill coin
// drop spawns (Fruit::CollisionResponse @0x001de778 calls Coin::MakeCoins
// with silent=true -- see Fruit.cpp:1410-1412). This test replicates that
// exact call convention: coinsPerCoin=1, target=NULL (defaults to
// COIN_DEFAULT_TARGET (220,-140,0)), flyFXName/collectFXName=NULL (defaults
// "coin_fly"/"coin_collect"), onArrived=Coin::DefaultArrivedDelegate(),
// silent=true, delayStep=0.02f, delayCap=0.15f.
//
// silent=true drives the full state arc (WAITING -> FLYING -> DECEL -> HOMING
// -> ARRIVED) vs. non-silent's WAITING -> HOMING shortcut, so it is both the
// faithful real-call-site value AND the one that exercises every
// Coin::_Update state.
//
// --------------------------------------------------------------------------
// WHAT ACTUALLY PUTS COIN PIXELS ON SCREEN (v1.6.1): PARTICLES, NOT A MODEL.
// --------------------------------------------------------------------------
// v1.6.1 Coin::Draw @0x001d8810 is MODEL-ONLY: the whole body is the m_Silent /
// s_coinModel / m_State gates followed by one `bl` to Mortar::Model::Draw. No
// quad, no texture bind, no 2D path. And s_coinModel @0x003328c0 is never
// assigned in v1.6.1 -- v1.6.1 Coin::LoadContent @0x001d7920 is six
// instructions that set s_isContentLoaded and load nothing. So Coin::Draw can
// never emit a pixel, and ActorManager::Draw is NOT this entity's render path.
//
// Provenance note, because this file previously said the opposite: an earlier
// header here claimed Coin::LoadContent loads "models/Fruit/coin.mmd" via
// MeshManager and that a 2026-05-23 RE comment denying the asset was stale.
// That is inverted -- the 2026-05-23 reading was CORRECT and the 2026-07-08
// "fix" was the error. The port's coin.mmd load has been removed;
// src/entities/Coin.cpp:62-71 carries the accurate RE note for s_coinModel.
//
// The coin's real on-screen presence is PSPParticleManager emitters spawned in
// v1.6.1 Coin::_Update @0x001d81bc: a trail emitter at +0x68 repositioned every
// frame via m_DirSin/m_DirCos, plus one-shot sparkle bursts at +0x6c on launch
// (state 3) and arrival (state 4). Those live in the PARTICLE draw pass, so
// this harness must tick and draw PSPParticleManager -- ActorManager::Draw
// alone renders nothing at all for a coin.
//
// FX names come from the call site. BonusScreen::AwardScores passes
// "bonus_star_trail" / "bonus_star_impact" (silent=false); the IN-GAME path
// this test replicates (Fruit::CollisionResponse, SlashEntity::Update) passes
// NULL, which MakeCoins substitutes with "coin_fly" / "coin_collect".
// "coin_fly" DOES NOT EXIST in particles_fast.xml or particles_slow.xml, so
// the in-game coin has NO TRAIL -- it is completely invisible during its
// ballistic FLYING arc. Its only visual is the "coin_collect" burst
// (5x sparkles_coins_burst on sparkle_32 + 1x coins_shine on coin_shine.tex),
// emitted ONCE when the DECEL timer crosses 0.01s and ONCE more on the HOMING
// arrival branch -- not per tick.
//
// Confidence: binary disassembly + the shipped particle XML. NOT
// runtime-confirmed -- an HLE session was attempted but peek/screenshot both
// hung, so s_coinModel was never read live. The "coin_fly" conclusion is
// positive evidence, not absence: it is missing from both particle XMLs while
// "coin_collect", "bonus_star_trail" and "bonus_star_impact" are all present
// and fully defined.
//
// --------------------------------------------------------------------------
// Captures 3 frames along the trajectory:
//   just_spawned -- immediately after MakeCoins, before any tick (state 0,
//                   WAITING). Nothing has been emitted yet -- pixel count is
//                   logged only, NOT asserted.
//   mid_flight   -- first tick where any coin reaches state 2 (FLYING).
//                   EXPECTED TO BE EMPTY: no model, and "coin_fly" has no
//                   emitter template, so the coin is genuinely invisible here.
//                   Logged only, NOT asserted -- asserting pixels here is what
//                   the old MIN_DRAWN_PIXELS=50 check did, and it only ever
//                   passed because the port wrongly loaded coin.mmd.
//   homing       -- first tick where any coin reaches state 4 (HOMING). This
//                   is the first frame the "coin_collect" burst exists.
//                   Hard-asserted (see PARTICLE / DRAW below).
//
// Assertions:
//   SPAWN:    GetNumEntities(2) == coinCount immediately after MakeCoins.
//   STATE:    both FLYING and HOMING are reached within the tick guard.
//   PARTICLE: the homing capture draws >= MIN_HOMING_PARTICLES particles,
//             summed over the three GameDraw depth layers.
//   DRAW:     the homing capture has >= MIN_HOMING_PIXELS non-background
//             pixels (proves the particles reached the framebuffer, not just
//             the live-list).
//
// Port specific: standalone coin spawn/state/particle regression test.
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
#include "particle/PSPParticleManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <list>

// Background clear colour: dark wood grey (matches scene_special_fruit's BG
// so the gold sparkle/shine particles read clearly against it).
static const unsigned char BG_R         = 33;
static const unsigned char BG_G         = 26;
static const unsigned char BG_B         = 20;
static const int           BG_THRESHOLD = 30;

// Minimum particles the homing capture must draw, summed over the three
// GameDraw depth layers.
//
// Floor = ONE complete "coin_collect" emission. particles_fast.xml defines
// coin_collect as two particleSets with init counts 5 (sparkles_coins_burst)
// and 1 (coins_shine) and perSec=0, so a single emitter instantiation yields
// exactly 6 particles. The burst is a ONE-SHOT per coin (DECEL timer crossing
// 0.01s), fired 1-3 ticks before that coin reaches HOMING, so the homing
// capture sees the still-live bursts of however many coins have already
// decelerated -- at minimum the one that triggered the capture. 6 is therefore
// both the floor and a meaningful one: nothing else in this scene emits
// particles, so a regression drops it to 0, never to 1..5.
static const int MIN_HOMING_PARTICLES = 6;

// Minimum non-background pixels in the homing capture. Secondary check: it
// proves the particles reached the framebuffer rather than only the live-list
// (missing texture, wrong layer, culled projection).
//
// Sized well under one sparkle quad so it never becomes the limiting factor:
// sparkles_coins_burst starts at size 20-25 game units, and the default
// 960x640 drawable maps the 480x320-unit view at ~2 px/unit, so one particle
// covers roughly a 40x40 px quad -- of which the sparkle_32 star art is only
// partly opaque. 100 px is a fraction of a single particle's opaque area, and
// zero emission gives exactly 0 here because the coin has no other visual
// (Coin::Draw @0x001d8810 can never fire -- see the header note).
static const int MIN_HOMING_PIXELS = 100;

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

// Advance the simulation by one fixed tick, in GameUpdate's order:
// ActorManager::Update (drives Coin::Update -> Coin::_Update, which adds and
// re-adds the "coin_collect" emitter) and THEN PSPParticleManager::Update.
//
// The order is load-bearing, not cosmetic. Coin::_Update tears its collect
// emitter down at the TOP of every call and re-adds it inside states 3/4, so
// the emitter only ever exists across the gap between two coin updates. If
// PSPParticleManager::Update does not run inside that gap, the emitter is
// recycled before its spawn pass and NO particle is ever created -- the scene
// renders empty even though the port is correct. GameUpdate has the same
// ordering (src/game/GameInit.cpp: actorManager->Update, then the
// PSPParticleManager block in the common tail).
//
// Coin::Update's fixed-step wrapper reads game_work.dt directly rather than
// its dt param, so that is set here before dispatch.
static void TickCoins(Mortar::ActorManager* am, float dt) {
    game_work.dt = dt;
    am->Update(dt);
    PSPParticleManager::GetInstance().Update(dt, false);
}

// Render one frame: clear to BG_*, set up the standard perspective, run
// ActorManager::Draw, then the particle pass. Returns the number of particles
// drawn this frame (summed over the three depth layers).
//
// ActorManager::Draw is kept because it is the faithful call sequence, but for
// a coin it produces nothing -- Coin::Draw @0x001d8810 is model-only and
// s_coinModel is permanently null. Every coin pixel comes from the
// PSPParticleManager pass below.
//
// Layer order and depth state mirror GameDraw @0x001cd720: depth write off
// after the actor pass, pm.Draw(-1) behind, then depth test off and
// pm.Draw(0) / pm.Draw(1). All three layers are issued rather than just the
// coin templates' drawOrder=0 so the test does not silently miss a template
// whose depth layer changes.
//
// `dt` is the amount the particle pass advances by. PSPParticleManager::Draw
// is a FUSED integrate+render (see PSPParticleManager.h), so a re-render of
// the same frame must pass dt=0 or it double-advances every particle.
static int RenderCoinFrame(fn::TestHarness& h, float dt) {
    SDL_Window* window = static_cast<SDL_Window*>(h.window);
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    glClearColor((float)BG_R / 255.0f, (float)BG_G / 255.0f, (float)BG_B / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);
    }

    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
    if (h.game.actorManager) {
        h.game.actorManager->Draw(h.game.renderer);
    }

    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    dm.SetDepthBufferWrite(false);
    pm.Draw(dt, false, -1);
    int drawnParticles = pm.GetDrawnParticleCount();
    dm.SetDepthBuffer(false);
    pm.Draw(dt, false, 0);
    drawnParticles += pm.GetDrawnParticleCount();
    pm.Draw(dt, false, 1);
    drawnParticles += pm.GetDrawnParticleCount();

    return drawnParticles;
}

// Warm-up + measurement frame, screenshot, non-background pixel count and
// drawn-particle count for one capture point. Mirrors scene_fruit_splat's
// capture-loop pattern; the measurement frame passes dt=0 so re-rendering the
// same simulation state does not advance the particles a second time.
static int CaptureFrame(fn::TestHarness& h, const char* name, int* outParticles) {
    RenderCoinFrame(h, TICK_DT);
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    int particles = RenderCoinFrame(h, 0.0f); // measurement frame, no swap

    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    int drawn = pixels ? CountNonBackground(pixels, fw, fh) : 0;
    free(pixels);

    h.ScreenshotPng(name);
    if (outParticles) *outParticles = particles;
    return drawn;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone coin spawn/state/particle regression test.

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

    Coin::MakeCoins(coinCount, /*coinsPerCoin=*/1, spawnPos, baseAngle, angleSpread,
                    /*target=*/NULL, /*delayStep=*/0.02f, /*delayCap=*/0.15f,
                    /*flyFXName=*/NULL, /*collectFXName=*/NULL,
                    Coin::DefaultArrivedDelegate(), /*silent=*/true);

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
            RenderCoinFrame(h, TICK_DT);
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
        Coin::ClearCoins(false);
        return h.Shutdown();
    }

    // --- Headless: capture 3 frames along the trajectory ---

    // just_spawned: state 0 (WAITING) for all coins, before any tick. Nothing
    // has been emitted yet -- logged only, not asserted.
    int justSpawnedParticles = 0;
    int justSpawnedPixels = CaptureFrame(h, "coin/just_spawned", &justSpawnedParticles);
    printf("[scene_coin] just_spawned: state0Count=%d particles=%d drawnPixels=%d "
           "(informational; nothing emitted yet)\n",
           CountCoinsInState(am, 0), justSpawnedParticles, justSpawnedPixels);

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
    int midFlightParticles = 0;
    if (reachedFlying) {
        // Informational only. The in-game coin is INVISIBLE while FLYING: no
        // model (s_coinModel is permanently null) and no trail (the default
        // "coin_fly" FX name has no emitter template in either particle XML).
        // Zero here is the correct, binary-faithful result.
        midFlightPixels = CaptureFrame(h, "coin/mid_flight", &midFlightParticles);
        printf("[scene_coin] mid_flight: ticks=%d flying=%d decel=%d homing=%d "
               "particles=%d drawnPixels=%d (informational; no model + no "
               "\"coin_fly\" emitter -> expected empty)\n",
               ticksToFlying, CountCoinsInState(am, 2), CountCoinsInState(am, 3),
               CountCoinsInState(am, 4), midFlightParticles, midFlightPixels);
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
    int homingParticles = 0;
    if (reachedHoming) {
        // The "coin_collect" burst was emitted 1-3 ticks earlier, when this
        // coin's DECEL timer crossed 0.01s; TickCoins' PSPParticleManager::Update
        // ran the spawn pass in that same tick and the particles (life 30 / 10)
        // are still alive here.
        homingPixels = CaptureFrame(h, "coin/homing", &homingParticles);
        printf("[scene_coin] homing: extra_ticks=%d homingCount=%d particles=%d drawnPixels=%d\n",
               ticksToHoming, CountCoinsInState(am, 4), homingParticles, homingPixels);
    } else {
        fprintf(stderr, "[scene_coin] FAIL: no coin reached HOMING within %d additional ticks\n",
                MAX_TICKS);
    }

    Coin::ClearCoins(false);

    bool particlePass = (homingParticles >= MIN_HOMING_PARTICLES);
    if (!particlePass) {
        fprintf(stderr, "[scene_coin] FAIL (PARTICLE): homing drew %d particles, need >= %d "
                "(one complete \"coin_collect\" burst = 5 sparkles_coins_burst + 1 coins_shine)\n",
                homingParticles, MIN_HOMING_PARTICLES);
    }

    bool drawPass = (homingPixels >= MIN_HOMING_PIXELS);
    if (!drawPass) {
        fprintf(stderr, "[scene_coin] FAIL (DRAW): homing=%d non-background pixels, need >= %d\n",
                homingPixels, MIN_HOMING_PIXELS);
    }

    bool overallPass = spawnPass && reachedFlying && reachedHoming && particlePass && drawPass;
    printf("[scene_coin] SPAWN=%s STATE=%s PARTICLE=%s DRAW=%s\n",
           spawnPass ? "PASS" : "FAIL",
           (reachedFlying && reachedHoming) ? "PASS" : "FAIL",
           particlePass ? "PASS" : "FAIL",
           drawPass ? "PASS" : "FAIL");

    h.Shutdown();
    return overallPass ? 0 : 1;
}
