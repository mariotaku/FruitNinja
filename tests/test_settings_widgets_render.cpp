// test_settings_widgets_render.cpp -- isolated render screenshot for the CheckBox
// and SliderControl widgets (dead code in v1.6.1, but fully implemented).
//
// Usage: test_settings_widgets_render [--screenshot|--interactive|--headless]
//
// Renders:
//   * CheckBox unchecked + CheckBox checked (two instances) -- the state shows as
//     a different quad texture (unchecked=grey, checked=green).
//   * SliderControl with the thumb at min / mid / max value (three instances) --
//     the thumb quad translates along the track by (value/max).
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/settings_widgets/widgets.png
//
// NOTE: this validates widget GEOMETRY + STATE (checkbox on/off, thumb position by
// value), NOT the final shipped art. The faithful textures (checked.tex,
// unchecked.tex, _dialog_box.tex, slider_will.tex) are NOT shipped in v1.6.1 (the
// widgets are dead code), so the test injects in-memory solid-colour substitute
// textures via CheckBox/SliderControl::SetTexturesForTest. Widget positions are
// test-chosen (v1.6.1 never places these widgets), only their relative geometry and
// value-driven state are meaningful here.
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "hud/CheckBox.h"
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
#include <SDL.h>

// ---------------------------------------------------------------------------
// Build a solid-colour GL texture wrapped in a Texture2D_Bada. Gives the widget
// a valid texId + apparent dimensions (SliderControl's ctor reads the dims to
// size its track/thumb quads).
// ---------------------------------------------------------------------------
static Mortar::SmartPtr<Mortar::Texture> MakeSolidTex(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
    for (int i = 0; i < w * h; ++i) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = (a != 255);
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw all widgets. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h,
                     CheckBox* cbUnchecked, CheckBox* cbChecked,
                     SliderControl* slMin, SliderControl* slMid, SliderControl* slMax)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    // hudScale of (1,1,1): label tint is identity (pure Yellow), quads unaffected.
    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    cbUnchecked->Draw(hudScale);
    cbChecked->Draw(hudScale);
    slMin->Draw(hudScale);
    slMid->Draw(hudScale);
    slMax->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "settings_widgets/widgets");
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
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texChecked   = MakeSolidTex(40, 200, 40, 255, 64, 64);   // green
    Mortar::SmartPtr<Mortar::Texture> texUnchecked = MakeSolidTex(70, 70, 70, 255, 64, 64);    // grey
    Mortar::SmartPtr<Mortar::Texture> texTrack     = MakeSolidTex(120, 120, 120, 255, 200, 20); // light grey bar
    Mortar::SmartPtr<Mortar::Texture> texThumb     = MakeSolidTex(240, 140, 20, 255, 20, 34);   // orange knob

    CheckBox::SetTexturesForTest(texChecked, texUnchecked);
    SliderControl::SetTexturesForTest(texTrack, texThumb);

    int failures = 0;

    {
        // ---- CheckBoxes ----
        // Quad is a HARDCODED 128x64 at pos; the two boxes are separated along the
        // pos.x axis by 150 (> the 128 quad extent) so they do not overlap.
        // Both default to CHECKED (binary ctor sets m_Checked=1); clear one so the
        // two instances render distinct texture state (grey unchecked vs green checked).
        CheckBox cbUnchecked(Vec3(80.0f, -140.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "OFF");
        CheckBox cbChecked  (Vec3(-70.0f, -140.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "ON");
        cbUnchecked.SetCheckedForTest(false);

        if (!cbChecked.IsChecked() || cbUnchecked.IsChecked()) {
            std::fprintf(stderr, "FAIL: checkbox state not as expected (checked=%d unchecked=%d)\n",
                         (int)cbChecked.IsChecked(), (int)cbUnchecked.IsChecked());
            ++failures;
        }

        // ---- Sliders (min / mid / max) ----
        // min=0, max=100. Track is 200 long (from the injected 200px track texture);
        // thumb travels ~ +/-94 about pos.x. Separated along pos.y by 80 so the three
        // bars sit side-by-side.
        SliderControl slMin(Vec3(0.0f, -40.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MIN", 0, 100, 24, 0);
        SliderControl slMid(Vec3(0.0f, 40.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MID", 0, 100, 24, 50);
        SliderControl slMax(Vec3(0.0f, 120.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                            "MAX", 0, 100, 24, 100);

        if (slMin.GetValue() != 0 || slMid.GetValue() != 50 || slMax.GetValue() != 100) {
            std::fprintf(stderr, "FAIL: slider initial values not stored correctly\n");
            ++failures;
        }
        // Track width must have been sized from the injected 200px track texture (fields
        // are public on SliderControl).
        std::printf("[settings_widgets] slider trackW=%.1f trackH=%.1f thumbW=%.1f thumbH=%.1f\n",
                    (double)slMin.m_TrackWidth, (double)slMin.m_TrackHeight,
                    (double)slMin.m_ThumbWidth, (double)slMin.m_ThumbHeight);
        if (slMin.m_TrackWidth != 200.0f) {
            std::fprintf(stderr, "FAIL: slider trackW=%.1f (expected 200)\n",
                         (double)slMin.m_TrackWidth);
            ++failures;
        }

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &cbUnchecked, &cbChecked, &slMin, &slMid, &slMax);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &cbUnchecked, &cbChecked, &slMin, &slMid, &slMax);
            if (!h.ScreenshotPng("settings_widgets/widgets")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[settings_widgets] screenshot written\n");
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
                DrawPass(h, &cbUnchecked, &cbChecked, &slMin, &slMid, &slMax);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }
    } // widgets destroyed while GL context still alive

    // Release the substitute textures (both the static-slot refs and the local
    // refs) while the GL context is still alive -- glDeleteTextures runs inside the
    // Texture2D_Bada dtor, which must not happen after Shutdown tears down GL.
    CheckBox::UnloadContent();
    SliderControl::UnloadContent();
    texChecked.SetNull();
    texUnchecked.SetNull();
    texTrack.SetNull();
    texThumb.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: settings_widgets_render OK\n");
    return h.Shutdown();
}
