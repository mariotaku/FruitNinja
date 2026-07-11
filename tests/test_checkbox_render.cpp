// test_checkbox_render.cpp -- isolated render screenshot for the CheckBox widget
// (dead code in v1.6.1, but fully implemented).
//
// Usage: test_checkbox_render [--screenshot|--interactive|--headless]
//
// Renders CheckBox unchecked + CheckBox checked (two instances) -- the state shows
// as a rounded-square CHECKBOX texture (grey outline / transparent interior =
// unchecked, green fill + white checkmark tick = checked). CheckBox::Draw scales a
// HARDCODED 128x64 quad and swaps between two textures purely by m_Checked; the
// real binary textures are named checked.tex / unchecked.tex (a tick-box, not an
// on/off switch), so the placeholder mirrors that shape, sized to fit within the
// widget's hardcoded hit-rect (pos.x +/-36 / pos.y +/-28.5).
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/checkbox/checkbox.png
//
// NOTE: validates widget GEOMETRY + STATE (checkbox on/off), NOT the final shipped
// art. The faithful textures (checked.tex / unchecked.tex) are NOT shipped in
// v1.6.1 (the widget is dead code), so the test injects in-memory
// PROCEDURALLY-DRAWN substitute textures via CheckBox::SetTexturesForTest --
// rounded-rect + checkmark signed-distance fills, no external image deps. Widget
// positions are test-chosen (v1.6.1 never places this widget); only its geometry
// and checked/unchecked state are meaningful here.
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "hud/CheckBox.h"
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

// Placeholder-art texture makers (MakeCheckboxTex + the SDF helpers) are shared
// with test_slider_render.cpp and test_settings_interactive.cpp -- see the header.
#include "widget_placeholder_art.h"
using namespace fn_widget_art;

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw both checkboxes. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, CheckBox* cbUnchecked, CheckBox* cbChecked)
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
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "checkbox/checkbox");
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
    // CheckBox's quad is a HARDCODED 128x64 (2:1) -- generate the checkbox textures
    // at that exact size so the 1:1 texel mapping is crisp (no stretch).
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texChecked   = MakeCheckboxTex(true,  128, 64); // checked   -- green box, white tick
    Mortar::SmartPtr<Mortar::Texture> texUnchecked = MakeCheckboxTex(false, 128, 64); // unchecked -- empty box, grey outline

    CheckBox::SetTexturesForTest(texChecked, texUnchecked);

    int failures = 0;

    {
        // Quad is a HARDCODED 128x64 at pos; the two boxes are separated along the
        // pos.x axis by 150 (> the 128 quad extent) so they do not overlap.
        // Both default to CHECKED (binary ctor sets m_Checked=1); clear one so the
        // two instances render distinct texture state (grey unchecked vs green checked).
        CheckBox cbChecked  (Vec3(-75.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "ON");
        CheckBox cbUnchecked(Vec3( 75.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "OFF");
        cbUnchecked.SetCheckedForTest(false);

        if (!cbChecked.IsChecked() || cbUnchecked.IsChecked()) {
            std::fprintf(stderr, "FAIL: checkbox state not as expected (checked=%d unchecked=%d)\n",
                         (int)cbChecked.IsChecked(), (int)cbUnchecked.IsChecked());
            ++failures;
        }

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &cbUnchecked, &cbChecked);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &cbUnchecked, &cbChecked);
            if (!h.ScreenshotPng("checkbox/checkbox")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[checkbox] screenshot written\n");
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
                DrawPass(h, &cbUnchecked, &cbChecked);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }
    } // widgets destroyed while GL context still alive

    // Release the substitute textures (both the static-slot refs and the local
    // refs) while the GL context is still alive -- glDeleteTextures runs inside the
    // Texture2D_Bada dtor, which must not happen after Shutdown tears down GL.
    CheckBox::UnloadContent();
    texChecked.SetNull();
    texUnchecked.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: checkbox_render OK\n");
    return h.Shutdown();
}
