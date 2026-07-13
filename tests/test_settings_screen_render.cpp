// test_settings_screen_render.cpp -- render screenshots for the port-improvement
// SettingsScreen (the in-game settings modal built from the src/ui/ port-only
// widget toolkit: UiDropdown / UiCheckbox / UiSlider).
//
// Usage: test_settings_screen_render [--screenshot|--interactive|--headless]
//
// SettingsScreen no longer drives its widgets' Update/Draw directly -- each
// widget (LANGUAGE UiDropdown, MOTION MODE / FPS COUNTER UiCheckbox,
// SENSITIVITY UiSlider, the close BSButton) is AddControl'd to game_work.mHud
// with HUD_LAYER_TOP_MOST (see SettingsScreen::Init()), so this test drives
// the HUD's own Update/Draw (component-isolation mode, TOP_MOST layer mask)
// rather than calling screen.Draw() directly -- that only paints the
// backdrop/plate/labels now.
//
// Three captures:
//   settings.png          -- collapsed (default state, all rows visible).
//   settings_expanded.png -- LANGUAGE dropdown forced open (SetOpenForTest via
//                             SettingsScreen::GetLangDropForTest()), proving the
//                             draw-order overlay (dropdown panel painted after
//                             every sibling widget -- see Init()'s AddControl
//                             order) and the m_Active input/draw gate on the
//                             other widgets while the panel is open.
//   settings_scrolled.png -- dropdown closed again, then scrolled to
//                             m_MaxScroll (SetScrollForTest, which clamps to
//                             [0, m_MaxScroll]) so BOTH edge fades are
//                             visible: top fade over the collapsed LANGUAGE
//                             bar dissolving above, and the FPS COUNTER row
//                             revealed at the bottom of the viewport.
//   settings_scrolled_open.png -- pane still scrolled to m_MaxScroll AND the
//                             LANGUAGE dropdown re-opened (SetOpenForTest),
//                             to check whether the collapsed dropdown's open
//                             panel spills over the plate edge when scrolled.
//
// Output PNGs (--screenshot mode):
//   tmp/test/screenshots/settings_screen/settings.png
//   tmp/test/screenshots/settings_screen/settings_expanded.png
//   tmp/test/screenshots/settings_screen/settings_scrolled.png
//   tmp/test/screenshots/settings_screen/settings_scrolled_open.png
//
// NOTE: SettingsScreen is a port-improvement with NO binary counterpart; this
// test validates that the screen constructs, seeds its widgets from the live
// globals, and draws its panel + rows without crashing -- NOT any
// binary-faithful layout.
//
// C++11 / GCC 4.4.1 clean (host-only test TU; kept lambda/auto/range-for free).

#include "test_harness.h"
#include "screens/SettingsScreen.h"
#include "ui/UiDropdown.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "game/GameWork.h"
#include <cstdio>
#include <SDL.h>

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "settings_screen/settings");
    // Burn-in frames: let GameInitialise load fonts (pFontMain) before we draw labels.
    h.SetInitFrames(90);
    if (!h.ParseFlags()) return 1;
    // Component isolation: clears the HUD so only our screen + its AddControl'd
    // widgets render on the background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    int failures = 0;
    const int kLayerMask = Mortar::HUD_LAYER_TOP_MOST;

    // Heap-allocated + AddControl'd (mirrors test_gamemodescreen.cpp /
    // test_aboutscreen.cpp / test_shopscreen.cpp) -- NOT deleted explicitly.
    // The screen must be AddControl'd BEFORE Init(): Init() AddControl's the
    // widgets, and HUD::Draw has no per-control sort key (only control-list
    // order), so the screen needs to already be first in the list for its own
    // Draw() (backdrop+plate+labels) to paint behind them. See
    // SettingsScreen.h usage note / SettingsScreen::Toggle(). Both the screen
    // and every widget it AddControl's live in game_work.mHud->controls until
    // h.Shutdown() -> game.shutdown() -> HUD::Release() deletes them (m_bNoDestructor
    // defaults to 0), while the GL context is still alive.
    SettingsScreen* screen = new SettingsScreen();
    game_work.mHud->AddControl(screen, false);
    screen->Init();
    // Test-only: skip the drop-in animation (Toggle() would normally start it
    // in ANIM_OPENING, off-screen) so captures are deterministic at rest
    // (offset 0, full backdrop alpha) regardless of settle-frame count -- the
    // real open animation is ~0.28s/~17 frames at 60fps dt, longer than the
    // settle windows below. See SettingsScreen.h SetAnimOpenForTest().
    screen->SetAnimOpenForTest();

    // ---- Settle (drives every widget's Update via HUD::Update) ----
    h.RunComponentHeadless(8, kLayerMask);

    if (h.IsScreenshot()) {
        h.RunComponentHeadless(1, kLayerMask);
        if (!h.ScreenshotPng("settings_screen/settings")) {
            std::fprintf(stderr, "FAIL: ScreenshotPng (collapsed) failed\n");
            ++failures;
        } else {
            std::printf("[settings_screen] collapsed screenshot written\n");
        }
    }

    // ---- Expanded: force the LANGUAGE dropdown open, re-settle, capture ----
    UiDropdown* drop = screen->GetLangDropForTest();
    if (!drop) {
        std::fprintf(stderr, "FAIL: GetLangDropForTest() returned null\n");
        ++failures;
    } else {
        drop->SetOpenForTest(true);
        drop->SetHoverRowForTest(2);

        h.RunComponentHeadless(4, kLayerMask);

        if (!drop->IsOpen()) {
            std::fprintf(stderr, "FAIL: dropdown did not stay open after settle frames\n");
            ++failures;
        }

        if (h.IsScreenshot()) {
            h.RunComponentHeadless(1, kLayerMask);
            if (!h.ScreenshotPng("settings_screen/settings_expanded")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng (expanded) failed\n");
                ++failures;
            } else {
                std::printf("[settings_screen] expanded screenshot written\n");
            }
        }
    }

    // ---- Close the dropdown before scrolling so the scrolled capture shows
    // the collapsed LANGUAGE bar, not the still-open dropdown panel ----
    if (drop) {
        drop->SetOpenForTest(false);
        h.RunComponentHeadless(1, kLayerMask);
    }

    // ---- Scrolled: drive to m_MaxScroll (SetScrollForTest clamps), re-settle,
    // capture, then reset back to 0 ----
    screen->SetScrollForTest(1000.0f);

    h.RunComponentHeadless(4, kLayerMask);

    if (h.IsScreenshot()) {
        h.RunComponentHeadless(1, kLayerMask);
        if (!h.ScreenshotPng("settings_screen/settings_scrolled")) {
            std::fprintf(stderr, "FAIL: ScreenshotPng (scrolled) failed\n");
            ++failures;
        } else {
            std::printf("[settings_screen] scrolled screenshot written\n");
        }
    }

    // ---- Scrolled + open: re-open the dropdown while still scrolled to
    // m_MaxScroll, re-settle, capture, then reset (close + scroll 0) ----
    if (drop) {
        drop->SetOpenForTest(true);
        screen->SetScrollForTest(1000.0f);

        h.RunComponentHeadless(4, kLayerMask);

        if (h.IsScreenshot()) {
            h.RunComponentHeadless(1, kLayerMask);
            if (!h.ScreenshotPng("settings_screen/settings_scrolled_open")) {
                std::fprintf(stderr, "FAIL: ScreenshotPng (scrolled_open) failed\n");
                ++failures;
            } else {
                std::printf("[settings_screen] scrolled+open screenshot written\n");
            }
        }

        drop->SetOpenForTest(false);
    }

    screen->SetScrollForTest(0.0f);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, -1, kLayerMask);
    }

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: settings_screen_render OK\n");
    return h.Shutdown();
}
