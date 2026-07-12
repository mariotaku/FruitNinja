// test_settings_screen_render.cpp -- render screenshot for the port-improvement
// SettingsScreen (the in-game settings modal built from the resurrected dead-code
// widget stack: ComboBox / CheckBox / SliderControl).
//
// Usage: test_settings_screen_render [--screenshot|--interactive|--headless]
//
// Renders the whole modal panel once: the dialog_box plate, the four labelled
// rows -- LANGUAGE (ComboBox, collapsed), MOTION MODE (CheckBox), SENSITIVITY
// (SliderControl, indented), FPS COUNTER (CheckBox) -- each drawn by the screen's
// own Draw(). The screen loads the real staged widget textures in Init() (see
// SettingsScreen.cpp) -- generated from assets/ui-widgets/*.svg by
// fn_asset_staging (tools/assets/svg-to-webp.mjs, mandatory) -- plus a couple of
// procedural-only fills (WidgetPlaceholderArt.h) for elements with no real .tex
// counterpart (list-row tint, modal backdrop dim).
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/settings_screen/settings.png
//
// NOTE: SettingsScreen is a port-improvement with NO binary counterpart; this test
// validates that the screen constructs, seeds its widgets from the live globals,
// and draws its panel + rows without crashing -- NOT any binary-faithful layout.
//
// C++11 / GCC 4.4.1 clean (host-only test TU; kept lambda/auto/range-for free).

#include "test_harness.h"
#include "screens/SettingsScreen.h"
#include "hud/HUD.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include <cstdio>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw the settings modal. Caller swaps.
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, SettingsScreen* screen)
{
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };
    screen->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "settings_screen/settings");
    // Burn-in frames: let GameInitialise load fonts (pFontMain) before we draw labels.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our screen renders on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    int failures = 0;

    {
        // The screen owns its widgets; Init() builds them + loads their textures.
        SettingsScreen screen;
        screen.Init();

        // ---- Settle (drives each widget's Update via the screen) + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            screen.Update(1.0f / 60.0f);
            DrawPass(h, &screen);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &screen);
            if (!h.ScreenshotPng("settings_screen/settings")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[settings_screen] screenshot written\n");
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
                screen.Update(1.0f / 60.0f);
                DrawPass(h, &screen);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }

        // screen.Release() runs in the dtor here, while the GL context is still
        // alive (widget/placeholder-texture teardown does glDeleteTextures).
    }

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: settings_screen_render OK\n");
    return h.Shutdown();
}
