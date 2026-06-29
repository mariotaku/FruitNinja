// test_aboutscreen -- headless layout guard + optional screenshot for AboutScreen.
//
// Usage: test_aboutscreen [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the AboutScreen ctor+Init path creates:
//     - 9 BakedStringBox credit-text controls (title, heading, version, 6 credits)
//     - >= 10 marquee lines in the scrolling credits list
//     - Screen transitions state 0->1 (not pending-removal after settle)
//
// --screenshot: renders AboutScreen to stable state and writes:
//   tmp/test/screenshots/about/<lang>.png  (e.g. about/en.png, about/zh.png)
//
// --lang=<name>  Language name or numeric flag (same set as test_fruitfact).
//                Applied before the screen builds text so strings render in the
//                chosen locale.
//
// Example screenshot commands:
//   test_aboutscreen --screenshot
//   test_aboutscreen --screenshot --lang=english_us
//   test_aboutscreen --screenshot --lang=chinese
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^aboutscreen$ --output-on-failure

#include "test_harness.h"
#include "screens/AboutScreen.h"
#include "hud/HUD.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Language flag parsed from --lang= for screenshot label composition.
// -1 = not specified.
static int g_LangFlag = -1;

// Short language tag table (matches kLanguageSuffix order in StringTable.cpp).
static const char* const kLangShort[] = {
    "en", "en_uk", "fr", "es", "de", "it", "nl", "sv", "da", "nb",
    "fi", "ko", "ja", "zh", "zh_hant", "es_419", "pl", "pt", "pt_br", "ru",
    "ar", "dbg"
};
static const int kLangShortCount = 22;

// Build the PNG label: "about/<lang>" e.g. "about/zh", or "about/default" when
// no --lang= was specified (avoids collision with --lang=english_us -> "about/en").
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "about/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "about/default");
    }
}

int main(int argc, char* argv[]) {
    // Parse --lang= here for label composition; harness also parses it for
    // locale application (two passes on the same args, harmless).
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--lang=", 7) == 0) {
            g_LangFlag = ParseLanguageArg(argv[i] + 7);
        }
    }

    char shotLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), g_LangFlag);

    fn::TestHarness h(argc, argv, shotLabel);
    // 60 burn-in frames: fonts/textures live before component renders.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation mode: Init() + clears HUD so AboutScreen renders on
    // a clean background. Mirrors DojoScreen test pattern.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Construct AboutScreen with null parent -- m_pParent is only accessed in
    // state 2 (back/quit), which this test never reaches (state stays at 0->1).
    AboutScreen* screen = new AboutScreen(nullptr);
    screen->Init();
    game_work.mHud->AddControl(screen);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        return h.Shutdown();
    }

    // Settle 60 frames with multi-pass rendering so all HUD layer passes fire:
    //   0x80 pass: HUD_LAYER_POST_ACTOR (AboutScreen's layer).
    // Also advances m_TransitionAlpha from 0 to ~1 (0.125 lerp; ~17 frames to 0.99).
    for (int i = 0; i < 60; ++i) {
        h.RunComponentHeadlessMultiPass(1);
    }

    int failures = 0;

    // --- Assertion 1: BakedStringBox text controls built in ctor ---
    // AboutScreen::AboutScreen @ 0x0015b764 builds 9 boxes when TTF font is live.
    // GetBuiltBoxCount() counts non-null {title, heading, version, credit0..5}.
    int boxCount = screen->GetBuiltBoxCount();
    printf("[RESULT] GetBuiltBoxCount()=%d\n", boxCount);

    if (boxCount < 9) {
        fprintf(stderr,
            "FAIL: GetBuiltBoxCount()=%d < 9 -- TTF ctor path broken or "
            "gangofchinese.ttf missing\n", boxCount);
        ++failures;
    } else {
        printf("PASS: all 9 BakedStringBox credit-text controls built\n");
    }

    // --- Assertion 2: Marquee scrolling credits list populated ---
    // CreateCreditsMarquee @ 0x0015ac0c adds heading + dev names + colour-leader lines.
    // Expected count >= 10 (1 heading + 6 dev-name lines + 3 lead lines + optionals).
    int marqueeCount = screen->GetMarqueeCount();
    printf("[RESULT] GetMarqueeCount()=%d\n", marqueeCount);

    if (marqueeCount < 10) {
        fprintf(stderr,
            "FAIL: GetMarqueeCount()=%d < 10 -- CreateCreditsMarquee path broken\n",
            marqueeCount);
        ++failures;
    } else {
        printf("PASS: marquee has %d lines\n", marqueeCount);
    }

    // --- Assertion 3: Screen reached idle state (not pending removal) ---
    // After 60 frames: state 0 (alpha lerp) advances to state 1 (idle).
    // IsPendingRemoval() would be set only if state machine fell through to state 2.
    if (screen->IsPendingRemoval()) {
        fprintf(stderr,
            "FAIL: screen is pending removal -- state machine did not reach state 1 idle\n");
        ++failures;
    } else {
        printf("PASS: screen not pending removal (reached idle state 1)\n");
    }

    // --- Screenshot (only when --screenshot flag is present) ---
    if (h.IsScreenshot()) {
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

    printf("PASS: AboutScreen layout OK\n");
    return h.Shutdown();
}
