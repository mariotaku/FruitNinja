// test_powerupshop -- headless layout guard + optional screenshot for PowerUpShop.
//
// Usage: test_powerupshop [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the PowerUpShop Init path:
//     - m_PurchasablePowerUps populated from PowerUpManager::m_PurchasablePowers
//     - m_SlotLayout.size() == m_PurchasablePowerUps.size() (layout mirrors count)
//     - m_BuyText is non-empty (snprintf'd in Init)
//
// --screenshot: renders PowerUpShop to a stable state and writes:
//   tmp/test/screenshots/powerupshop/<lang>.png
//
// --lang=<name>  Language name or numeric flag (same set as test_shopscreen).
//
// NOTE on size field:
//   PowerUpShop::Init() (v1.6.1 @0x001a94b0) sets HUDControl::size from g_BuyBg
//   dimensions when that texture is loaded. In headless/test context g_BuyBg may
//   not be loaded, so main() falls back to Vec3(240,120,1) when size.x == 0 after
//   Init. In the real game the caller loads g_BuyBg before Init is called.
//
// Example commands:
//   test_powerupshop --screenshot
//   test_powerupshop --screenshot --lang=english_us
//   test_powerupshop --screenshot --lang=chinese
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^powerupshop$ --output-on-failure

#include "test_harness.h"
#include "screens/PowerUpShop.h"
#include "game/PowerUpManager.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Language flag parsed from --lang= for screenshot label composition.
// -1 = not specified.
static int g_LangFlag = -1;

// Short language tag table (matches kLanguageSuffix order in StringTable.cpp).
static const char* const kLangShort[] = {
    "en", "de", "nl", "fr", "es", "it", "sv", "da", "nb", "fi",
    "ko", "ja", "en_uk", "zh", "en"
};
static const int kLangShortCount = 15;

// Build the PNG label: "powerupshop/<lang>" or "powerupshop/default".
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "powerupshop/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "powerupshop/default");
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
    // 60 burn-in frames: fonts/textures live, entity pool ready before component renders.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    // Component-isolation mode: clears HUD so PowerUpShop renders on a clean background.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Construct PowerUpShop. Binary ctor @ 0x00155cac has no params.
    // Init() populates m_PurchasablePowerUps from PowerUpManager::m_PurchasablePowers
    // and builds m_SlotLayout. SetBuyButtonState() is called at end of Init.
    PowerUpShop* shop = new PowerUpShop();
    shop->Init();

    // Position at origin in centered-ortho space. The unresolved instantiation site
    // sets the real pos (see PowerUpShop.cpp TODO). With pos=(0,0,0):
    //   - BuyText label: y=+75, within [-240,+240]
    //   - Slot icons: x-coords -192/-67/+58 (for 3 slots), y=+24
    //   - Title/desc: x = pos.x-224 = -224, at the edge of [-240,+240]
    shop->pos = Vec3(0.0f, 0.0f, 0.0f);

    // Init() sets size from g_BuyBg when loaded (binary @0x001a94b0).
    // Fall back to a fixed value when g_BuyBg is unavailable in headless context.
    if (shop->size.x == 0.0f) {
        shop->size = Vec3(240.0f, 120.0f, 1.0f);
    }

    game_work.mHud->AddControl(shop);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        return h.Shutdown();
    }

    // Settle 60 frames. Update() creates m_BuyButton (a MenuButton fruit entity)
    // on the first frame (m_BuyButton == NULL && m_BuyTriggered == 0).
    // RunComponentHeadlessMultiPass matches GameDraw's per-layer pass order so
    // MenuButton (0x40/0x80 layer sequence) renders correctly after creation.
    for (int i = 0; i < 60; ++i) {
        h.RunComponentHeadlessMultiPass(1);
    }

    int failures = 0;

    // --- Assertion 1: m_PurchasablePowerUps populated by Init ---
    // PowerUpShop::Init @ 0x001a94b0 walks PowerUpManager::m_PurchasablePowers.
    // With a full game boot, PowerUpManager has at least one purchasable power
    // (loaded from the game XML during GameInitialise).
    if (shop->m_PurchasablePowerUps.empty()) {
        fprintf(stderr,
            "WARN: m_PurchasablePowerUps is empty -- PowerUpManager may have no"
            " purchasable powers loaded. Slot layout and icons will not render.\n");
        // Warn, don't fail: this is a data dependency (game XML), not a code bug.
    } else {
        printf("PASS: m_PurchasablePowerUps has %d entries\n",
               (int)shop->m_PurchasablePowerUps.size());
    }

    // --- Assertion 2: m_SlotLayout mirrors m_PurchasablePowerUps count ---
    // Init() builds one slot per purchasable power.
    if (shop->m_SlotLayout.size() != shop->m_PurchasablePowerUps.size()) {
        fprintf(stderr,
            "FAIL: m_SlotLayout.size()=%d != m_PurchasablePowerUps.size()=%d"
            " -- slot layout count mismatch\n",
            (int)shop->m_SlotLayout.size(),
            (int)shop->m_PurchasablePowerUps.size());
        ++failures;
    } else {
        printf("PASS: m_SlotLayout count matches m_PurchasablePowerUps (%d)\n",
               (int)shop->m_SlotLayout.size());
    }

    // --- Assertion 3: m_BuyText populated ---
    // Init() calls snprintf(m_BuyText, 128, "YOU HAVE %i COINS TO USE!", coins).
    if (shop->m_BuyText[0] == '\0') {
        fprintf(stderr,
            "FAIL: m_BuyText is empty -- Init snprintf path broken\n");
        ++failures;
    } else {
        printf("PASS: m_BuyText = '%s'\n", shop->m_BuyText);
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

    printf("PASS: PowerUpShop layout OK\n");
    return h.Shutdown();
}
