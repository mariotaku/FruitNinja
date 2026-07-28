// test_gamemodescreen -- headless layout guard + optional screenshot for GameModeScreen.
//
// Usage: test_gamemodescreen [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the GameModeScreen activation flow:
//     - 3 BakedStringBox title/desc/info boxes built in ctor (TTF path)
//     - 4 mode buttons (back, classic, zen, arcade) created via CreateControls
//       after state 0 -> state 2 alpha transition
//
// --screenshot: renders GameModeScreen to stable state and writes:
//   tmp/test/screenshots/gamemode/<lang>.png  (e.g. gamemode/en.png, gamemode/zh.png)
//
// --lang=<name>  Language name or numeric flag (same set as test_fruitfact).
//
// Example screenshot commands:
//   test_gamemodescreen --screenshot
//   test_gamemodescreen --screenshot --lang=english_us
//   test_gamemodescreen --screenshot --lang=chinese
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^gamemodescreen$ --output-on-failure

#include "test_harness.h"
#include "screens/GameModeScreen.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
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

// Build the PNG label: "gamemode/<lang>" or "gamemode/default".
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "gamemode/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "gamemode/default");
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
    // Component-isolation mode: Init() + clears HUD so GameModeScreen renders on
    // a clean background. Mirrors DojoScreen test pattern.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Create GameModeScreen. isFromPause=false matches the normal Play -> mode-select path.
    // Binary: GameModeScreen ctor @ 0x0013e524 (bool isFromPause).
    GameModeScreen* screen = new GameModeScreen(false);
    game_work.mHud->AddControl(screen);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        return h.Shutdown();
    }

    // Settle 60 frames with multi-pass rendering. GameModeScreen::Update drives the
    // state 0 transition: alpha += (1-alpha)*0.15 per frame. Threshold 0.999 is
    // crossed around frame 30; CreateControls() fires and buttons join the HUD.
    // 60 frames ensures state 2 (idle) is fully settled.
    // Per-pass layer order: 0x40 (MenuButton scratch bg), 0x80 (button face+label,
    // GameModeScreen panel), 0x01 (DEFAULT -- GameModeScreen draw layer).
    for (int i = 0; i < 60; ++i) {
        h.RunComponentHeadlessMultiPass(1);
    }

    int failures = 0;

    // --- Assertion 1: BakedStringBox boxes built in ctor (TTF path) ---
    // GameModeScreen ctor @ 0x0013e524 builds m_pTitleBox/m_pDescBox/m_pInfoBox
    // from LocalizedString IDs. Non-null confirms TTF font is live and strings load.
    if (!screen->m_pTitleBox) {
        fprintf(stderr,
            "FAIL: m_pTitleBox null -- TTF ctor path broken or gangofchinese.ttf missing\n");
        ++failures;
    } else {
        printf("PASS: m_pTitleBox non-null (zen feature-bullet box)\n");
    }

    if (!screen->m_pDescBox) {
        fprintf(stderr, "FAIL: m_pDescBox null -- TTF ctor path broken\n");
        ++failures;
    } else {
        printf("PASS: m_pDescBox non-null (MODE SELECT box)\n");
    }

    if (!screen->m_pInfoBox) {
        fprintf(stderr, "FAIL: m_pInfoBox null -- TTF ctor path broken\n");
        ++failures;
    } else {
        printf("PASS: m_pInfoBox non-null (MULTIPLAYER box)\n");
    }

    // --- Assertion 2: Mode buttons created via CreateControls (state 0->2) ---
    // CreateControls @ 0x001819bc fires when alpha crosses 0.999, adding 4 MenuButtons.
    // All four ptrs are public members of GameModeScreen (binary layout +0xa0..+0xcc).
    printf("[RESULT] m_pBackButton=%p m_pClassicButton=%p "
           "m_pZenButton=%p m_pArcadeButton=%p\n",
           (void*)screen->m_pBackButton, (void*)screen->m_pClassicButton,
           (void*)screen->m_pZenButton,  (void*)screen->m_pArcadeButton);

    if (!screen->m_pBackButton) {
        fprintf(stderr,
            "FAIL: m_pBackButton null -- CreateControls not reached or state 0 transition broken\n");
        ++failures;
    } else {
        printf("PASS: m_pBackButton in HUD (pos=%.1f,%.1f)\n",
               screen->m_pBackButton->pos.x, screen->m_pBackButton->pos.y);
    }

    if (!screen->m_pClassicButton) {
        fprintf(stderr, "FAIL: m_pClassicButton null\n");
        ++failures;
    } else {
        printf("PASS: m_pClassicButton in HUD (pos=%.1f,%.1f)\n",
               screen->m_pClassicButton->pos.x, screen->m_pClassicButton->pos.y);
    }

    if (!screen->m_pZenButton) {
        fprintf(stderr, "FAIL: m_pZenButton null\n");
        ++failures;
    } else {
        printf("PASS: m_pZenButton in HUD (pos=%.1f,%.1f)\n",
               screen->m_pZenButton->pos.x, screen->m_pZenButton->pos.y);
    }

    if (!screen->m_pArcadeButton) {
        fprintf(stderr, "FAIL: m_pArcadeButton null\n");
        ++failures;
    } else {
        printf("PASS: m_pArcadeButton in HUD (pos=%.1f,%.1f)\n",
               screen->m_pArcadeButton->pos.x, screen->m_pArcadeButton->pos.y);
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

    printf("PASS: GameModeScreen layout OK\n");
    return h.Shutdown();
}
