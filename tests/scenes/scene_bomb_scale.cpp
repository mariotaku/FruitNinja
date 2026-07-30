// scene_bomb_scale.cpp -- pixel-level bomb-scale regression test.
//
// Renders TWO bombs side-by-side with different entity->scale values:
//   HOME bomb (left half):  scale = 0.4675 (Vec3 uniform)
//   DOJO bomb (right half): scale = 0.3857 (= 0.825 * 0.4675)
//
// Reads back the framebuffer via glReadPixels, splits into left/right halves,
// measures the bounding box of non-background pixels in each half, and asserts
// that the Dojo bomb's rendered extent is ~0.825x the Home bomb's.
//
// If the ratio is ~1.0 (both same pixel size) the test FAILS -- this confirms
// the rendering bug where entity->scale is set correctly but Bomb::Draw does
// not honour it.
//
// Background: bright green (0,255,0). Bomb model is predominantly dark grey /
// black / brown, so any pixel with R<200 && G==255 && B<200 is background.
//
// Port specific: standalone entity scale regression test
//
// Run:
//   ctest -R bomb_scale --output-on-failure
//   ./build/host/tests/scenes/scene_bomb_scale.exe --screenshot
//   ./build/host/tests/scenes/scene_bomb_scale.exe --interactive   (visual check)
//
// Screenshot written to: tmp/test/screenshots/scene_bomb_scale.png

#include "../test_harness.h"
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

// Scale constants matching test_menubutton_scale / test_dojoscreen_bomb_scale.
// Home bomb scale (from FruitInfo_GetBombSize() * 0.01 * 1.0 in Bomb::Init with
// default scaleFactor=1.0; binary Init computes ~46.75 * 0.01 = 0.4675).
static const float HOME_SCALE   = 0.4675f;
// Dojo bomb scale = HOME_SCALE * DojoScreen shrink ratio (0.825x confirmed by
// both test_menubutton_scale and test_dojoscreen_bomb_scale).
static const float DOJO_SCALE   = 0.3857f;
static const float EXPECTED_RATIO = 0.825f;
static const float RATIO_TOLERANCE = 0.10f;  // 10% rasterisation slack

// Horizontal position offsets (in game ortho coordinates [-240..+240] x-axis).
// Each bomb is placed 100 units from centre so they're well-separated but both
// within the visible [-240,+240] x range.
static const float HOME_POS_X   =  -100.0f;
static const float DOJO_POS_X   =  100.0f;
// Vertical: both at y=0 (screen centre), z=0 + normal ZPosition.
static const float BOMB_POS_Y   =    0.0f;
// Z: matches menu path (m_ZPosition set from GetBombZPosition in Init, but we
// override to a fixed 150 so both are at the same z depth).
static const float BOMB_Z_POS   =  150.0f;

// Background colour: bright green (R=0, G=255, B=0).
// A pixel is considered "background" if it is within a tolerance of this.
static const unsigned char BG_R = 0;
static const unsigned char BG_G = 255;
static const unsigned char BG_B = 0;
// A pixel is non-background if it differs from BG_* by more than BG_TOLERANCE
// in ANY single channel (max, not summed, across R/G/B). See
// scene_fruit_splat.cpp for why summed-over-3-channels-then-thresholded is
// wrong: it lets a real per-channel shift hide under a combined threshold.
// Bomb models are opaque against this bright-green key so this rarely bites
// here, but the metric should be consistent project-wide. 8 absorbs
// float->8-bit blend rounding (glReadPixels returns raw framebuffer bytes,
// no PNG/JPEG requantisation in this path).
static const int BG_TOLERANCE = 8;

struct BombSceneBounds {
    int minX, maxX, minY, maxY;
    int nonBgCount;
};

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int dr = abs((int)r - (int)BG_R);
    int dg = abs((int)g - (int)BG_G);
    int db = abs((int)b - (int)BG_B);
    int maxDiff = dr > dg ? dr : dg;
    if (db > maxDiff) maxDiff = db;
    return maxDiff <= BG_TOLERANCE;
}

// Measure bounding box of non-background pixels in [xStart,xEnd) x [0,h).
// Pixels array is RGB, bottom-up from glReadPixels. We flip y here so minY
// corresponds to the visual top (consistent with reported coordinates).
// NOTE: glReadPixels returns bottom-up; y=0 in the buffer is the bottom pixel row.
static BombSceneBounds MeasureBounds(const unsigned char* pixels, int w, int h,
                                     int xStart, int xEnd) {
    BombSceneBounds b;
    b.minX = xEnd;   b.maxX = xStart - 1;
    b.minY = h;      b.maxY = -1;
    b.nonBgCount = 0;

    for (int y = 0; y < h; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const unsigned char* px = pixels + (y * w + x) * 3;
            if (!IsBackground(px[0], px[1], px[2])) {
                ++b.nonBgCount;
                if (x < b.minX) b.minX = x;
                if (x > b.maxX) b.maxX = x;
                if (y < b.minY) b.minY = y;
                if (y > b.maxY) b.maxY = y;
            }
        }
    }
    return b;
}

// Render one frame with both bombs. Returns false if a fatal setup error occurs.
static bool RenderBombFrame(Bomb* homeBomb, Bomb* dojoBomb, Game& game,
                            SDL_Window* window) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    dm.BeginFrame();

    // Override clear with bright green so non-bomb pixels are trivially detected.
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Standard game projection (centered ortho).
    if (game_work.m_FruitCamera)
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

    // 3D entity draw: depth write + test ON, matching GameDraw.
    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);

    // Draw both bombs manually (not via ActorManager::Draw which skips entities
    // with ENT_INACTIVE or ENT_KILLED; our bombs are live so either path works,
    // but direct draw is simpler and deterministic).
    if (homeBomb) homeBomb->Draw(game.renderer);
    if (dojoBomb) dojoBomb->Draw(game.renderer);

    dm.SetDepthBuffer(false);
    return true;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone entity scale regression test

    fn::TestHarness h(argc, argv, "scene_bomb_scale");
    h.SetInteractiveDefault(false);   // headless by default (CTest mode)
    h.SetInitFrames(0);

    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // GameInit sets up the factory delegates, ActorManager pools, FruitCamera,
    // FruitInfo tables (bomb size), and calls Bomb::LoadContent.
    // Entered through the task dispatcher (NOT a bare GameInit(0)) so
    // GameTaskExit dispatches GameExit at teardown -- see EnterGameState().
    h.EnterGameState();
    Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);

    // Verify bomb model loaded.
    // (g_bombData is file-static in Bomb.cpp; we probe it indirectly by spawning.)
    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: actorManager is null after GameInit\n");
        return 1;
    }

    // --- Spawn HOME bomb (entity type 1) ---
    Mortar::Entity* eHome = am->Add(1, true);
    if (!eHome) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: actorManager->Add(1) for home bomb returned null\n");
        return 1;
    }
    Bomb* homeBomb = static_cast<Bomb*>(eHome);
    homeBomb->Init(NULL, 0, NULL);

    // Override position and scale to known test values.
    homeBomb->pos           = _Vector3<float>(HOME_POS_X, BOMB_POS_Y, 0.0f);
    homeBomb->vel           = _Vector3<float>(0.0f, 0.0f, 0.0f);
    homeBomb->m_AccelForce  = _Vector3<float>(0.0f, 0.0f, 0.0f);
    homeBomb->m_ZPosition   = BOMB_Z_POS;
    // Bomb::Init sets scale = Vec3::One() * (bombSize * 0.01 * scaleFactor).
    // We override to the exact test value.
    homeBomb->scale         = _Vector3<float>(HOME_SCALE, HOME_SCALE, HOME_SCALE);
    // Lock rotation for a stable screenshot.
    homeBomb->m_RotVelX     = (int16_t)0;
    homeBomb->m_RotVelY     = (int16_t)0;
    homeBomb->m_RotX        = (int16_t)0;
    homeBomb->m_RotY        = (int16_t)0;
    // Activate.
    homeBomb->flags &= ~(uint32_t)(0x01 | 0x10);

    // --- Spawn DOJO bomb (entity type 1) ---
    Mortar::Entity* eDojo = am->Add(1, true);
    if (!eDojo) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: actorManager->Add(1) for dojo bomb returned null\n");
        homeBomb->KillBomb();
        return 1;
    }
    Bomb* dojoBomb = static_cast<Bomb*>(eDojo);
    dojoBomb->Init(NULL, 0, NULL);

    dojoBomb->pos           = _Vector3<float>(DOJO_POS_X, BOMB_POS_Y, 0.0f);
    dojoBomb->vel           = _Vector3<float>(0.0f, 0.0f, 0.0f);
    dojoBomb->m_AccelForce  = _Vector3<float>(0.0f, 0.0f, 0.0f);
    dojoBomb->m_ZPosition   = BOMB_Z_POS;
    dojoBomb->scale         = _Vector3<float>(DOJO_SCALE, DOJO_SCALE, DOJO_SCALE);
    dojoBomb->m_RotVelX     = (int16_t)0;
    dojoBomb->m_RotVelY     = (int16_t)0;
    dojoBomb->m_RotX        = (int16_t)0;
    dojoBomb->m_RotY        = (int16_t)0;
    dojoBomb->flags &= ~(uint32_t)(0x01 | 0x10);

    printf("[scene_bomb_scale] home bomb: pos=(%.1f,%.1f) scale=%.4f\n",
           homeBomb->pos.x, homeBomb->pos.y, homeBomb->scale.x);
    printf("[scene_bomb_scale] dojo bomb: pos=(%.1f,%.1f) scale=%.4f\n",
           dojoBomb->pos.x, dojoBomb->pos.y, dojoBomb->scale.x);
    printf("[scene_bomb_scale] expected rendered ratio (dojo/home) = %.3f +/- %.3f\n",
           EXPECTED_RATIO, RATIO_TOLERANCE);

    if (h.IsInteractive()) {
        // Interactive loop: render continuously so the user can visually inspect.
        SDL_GL_SetSwapInterval(1);
        bool running = true;
        while (running && h.game.running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            }
            if (!RenderBombFrame(homeBomb, dojoBomb, h.game,
                                 static_cast<SDL_Window*>(h.window))) break;
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
        }
        homeBomb->KillBomb();
        dojoBomb->KillBomb();
        return h.Shutdown();
    }

    // --- Headless measurement mode ---
    // Render a warm-up frame (textures may need one tick to upload).
    RenderBombFrame(homeBomb, dojoBomb, h.game, static_cast<SDL_Window*>(h.window));
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));

    // Render the measurement frame without calling SDL_GL_SwapWindow so
    // glReadPixels captures the back buffer that was just rendered.
    RenderBombFrame(homeBomb, dojoBomb, h.game, static_cast<SDL_Window*>(h.window));

    // Read pixels (RGB, bottom-up).
    int fw = 0, fh = 0;
    unsigned char* pixels = h.ReadPixels(&fw, &fh);
    if (!pixels) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: glReadPixels returned null\n");
        homeBomb->KillBomb();
        dojoBomb->KillBomb();
        return h.Shutdown();
    }

    // Save screenshot for visual inspection (re-reads the intact back buffer).
    h.ScreenshotPng("scene_bomb_scale");

    // Measure each half.
    // The window is 960x640 (from TestHarness::Init).
    // Left half  = x in [0, fw/2)   -> HOME bomb (world x=-100, rendered to left)
    // Right half = x in [fw/2, fw)  -> DOJO bomb (world x=+100, rendered to right)
    //
    // In the centered ortho (x: -240..+240 maps to screen right-to-left on Bada),
    // the port uses x: -240 = left edge, +240 = right edge of the window.
    // home bomb at x=-100 -> left half; dojo bomb at x=+100 -> right half.
    int midX = fw / 2;
    BombSceneBounds leftBounds  = MeasureBounds(pixels, fw, fh, 0,    midX);
    BombSceneBounds rightBounds = MeasureBounds(pixels, fw, fh, midX, fw);
    free(pixels);

    printf("[scene_bomb_scale] LEFT  (home bomb):  nonBgPixels=%d  bbox=[%d..%d]x[%d..%d]  w=%d h=%d\n",
           leftBounds.nonBgCount,
           leftBounds.minX, leftBounds.maxX, leftBounds.minY, leftBounds.maxY,
           leftBounds.maxX - leftBounds.minX + 1,
           leftBounds.maxY - leftBounds.minY + 1);
    printf("[scene_bomb_scale] RIGHT (dojo bomb):  nonBgPixels=%d  bbox=[%d..%d]x[%d..%d]  w=%d h=%d\n",
           rightBounds.nonBgCount,
           rightBounds.minX, rightBounds.maxX, rightBounds.minY, rightBounds.maxY,
           rightBounds.maxX - rightBounds.minX + 1,
           rightBounds.maxY - rightBounds.minY + 1);

    // Require both bombs actually rendered (non-trivially).
    const int MIN_PIXELS = 50;
    if (leftBounds.nonBgCount < MIN_PIXELS) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: home bomb (left) did not render: "
                "only %d non-background pixels (< %d min)\n",
                leftBounds.nonBgCount, MIN_PIXELS);
        fprintf(stderr, "[scene_bomb_scale] Possible causes: model not loaded, "
                "bomb off-screen, all pixels matched background colour\n");
        homeBomb->KillBomb();
        dojoBomb->KillBomb();
        return 1;
    }
    if (rightBounds.nonBgCount < MIN_PIXELS) {
        fprintf(stderr, "[scene_bomb_scale] FAIL: dojo bomb (right) did not render: "
                "only %d non-background pixels (< %d min)\n",
                rightBounds.nonBgCount, MIN_PIXELS);
        homeBomb->KillBomb();
        dojoBomb->KillBomb();
        return 1;
    }

    // Compute rendered extents (bounding-box width and height for each bomb).
    int homeW = leftBounds.maxX  - leftBounds.minX  + 1;
    int homeH = leftBounds.maxY  - leftBounds.minY  + 1;
    int dojoW = rightBounds.maxX - rightBounds.minX + 1;
    int dojoH = rightBounds.maxY - rightBounds.minY + 1;

    float ratioW = (homeW > 0) ? (float)dojoW / (float)homeW : 0.0f;
    float ratioH = (homeH > 0) ? (float)dojoH / (float)homeH : 0.0f;
    float ratioPixels = (leftBounds.nonBgCount > 0)
                        ? (float)rightBounds.nonBgCount / (float)leftBounds.nonBgCount
                        : 0.0f;

    printf("[scene_bomb_scale] home bbox w=%d h=%d\n", homeW, homeH);
    printf("[scene_bomb_scale] dojo bbox w=%d h=%d\n", dojoW, dojoH);
    printf("[scene_bomb_scale] measured ratio (dojo/home): width=%.3f height=%.3f pixels=%.3f\n",
           ratioW, ratioH, ratioPixels);
    printf("[scene_bomb_scale] expected ratio = %.3f  tolerance = +/-%.3f\n",
           EXPECTED_RATIO, RATIO_TOLERANCE);

    // Use the average of width and height ratios as the primary metric.
    float avgRatio = (ratioW + ratioH) * 0.5f;
    printf("[scene_bomb_scale] average (w+h)/2 ratio = %.3f\n", avgRatio);

    bool pass = (fabsf(avgRatio - EXPECTED_RATIO) <= RATIO_TOLERANCE);

    if (!pass) {
        // Distinguish "same size" (rendering bug) from "wrong ratio" (other bug).
        if (fabsf(avgRatio - 1.0f) < RATIO_TOLERANCE) {
            fprintf(stderr, "[scene_bomb_scale] FAIL: both bombs render at the SAME pixel "
                    "size (ratio=%.3f ~1.0) -- confirms rendering bug: "
                    "Bomb::Draw is NOT honouring entity->scale\n", avgRatio);
        } else {
            fprintf(stderr, "[scene_bomb_scale] FAIL: ratio %.3f deviates from expected "
                    "%.3f by %.3f (tolerance %.3f)\n",
                    avgRatio, EXPECTED_RATIO,
                    fabsf(avgRatio - EXPECTED_RATIO), RATIO_TOLERANCE);
        }
    } else {
        printf("[scene_bomb_scale] PASS: dojo bomb renders %.3f x home bomb "
               "(expected %.3f +/- %.3f) -- scale honoured correctly\n",
               avgRatio, EXPECTED_RATIO, RATIO_TOLERANCE);
    }

    homeBomb->KillBomb();
    dojoBomb->KillBomb();
    h.Shutdown();
    return pass ? 0 : 1;
}
