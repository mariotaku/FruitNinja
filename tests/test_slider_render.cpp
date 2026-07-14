// test_slider_render.cpp -- isolated render screenshot for the SliderControl
// widget (dead code in v1.6.1, but fully implemented).
//
// Usage: test_slider_render [--screenshot|--interactive|--headless]
//
// Renders SliderControl with the thumb at min / mid / max value (three instances)
// -- the thumb quad (a round knob) translates along the track by (value/max). The
// knob is sized to 55px so the slider's visible height matches the CheckBox box.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/slider/slider.png
//
// NOTE: validates widget GEOMETRY + STATE (thumb position by value). The port
// stages the real box.tex (track, 64x40 -- shared with ComboBox/ListBox) /
// slider_will.tex (thumb, 32x32) art at build time -- generated from
// assets/ui-widgets/*.svg by fn_asset_staging (tools/assets/svg-to-webp.mjs,
// mandatory -- fails the build if generation fails) -- so this test LOADS
// THE REAL TEXTURES via LoadLocalisedTexture and injects them via
// SliderControl::SetTexturesForTest. Widget positions are test-chosen
// (v1.6.1 never places this widget); only relative geometry and value-driven
// state are meaningful here.
//
// SliderControl's ctor reads m_TrackWidth/m_TrackHeight/m_ThumbWidth/m_ThumbHeight
// straight from the LOADED texture's pixel dims * size (SliderControl.cpp ctor,
// ~line 65) -- so the geometry assertions below target the real box.tex
// nominal dims (64x40; the loader prefers the hd_ 2x variant and halves its
// reported apparent size back to nominal -- see TextureManager.cpp).
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "hud/SliderControl.h"
#include "game/GameWork.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cmath>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw all three sliders. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h,
                     SliderControl* slMin, SliderControl* slMid, SliderControl* slMax)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    slMin->Draw(hudScale);
    slMid->Draw(hudScale);
    slMax->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "slider/slider");
    // Burn-in frames: let GameInitialise load fonts (pFontMain) before we draw labels.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our widgets render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Real staged textures (see file header note). Inject BEFORE constructing
    // the sliders: the SliderControl ctor reads the track/thumb texture dims to
    // size its quads.
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texTrack = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    Mortar::SmartPtr<Mortar::Texture> texThumb = Mortar::TextureManager::LoadLocalisedTexture("slider_will.tex");

    if (!texTrack.IsValid() || !texThumb.IsValid()) {
        std::fprintf(stderr, "FAIL: failed to load staged box.tex/slider_will.tex "
                             "-- check fn_asset_staging ran and FN_DATA_DIR_PATH is set\n");
        h.Shutdown();
        return 1;
    }

    SliderControl::SetTexturesForTest(texTrack, texThumb);

    int failures = 0;

    {
        // min=0, max=100. Track length comes from the loaded box.tex's
        // nominal pixel width (64); thumb travels proportionally about pos.x.
        // Separated along pos.y by 80 so the three bars sit stacked.
        SliderControl slMin(_Vector3<float>(0.0f,  80.0f, 0.0f), _Vector3<float>(1.0f, 1.0f, 1.0f),
                            "MIN", 0, 100, 24, 0);
        SliderControl slMid(_Vector3<float>(0.0f,   0.0f, 0.0f), _Vector3<float>(1.0f, 1.0f, 1.0f),
                            "MID", 0, 100, 24, 50);
        SliderControl slMax(_Vector3<float>(0.0f, -80.0f, 0.0f), _Vector3<float>(1.0f, 1.0f, 1.0f),
                            "MAX", 0, 100, 24, 100);

        if (slMin.GetValue() != 0 || slMid.GetValue() != 50 || slMax.GetValue() != 100) {
            std::fprintf(stderr, "FAIL: slider initial values not stored correctly\n");
            ++failures;
        }
        // Track width must have been sized from the loaded real box.tex
        // (64px nominal; read via the geometry accessors -- fields are private).
        std::printf("[slider] trackW=%.1f trackH=%.1f thumbW=%.1f thumbH=%.1f\n",
                    (double)slMin.TrackWidth(), (double)slMin.TrackHeight(),
                    (double)slMin.ThumbWidth(), (double)slMin.ThumbHeight());
        if (slMin.TrackWidth() != 64.0f) {
            std::fprintf(stderr, "FAIL: slider trackW=%.1f (expected 64)\n",
                         (double)slMin.TrackWidth());
            ++failures;
        }

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &slMin, &slMid, &slMax);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &slMin, &slMid, &slMax);
            if (!h.ScreenshotPng("slider/slider")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[slider] screenshot written\n");
            }
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsInteractive()) {
            bool running = true;
            while (running) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { running = false; break; }
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                }
                if (!running) break;
                DrawPass(h, &slMin, &slMid, &slMax);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }
    } // widgets destroyed while GL context still alive

    // Release the loaded textures (both the static-slot refs and the local
    // refs) while the GL context is still alive -- glDeleteTextures runs inside the
    // Texture2D_Bada dtor, which must not happen after Shutdown tears down GL.
    SliderControl::UnloadContent();
    texTrack.SetNull();
    texThumb.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: slider_render OK\n");
    return h.Shutdown();
}
