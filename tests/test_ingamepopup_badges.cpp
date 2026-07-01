// test_ingamepopup_badges -- headless localisation guard + optional screenshot
// for IngamePopup NEW / SELECTED / NEW BEST! badge labels.
//
// Usage: test_ingamepopup_badges [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies per locale that:
//     - GETSTRING(0x399/LSTR_MENU_TEXTURE_09)  ("NEW")      -- non-null, non-empty
//     - GETSTRING(0x3c5/LSTR_MENU_TEXTURE_53)  ("SELECTED") -- non-null, non-empty
//     - GETSTRING(0x2dc/LSTR_GAME_TEXTURE_02)  ("NEW BEST!") -- non-null, non-empty
//     - GetIngamePopup(0x10) non-null with at least 1 text box (NEW badge)
//     - GetIngamePopup(0x11) non-null with at least 1 text box (SELECTED badge)
//     - GetIngamePopup(0x0F) non-null with at least 1 text box (NEW BEST! banner)
//
// --screenshot: renders all 3 badges laid out on screen (NEW top, SELECTED middle,
//   NEW BEST! bottom) and writes:
//   tmp/test/screenshots/badges/<lang>.png
//
// --lang=<name>  Language name or numeric flag (same set as test_shopscreen).
//   Applied BEFORE game.init() so the string table + BakedStringBox glyphs are
//   baked in the requested locale (same path as the real game).
//   arabic (0x14) auto-loads arabic.ttf via PreloadFontsTTF and renders RTL glyphs.
//
// Example commands:
//   test_ingamepopup_badges                            # headless assertions
//   test_ingamepopup_badges --screenshot               # default locale
//   test_ingamepopup_badges --screenshot --lang=chinese
//   test_ingamepopup_badges --screenshot --lang=arabic
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^ingamepopup_badges$ --output-on-failure
//
// Binary refs:
//   BuildAllPopups  v1.6.1 @0x0016e578 (called from PreloadRings @0x0011c644,
//                   which is called from GameInitialise during game.init())
//   LSTR_MENU_TEXTURE_09 = 0x399  ("NEW")
//   LSTR_MENU_TEXTURE_53 = 0x3c5  ("SELECTED")
//   LSTR_GAME_TEXTURE_02 = 0x2dc  ("NEW BEST!")

#include "test_harness.h"
#include "hud/IngamePopup.h"
#include "engine/util/StringTable.h"
#include "game/GameWork.h"
#include "engine/math/Vec3.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Language flag parsed from --lang= for screenshot label composition.
// -1 = not specified.
static int g_LangFlag = -1;

// Short language tag table (matches kLanguageSuffix order in StringTable.cpp).
// Index = languageFlag value; used to build the output PNG filename.
static const char* const kLangShort[] = {
    "en", "en_uk", "fr", "es", "de", "it", "nl", "sv", "da", "nb",
    "fi", "ko", "ja", "zh", "zh_hant", "es_419", "pl", "pt", "pt_br", "ru",
    "ar", "dbg"
};
static const int kLangShortCount = 22;

// Build the PNG label: "badges/<lang>" or "badges/default".
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "badges/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "badges/default");
    }
}

// One full render pass: clear, setup ortho, draw 3 badges, swap.
// Draws directly without going through the HUD (badges are not HUDControls).
//
// Badge layout in centered-ortho world space
//   (X: +160=top, -160=bottom landscape; Y: -240=left, +240=right; centre (0,0)):
//   NEW       (0x10): X= 60, Y=0 -- upper third
//   SELECTED  (0x11): X=  0, Y=0 -- middle
//   NEW BEST! (0x0F): X=-60, Y=0 -- lower third
//
// Draw scales match in-game usage: NEW 0.8, SELECTED 0.8, NEW BEST! 1.0.
static void RenderFrame(SDL_Window* window) {
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glViewport(0, 0, ww, wh);

    Mortar::DisplayManager::GetInstance().BeginFrame();
    MatrixManager::GetInstance().SetupOrtho(
        160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    IngamePopup* pNew  = GetIngamePopup(0x10);
    IngamePopup* pSel  = GetIngamePopup(0x11);
    IngamePopup* pBest = GetIngamePopup(0x0F);

    if (pNew)  { Vec3 pos( 60.0f, 0.0f, 0.0f); pNew->Draw(pos, 0.8f); }
    if (pSel)  { Vec3 pos(  0.0f, 0.0f, 0.0f); pSel->Draw(pos, 0.8f); }
    if (pBest) { Vec3 pos(-60.0f, 0.0f, 0.0f); pBest->Draw(pos, 1.0f); }

    SDL_GL_SwapWindow(window);
}

int main(int argc, char* argv[]) {
    // Parse --lang= for label composition; harness also parses it for the
    // languageFlag write that happens BEFORE game.init() inside Init().
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--lang=", 7) == 0) {
            g_LangFlag = ParseLanguageArg(argv[i] + 7);
        }
    }

    char shotLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), g_LangFlag);

    fn::TestHarness h(argc, argv, shotLabel);
    // 60 burn-in frames: fonts + textures live before badge text is queried.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation mode: boots game (which calls PreloadRings -> BuildAllPopups),
    // then clears HUD so only the explicit badge draws appear on screen.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Badge popups were built by BuildAllPopups() inside PreloadRings(), which
    // ran during game.init(). The language was already set from --lang= before
    // game.init() (handled by TestHarness::Init). No second BuildAllPopups() call
    // needed; the if(!s_Popups[type]) guard makes it idempotent anyway.

    int failures = 0;

    // --- Assertion 1: GETSTRING returns valid (non-null, non-empty) strings ---
    // Pins that all 3 badge keys localise in the requested language.
    {
        const char* s = GETSTRING(LSTR_MENU_TEXTURE_09, 0);
        if (!s || s[0] == '\0') {
            fprintf(stderr,
                "FAIL: GETSTRING(LSTR_MENU_TEXTURE_09 / 0x399) null or empty\n");
            ++failures;
        } else {
            printf("PASS: GETSTRING(0x399) = '%s'\n", s);
        }
    }
    {
        const char* s = GETSTRING(LSTR_MENU_TEXTURE_53, 0);
        if (!s || s[0] == '\0') {
            fprintf(stderr,
                "FAIL: GETSTRING(LSTR_MENU_TEXTURE_53 / 0x3c5) null or empty\n");
            ++failures;
        } else {
            printf("PASS: GETSTRING(0x3c5) = '%s'\n", s);
        }
    }
    {
        const char* s = GETSTRING(LSTR_GAME_TEXTURE_02, 0);
        if (!s || s[0] == '\0') {
            fprintf(stderr,
                "FAIL: GETSTRING(LSTR_GAME_TEXTURE_02 / 0x2dc) null or empty\n");
            ++failures;
        } else {
            printf("PASS: GETSTRING(0x2dc) = '%s'\n", s);
        }
    }

    // --- Assertion 2: badge popups non-null with BakedStringBox text built ---
    // PreloadRings -> BuildAllPopups creates types 0x10, 0x11, 0x0F (in binary order).
    IngamePopup* pNew  = GetIngamePopup(0x10);
    IngamePopup* pSel  = GetIngamePopup(0x11);
    IngamePopup* pBest = GetIngamePopup(0x0F);

    if (!pNew) {
        fprintf(stderr,
            "FAIL: GetIngamePopup(0x10) null -- NEW badge not built by PreloadRings\n");
        ++failures;
    } else if (pNew->m_TextBoxes.empty()) {
        fprintf(stderr,
            "FAIL: NEW badge (0x10) m_TextBoxes empty -- BakedStringBox not created\n");
        ++failures;
    } else {
        printf("PASS: NEW badge (0x10): %d text box(es)\n",
               (int)pNew->m_TextBoxes.size());
    }

    if (!pSel) {
        fprintf(stderr,
            "FAIL: GetIngamePopup(0x11) null -- SELECTED badge not built by PreloadRings\n");
        ++failures;
    } else if (pSel->m_TextBoxes.empty()) {
        fprintf(stderr,
            "FAIL: SELECTED badge (0x11) m_TextBoxes empty -- BakedStringBox not created\n");
        ++failures;
    } else {
        printf("PASS: SELECTED badge (0x11): %d text box(es)\n",
               (int)pSel->m_TextBoxes.size());
    }

    if (!pBest) {
        fprintf(stderr,
            "FAIL: GetIngamePopup(0x0F) null -- NEW BEST! banner not built by PreloadRings\n");
        ++failures;
    } else if (pBest->m_TextBoxes.empty()) {
        fprintf(stderr,
            "FAIL: NEW BEST! banner (0x0F) m_TextBoxes empty -- BakedStringBox not created\n");
        ++failures;
    } else {
        printf("PASS: NEW BEST! banner (0x0F): %d text box(es)\n",
               (int)pBest->m_TextBoxes.size());
    }

    // Interactive mode: keep rendering until ESC / close.
    if (h.IsInteractive()) {
        bool running = true;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT)                                        { running = false; break; }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE)                           { running = false; break; }
            }
            if (!running) break;
            RenderFrame(h.window);
            SDL_Delay(16);
        }
        return h.Shutdown();
    }

    // Settle 60 frames: BakedStringBox GL textures are uploaded on first Draw
    // and the text rasterisation can span several frames for complex scripts
    // (Arabic uses arabic.ttf; Chinese/Japanese/Korean use gangofchinese.ttf).
    for (int i = 0; i < 60; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { h.Shutdown(); return 0; }
        }
        RenderFrame(h.window);
    }

    // --- Screenshot ---
    if (h.IsScreenshot()) {
        // One final render to ensure the framebuffer is current before readback.
        RenderFrame(h.window);
        if (!h.ScreenshotPng(shotLabel)) {
            fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", shotLabel);
            ++failures;
        } else {
            printf("[%s] screenshot written\n", shotLabel);
        }
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: IngamePopup badges OK (lang=%d)\n", g_LangFlag);
    return h.Shutdown();
}
