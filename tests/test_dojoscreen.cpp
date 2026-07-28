// test_dojoscreen -- headless layout guard + optional screenshot for DojoScreen.
//
// Usage: test_dojoscreen [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the faithful Init()->Reset()->CreateButtons() activation creates:
//     - 3 ring buttons in HUD: back-bomb, shop (pineapple), about (plum)
//     - 2 BSButtons (FB+TW social stubs) with DISTINCT positions
//
// --screenshot: renders DojoScreen to stable state and writes:
//   tmp/test/screenshots/dojo/<lang>.png  (e.g. dojo/en.png, dojo/zh.png)
//
// --lang=<name>  Language name or numeric flag (same set as test_fruitfact).
//                Applied for both headless (string IDs) and screenshot (font).
//
// Example screenshot commands:
//   test_dojoscreen --screenshot
//   test_dojoscreen --screenshot --lang=english_us
//   test_dojoscreen --screenshot --lang=chinese
//
// Run headless (ctest):
//   ctest --test-dir build/host -R ^dojoscreen$ --output-on-failure

#include "test_harness.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/BSButton.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "screens/DojoScreen.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <list>
#include <vector>

// Language flag parsed from --lang= for screenshot label composition.
// -1 = not specified.
static int g_LangFlag = -1;

// Short language tag table (matches kLanguageSuffix order in StringTable.cpp).
static const char* const kLangShort[] = {
    "en", "de", "nl", "fr", "es", "it", "sv", "da", "nb", "fi",
    "ko", "ja", "en_uk", "zh", "en"
};
static const int kLangShortCount = 15;

// Build the PNG label: "dojo/<lang>" e.g. "dojo/zh", or "dojo/default" when
// no --lang= was specified (avoids collision with --lang=english_us -> "dojo/en").
static void BuildShotLabel(char* out, size_t outSize, int langFlag) {
    if (langFlag >= 0 && langFlag < kLangShortCount) {
        snprintf(out, outSize, "dojo/%s", kLangShort[langFlag]);
    } else {
        snprintf(out, outSize, "dojo/default");
    }
}

// Collect controls added AFTER the snapshot (skip pre-existing).
static void CollectNewControls(
    const std::list<HUDControl*>& existing,
    std::list<MenuButton*>& outMenuButtons,
    std::vector<BSButton*>& outBSButtons)
{
    outMenuButtons.clear();
    outBSButtons.clear();
    if (!game_work.mHud) return;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* ctrl = *it;
        bool wasExisting = false;
        for (std::list<HUDControl*>::const_iterator eit = existing.begin();
             eit != existing.end(); ++eit) {
            if (*eit == ctrl) { wasExisting = true; break; }
        }
        if (wasExisting) continue;

        MenuButton* mb = dynamic_cast<MenuButton*>(ctrl);
        if (mb) { outMenuButtons.push_back(mb); continue; }

        BSButton* bb = dynamic_cast<BSButton*>(ctrl);
        if (bb) outBSButtons.push_back(bb);
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
    // Component-isolation mode: Init() + clears HUD so DojoScreen renders on
    // a clean background (no MainScreen chrome). Mirrors test_fruitfact pattern.
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    int fruitCount = FruitInfo_GetCount();
    if (fruitCount <= 0) {
        fprintf(stderr, "FAIL: FruitInfo_GetCount()=%d -- fruitlist.xml not loaded\n",
                fruitCount);
        return 1;
    }

    const int bombType      = fruitCount;
    const int pineappleType = Fruit::FruitType("pineapple", false);
    const int plumType      = Fruit::FruitType("plum", false);
    printf("[SETUP] fruitCount=%d bombType=%d pineappleType=%d plumType=%d\n",
           fruitCount, bombType, pineappleType, plumType);

    // Snapshot HUD (will be empty after InitComponent).
    std::list<HUDControl*> existing;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        existing.push_back(*it);
    }
    printf("[SETUP] pre-DojoScreen HUD count=%d\n", (int)existing.size());

    // Activate DojoScreen via the faithful binary flow:
    //   new DojoScreen (ctor adds BSButtons to HUD immediately)
    //   Init() -> Reset() -> CreateButtons()  (v1.6.1 @0x00169e80)
    //   AddControl (adds DojoScreen itself to HUD)
    DojoScreen* dojo = new DojoScreen();
    dojo->Init();
    game_work.mHud->AddControl(dojo);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        return h.Shutdown();
    }

    // Settle 60 frames with multi-pass rendering so all ring button passes fire:
    //   0x40 pass: MenuButton scratch backdrop (MENU_BG)
    //   0x80 pass: ring face + label, BSButton, DojoScreen panel (POST_ACTOR)
    // Also advances m_TransitionAlpha from 0 to ~1 (0.25 lerp; ~17 frames to 0.99).
    // Port specific: m_TransitionAlpha now eases in DojoScreen::UpdateRealtime
    // (per-presented-frame, dt-scaled -- see DojoScreen.cpp), not the 60Hz
    // Update() that RunComponentHeadlessMultiPass drives via HUD::Update.
    // The real game loop pumps both per presented frame (GameSDL.cpp calls
    // Game::tickRealtimeUi alongside stepUpdate); without the paired call here
    // m_TransitionAlpha never advances and the BSButton anchor assertions below
    // stall at alpha=0. Mirrors the same fix in test_ring_texture_lifecycle.cpp
    // / test_screen.cpp.
    for (int i = 0; i < 60; ++i) {
        h.RunComponentHeadlessMultiPass(1);
        h.game.tickRealtimeUi(1.0f / 60.0f);
    }

    // Collect controls added by DojoScreen and its Init path.
    std::list<MenuButton*> menuButtons;
    std::vector<BSButton*> bsButtons;
    CollectNewControls(existing, menuButtons, bsButtons);

    printf("[RESULT] new MenuButtons=%d  new BSButtons=%d\n",
           (int)menuButtons.size(), (int)bsButtons.size());

    // --- Assertion 1: all three ring buttons in HUD ---
    MenuButton* backBtn  = NULL;
    MenuButton* shopBtn  = NULL;
    MenuButton* aboutBtn = NULL;
    for (std::list<MenuButton*>::iterator it = menuButtons.begin();
         it != menuButtons.end(); ++it) {
        MenuButton* mb = *it;
        if (mb->m_FruitType == bombType)           backBtn  = mb;
        else if (mb->m_FruitType == pineappleType) shopBtn  = mb;
        else if (mb->m_FruitType == plumType)      aboutBtn = mb;
    }

    int failures = 0;

    if (!backBtn) {
        fprintf(stderr,
            "FAIL: back-bomb ring (m_pBackButton, fruitType=%d) not in HUD"
            " -- Init()->Reset()->CreateButtons() path broken\n", bombType);
        ++failures;
    } else {
        printf("PASS: back-bomb ring in HUD (pos=%.1f,%.1f restScale=%.3f)\n",
               backBtn->pos.x, backBtn->pos.y, backBtn->m_RestScale.x);
    }

    if (!shopBtn) {
        fprintf(stderr,
            "FAIL: shop ring (m_pShopButton, fruitType=%d/pineapple) not in HUD\n",
            pineappleType);
        ++failures;
    } else {
        printf("PASS: shop ring in HUD (pos=%.1f,%.1f)\n",
               shopBtn->pos.x, shopBtn->pos.y);
    }

    if (!aboutBtn) {
        fprintf(stderr,
            "FAIL: about ring (m_pAboutButton, fruitType=%d/plum) not in HUD\n",
            plumType);
        ++failures;
    } else {
        printf("PASS: about ring in HUD (pos=%.1f,%.1f)\n",
               aboutBtn->pos.x, aboutBtn->pos.y);
    }

    // --- Assertion 2: BSButton positions match UpdateBSButton anchor values ---
    // After 60 settle frames m_TransitionAlpha == 1.0f exactly (state-0 sets it when
    // > ALPHA_IN_DONE). At alpha=1 UpdateBSButton produces offset=0, so positions equal
    // the per-index anchors: FB idx=0 -> (152,100,0), TW idx=1 -> (152,54,0).
    // This assertion catches the regression where both were at (152,100,0) and overlapped.
    if ((int)bsButtons.size() < 2) {
        fprintf(stderr,
            "FAIL: expected 2 BSButtons (FB+TW) in HUD, found %d\n",
            (int)bsButtons.size());
        ++failures;
    } else {
        _Vector3<float> pos0 = bsButtons[0]->pos;
        _Vector3<float> pos1 = bsButtons[1]->pos;
        printf("[BSBUTTON] btn0 pos=(%.1f,%.1f,%.1f)  btn1 pos=(%.1f,%.1f,%.1f)\n",
               pos0.x, pos0.y, pos0.z, pos1.x, pos1.y, pos1.z);

        // Expected anchors at alpha=1 (UpdateBSButton: anchor = (152, 100 - 46*idx, 0)).
        static const float kEps = 1.0f;
        static const float kExpFBx = 152.0f, kExpFBy = 100.0f;  // idx=0
        static const float kExpTWx = 152.0f, kExpTWy =  54.0f;  // idx=1 (100-46)

        bool fbOk = (pos0.x >= kExpFBx - kEps && pos0.x <= kExpFBx + kEps &&
                     pos0.y >= kExpFBy - kEps && pos0.y <= kExpFBy + kEps);
        bool twOk = (pos1.x >= kExpTWx - kEps && pos1.x <= kExpTWx + kEps &&
                     pos1.y >= kExpTWy - kEps && pos1.y <= kExpTWy + kEps);

        if (!fbOk) {
            fprintf(stderr,
                "FAIL: FB BSButton (idx=0) at (%.1f,%.1f), expected (%.1f,%.1f) "
                "+/-%.1f -- UpdateBSButton not running or wrong anchor\n",
                pos0.x, pos0.y, kExpFBx, kExpFBy, kEps);
            ++failures;
        } else {
            printf("PASS: FB BSButton at expected anchor (%.1f,%.1f)\n", pos0.x, pos0.y);
        }

        if (!twOk) {
            fprintf(stderr,
                "FAIL: TW BSButton (idx=1) at (%.1f,%.1f), expected (%.1f,%.1f) "
                "+/-%.1f -- 46-unit vertical gap not achieved (overlap regression)\n",
                pos1.x, pos1.y, kExpTWx, kExpTWy, kEps);
            ++failures;
        } else {
            printf("PASS: TW BSButton at expected anchor (%.1f,%.1f)\n", pos1.x, pos1.y);
        }

        // Belt-and-suspenders: gap along y-axis >= 40 units.
        float gap = pos0.y - pos1.y;
        if (gap < 40.0f) {
            fprintf(stderr,
                "FAIL: FB/TW vertical gap=%.1f < 40 -- buttons visually overlap\n", gap);
            ++failures;
        } else {
            printf("PASS: FB/TW vertical gap=%.1f >= 40\n", gap);
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

    printf("PASS: DojoScreen layout OK\n");
    return h.Shutdown();
}
