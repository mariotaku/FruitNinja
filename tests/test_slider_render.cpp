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
// NOTE: validates widget GEOMETRY + STATE (thumb position by value), NOT the final
// shipped art. The faithful textures (slider_will.tex) are NOT shipped in v1.6.1
// (the widget is dead code), so the test injects in-memory PROCEDURALLY-DRAWN
// substitute textures via SliderControl::SetTexturesForTest -- a solid track bar +
// a circle-SDF knob, no external image deps. Widget positions are test-chosen
// (v1.6.1 never places this widget); only relative geometry and value-driven state
// are meaningful here.
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "hud/SliderControl.h"
#include "game/GameWork.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cmath>
#include <SDL.h>

// Placeholder-art texture makers (MakeSolidTex / MakeCircleTex + the SDF helpers)
// are shared with test_checkbox_render.cpp and test_settings_interactive.cpp.
#include "widget_placeholder_art.h"
using namespace fn_widget_art;

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
    // Substitute textures (real art is not shipped -- see file header note).
    // Inject BEFORE constructing the sliders: the SliderControl ctor reads the
    // track/thumb texture dims to size its quads.
    //
    // Knob sized to 55px so the slider's visible height matches the 55px checkbox
    // box (2*halfBox, default 27.5) -- unifies the UI height across the widgets.
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texTrack = MakeSolidTex(120, 120, 120, 255, 200, 20); // light grey bar
    Mortar::SmartPtr<Mortar::Texture> texThumb = MakeCircleTex(240, 140, 20, 55, 55);        // round orange knob (55px = checkbox height)

    SliderControl::SetTexturesForTest(texTrack, texThumb);

    int failures = 0;

    {
        // min=0, max=100. Track is 200 long (from the injected 200px track texture);
        // thumb travels ~ +/-94 about pos.x. Separated along pos.y by 80 so the three
        // bars sit stacked.
        SliderControl slMin(Vec3(0.0f,  80.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MIN", 0, 100, 24, 0);
        SliderControl slMid(Vec3(0.0f,   0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MID", 0, 100, 24, 50);
        SliderControl slMax(Vec3(0.0f, -80.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MAX", 0, 100, 24, 100);

        if (slMin.GetValue() != 0 || slMid.GetValue() != 50 || slMax.GetValue() != 100) {
            std::fprintf(stderr, "FAIL: slider initial values not stored correctly\n");
            ++failures;
        }
        // Track width must have been sized from the injected 200px track texture
        // (read via the geometry accessors -- fields are private).
        std::printf("[slider] trackW=%.1f trackH=%.1f thumbW=%.1f thumbH=%.1f\n",
                    (double)slMin.TrackWidth(), (double)slMin.TrackHeight(),
                    (double)slMin.ThumbWidth(), (double)slMin.ThumbHeight());
        if (slMin.TrackWidth() != 200.0f) {
            std::fprintf(stderr, "FAIL: slider trackW=%.1f (expected 200)\n",
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

    // Release the substitute textures (both the static-slot refs and the local
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
