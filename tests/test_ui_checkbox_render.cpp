// test_ui_checkbox_render.cpp -- isolated render screenshot for the port-only
// UiCheckbox widget (src/ui/UiCheckbox.h). NO binary counterpart -- see
// src/ui/UiWidget.h for why this toolkit exists.
//
// Usage: test_ui_checkbox_render [--screenshot|--interactive|--headless]
//
// Renders two UiCheckbox instances (unchecked + checked) using the REAL
// staged box.tex (NineSlice background) and check.tex (tick glyph) textures
// -- generated at build time from assets/ui-widgets/box.svg / check.svg by
// fn_asset_staging. NO procedural fallback.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/ui_checkbox/ui_checkbox.png
//
// C++11 / GCC 4.4.1 clean (host-only test TU, but kept lambda/auto/range-for free).

#include "test_harness.h"
#include "ui/UiCheckbox.h"
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
#include <SDL.h>

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw both checkboxes. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, UiCheckbox* cbUnchecked, UiCheckbox* cbChecked)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    cbUnchecked->Draw(hudScale);
    cbChecked->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "ui_checkbox/ui_checkbox");
    // Burn-in frames: let GameInitialise settle before we draw.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our widgets render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    Mortar::SmartPtr<Mortar::Texture> texBox   = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    Mortar::SmartPtr<Mortar::Texture> texCheck = Mortar::TextureManager::LoadLocalisedTexture("check.tex");

    if (!texBox.IsValid() || !texCheck.IsValid()) {
        std::fprintf(stderr, "FAIL: failed to load staged box.tex/check.tex "
                             "-- check fn_asset_staging ran and FN_DATA_DIR_PATH is set\n");
        h.Shutdown();
        return 1;
    }

    int failures = 0;

    {
        // Two checkboxes spaced apart along X so they don't overlap.
        UiCheckbox cbUnchecked(_Vector3<float>(-60.0f, 0.0f, 0.0f), 40.0f, false);
        UiCheckbox cbChecked  (_Vector3<float>( 60.0f, 0.0f, 0.0f), 40.0f, true);

        cbUnchecked.SetBoxTexture(texBox);
        cbChecked.SetBoxTexture(texBox);
        cbUnchecked.SetCheckGlyph(texCheck);
        cbChecked.SetCheckGlyph(texCheck);

        if (cbUnchecked.IsChecked() || !cbChecked.IsChecked()) {
            std::fprintf(stderr, "FAIL: checkbox state not as expected (unchecked=%d checked=%d)\n",
                         (int)cbUnchecked.IsChecked(), (int)cbChecked.IsChecked());
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
            if (!h.ScreenshotPng("ui_checkbox/ui_checkbox")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[ui_checkbox] screenshot written\n");
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
                cbUnchecked.Update(1.0f / 60.0f);
                cbChecked.Update(1.0f / 60.0f);
                DrawPass(h, &cbUnchecked, &cbChecked);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }

        cbUnchecked.Release();
        cbChecked.Release();
    } // widgets destroyed while GL context still alive

    // Release the loaded textures (both the local refs) while the GL context
    // is still alive -- glDeleteTextures runs inside the Texture2D_Bada dtor,
    // which must not happen after Shutdown tears down GL.
    texBox.SetNull();
    texCheck.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: ui_checkbox_render OK\n");
    return h.Shutdown();
}
