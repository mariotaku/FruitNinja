// test_ui_dropdown_render.cpp -- isolated render screenshot for the port-only
// UiDropdown widget (src/ui/UiDropdown.h). NO binary counterpart -- see
// src/ui/UiWidget.h for why this toolkit exists.
//
// Usage: test_ui_dropdown_render [--screenshot|--interactive|--headless]
//
// Renders two UiDropdown instances using the REAL staged box.tex (NineSlice
// bar/panel) + caret.tex (collapsed-bar glyph) textures -- generated at
// build time from assets/ui-widgets/box.svg / caret.svg by fn_asset_staging.
// NO procedural fallback.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/ui_dropdown/ui_dropdown.png

#include "test_harness.h"
#include "ui/UiDropdown.h"
#include "game/GameWork.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <SDL.h>

// ---------------------------------------------------------------------------
// Render pass: clear + ortho + draw both dropdowns. Caller swaps.
// Panel-overlay convention: draw the OPEN dropdown last so its panel overlays
// any sibling widget drawn earlier in the same frame (see UiDropdown.h).
// ---------------------------------------------------------------------------
static void DrawPass(fn::TestHarness& h, UiDropdown* ddCollapsed, UiDropdown* ddOpen) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(h.window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    float hudScale[3] = { 1.0f, 1.0f, 1.0f };

    ddCollapsed->Draw(hudScale);
    ddOpen->Draw(hudScale);
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "ui_dropdown/ui_dropdown");
    // Burn-in frames: let GameInitialise settle before we draw.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our widgets render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    Mortar::SmartPtr<Mortar::Texture> texBar = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    // caret.tex is staged (assets/ui-widgets/caret.svg -> generated/caret.tex);
    // no substitution needed.
    Mortar::SmartPtr<Mortar::Texture> texCaret = Mortar::TextureManager::LoadLocalisedTexture("caret.tex");
    // list_fade.tex: open-list top/bottom edge fade (UiDropdown::DrawFadeEdges).
    Mortar::SmartPtr<Mortar::Texture> texFade = Mortar::TextureManager::LoadLocalisedTexture("list_fade.tex");
    // list_item.tex: borderless glossy row-highlight gradient (selected/hover).
    Mortar::SmartPtr<Mortar::Texture> texItem = Mortar::TextureManager::LoadLocalisedTexture("list_item.tex");

    if (!texBar.IsValid()) {
        std::fprintf(stderr, "FAIL: failed to load staged box.tex "
                             "-- check fn_asset_staging ran and FN_DATA_DIR_PATH is set\n");
        h.Shutdown();
        return 1;
    }
    if (!texCaret.IsValid()) {
        std::fprintf(stderr, "[ui_dropdown] warning: caret.tex not staged, "
                             "collapsed-bar caret glyph will be skipped (DrawGlyphQuad guards on validity)\n");
    }
    if (!texFade.IsValid()) {
        std::fprintf(stderr, "[ui_dropdown] warning: list_fade.tex not staged, "
                             "open-list edge fade will be skipped (DrawFadeEdges guards on validity)\n");
    }
    if (!texItem.IsValid()) {
        std::fprintf(stderr, "[ui_dropdown] warning: list_item.tex not staged, "
                             "row highlight will fall back to the box.tex NineSlice\n");
    }

    int failures = 0;

    {
        // ALL CAPS: font_fruit_ninja.fnt (Data/fonts/font_fruit_ninja.fnt) has
        // only 92 chars -- digits/uppercase/punctuation, NO lowercase glyphs
        // (matches the binary; SettingsScreen labels are all-caps for the
        // same reason). Mixed-case strings would silently drop every
        // lowercase glyph (GetCharTemplate returns null, glyph skipped).
        std::vector<std::string> items;
        items.push_back("APPLE");
        items.push_back("BANANA");
        items.push_back("CHERRY");
        items.push_back("DRAGONFRUIT");
        items.push_back("ELDERBERRY");
        items.push_back("FIG");
        items.push_back("GRAPE");
        items.push_back("HONEYDEW");
        items.push_back("KIWI");
        items.push_back("LEMON");
        items.push_back("MANGO");
        items.push_back("NECTARINE");
        items.push_back("ORANGE");
        items.push_back("PAPAYA");
        items.push_back("QUINCE");
        items.push_back("RASPBERRY");
        items.push_back("STRAWBERRY");
        items.push_back("TANGERINE");
        items.push_back("UGLI FRUIT");
        items.push_back("WATERMELON");

        const float barW = 130.0f;
        const float barH = 24.0f;
        const uint8_t visibleRows = 6;

        // (a) LEFT, collapsed: selected index > 0, default closed state.
        UiDropdown ddCollapsed(Vec3(-80.0f, 60.0f, 0.0f), items, 5, visibleRows, barW, barH);
        ddCollapsed.SetBoxTexture(texBar);
        ddCollapsed.SetCaretTexture(texCaret);

        // (b) RIGHT, force-opened: scrolled to a mid window (float world-Y
        // offset, not a row index -- see UiDropdown::m_ScrollOffset: 0 = list
        // top, +maxScroll = list bottom) + a hover row distinct from the
        // selected row's position, so both highlights show.
        int midScrollRow = (int)items.size() / 2 - 2;
        if (midScrollRow < 0) midScrollRow = 0;
        UiDropdown ddOpen(Vec3(70.0f, 100.0f, 0.0f), items, 10, visibleRows, barW, barH);
        ddOpen.SetBoxTexture(texBar);
        ddOpen.SetCaretTexture(texCaret);
        ddOpen.SetFadeTexture(texFade);
        ddOpen.SetItemTexture(texItem);
        ddOpen.SetOpenForTest(true);
        const float rowH = 28.0f;  // matches UiDropdown's default m_RowH
        ddOpen.SetScrollOffsetForTest((float)midScrollRow * rowH);
        // selected=10 lands at row (10-midScrollRow); pick a hover row
        // distinct from it so the gold selected-row and amber hover-row
        // highlights are both visible simultaneously in the screenshot.
        int selectedRow = 10 - midScrollRow;
        int hoverRow = (selectedRow == 2) ? 4 : 2;
        ddOpen.SetHoverRowForTest(hoverRow);

        if (ddCollapsed.GetSelected() != 5) {
            std::fprintf(stderr, "FAIL: ddCollapsed selected=%d (expected 5)\n", ddCollapsed.GetSelected());
            ++failures;
        }
        if (ddCollapsed.IsOpen()) {
            std::fprintf(stderr, "FAIL: ddCollapsed should be closed by default\n");
            ++failures;
        }
        if (ddOpen.GetSelected() != 10) {
            std::fprintf(stderr, "FAIL: ddOpen selected=%d (expected 10)\n", ddOpen.GetSelected());
            ++failures;
        }
        if (!ddOpen.IsOpen()) {
            std::fprintf(stderr, "FAIL: ddOpen should be open after SetOpenForTest(true)\n");
            ++failures;
        }

        std::printf("[ui_dropdown] ddCollapsed selected=%d open=%d; "
                     "ddOpen selected=%d open=%d scrollTopRow=%d hoverRow=%d\n",
                     ddCollapsed.GetSelected(), (int)ddCollapsed.IsOpen(),
                     ddOpen.GetSelected(), (int)ddOpen.IsOpen(), midScrollRow, hoverRow);

        // ---- Settle + screenshot ----
        for (int frame = 0; frame < 8; ++frame) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {}
            DrawPass(h, &ddCollapsed, &ddOpen);
            SDL_GL_SwapWindow(h.window);
        }

        if (h.IsScreenshot()) {
            DrawPass(h, &ddCollapsed, &ddOpen);
            if (!h.ScreenshotPng("ui_dropdown/ui_dropdown")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng failed\n");
                ++failures;
            } else {
                std::printf("[ui_dropdown] screenshot written\n");
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
                ddCollapsed.Update(1.0f / 60.0f);
                ddOpen.Update(1.0f / 60.0f);
                DrawPass(h, &ddCollapsed, &ddOpen);
                SDL_GL_SwapWindow(h.window);
                SDL_Delay(16);
            }
        }

        ddCollapsed.Release();
        ddOpen.Release();
    } // widgets destroyed while GL context still alive

    // Release the loaded textures while the GL context is still alive --
    // glDeleteTextures runs inside the Texture2D_Bada dtor, which must not
    // happen after Shutdown tears down GL.
    texBar.SetNull();
    texCaret.SetNull();
    texFade.SetNull();
    texItem.SetNull();

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: ui_dropdown_render OK\n");
    return h.Shutdown();
}
