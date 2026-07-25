// test_gamemodescreen -- headless layout guard + optional screenshot for GameModeScreen.
//
// Usage: test_gamemodescreen [--screenshot] [--interactive] [--lang=<name|flag>]
//
// Default (no flags): headless assertions only (runs via ctest -E screenshot).
//   Verifies the GameModeScreen activation flow:
//     - 3 BakedStringBox title/desc/info boxes built in ctor (TTF path)
//     - 4 mode buttons (back, classic, zen, arcade) created via CreateControls
//       after state 0 -> state 2 alpha transition
//     - the 5th "VS" ring (m_pOnlineMpButton) is present OFFLINE (no transport
//       installed) with the CONNECT skin -- MP-revival: closes the mode-select
//       dead end, since slicing this ring is what starts matchmaking
//       (P2PConnectCallback). Re-skins to ONLINE (VersusModeCallback) in place
//       once a LoopbackTransport connects -- see
//       GameModeScreen::UpdateOnlineMultiplayerButton's DIFFERS comment
//       (v1.6.1 Bada @0x0018234c / iOS 1.6.1 @0x000501fc)
//
// --screenshot: renders GameModeScreen to stable state and writes:
//   tmp/test/screenshots/gamemode/<lang>.png  (e.g. gamemode/en.png, gamemode/zh.png)
//   tmp/test/screenshots/gamemode/versus_online.png (VS ring, transport connected)
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
#include "engine/network/IMpTransport.h"
#include "engine/network/LoopbackTransport.h"
#include "engine/network/MpTransport.h"
#include "engine/network/P2PMessageHandling.h"
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
    GameModeScreen* screen = new GameModeScreen(h.game, false);
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

    // --- Assertion 2.5: VS ring present OFFLINE with the CONNECT skin ---
    // MP-revival: closes the mode-select dead end -- matchmaking can only ever
    // be initiated by SLICING this ring (P2PConnectCallback), so it must exist
    // and be slice-able before any transport connects, not only after. No
    // transport is installed yet, so IsP2POnline()/IsP2PConnecting() are both
    // false; the create path in UpdateOnlineMultiplayerButton (v1.6.1 Bada
    // @0x0018234c / iOS 1.6.1 @0x000501fc) now runs whenever
    // s_supportsP2P && !m_bIsFromPause (true for this screen on host builds)
    // regardless of connection state -- see its DIFFERS comment.
    if (!screen->m_pOnlineMpButton) {
        fprintf(stderr,
            "FAIL: m_pOnlineMpButton null OFFLINE -- UpdateOnlineMultiplayerButton no longer "
            "creates the VS ring while disconnected (mode-select dead end regression)\n");
        ++failures;
    } else {
        printf("PASS: m_pOnlineMpButton non-null OFFLINE (pos=%.1f,%.1f)\n",
               screen->m_pOnlineMpButton->pos.x, screen->m_pOnlineMpButton->pos.y);

        if (screen->m_bOnlineMpButtonSkinOnline) {
            fprintf(stderr, "FAIL: m_bOnlineMpButtonSkinOnline true while offline (should be CONNECT skin)\n");
            ++failures;
        } else {
            printf("PASS: m_bOnlineMpButtonSkinOnline false (CONNECT skin) while offline\n");
        }

        if (!screen->m_pOnlineMpButton->m_pTrackedFruit) {
            fprintf(stderr,
                "FAIL: m_pOnlineMpButton->m_pTrackedFruit null OFFLINE (vs_watermelon not created)\n");
            ++failures;
        } else {
            printf("PASS: m_pOnlineMpButton->m_pTrackedFruit non-null OFFLINE (vs_watermelon)\n");
        }
    }

    // --- Screenshot (only when --screenshot flag is present) ---
    // Now includes the offline VS ring (CONNECT skin) in the default capture.
    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng(shotLabel)) {
            fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", shotLabel);
            ++failures;
        } else {
            printf("[%s] screenshot written\n", shotLabel);
        }
    }

    // --- Assertion 3: VERSUS RING (m_pOnlineMpButton) re-skins to ONLINE in
    // place once a transport connects (m_bOnlineMpButtonSkinOnline flips
    // true, callback becomes VersusModeCallback) -- see
    // GameModeScreen::UpdateOnlineMultiplayerButton's DIFFERS comment. The
    // ring itself is the SAME instance grown above OFFLINE; this section no
    // longer exercises first-creation (that's Assertion 2.5).
    {
        Mortar::LoopbackTransport* a = 0;
        Mortar::LoopbackTransport* b = 0;
        Mortar::LoopbackTransport::CreatePair(a, b);
        Mortar::SetMpTransport(a);
        a->Host();
        b->Join("");

        // Host()/Join() are async (see IMpTransport.h): pump PollEvent() on
        // both ends until 'a' resolves to CONNECTED. Bounded loop so a
        // transport regression fails the test instead of hanging it.
        for (int i = 0; i < 16 && !a->IsConnected(); ++i) {
            a->PollEvent();
            b->PollEvent();
        }

        if (!a->IsConnected()) {
            fprintf(stderr, "FAIL: LoopbackTransport did not reach CONNECTED after pumping PollEvent()\n");
            ++failures;
        } else {
            printf("PASS: LoopbackTransport connected\n");
        }

        if (!IsP2POnline()) {
            fprintf(stderr, "FAIL: IsP2POnline() false after transport connect\n");
            ++failures;
        } else {
            printf("PASS: IsP2POnline() true\n");
        }

        // Drive Update() (case 2 -> UpdateOnlineMultiplayerButton) enough
        // frames to observe the in-place re-skin to ONLINE (the ring is
        // already grown in from the OFFLINE assertion above).
        for (int i = 0; i < 90; ++i) {
            h.RunComponentHeadlessMultiPass(1);
        }

        printf("[RESULT] m_pOnlineMpButton=%p", (void*)screen->m_pOnlineMpButton);
        if (screen->m_pOnlineMpButton) {
            printf(" pos=(%.1f,%.1f) trackedFruit=%p",
                   screen->m_pOnlineMpButton->pos.x, screen->m_pOnlineMpButton->pos.y,
                   (void*)screen->m_pOnlineMpButton->m_pTrackedFruit);
        }
        printf("\n");

        if (!screen->m_pOnlineMpButton) {
            fprintf(stderr,
                "FAIL: m_pOnlineMpButton null -- UpdateOnlineMultiplayerButton create path not reached\n");
            ++failures;
        } else {
            printf("PASS: m_pOnlineMpButton non-null (VS ring, pos=%.1f,%.1f)\n",
                   screen->m_pOnlineMpButton->pos.x, screen->m_pOnlineMpButton->pos.y);

            bool inHud = false;
            if (game_work.mHud) {
                for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
                     it != game_work.mHud->controls.end(); ++it) {
                    if (*it == screen->m_pOnlineMpButton) { inHud = true; break; }
                }
            }
            if (!inHud) {
                fprintf(stderr, "FAIL: m_pOnlineMpButton not found in game_work.mHud->controls\n");
                ++failures;
            } else {
                printf("PASS: m_pOnlineMpButton in HUD\n");
            }

            if (!screen->m_pOnlineMpButton->m_pTrackedFruit) {
                fprintf(stderr, "FAIL: m_pOnlineMpButton->m_pTrackedFruit null (vs_watermelon not created)\n");
                ++failures;
            } else {
                printf("PASS: m_pOnlineMpButton->m_pTrackedFruit non-null (vs_watermelon)\n");
            }

            if (!screen->m_bOnlineMpButtonSkinOnline) {
                fprintf(stderr, "FAIL: m_bOnlineMpButtonSkinOnline still false after transport connect\n");
                ++failures;
            } else {
                printf("PASS: m_bOnlineMpButtonSkinOnline true (ONLINE skin) after transport connect\n");
            }
        }

        // --- Screenshot (only when --screenshot flag is present) ---
        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("gamemode/versus_online")) {
                fprintf(stderr, "FAIL: ScreenshotPng('gamemode/versus_online') failed\n");
                ++failures;
            } else {
                printf("[gamemode/versus_online] screenshot written\n");
            }
        }

        // Teardown: unhook the transport before deleting it so no other test
        // state (or a later screen in this same process) sees a dangling
        // pointer via GetMpTransport().
        Mortar::SetMpTransport(0);
        delete a;
        delete b;
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: GameModeScreen layout OK\n");
    return h.Shutdown();
}
