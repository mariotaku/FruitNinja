// scene_special_fruit.cpp -- special fruit / powerup object rendering test.
//
// Renders each special / powerup fruit spawn object and writes a PNG
// screenshot per object so untextured (white-solid) renders are visually
// catchable in CI without requiring GPU inspection.
//
// The frenzy banana currently renders white (banana_speed texture not bound
// in Fruit::Draw). This test exposes that as a white PNG and logs a WARN
// so the symptom is visible even though it does not fail the overall test.
//
// Subjects (fruitlist.xml names):
//   frenzy            -- arcade speed banana   (modelName="banana_speed")
//   fourth_banana     -- arcade time banana    (modelName="black_banana")
//   freeze            -- arcade freeze banana  (modelName="banana_ice")
//   scorex2           -- arcade score x2 banana (modelName="banana_x2")
//   dragon            -- dragon fruit          (score=50, special)
//   super_dragon      -- super dragon          (score=0, modelName="dragon")
//   super_pomegranate -- super pomegranate     (score=0)
//   bomb              -- standard bomb (entity type 1, regression)
//
// Assertions:
//   PASS per-object: >= MIN_DRAWN_PIXELS non-background pixels were drawn.
//   WARN (logged, not failing): >80% of non-bg pixels are near-white
//         (R,G,B all > 220) -- flags likely untextured renders.
//   Overall: exits 0 if all rendered objects passed; 1 if any produced 0
//            non-background pixels (catastrophic draw failure).
//
// PNG outputs: tmp/test/screenshots/special_fruit/<name>.png
//
// Run:
//   ctest -R special_fruit --output-on-failure
//   ./build/host/tests/scenes/scene_special_fruit.exe --interactive
//
// Port specific: standalone special fruit / powerup render test.

#include "../test_harness.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
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

// Background clear colour: glClearColor(0.13f, 0.10f, 0.08f, 1.0f) -- dark wood grey.
// Approximate [0,255]: (33, 26, 20).
static const unsigned char BG_R         = 33;
static const unsigned char BG_G         = 26;
static const unsigned char BG_B         = 20;

// A pixel is non-background if it differs from BG_* by more than BG_TOLERANCE
// in ANY single channel (max, not summed, across R/G/B). See
// scene_fruit_splat.cpp for why summed-over-3-channels-then-thresholded is
// wrong: it lets a real per-channel shift hide under a combined threshold.
// These fruit/bomb models are opaque so this rarely bites here, but the
// metric should be consistent project-wide. 8 absorbs float->8-bit blend
// rounding (glReadPixels returns raw framebuffer bytes, no PNG/JPEG
// requantisation in this path).
static const int           BG_TOLERANCE = 8;

// Pixels where all three channels exceed WHITE_THRESH are "near-white" -- flags
// a likely untextured (white-solid) render when >WHITE_PCT_WARN% of non-bg pixels.
static const unsigned char WHITE_THRESH  = 220;
static const int           WHITE_PCT_WARN = 80;

// Minimum non-background pixels per object to count as "rendered".
static const int MIN_DRAWN_PIXELS = 100;

// Warm-up frames before screenshot (60 = 1 s at 60 Hz; covers model/tex upload).
static const int WARM_FRAMES = 60;

// Screen centre in game ortho coordinates.
static const float CENTER_X = 0.0f;
static const float CENTER_Y = 0.0f;

// Slow Y-axis spin for centred preview (matched to scene_fruit.cpp SPIN_MAGNITUDE).
static const float SPIN_MAG = 2.5f;

// Entry table.
// fruitName == NULL -> spawn as Bomb (entity type 1); otherwise as Fruit (entity type 0)
// via Fruit::FruitType(fruitName, false).
struct SpecialEntry {
    const char* fruitName;  // NULL = bomb
    const char* label;      // screenshot sub-name under "special_fruit/"
};

static const SpecialEntry kEntries[] = {
    { "frenzy",            "frenzy"            },
    { "fourth_banana",     "fourth_banana"      },
    { "freeze",            "freeze"             },
    { "scorex2",           "scorex2"            },
    { "dragon",            "dragon"             },
    { "super_dragon",      "super_dragon"       },
    { "super_pomegranate", "super_pomegranate"  },
    { NULL,                "bomb"               },
};
static const int kEntryCount = (int)(sizeof(kEntries) / sizeof(kEntries[0]));

// ---- pixel analysis ----

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int dr = abs((int)r - (int)BG_R);
    int dg = abs((int)g - (int)BG_G);
    int db = abs((int)b - (int)BG_B);
    int maxDiff = dr > dg ? dr : dg;
    if (db > maxDiff) maxDiff = db;
    return maxDiff <= BG_TOLERANCE;
}

// Count non-background and near-white pixels in a bottom-up RGB buffer.
static void CountPixels(const unsigned char* px, int w, int h,
                        int* outNonBg, int* outWhite) {
    int nonBg = 0, white = 0;
    for (int i = 0; i < w * h; ++i) {
        unsigned char r = px[i * 3 + 0];
        unsigned char g = px[i * 3 + 1];
        unsigned char b = px[i * 3 + 2];
        if (!IsBackground(r, g, b)) {
            ++nonBg;
            if (r > WHITE_THRESH && g > WHITE_THRESH && b > WHITE_THRESH)
                ++white;
        }
    }
    if (outNonBg) *outNonBg = nonBg;
    if (outWhite) *outWhite = white;
}

// ---- entity spawn helpers ----

static Fruit* SpawnFruit(Mortar::ActorManager* am, int fruitType) {
    Mortar::Entity* e = am->Add(0, true);
    if (!e) return NULL;
    Fruit* f = static_cast<Fruit*>(e);
    f->Init(NULL, (long)fruitType, NULL);

    f->pos               = _Vector3<float>(CENTER_X, CENTER_Y, 0.0f);
    f->vel               = _Vector3<float>(0.0f, 0.0f, 0.0f);
    f->m_Gravity         = _Vector3<float>(0.0f, 0.0f, 0.0f);
    f->m_bBallisticEnable = 0;

    _Vector3<float> spinAxis(0.0f, 1.0f, 0.0f);
    f->RotateFacingUp(false, spinAxis);
    f->m_RotVel1 = _Vector3<float>(0.0f, SPIN_MAG, 0.0f);
    f->m_RotVel2 = _Vector3<float>(0.0f, SPIN_MAG, 0.0f);

    f->flags &= ~(uint32_t)(0x01 | 0x10); // clear ENT_INACTIVE | ENT_KILLED
    return f;
}

static Bomb* SpawnBomb(Mortar::ActorManager* am) {
    Mortar::Entity* e = am->Add(1, true);
    if (!e) return NULL;
    Bomb* b = static_cast<Bomb*>(e);
    b->Init(NULL, 0, NULL);

    b->pos          = _Vector3<float>(CENTER_X, CENTER_Y, 0.0f);
    b->vel          = _Vector3<float>(0.0f, 0.0f, 0.0f);
    b->m_AccelForce = _Vector3<float>(0.0f, 0.0f, 0.0f);
    b->m_RotVelX    = (int16_t)2;
    b->m_RotVelY    = (int16_t)1;
    b->flags       &= ~(uint32_t)(0x01 | 0x10);
    return b;
}

static void KillCurrent(Fruit* fruit, Bomb* bomb) {
    if (fruit) fruit->KillFruit(false);
    if (bomb)  bomb->KillBomb();
}

// ---- draw pass (no swap; caller calls SDL_GL_SwapWindow when desired) ----

static void DrawScene(fn::TestHarness& h, Fruit* fruit, Bomb* bomb) {
    SDL_Window* sdlwin = static_cast<SDL_Window*>(h.window);

    float dt = 0.0f;
    SystemManager::GetInstance().Update(&dt);

    if (fruit && !(fruit->flags & (uint32_t)(0x01 | 0x10))) {
        fruit->Update(dt);
        fruit->PostUpdate(dt);
        // Re-pin to centre to suppress any residual positional drift.
        fruit->pos.x = CENTER_X;
        fruit->pos.y = CENTER_Y;
    }
    if (bomb && !(bomb->flags & (uint32_t)(0x01 | 0x10))) {
        bomb->Update(dt);
        bomb->PostUpdate(dt);
        bomb->pos.x = CENTER_X;
        bomb->pos.y = CENTER_Y;
    }

    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(sdlwin, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    glClearColor(0.13f, 0.10f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera)
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
    if (h.game.actorManager)
        h.game.actorManager->Draw(h.game.renderer);
    dm.SetDepthBuffer(false);
}

int main(int argc, char* argv[]) {
    // Port specific: standalone special fruit / powerup render test.

    fn::TestHarness h(argc, argv, "scene_special_fruit");
    h.SetInteractiveDefault(false);
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    // GameInit wires ActorManager pools, FruitCamera, FruitInfo tables, and
    // Fruit / Bomb factory delegates. Needed before any am->Add() call.
    // Entered through the task dispatcher (NOT a bare GameInit(0)) so
    // GameTaskExit dispatches GameExit at teardown -- see EnterGameState().
    h.EnterGameState();
    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "[scene_special_fruit] FAIL: actorManager null after GameInit\n");
        return 1;
    }

    SDL_Window* sdlwin = static_cast<SDL_Window*>(h.window);

    if (h.IsInteractive()) {
        // Interactive: show one entry at a time; SPACE or click cycles forward.
        SDL_GL_SetSwapInterval(1);

        int    step      = 0;
        bool   needSpawn = true;
        bool   running   = true;
        Fruit* curFruit  = NULL;
        Bomb*  curBomb   = NULL;

        while (running && h.game.running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) { running = false; break; }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_SPACE) {
                    step = (step + 1) % kEntryCount;
                    needSpawn = true;
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    step = (step + 1) % kEntryCount;
                    needSpawn = true;
                }
            }
            if (!running) break;

            if (needSpawn) {
                KillCurrent(curFruit, curBomb);
                curFruit = NULL;
                curBomb  = NULL;

                const SpecialEntry& e = kEntries[step];
                if (e.fruitName) {
                    int ft = Fruit::FruitType(e.fruitName, false);
                    if (ft >= 0) {
                        curFruit = SpawnFruit(am, ft);
                    } else {
                        fprintf(stderr, "[scene_special_fruit] '%s' not in FruitInfo\n",
                                e.fruitName);
                    }
                } else {
                    curBomb = SpawnBomb(am);
                }
                printf("[scene_special_fruit] [%d/%d] showing: %s\n",
                       step + 1, kEntryCount, e.label);
                needSpawn = false;
            }

            DrawScene(h, curFruit, curBomb);
            SDL_GL_SwapWindow(sdlwin);
        }

        KillCurrent(curFruit, curBomb);
        return h.Shutdown();
    }

    // Headless: render each entry, write PNG, check pixel counts.
    bool anyRenderFail = false;

    for (int i = 0; i < kEntryCount; ++i) {
        const SpecialEntry& e = kEntries[i];

        Fruit* fruit = NULL;
        Bomb*  bomb  = NULL;

        if (e.fruitName) {
            int ft = Fruit::FruitType(e.fruitName, false);
            if (ft < 0) {
                fprintf(stderr, "[scene_special_fruit] WARN: '%s' not in FruitInfo -- skip\n",
                        e.fruitName);
                continue;
            }
            fruit = SpawnFruit(am, ft);
            if (!fruit) {
                fprintf(stderr,
                        "[scene_special_fruit] WARN: SpawnFruit('%s') pool full -- skip\n",
                        e.fruitName);
                continue;
            }
            printf("[scene_special_fruit] [%d/%d] '%s' fruitType=%d\n",
                   i + 1, kEntryCount, e.fruitName, ft);
        } else {
            bomb = SpawnBomb(am);
            if (!bomb) {
                fprintf(stderr,
                        "[scene_special_fruit] WARN: SpawnBomb() pool full -- skip\n");
                continue;
            }
            printf("[scene_special_fruit] [%d/%d] bomb\n", i + 1, kEntryCount);
        }

        // Warm-up: let model and texture streaming settle before screenshot.
        for (int f = 0; f < WARM_FRAMES; ++f) {
            DrawScene(h, fruit, bomb);
            SDL_GL_SwapWindow(sdlwin);
        }

        // Measurement frame: draw WITHOUT swap so glReadPixels captures it.
        DrawScene(h, fruit, bomb);

        // Pixel analysis (reads from the back buffer we just drew to).
        int fw = 0, fh = 0;
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int nonBg = 0, whitePx = 0;
        if (px) {
            CountPixels(px, fw, fh, &nonBg, &whitePx);
            free(px);
        }
        int whitePct = (nonBg > 0) ? (whitePx * 100 / nonBg) : 0;

        // PNG screenshot (also reads from the same back buffer; no swap yet).
        char ssName[80];
        snprintf(ssName, sizeof(ssName), "special_fruit/%s", e.label);
        h.ScreenshotPng(ssName);

        // Verdict.
        const char* verdict;
        if (nonBg < MIN_DRAWN_PIXELS) {
            verdict = "FAIL (did not render)";
            anyRenderFail = true;
        } else if (whitePct > WHITE_PCT_WARN) {
            verdict = "WARN (>80% white pixels -- possibly untextured)";
        } else {
            verdict = "OK";
        }
        printf("[scene_special_fruit] '%s': nonBg=%d whitePct=%d%% -- %s\n",
               e.label, nonBg, whitePct, verdict);

        // Swap to advance the swap chain (keeps GPU valid for next iteration).
        SDL_GL_SwapWindow(sdlwin);

        KillCurrent(fruit, bomb);
    }

    printf("[scene_special_fruit] %s\n",
           anyRenderFail ? "FAIL" : "PASS");

    h.Shutdown();
    return anyRenderFail ? 1 : 0;
}
