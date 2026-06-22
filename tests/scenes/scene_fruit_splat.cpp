// scene_fruit_splat.cpp -- splat spawn + draw regression test.
//
// Slices a single watermelon with SplatEntity::s_RandKillEnabled=false so all
// random kills in MakeSplat are disabled, giving deterministic splat spawn on
// every run.  Ticks UpdateActiveSplats until splats land, then renders
// SplatEntity::DrawActiveSplats and reads back the framebuffer.
//
// Two independent assertions:
//   SPAWN: after slicing 1 fruit with random kills off, alive splat count >= 1.
//          Failure here means a real suppression bug (alpha=0, FruitInfo parse, etc.).
//   DRAW:  after rendering, non-background pixels in the frame > a minimum.
//          Failure with SPAWN passing means a draw-state regression.
//
// Background: bright green (0,255,0) -- splat juice is coloured, so any pixel
// with |R-0|+|G-255|+|B-0| > 30 is considered non-background.
//
// Port specific: standalone splat spawn+draw regression test
//
// Run:
//   ctest -R fruit_splat -C Debug --output-on-failure
//   ./build/host/tests/scenes/Debug/scene_fruit_splat.exe --interactive
//
// Screenshot: tmp/test/screenshots/scene_fruit_splat.ppm

#include "../test_harness.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "entities/SplatEntity.h"
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
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#endif

// Background colour: bright green (R=0, G=255, B=0).
static const unsigned char BG_R = 0;
static const unsigned char BG_G = 255;
static const unsigned char BG_B = 0;
static const int BG_THRESHOLD   = 30;

// Minimum non-background pixels to consider the splat "drawn".
static const int MIN_DRAWN_PIXELS = 50;

// Fixed dt (1/60 s = one simulation frame).
static const float TICK_DT = 1.0f / 60.0f;

// Ticks to drive slice timer (base 0.03 s at 1/60 dt = 2 frames; use 10 for slack).
static const int SLICE_TICKS = 10;

// Ticks to let splats land (z drops past -50; 120 frames = 2 s at 1/60).
static const int LAND_TICKS  = 120;

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int diff = abs((int)r - (int)BG_R)
             + abs((int)g - (int)BG_G)
             + abs((int)b - (int)BG_B);
    return diff <= BG_THRESHOLD;
}

static int CountNonBackground(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* px = pixels + (y * w + x) * 3;
            if (!IsBackground(px[0], px[1], px[2])) ++count;
        }
    }
    return count;
}

// Count pool slots with m_bAlive set, regardless of SplatType.
static int CountAliveSplats() {
    int alive = 0;
    SplatEntity::ForEachInPool(
        [](SplatEntity* s, void* user) {
            int* p = static_cast<int*>(user);
            if (s && s->m_bAlive) ++(*p);
        },
        &alive);
    return alive;
}

// Count alive landed splats (m_SplatType >= 0) -- what DrawActiveSplats sees.
static int CountLandedSplats() {
    int n = 0;
    SplatEntity::ForEachInPool(
        [](SplatEntity* s, void* user) {
            int* p = static_cast<int*>(user);
            if (s && s->m_bAlive && s->m_SplatType >= 0) ++(*p);
        },
        &n);
    return n;
}

// Render one frame: clear to bright green, draw splats with correct depth state.
static void RenderSplatFrame(SDL_Window* window) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();

    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(PT_STANDARD, true);
    }

    // Depth state matching GameInit.cpp line 647-648 pre-splat pass:
    //   SetDepthBuffer(1)       -- depth test ON
    //   SetDepthBufferWrite(0)  -- depth writes OFF
    // With no fruits drawn first, the depth buffer holds 1.0 (max) from
    // glClear above.  Splats draw at z=-5500 which, in the ortho proj, maps
    // to a depth < 1.0 -- so GL_LESS passes without needing priming geometry.
    // Depth WRITE stays OFF so we don't stomp the buffer during the splat pass.
    dm.SetDepthBuffer(true);
    dm.SetDepthBufferWrite(false);

    SplatEntity::DrawActiveSplats();

    dm.SetDepthBuffer(false);
}

int main(int argc, char* argv[]) {
    // Port specific: standalone splat spawn+draw regression test

    fn::TestHarness h(argc, argv, "scene_fruit_splat");
    h.SetInteractiveDefault(false);
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    // GameInit wires: ActorManager pools, FruitCamera, FruitInfo tables,
    // SplatEntity pool (CreatePool), SlashEntities.
    // GameInitialise (inside game.init) handles SplatEntity::LoadContent.
    GameInit(0);
    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    // --- Pick fruit type (prefer watermelon; accept any type 0+) ---
    int fruitType = Fruit::FruitType("watermelon", false);
    if (fruitType < 0) {
        fprintf(stderr, "[scene_fruit_splat] WARN: 'watermelon' not found, using type 0\n");
        fruitType = 0;
    }

    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (!info) {
        fprintf(stderr, "[scene_fruit_splat] FAIL: FruitInfo_Get(%d) returned null\n", fruitType);
        return 1;
    }

    const unsigned int fruitAlpha = (unsigned int)info->m_FruitColour[3];
    printf("[scene_fruit_splat] fruitType=%d (%s)  colour BGRA=(%u,%u,%u,%u)\n",
           fruitType, info->m_Name,
           (unsigned)info->m_FruitColour[0],
           (unsigned)info->m_FruitColour[1],
           (unsigned)info->m_FruitColour[2],
           fruitAlpha);

    // --- Disable random kills for deterministic spawn ---
    // Test seam: SplatEntity::s_RandKillEnabled=false removes the 25% main kill
    // and the special-fruit secondary kill from MakeSplat. The alpha==0 suppression
    // (the real bug-guard) is NOT gated and still fires. Restored to true at exit.
    SplatEntity::s_RandKillEnabled = false;

    // --- Spawn and slice 1 fruit (deterministic with random kills off) ---
    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "[scene_fruit_splat] FAIL: actorManager null\n");
        return 1;
    }

    Fruit* lastFruit = NULL;

    Mortar::Entity* e = am->Add(0, true);
    if (!e) {
        fprintf(stderr, "[scene_fruit_splat] FAIL: actorManager->Add(0) returned null\n");
        return 1;
    }
    Fruit* fruit = static_cast<Fruit*>(e);
    fruit->Init(NULL, (long)fruitType, NULL);

    fruit->pos               = Vec3(0.0f, 0.0f, 0.0f);
    fruit->vel               = Vec3(0.0f, 0.0f, 0.0f);
    fruit->m_Gravity         = Vec3(0.0f, 0.0f, 0.0f);
    fruit->m_bBallisticEnable = 0;
    fruit->m_TimeScale       = 1.0f;
    fruit->flags            &= ~(uint32_t)(0x01 | 0x10);

    // Trigger slice: blade moving right at speed=60; SLICE_BLADE_SCALE=0.1 -> bladeSpeed=6.
    Vec3 bladeVel(60.0f, 10.0f, 0.0f);
    fruit->CollisionResponse(NULL, 0, 0, &bladeVel);

    // Tick Update until Slice() fires (timer 0.03 s at 1/60 dt = 2 frames).
    bool sliced = false;
    for (int i = 0; i < SLICE_TICKS; ++i) {
        fruit->Update(TICK_DT);
        fruit->PostUpdate(TICK_DT);
        if (fruit->m_bSliced) { sliced = true; break; }
    }
    lastFruit = fruit;

    int aliveAfterSlice = CountAliveSplats();
    printf("[scene_fruit_splat] SPAWN: sliced=%s  alive splats = %d\n",
           sliced ? "yes" : "no", aliveAfterSlice);

    bool spawnPass = (aliveAfterSlice >= 1);
    if (!spawnPass) {
        fprintf(stderr, "[scene_fruit_splat] FAIL (SPAWN): 0 alive splats after slicing 1 fruit.\n");
        if (!sliced) {
            fprintf(stderr, "  CAUSE: Fruit::Slice did not fire (m_SliceTimer not expiring).\n");
        } else if (fruitAlpha == 0) {
            fprintf(stderr, "  CAUSE: m_FruitColour[3] (alpha) = 0 for fruitType=%d (%s).\n",
                    fruitType, info->m_Name);
            fprintf(stderr, "  MakeSplat suppresses splats when m_ColA==0 (not gated by s_RandKillEnabled).\n");
        } else {
            fprintf(stderr, "  CAUSE: Slice ran but 0 splats spawned. "
                    "alpha=%u, s_RandKillEnabled=false -> random kills off. "
                    "Suspect pool empty or another suppression path.\n", fruitAlpha);
        }
    } else {
        printf("[scene_fruit_splat] SPAWN PASS: %d alive splat(s)\n", aliveAfterSlice);
    }

    // --- Tick UpdateActiveSplats until splats land ---
    for (int i = 0; i < LAND_TICKS; ++i) {
        SplatEntity::UpdateActiveSplats(TICK_DT);
        if (CountLandedSplats() > 0) {
            printf("[scene_fruit_splat] splats landed after %d tick(s)\n", i + 1);
            break;
        }
    }

    int landedCount = CountLandedSplats();
    printf("[scene_fruit_splat] landed splats = %d  (alive total = %d)\n",
           landedCount, CountAliveSplats());

    // Restore production default.
    SplatEntity::s_RandKillEnabled = true;

    // --- Interactive path ---
    if (h.IsInteractive()) {
        SDL_GL_SetSwapInterval(1);
        bool running = true;
        while (running && h.game.running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
            }
            SplatEntity::UpdateActiveSplats(TICK_DT);
            RenderSplatFrame(static_cast<SDL_Window*>(h.window));
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
        if (lastFruit) lastFruit->KillFruit(false);
        SplatEntity::RemoveAllSplats();
        return h.Shutdown();
    }

    // --- Headless: render + pixel readback ---
    // Warm-up frame.
    RenderSplatFrame(static_cast<SDL_Window*>(h.window));
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));

    // Measurement frame (no swap so glReadPixels captures back buffer).
    RenderSplatFrame(static_cast<SDL_Window*>(h.window));

    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    if (!pixels) {
        fprintf(stderr, "[scene_fruit_splat] FAIL: glReadPixels returned null\n");
        if (lastFruit) lastFruit->KillFruit(false);
        SplatEntity::RemoveAllSplats();
        return 1;
    }

    // Save screenshot.
    {
#ifdef _WIN32
        _mkdir("tmp"); _mkdir("tmp/test"); _mkdir("tmp/test/screenshots");
#else
        mkdir("tmp", 0755); mkdir("tmp/test", 0755); mkdir("tmp/test/screenshots", 0755);
#endif
        FILE* f = fopen("tmp/test/screenshots/scene_fruit_splat.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", fw, fh);
            for (int y = fh - 1; y >= 0; --y) {
                fwrite(pixels + (size_t)y * fw * 3, 1, (size_t)fw * 3, f);
            }
            fclose(f);
            printf("[scene_fruit_splat] screenshot: tmp/test/screenshots/scene_fruit_splat.ppm (%dx%d)\n",
                   fw, fh);
        } else {
            fprintf(stderr, "[scene_fruit_splat] WARN: could not write screenshot\n");
        }
    }

    int drawnPixels = CountNonBackground(pixels, fw, fh);
    free(pixels);

    printf("[scene_fruit_splat] DRAW: non-background pixels = %d (min required = %d)\n",
           drawnPixels, MIN_DRAWN_PIXELS);

    bool drawPass = (drawnPixels >= MIN_DRAWN_PIXELS);

    if (!drawPass) {
        fprintf(stderr, "[scene_fruit_splat] FAIL (DRAW): only %d non-background pixels "
                "(< %d minimum).\n", drawnPixels, MIN_DRAWN_PIXELS);
        fprintf(stderr, "  landed=%d\n", landedCount);
        fprintf(stderr, "  Possible causes:\n");
        if (landedCount == 0) {
            fprintf(stderr, "    - No landed splats at render time (m_SplatType still -1; "
                    "airborne phase not completing)\n");
        }
        fprintf(stderr, "    - Depth test rejecting (z=-5500 not passing GL_LESS against "
                "cleared depth buffer 1.0)\n");
        fprintf(stderr, "    - Blend state wrong or alpha decayed to 0 before render\n");
        fprintf(stderr, "    - Splat texture not loaded (white_splash.tex missing)\n");
        fprintf(stderr, "    - DrawActiveSplats bailed early (s_SplatTex.IsValid() == false)\n");
    } else {
        printf("[scene_fruit_splat] DRAW PASS: %d non-background pixels\n", drawnPixels);
    }

    if (lastFruit) lastFruit->KillFruit(false);
    SplatEntity::RemoveAllSplats();

    // Final verdict.
    printf("[scene_fruit_splat] SPAWN=%s DRAW=%s\n",
           spawnPass ? "PASS" : "FAIL",
           drawPass  ? "PASS" : "FAIL");

    h.Shutdown();
    return (spawnPass && drawPass) ? 0 : 1;
}
