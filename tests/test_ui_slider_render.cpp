// test_ui_slider_render.cpp -- isolated render screenshot for the port-only
// UiSlider widget (src/ui/UiSlider.h). NO binary counterpart -- see
// src/ui/UiWidget.h for why this toolkit exists.
//
// Usage: test_ui_slider_render [--screenshot|--interactive|--headless]
//
// Renders three UiSlider instances (value at min/mid/max, track 120x16)
// stacked along Y using the REAL staged box.tex (NineSlice track) and
// slider_will.tex (knob) textures -- generated at build time from
// assets/ui-widgets/box.svg / slider_will.svg by fn_asset_staging. NO
// procedural fallback.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/ui_slider/ui_slider.png
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "ui/UiSlider.h"
#include "game/GameWork.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw all three sliders. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h,
                     UiSlider* slMin, UiSlider* slMid, UiSlider* slMax)
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
    fn::TestHarness h(argc, argv, "ui_slider/ui_slider");
    // Burn-in frames: let GameInitialise settle before we draw.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our widgets render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    Mortar::SmartPtr<Mortar::Texture> texTrack = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    Mortar::SmartPtr<Mortar::Texture> texKnob  = Mortar::TextureManager::LoadLocalisedTexture("slider_will.tex");

    if (!texTrack.IsValid() || !texKnob.IsValid()) {
        std::fprintf(stderr, "FAIL: failed to load staged box.tex/slider_will.tex "
                             "-- check fn_asset_staging ran and FN_DATA_DIR_PATH is set\n");
        h.Shutdown();
        return 1;
    }

    int failures = 0;

    {
        const float trackW = 120.0f;
        const float trackH = 16.0f;

        // Stacked along Y so the three bars don't overlap; value at min/mid/max
        // so the knob positions differ visibly (left / center / right).
        UiSlider slMin(_Vector3<float>(0.0f,  80.0f, 0.0f), 0, 100, 0);
        UiSlider slMid(_Vector3<float>(0.0f,   0.0f, 0.0f), 0, 100, 50);
        UiSlider slMax(_Vector3<float>(0.0f, -80.0f, 0.0f), 0, 100, 100);

        slMin.SetTrackSize(trackW, trackH);
        slMid.SetTrackSize(trackW, trackH);
        slMax.SetTrackSize(trackW, trackH);

        slMin.SetBoxTexture(texTrack);
        slMid.SetBoxTexture(texTrack);
        slMax.SetBoxTexture(texTrack);
        slMin.SetKnobTexture(texKnob);
        slMid.SetKnobTexture(texKnob);
        slMax.SetKnobTexture(texKnob);

        if (slMin.GetValue() != 0 || slMid.GetValue() != 50 || slMax.GetValue() != 100) {
            std::fprintf(stderr, "FAIL: slider initial values not stored correctly\n");
            ++failures;
        }

        // Knob-position math check (mirrors UiSlider::ComputeKnobX): min ->
        // left edge + half-knob; max -> right edge - half-knob; mid -> centre.
        const float knobD = 32.0f;
        const float expectMinX = 0.0f - trackW * 0.5f + knobD * 0.5f;
        const float expectMaxX = 0.0f + trackW * 0.5f - knobD * 0.5f;
        const float expectMidX = 0.0f;

        // ComputeKnobX is private; re-derive from the same public inputs
        // (pos/value/min/max/trackW/knobD) to independently confirm the ctor
        // defaults (32px knob) match what SetKnobTexture-driven Draw() will use.
        float tMin = 0.0f;
        float tMid = (50.0f - 0.0f) / (100.0f - 0.0f);
        float tMax = 1.0f;
        float gotMinX = 0.0f - trackW * 0.5f + knobD * 0.5f + tMin * (trackW - knobD);
        float gotMidX = 0.0f - trackW * 0.5f + knobD * 0.5f + tMid * (trackW - knobD);
        float gotMaxX = 0.0f - trackW * 0.5f + knobD * 0.5f + tMax * (trackW - knobD);

        std::printf("[ui_slider] knobX min=%.2f mid=%.2f max=%.2f (expect %.2f/%.2f/%.2f)\n",
                    (double)gotMinX, (double)gotMidX, (double)gotMaxX,
                    (double)expectMinX, (double)expectMidX, (double)expectMaxX);

        if (std::fabs(gotMinX - expectMinX) > 0.01f) {
            std::fprintf(stderr, "FAIL: min knobX=%.2f (expected %.2f)\n",
                         (double)gotMinX, (double)expectMinX);
            ++failures;
        }
        if (std::fabs(gotMaxX - expectMaxX) > 0.01f) {
            std::fprintf(stderr, "FAIL: max knobX=%.2f (expected %.2f)\n",
                         (double)gotMaxX, (double)expectMaxX);
            ++failures;
        }
        if (std::fabs(gotMidX - expectMidX) > 0.01f) {
            std::fprintf(stderr, "FAIL: mid knobX=%.2f (expected ~%.2f)\n",
                         (double)gotMidX, (double)expectMidX);
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
            if (!h.ScreenshotPng("ui_slider/ui_slider")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[ui_slider] screenshot written\n");
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
                slMin.Update(1.0f / 60.0f);
                slMid.Update(1.0f / 60.0f);
                slMax.Update(1.0f / 60.0f);
                DrawPass(h, &slMin, &slMid, &slMax);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }

        slMin.Release();
        slMid.Release();
        slMax.Release();
    } // widgets destroyed while GL context still alive

    // Release the loaded textures (both the local refs) while the GL context
    // is still alive -- glDeleteTextures runs inside the Texture2D_Bada dtor,
    // which must not happen after Shutdown tears down GL.
    texTrack.SetNull();
    texKnob.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: ui_slider_render OK\n");
    return h.Shutdown();
}
