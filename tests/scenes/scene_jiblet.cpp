// scene_jiblet.cpp -- pomegranate jiblet spawn/fling render test.
//
// Renders the Jiblet entity (entity type 5) the super-fruit finale flings when
// the pomegranate explodes. This test replicates the exact 8-jiblet radial-fan
// call convention of SuperFruitControl::SpawnJibs @0x001bc748 (the JibletModel
// mesh-actor block):
//   - model  = MeshManager::Load("models/fruit/pomegranate_jiblet.mmd")
//              (the same path SuperFruitControl::LoadContent @0x001bda74 loads
//              into the file-static JibletModel SmartPtr)
//   - 8 jibs, one per 45-degree sector: ang = uniform((i+0.2)*45, (i+0.8)*45)
//     plus a shared random base offset uniform(0, 45)
//   - Jiblet::Init(fruitType, origin, uniform(0.8,1.25),
//                  dir * uniform(500,900), model,
//                  StringHash("<fruitModel>_jiblet"), dripRate, dir * 45)
//   - dripRate = IsFastHardware() ? 50 : 20 (drives the SplatEntity drip loop
//     in Jiblet::Update @0x001e5330)
//
// fruitType is Fruit::FruitType("super_pomegranate") -- the finale's host type.
//
// Jiblet timing notes (Jiblet::Init @0x001e50c0 / Update @0x001e5330):
//   - m_Age is seeded to -0.04, so the first ~2 ticks skip integration; the
//     jibs sit at the origin until age crosses 0, then fling outward.
//   - Draw @0x001e5750 has no state gate (only requires m_pModel), so the jibs
//     render from the very first frame.
//   - Bounds kill at |x| > 288 or |y| > 192; at <= 900 u/s the capture window
//     below stays well inside.
//
// Captures 2 frames:
//   just_spawned -- immediately after the spawn loop, before any tick (all 8
//                   jibs overlapping at the origin). Asserts non-background
//                   pixels (Draw has no state gate).
//   mid_flight   -- after TICKS_TO_MID fixed ticks (radial fan spread out).
//                   Asserts non-background pixels.
//
// Assertions:
//   SPAWN: GetNumEntities(5) == 8 immediately after the spawn loop.
//   ALIVE: GetNumEntities(5) == 8 after ticking (none bounds-killed) and at
//          least one jib has moved off the spawn origin.
//   DRAW:  just_spawned and mid_flight captures each have >= MIN_DRAWN_PIXELS
//          non-background pixels.
//
// Port specific: standalone jiblet spawn/fling/draw regression test.
//
// Run:
//   ctest -R scene_jiblet --output-on-failure
//   ./build/host/tests/scenes/scene_jiblet.exe --interactive
//
// Screenshots: tmp/test/screenshots/jiblet/{just_spawned,mid_flight}.png

#include "../test_harness.h"
#include "entities/Jiblet.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "render/gl_funcs.h"
#include "engine/asset/MeshManager.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "math/_Vector3.h"
#include "util/StringHash.h"
#include "Game.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <list>

// Background clear colour: dark wood grey (same as scene_coin's BG so the
// pomegranate jiblet's red flesh material reads clearly against it).
static const unsigned char BG_R         = 33;
static const unsigned char BG_G         = 26;
static const unsigned char BG_B         = 20;

// A pixel counts as non-background if it differs from BG_* by more than
// BG_TOLERANCE in ANY single channel (max, not summed, across R/G/B). See
// scene_fruit_splat.cpp for why: summing the abs diff over 3 channels and
// requiring the total to exceed 30 let a real but faint tint sit under the
// combined threshold even though every channel had visibly shifted, silently
// undercounting a well-formed blob. Per-channel max with a small tolerance
// (absorbs float->8-bit blend rounding, a couple of LSBs -- glReadPixels
// returns raw framebuffer bytes here, no PNG/JPEG requantisation) fixes that.
static const int BG_TOLERANCE = 8;

// Minimum non-background pixels to consider a capture "drawn". The jiblet
// mesh is opaque (unlit red pomegranate-flesh material, not alpha-blended
// juice), so it was never subject to the low-alpha undercount above -- 8
// jibs at 0.8-1.25x scale should trivially clear the old 50px floor already.
// Raised to 200 anyway since 50 was never a meaningful regression floor for
// 8 opaque mesh fragments; kept conservative (no asset-footprint RE performed
// for pomegranate_jiblet.mmd) rather than asserting a precise expected count.
static const int MIN_DRAWN_PIXELS = 200;

// Fixed dt (1/60 s = one simulation frame).
static const float TICK_DT = 1.0f / 60.0f;

// Ticks before the mid_flight capture. m_Age starts at -0.04 (~2-3 ticks of
// integration skipped), so 10 ticks gives ~7 integration steps: at <= 900 u/s
// the fastest jib travels ~105 units -- a visible fan, still inside the
// [-288,288]x[-192,192] bounds-kill box.
static const int TICKS_TO_MID = 10;

// The finale flings exactly 8 jibs (SuperFruitControl::SpawnJibs @0x001bc748).
static const int JIB_COUNT = 8;

// uniformRange(a,b): uniform float in [a, b) -- mirrors SuperFruitControl.cpp's
// file-static SuperFruitUniform helper used by SpawnJibs.
static inline float JibUniform(float a, float b) {
    return a + Math::g_Random.RandF(b - a);
}

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int dr = abs((int)r - (int)BG_R);
    int dg = abs((int)g - (int)BG_G);
    int db = abs((int)b - (int)BG_B);
    int maxDiff = dr > dg ? dr : dg;
    if (db > maxDiff) maxDiff = db;
    return maxDiff <= BG_TOLERANCE;
}

static int CountNonBackground(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if (!IsBackground(px[0], px[1], px[2])) ++count;
    }
    return count;
}

// Max distance-from-origin over the jib pool (type 5) -- movement liveness.
static float MaxJibDistFromOrigin(Mortar::ActorManager* am) {
    float maxD = 0.0f;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(5, it);
    while (e) {
        float d = sqrtf(e->pos.x * e->pos.x + e->pos.y * e->pos.y);
        if (d > maxD) maxD = d;
        e = am->GetEntityNext(5, it);
    }
    return maxD;
}

// Advance every pooled entity by one fixed simulation tick (mirrors
// scene_coin's TickCoins).
static void TickJiblets(Mortar::ActorManager* am, float dt) {
    game_work.dt = dt;
    am->Update(dt);
}

// Render one frame: clear to BG_*, standard perspective, draw all pooled
// entities via ActorManager::Draw (same depth state as scene_coin --
// pomegranate_jiblet.mmd is a normal depth-written 3D mesh).
static void RenderJibletFrame(fn::TestHarness& h) {
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
    dm.SetDepthBuffer(false);
}

// Warm-up + measurement frame, screenshot, and non-background pixel count
// for one capture point (mirrors scene_coin's CaptureFrame).
static int CaptureFrame(fn::TestHarness& h, const char* name) {
    RenderJibletFrame(h);
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    RenderJibletFrame(h); // measurement frame, no swap -- glReadPixels sees this

    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    int drawn = pixels ? CountNonBackground(pixels, fw, fh) : 0;
    free(pixels);

    h.ScreenshotPng(name);
    return drawn;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone jiblet spawn/fling/draw regression test.

    fn::TestHarness h(argc, argv, "scene_jiblet");
    h.SetInteractiveDefault(false);
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    // GameInit wires ActorManager pools (incl. type 5 = Jiblet), FruitCamera,
    // and the FruitInfo tables.
    GameInit(0);
    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "[scene_jiblet] FAIL: actorManager null\n");
        return 1;
    }

    // The finale's host fruit type: super_pomegranate.
    int fruitType = Fruit::FruitType("super_pomegranate", false);
    if (fruitType < 0) {
        fprintf(stderr, "[scene_jiblet] FAIL: 'super_pomegranate' not in FruitInfo\n");
        return 1;
    }
    const FruitInfo* fi = Fruit::FruitInfo((long)fruitType);
    const char* modelName = fi->m_ModelName;

    // The jiblet mesh SuperFruitControl::LoadContent @0x001bda74 loads into the
    // file-static JibletModel SmartPtr (not externally reachable; load the same
    // asset here).
    Mortar::SmartPtr<Mortar::Model> jibletModel;
    if (Mortar::MeshManager::GetInstance()) {
        jibletModel = Mortar::MeshManager::GetInstance()->Load(
            "models/fruit/pomegranate_jiblet.mmd");
    }
    if (!jibletModel.Get()) {
        fprintf(stderr, "[scene_jiblet] FAIL: models/fruit/pomegranate_jiblet.mmd "
                        "failed to load\n");
        return 1;
    }

    // --- Spawn: exact call convention of the SpawnJibs @0x001bc748 jiblet fan.
    {
        Game* g = Game::GetInstance();
        float dripRate = (g && g->IsFastHardware()) ? 50.0f : 20.0f;
        float angBase = JibUniform(0.0f, 45.0f);
        char jb[64];
        snprintf(jb, sizeof(jb), "%s_jiblet", modelName);
        uint32_t jh = StringHash(jb);

        _Vector3<float> origin(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < JIB_COUNT; ++i) {
            Jiblet* j = static_cast<Jiblet*>(am->Add(5, true));
            if (!j) continue;
            float ang = JibUniform((i + 0.2f) * 45.0f, (i + 0.8f) * 45.0f);
            uint16_t a16 = (uint16_t)((int)((angBase + ang) * 182.0f) & 0xffff);
            _Vector3<float> dir(CosIdx(a16), SinIdx(a16), 0.0f);
            j->Init(fruitType, origin,
                    JibUniform(0.8f, 1.25f),
                    dir * JibUniform(500.0f, 900.0f),
                    jibletModel, jh, dripRate, dir * 45.0f);
        }
    }

    int spawnedCount = am->GetNumEntities(5);
    printf("[scene_jiblet] SPAWN: requested=%d spawned(pool count)=%d\n",
           JIB_COUNT, spawnedCount);
    bool spawnPass = (spawnedCount == JIB_COUNT);
    if (!spawnPass) {
        fprintf(stderr, "[scene_jiblet] FAIL (SPAWN): expected %d jibs in pool, got %d\n",
                JIB_COUNT, spawnedCount);
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
            TickJiblets(am, TICK_DT);
            RenderJibletFrame(h);
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
        return h.Shutdown();
    }

    // --- Headless: capture 2 frames along the fling ---

    // just_spawned: before any tick, all 8 jibs overlapping at the origin.
    // Jiblet::Draw has no state gate, so this frame must already draw.
    int justSpawnedPixels = CaptureFrame(h, "jiblet/just_spawned");
    printf("[scene_jiblet] just_spawned: drawnPixels=%d\n", justSpawnedPixels);

    // mid_flight: tick TICKS_TO_MID fixed frames, then capture the radial fan.
    for (int i = 0; i < TICKS_TO_MID; ++i) {
        TickJiblets(am, TICK_DT);
    }
    int aliveCount = am->GetNumEntities(5);
    float maxDist = MaxJibDistFromOrigin(am);
    int midFlightPixels = CaptureFrame(h, "jiblet/mid_flight");
    printf("[scene_jiblet] mid_flight: ticks=%d alive=%d maxDistFromOrigin=%.1f "
           "drawnPixels=%d\n",
           TICKS_TO_MID, aliveCount, maxDist, midFlightPixels);

    bool alivePass = (aliveCount == JIB_COUNT) && (maxDist > 1.0f);
    if (!alivePass) {
        fprintf(stderr, "[scene_jiblet] FAIL (ALIVE): alive=%d (expected %d) "
                        "maxDistFromOrigin=%.1f (expected > 1.0)\n",
                aliveCount, JIB_COUNT, maxDist);
    }

    bool drawPass = (justSpawnedPixels >= MIN_DRAWN_PIXELS) &&
                    (midFlightPixels >= MIN_DRAWN_PIXELS);
    if (!drawPass) {
        fprintf(stderr, "[scene_jiblet] FAIL (DRAW): just_spawned=%d mid_flight=%d "
                        "(min required = %d each)\n",
                justSpawnedPixels, midFlightPixels, MIN_DRAWN_PIXELS);
    }

    bool overallPass = spawnPass && alivePass && drawPass;
    printf("[scene_jiblet] SPAWN=%s ALIVE=%s DRAW=%s\n",
           spawnPass ? "PASS" : "FAIL",
           alivePass ? "PASS" : "FAIL",
           drawPass ? "PASS" : "FAIL");

    h.Shutdown();
    return overallPass ? 0 : 1;
}
