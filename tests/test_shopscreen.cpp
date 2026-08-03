// test_shopscreen -- headless layout guard + optional screenshot for ShopScreen.
//
// Usage: test_shopscreen [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the ShopScreen Init path:
//     - m_pShopList (ScrollingMenu) is created by Init() and added to HUD
//     - ShopListItem TTF box creation: m_pBox0 (title) and m_pBox1 (category)
//       are non-null after NewDraw() (reuses test_shoplistitem assertion pattern).
//
// --screenshot: renders ShopScreen to stable state and writes:
//   tmp/test/screenshots/shop/<lang>.png  (e.g. shop/en.png, shop/zh.png)
//
// --lang=<name>  Language name or numeric flag (same set as test_fruitfact).
//
// Example screenshot commands:
//   test_shopscreen --screenshot
//   test_shopscreen --screenshot --lang=english_us
//   test_shopscreen --screenshot --lang=chinese
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^shopscreen$ --output-on-failure

#include "test_harness.h"
#include "screens/ShopScreen.h"
#include "hud/ShopListItem.h"
#include "hud/HUD.h"
#include "game/ItemInfo.h"
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

// Build the PNG label: "shop/<lang>" or "shop/default".
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "shop/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "shop/default");
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
    // Component-isolation mode: clears HUD so ShopScreen renders on a clean background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Construct ShopScreen with null parent -- m_pParent is only used in quit/back
    // states (2, 7, 8) and state 0 buy-button sound play, which this test never reaches.
    // Binary: ShopScreen::ShopScreen(DojoScreen*) @ 0x0015cdac.
    ShopScreen* screen = new ShopScreen(nullptr);
    // Init() creates the ScrollingMenu (m_pShopList) and adds it to the HUD.
    // Binary: ShopScreen::Init @ 0x0015f7ac calls CreateShopList.
    screen->Init();
    game_work.mHud->AddControl(screen);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        return h.Shutdown();
    }

    // Settle 60 frames. ShopScreen::Update state 0 transitions to state 1 after
    // alpha lerp (0.125 step; ~17 frames to 0.999). State 1 is the active/idle state.
    // Multi-pass ensures the 0x40/0x80 layer sequence the panel uses renders correctly.
    for (int i = 0; i < 60; ++i) {
        h.RunComponentHeadlessMultiPass(1);
    }

    int failures = 0;

    // --- Assertion 1: ScrollingMenu created by Init ---
    // ShopScreen::Init @ 0x0015f7ac calls CreateShopList() which allocates
    // m_pShopList and adds it to the HUD.
    if (!screen->m_pShopList) {
        fprintf(stderr,
            "FAIL: m_pShopList null -- ShopScreen::Init/CreateShopList path broken\n");
        ++failures;
    } else {
        printf("PASS: m_pShopList non-null (ScrollingMenu created by Init)\n");
    }

    // --- Assertion 2: ShopListItem TTF box creation (v1.6.1 NewDraw path) ---
    // Reuses the test_shoplistitem assertion pattern: build a minimal stub ItemInfo,
    // construct a ShopListItem, call NewDraw() to trigger lazy BakedStringBox build,
    // then verify m_pBox0 (title) and m_pBox1 (category) are non-null.
    // Passes in the same headless context as the ShopScreen render (GL + TTF font live).
    // Scoped to destroy item+info before h.Shutdown() (while GL context still alive).
    // Skip in screenshot mode: this stub item's NewDraw() draws into the current
    // framebuffer, which ScreenshotPng would then capture -- overlaying the real
    // first shop item ("ORIGINAL" + "TEST BLADE" bleed). The assertion only needs
    // to run in the headless ctest invocation (ctest -E screenshot).
    if (!h.IsScreenshot()) {
        fprintf(stdout, "[test_shopscreen] ShopListItem TTF box-creation check (NewDraw path)\n");
        // Prerequisite: game_work.m_pTTFFontMain must be live for NewDraw to build boxes.
        if (!game_work.m_pTTFFontMain) {
            fprintf(stderr, "WARN: game_work.m_pTTFFontMain null -- skipping box assertions\n");
        } else {
            // Build a minimal stub ItemInfo (type=0 BLADE, unlocked, no requirement).
            ItemInfo info;
            info.m_Type            = (int8_t)ITEM_TYPE_BLADE;  // 0
            info.m_Cost            = 0;
            info.m_pTitle          = (char*)"Test Blade";
            info.m_pDescText       = (char*)"A test blade item.";
            info.m_pLockedText     = nullptr;
            info.m_pProgressFmt    = nullptr;
            info.m_pTotalStatKey   = nullptr;
            info.m_CountDownFrom   = 0;
            info.m_pTextureName    = nullptr;
            info.m_bSeen           = 1;
            info.m_RequirementType = (int8_t)0;

            ShopListItem item;
            // pShopScreen=nullptr is safe: DrawDescription() early-returns on null.
            item.Create(&info, nullptr);
            // NewDraw's head gate is m_bOnscreen (+0x2D), matching the binary
            // @0x001b5910. ScrollingMenu::Update normally sets it; this fixture
            // drives NewDraw directly.
            item.SetOnscreen(true);

            // Precondition: boxes are null before NewDraw (lazy-build).
            if (item.m_pBox0 != nullptr || item.m_pBox1 != nullptr) {
                fprintf(stderr,
                    "FAIL: m_pBox0 or m_pBox1 non-null before NewDraw (lazy-build broken)\n");
                ++failures;
            }

            // Trigger lazy box build.
            item.NewDraw();

            if (!item.m_pBox0) {
                fprintf(stderr,
                    "FAIL: m_pBox0 (title BakedStringBox) null after NewDraw -- "
                    "v1.6.1 TTF path broken\n");
                ++failures;
            } else {
                printf("PASS: m_pBox0 (title) built by NewDraw\n");
            }

            if (!item.m_pBox1) {
                fprintf(stderr,
                    "FAIL: m_pBox1 (category BakedStringBox) null after NewDraw -- "
                    "v1.6.1 TTF path broken\n");
                ++failures;
            } else {
                printf("PASS: m_pBox1 (category) built by NewDraw\n");
            }

            // Null out the string literals before ItemInfo dtor free()s them
            // (literals are not heap-allocated; free(non-heap) corrupts the allocator).
            info.m_pName = nullptr; info.m_pTitle = nullptr; info.m_pDescText = nullptr;
            info.m_pLockedText = nullptr; info.m_pProgressFmt = nullptr;
            info.m_pTotalStatKey = nullptr; info.m_pTextureName = nullptr;
        }
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

    printf("PASS: ShopScreen layout OK\n");
    return h.Shutdown();
}
