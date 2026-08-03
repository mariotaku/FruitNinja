// test_slash_texture_render.cpp -- isolated render test for the SlashEntity
// blade/slash trail (blade.tex), investigating the "black blob" regression
// (slash trail renders solid black instead of the blade texture/colour).
//
// FINDING (see report / SlashEntity.cpp comments for detail): the bug is
// INTRINSIC to SlashEntity -- not a leaked GL blend/texenv state from other
// draws. Root cause: the file-static colour-cache global `g_ModColourOut`
// (SlashEntity.cpp) is BSS-zero-initialised to Colour(0,0,0,0) and is only
// primed to the real palette colour (White by default) the first time ANY
// SlashEntity's UpdatePoints() reaches a "body" (non-head-cap) trail segment
// with ModColourType==0 (SlashEntity.cpp UpdatePoints ~line 1272). Until that
// first priming happens, SlashEntity::Update()'s per-tick colour blend
// (SlashEntity.cpp ~line 1822-1839) copies the zero colour into
// m_HighlightColour, then unconditionally forces alpha=0xFF while carrying
// r/g/b through from the zero value -- producing m_BaseColour =
// (0,0,0,255), OPAQUE BLACK. Every vertex baked/rewritten while this holds
// (AddPoint + UpdatePoints' head-cap/body colour writes) is opaque black;
// DrawSlice's own blend/texenv setup is unconditional and correct, so the
// texture is genuinely present but MODULATEd by a black vertex colour.
//
// This means the FIRST slash trail of a fresh session/process renders BLACK
// until pointCount grows enough for UpdatePoints to hit its first body
// segment (usually within 1-2 ticks of a real swipe) -- after which the
// shared global self-corrects to White and every subsequent trail (and the
// rest of that same trail) renders correctly. This test freezes the exact
// moment the bug is visible and also rules out a GL state-leak explanation.
//
// Four render passes (each its own screenshot):
//   black_intrinsic   -- captured right after the 2nd-ever Update() tick for
//                        a fresh SlashEntity (first tick with pointCount>3,
//                        i.e. the first tick DrawSlice actually draws
//                        anything). Reproduces the bug: EXPECTED to be black.
//   white_selfcorrected -- one tick later; g_ModColourOut is now primed
//                        White by the previous tick's UpdatePoints body pass,
//                        so the whole ribbon re-bakes White. EXPECTED non-black.
//   white_leaked_state -- same corrected trail, but immediately before the
//                        draw we deliberately corrupt glBlendFunc (additive
//                        GL_ONE,GL_ONE) and glTexEnv (GL_REPLACE) WITHOUT
//                        resetting. DrawSlice (SlashEntity.cpp TexEnvModulate()
//                        call) and Renderer::DrawTriStrip (unconditional
//                        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA) each
//                        call) both reset the corrupted state before drawing,
//                        so this is EXPECTED to look IDENTICAL to the clean
//                        variant -- proving GL state leak is NOT a viable
//                        explanation for this bug.
//   white_clean_state -- same trail, GL state explicitly reset to sane
//                        values first. Direct comparison baseline for the
//                        leaked-state pass.
//
// Usage: test_slash_texture_render [--screenshot] [--interactive]
// Default (no flags): headless assertions (pixel readback). Passes via
//   ctest -E screenshot.
// --screenshot: also writes PNGs to tmp/test/screenshots/slashtexture/*.png
//
// C++11 / GCC 4.4.1 clean: no lambdas, no auto, no range-for, no enum class.

#include "test_harness.h"
#include "entities/SlashEntity.h"
#include "input/InputEvent.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "game/FruitCamera.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Touch injection -- direct calls on the public SlashEntity touch API.
// World coords are the centred ortho space used throughout the port
// (X horizontal +/-240, Y vertical +/-160, Y-up); see
// MatrixManager::SetupOrtho(160,-160,-240,240,...) and precedent in
// test_shoplistitem_render.cpp's kItemX/kRowY comments.
// ---------------------------------------------------------------------------
static void SimulateTouchDown(SlashEntity* s, float x, float y) {
    InputEvent evX;
    FN_MakeTouchAxisEvent(evX, 0, 0, false, x, y);
    s->TouchMoveX(&evX);
    InputEvent evY;
    FN_MakeTouchAxisEvent(evY, 0, 0, true, x, y);
    s->TouchMoveY(&evY);

    // The press event deliberately carries position (0,0): TouchDown's
    // press-edge arm re-seeds the stroke from it, which is what this test's
    // golden was captured against.
    InputEvent evDown;
    FN_MakeTouchButtonEvent(evDown, 0, INPUT_ACTION_DOWN_EDGE, 0, 0.0f, 0.0f);
    s->TouchDown(&evDown);
}

static void SimulateTouchMove(SlashEntity* s, float x, float y) {
    InputEvent evX;
    FN_MakeTouchAxisEvent(evX, 0, 0, false, x, y);
    s->TouchMoveX(&evX);
    InputEvent evY;
    FN_MakeTouchAxisEvent(evY, 0, 0, true, x, y);
    s->TouchMoveY(&evY);
    s->UpdateTouchDown(&evY);
}

// One simulation tick -- mirrors GameUpdate's per-frame SlashEntity dispatch
// (GameInit.cpp ~line 472-484): one PreUpdate(0) for the first live channel,
// then Update+PostUpdate for every channel.
static void Tick(float dt) {
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->PreUpdate(0.0f);
            break;
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->Update(dt);
            g_pSlashEntities[i]->PostUpdate(dt);
        }
    }
}

// Render one frame: clear to a distinctive background (never white/black,
// so "nothing drawn" vs "drawn black" vs "drawn white/textured" are all
// distinguishable), set up the same camera GameDraw uses, draw all slash
// trails. Does NOT swap -- caller reads the back buffer, then swaps.
static const float kBgR = 0.10f;
static const float kBgG = 0.35f;
static const float kBgB = 0.55f;

static void RenderSlashFrame(SDL_Window* window) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();
    glClearColor(kBgR, kBgG, kBgB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, false);
    }

    dm.SetDepthBuffer(false);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
    }
}

// ---------------------------------------------------------------------------
// Pixel sampling. World-space sample columns lie on the horizontal
// centreline (world Y=0 -> screen row height/2, independent of whether the
// readback buffer is top-down or bottom-up since NDC 0 maps to the middle
// row either way). World X in [-240,240] maps linearly to screen columns.
// For each sample column, search a small vertical band (miter half-width)
// for the first non-background pixel and classify it.
// ---------------------------------------------------------------------------
static bool IsCloseTo(unsigned char a, unsigned char b, int eps) {
    int d = (int)a - (int)b;
    if (d < 0) d = -d;
    return d <= eps;
}

struct SampleResult {
    int hitColumns;    // columns where a non-background pixel was found
    int blackColumns;  // of those, columns where it was ~(0,0,0)
    int totalColumns;
};

static SampleResult SampleRibbon(unsigned char* pixels, int ww, int wh,
                                 const float* worldXs, int count) {
    SampleResult r;
    r.hitColumns = 0;
    r.blackColumns = 0;
    r.totalColumns = count;

    unsigned char bgR = (unsigned char)(kBgR * 255.0f);
    unsigned char bgG = (unsigned char)(kBgG * 255.0f);
    unsigned char bgB = (unsigned char)(kBgB * 255.0f);

    int centreRow = wh / 2;
    for (int i = 0; i < count; ++i) {
        float ndcX = worldXs[i] / 240.0f;
        int px = (int)((ndcX + 1.0f) * 0.5f * (float)ww);
        if (px < 0 || px >= ww) continue;

        bool foundNonBg = false;
        bool allBlack = true;
        for (int dy = -25; dy <= 25; ++dy) {
            int y = centreRow + dy;
            if (y < 0 || y >= wh) continue;
            size_t off = ((size_t)y * (size_t)ww + (size_t)px) * 3;
            unsigned char pr = pixels[off + 0];
            unsigned char pg = pixels[off + 1];
            unsigned char pb = pixels[off + 2];
            bool isBg = IsCloseTo(pr, bgR, 10) && IsCloseTo(pg, bgG, 10) && IsCloseTo(pb, bgB, 10);
            if (!isBg) {
                foundNonBg = true;
                bool isBlackPx = (pr < 12 && pg < 12 && pb < 12);
                if (!isBlackPx) allBlack = false;
            }
        }
        if (foundNonBg) {
            ++r.hitColumns;
            if (allBlack) ++r.blackColumns;
        }
    }
    return r;
}

static const float kSampleXs[] = { -100.0f, -50.0f, 0.0f, 50.0f, 100.0f };
static const int   kSampleCount = 5;

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "slashtexture/default");
    h.SetInitFrames(0);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // GameInit(0) creates + Init()s all 16 g_pSlashEntities[] and calls
    // SlashEntity::LoadContent() (via game.init() -> GameInitialise) has
    // already loaded blade.tex by this point. No prior SlashEntity::Update()
    // tick has ever run in this process, so the shared g_ModColourOut global
    // is guaranteed to still be at its BSS-zero default -- required to
    // reproduce the bug's exact trigger window.
    // Entered through the task dispatcher (NOT a bare GameInit(0)) so
    // GameTaskExit dispatches GameExit at teardown -- see EnterGameState().
    // Still no SlashEntity::Update tick: GameTaskUpdate's !initialized branch
    // returns right after the init handler, and GameTaskDraw's s_updated gate
    // is still false, so the g_ModColourOut BSS-zero window above is preserved.
    h.EnterGameState();
    if (!g_pSlashEntities[0]) {
        fprintf(stderr, "FAIL: g_pSlashEntities[0] null after GameInit(0)\n");
        h.Shutdown();
        return 1;
    }
    printf("[slash_texture] g_pSlashEntities[0]=%p OK\n", (void*)g_pSlashEntities[0]);

    int failures = 0;
    SlashEntity* slash0 = g_pSlashEntities[0];

    // -----------------------------------------------------------------------
    // Build a horizontal trail across the screen centreline (world Y=0):
    // seed at x=-150, then one long move to x=150 (300 world units,
    // POINT_SPACING=64 -> several interpolated body points, well past the
    // pointCount>3 DrawSlice gate and past the pointCount>2 UpdatePoints
    // body-path threshold).
    // -----------------------------------------------------------------------
    SimulateTouchDown(slash0, -150.0f, 0.0f);
    Tick(1.0f / 60.0f);   // Tick 1: seed only (pointCount==2) -- DrawSlice would no-op (gate <=3).

    SimulateTouchMove(slash0, 150.0f, 0.0f);
    Tick(1.0f / 60.0f);   // Tick 2: pointCount>3 for the first time. See header comment:
                          // AddPoint (during SimulateTouchMove) and UpdatePoints' colour
                          // rewrite (during this Tick) both bake m_BaseColour, which by
                          // now has already been corrupted to opaque black by tick 1's
                          // Update() colour-blend step (SlashEntity.cpp ~1822-1839) reading
                          // the still-unprimed g_ModColourOut. This is the exact frame the
                          // bug is visible in.

    printf("[slash_texture] pointCount after seed+move+2 ticks: %d\n", slash0->GetPointCount());

    // -----------------------------------------------------------------------
    // Pass 1: black_intrinsic -- reproduce the bug with otherwise-clean GL
    // state (no other draws beforehand in this process).
    // -----------------------------------------------------------------------
    {
        RenderSlashFrame(static_cast<SDL_Window*>(h.window));
        int ww = 0, wh = 0;
        unsigned char* pixels = h.ReadPixels(&ww, &wh);
        if (!pixels) {
            fprintf(stderr, "FAIL: ReadPixels null (pass 1)\n");
            ++failures;
        } else {
            SampleResult res = SampleRibbon(pixels, ww, wh, kSampleXs, kSampleCount);
            printf("[slash_texture] PASS 1 black_intrinsic: hit=%d/%d black=%d/%d "
                   "(m_BaseColour R=%u G=%u B=%u A=%u)\n",
                   res.hitColumns, res.totalColumns, res.blackColumns, res.totalColumns,
                   slash0->GetBaseColour().r, slash0->GetBaseColour().g,
                   slash0->GetBaseColour().b, slash0->GetBaseColour().a);
            if (res.hitColumns == 0) {
                printf("[slash_texture]   NOTE: nothing drawn at all this pass "
                       "(texture invalid or pointCount<=3) -- not the black-vertex-colour bug.\n");
            } else if (res.blackColumns == res.hitColumns) {
                printf("[slash_texture]   CONFIRMED: ribbon reproduces solid-black "
                       "regression intrinsically (clean GL state, correctly-loaded texture, "
                       "single isolated SlashEntity).\n");
            } else {
                printf("[slash_texture]   NOTE: ribbon did NOT render black this pass "
                       "(bug window may have shifted -- see report).\n");
            }
            if (h.IsScreenshot()) h.ScreenshotPng("slashtexture/black_intrinsic");
            free(pixels);
        }
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

    // -----------------------------------------------------------------------
    // Pass 2: white_selfcorrected -- one more tick. g_ModColourOut was primed
    // White during tick 2's UpdatePoints body pass, so m_BaseColour is now
    // White; this tick's UpdatePoints rewrites the whole buffer with it.
    // EXPECTED non-black -- hard assertion.
    // -----------------------------------------------------------------------
    SimulateTouchMove(slash0, 200.0f, 0.0f);
    Tick(1.0f / 60.0f);
    {
        RenderSlashFrame(static_cast<SDL_Window*>(h.window));
        int ww = 0, wh = 0;
        unsigned char* pixels = h.ReadPixels(&ww, &wh);
        if (!pixels) {
            fprintf(stderr, "FAIL: ReadPixels null (pass 2)\n");
            ++failures;
        } else {
            SampleResult res = SampleRibbon(pixels, ww, wh, kSampleXs, kSampleCount);
            printf("[slash_texture] PASS 2 white_selfcorrected: hit=%d/%d black=%d/%d "
                   "(m_BaseColour R=%u G=%u B=%u A=%u)\n",
                   res.hitColumns, res.totalColumns, res.blackColumns, res.totalColumns,
                   slash0->GetBaseColour().r, slash0->GetBaseColour().g,
                   slash0->GetBaseColour().b, slash0->GetBaseColour().a);
            if (res.hitColumns == 0) {
                fprintf(stderr, "FAIL: pass 2 drew nothing (expected a visible white ribbon)\n");
                ++failures;
            } else if (res.blackColumns > 0) {
                fprintf(stderr, "FAIL: pass 2 still shows black columns (%d/%d) -- "
                        "self-correction did not happen as expected\n",
                        res.blackColumns, res.hitColumns);
                ++failures;
            } else {
                printf("[slash_texture]   PASS: ribbon self-corrected to non-black.\n");
            }
            if (h.IsScreenshot()) h.ScreenshotPng("slashtexture/white_selfcorrected");
            free(pixels);
        }
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

    // -----------------------------------------------------------------------
    // Pass 3: white_leaked_state -- same corrected trail (no further ticks),
    // deliberately corrupt blend func (additive) + texenv (REPLACE)
    // immediately before drawing, WITHOUT resetting. DrawSlice's own
    // TexEnvModulate() call (SlashEntity.cpp DrawSlice, before bladeTex->Set())
    // and Renderer::DrawTriStrip's own unconditional glBlendFunc(SRC_ALPHA,
    // ONE_MINUS_SRC_ALPHA) (Renderer.cpp DrawTriStrip) should both overwrite
    // this leaked state before any pixel is drawn -- predicted to look
    // IDENTICAL to pass 4 (clean state). EXPECTED non-black -- hard assertion.
    // -----------------------------------------------------------------------
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);                                    // leaked additive blend
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_REPLACE); // leaked texenv
        // Deliberately NOT reset -- DrawSlice/DrawTriStrip must fix this themselves.

        RenderSlashFrame(static_cast<SDL_Window*>(h.window));
        int ww = 0, wh = 0;
        unsigned char* pixels = h.ReadPixels(&ww, &wh);
        if (!pixels) {
            fprintf(stderr, "FAIL: ReadPixels null (pass 3)\n");
            ++failures;
        } else {
            SampleResult res = SampleRibbon(pixels, ww, wh, kSampleXs, kSampleCount);
            printf("[slash_texture] PASS 3 white_leaked_state: hit=%d/%d black=%d/%d\n",
                   res.hitColumns, res.totalColumns, res.blackColumns, res.totalColumns);
            if (res.hitColumns == 0) {
                fprintf(stderr, "FAIL: pass 3 drew nothing\n");
                ++failures;
            } else if (res.blackColumns > 0) {
                fprintf(stderr, "FAIL: pass 3 renders black (%d/%d) with leaked GL state -- "
                        "DrawSlice/DrawTriStrip did NOT reset blend/texenv as expected\n",
                        res.blackColumns, res.hitColumns);
                ++failures;
            } else {
                printf("[slash_texture]   PASS: leaked blend/texenv state had no visible "
                       "effect -- DrawSlice/DrawTriStrip self-reset both unconditionally.\n");
            }
            if (h.IsScreenshot()) h.ScreenshotPng("slashtexture/white_leaked_state");
            free(pixels);
        }
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

    // -----------------------------------------------------------------------
    // Pass 4: white_clean_state -- same trail again, GL state explicitly
    // reset to sane values first. Direct comparison baseline for pass 3.
    // -----------------------------------------------------------------------
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // ES2 migration: no glTexEnv; texture*color modulate is done in the shader.

        RenderSlashFrame(static_cast<SDL_Window*>(h.window));
        int ww = 0, wh = 0;
        unsigned char* pixels = h.ReadPixels(&ww, &wh);
        if (!pixels) {
            fprintf(stderr, "FAIL: ReadPixels null (pass 4)\n");
            ++failures;
        } else {
            SampleResult res = SampleRibbon(pixels, ww, wh, kSampleXs, kSampleCount);
            printf("[slash_texture] PASS 4 white_clean_state: hit=%d/%d black=%d/%d\n",
                   res.hitColumns, res.totalColumns, res.blackColumns, res.totalColumns);
            if (res.hitColumns == 0) {
                fprintf(stderr, "FAIL: pass 4 drew nothing\n");
                ++failures;
            } else if (res.blackColumns > 0) {
                fprintf(stderr, "FAIL: pass 4 renders black (%d/%d) even with clean GL state\n",
                        res.blackColumns, res.hitColumns);
                ++failures;
            } else {
                printf("[slash_texture]   PASS: clean-state ribbon renders non-black.\n");
            }
            if (h.IsScreenshot()) h.ScreenshotPng("slashtexture/white_clean_state");
            free(pixels);
        }
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: slash_texture_render OK (see PASS 1 output above for the "
           "intrinsic black-bug reproduction diagnostic)\n");
    return h.Shutdown();
}
