// test_bonus_screen -- unit test for BonusScreen rendering with faked awards.
//
// Spawns a BonusScreen directly with 3 mock awards (no BonusManager), adds it
// to the HUD, manually advances m_Timer (production drives this from
// GameOverScreen), ticks 600 frames, asserts size populated, score advances,
// dismiss flag fires, and bright + dark pixels show in the readback.
//
// Run via:
//   ctest --test-dir build/host -R bonus_screen --output-on-failure
//   ./build/host/tests/Debug/test_bonus_screen.exe              # headless
//   ./build/host/tests/Debug/test_bonus_screen.exe --interactive  # visible window
//   ./build/host/tests/Debug/test_bonus_screen.exe --screenshot   # dump PPM

#include "test_harness.h"
#include "screens/BonusScreen.h"
#include "hud/HUD.h"
#include "engine/math/_Vector3.h"
#include "render/Font.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

static const int TIMEOUT_FRAMES = 600;
static const float kDtFixed = 1.0f / 60.0f;

// Per-tick callback for the interactive loop -- advance m_Timer so the
// reveal animation runs. Returns false once the dismiss flag fires so the
// window doesn't sit blank after BonusScreen disappears.
static bool BonusTick(Game& /*game*/, int /*frame*/, void* userdata) {
    BonusScreen* bs = (BonusScreen*)userdata;
    if (!bs) return false;
    bs->m_Timer += kDtFixed;
    return bs->m_bPendingRemoval == 0;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "bonus_screen");
    h.SetInitFrames(120);  // burn through GameInit so HUD is live
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;  // strips the main-menu HUD -> board renders alone
    if (!game_work.mHud) { std::fprintf(stderr, "FAIL: mHud null after boot\n"); return 1; }

    // Construct BonusScreen with 3 mock awards.
    BonusScreen* bs = new BonusScreen();
    if (bs->size.x <= 0.0f || bs->size.y <= 0.0f) {
        std::fprintf(stderr, "FAIL: ctor size=(%.1f,%.1f) -- texture failed to load\n",
                     bs->size.x, bs->size.y);
        delete bs;
        return 2;
    }
    std::printf("OK: ctor size=(%.1f,%.1f)\n", bs->size.x, bs->size.y);

    bs->pos = _Vector3<float>(0.0f, 0.0f, 0.0f); // binary ctor @0x162d1c settles pos = Vec3::Zero
    Mortar::SmartPtr<Mortar::Texture> noTex;
    // Award names default to the fixed keys, but FN_BONUS_NAMES="a|b|c" overrides
    // them so a screenshot run can preview label text across lengths (long-name
    // clipping/overflow diagnosis on the 220px-wide rank label box).
    const char* dn0 = "ALL_APPLES";
    const char* dn1 = "STRAIGHT_3";
    const char* dn2 = "FRUIT_FRENZY";
    static char nbuf[256];
    const char* envn = std::getenv("FN_BONUS_NAMES");
    if (envn) {
        std::strncpy(nbuf, envn, sizeof(nbuf) - 1);
        nbuf[sizeof(nbuf) - 1] = '\0';
        char* p1 = std::strchr(nbuf, '|');
        if (p1) {
            *p1 = '\0'; dn0 = nbuf;
            char* rest = p1 + 1; dn1 = rest;
            char* p2 = std::strchr(rest, '|');
            if (p2) { *p2 = '\0'; dn2 = p2 + 1; }
        }
    }
    bs->AddAward(Colour(0xAD, 0x7E, 0x00, 0xFF), noTex, dn0, 150);
    bs->AddAward(Colour(0x00, 0xAD, 0x7E, 0xFF), noTex, dn1, 300);
    bs->AddAward(Colour(0x7E, 0xAD, 0x00, 0xFF), noTex, dn2, 500);
    if (bs->m_Awards.size() != 3) {
        std::fprintf(stderr, "FAIL: m_Awards.size()=%zu expected 3\n", bs->m_Awards.size());
        return 3;
    }
    // Binary default ctor sets m_Multiplier = 1; AddAward leaves it alone.
    // Verify the default propagated; bump to 2/3/4 anyway so the test
    // exercises non-trivial DisplayedScore = tier * multiplier.
    if (bs->m_Awards[0].m_Multiplier != 1) {
        std::fprintf(stderr, "FAIL: default m_Multiplier=%d expected 1 (BonusAwardHud ctor regression)\n",
                     bs->m_Awards[0].m_Multiplier);
        return 3;
    }
    bs->m_Awards[0].m_Multiplier = 2;
    bs->m_Awards[1].m_Multiplier = 3;
    bs->m_Awards[2].m_Multiplier = 4;
    std::printf("OK: 3 awards added (tiers 150/300/500, default mult=1 verified, bumped to 2/3/4)\n");

    game_work.mHud->AddControl(bs);

    if (h.IsScreenshot()) {
        // Capture the fully-revealed board (all 3 awards shown, settled,
        // before transition-out at m_Timer>7.0 and before dismissal ~7.2)
        // so long-name label rendering is visible. The dismiss loop below
        // would otherwise remove the board from the HUD first.
        // FN_BONUS_TIMER overrides the pinned m_Timer so a run can capture a
        // specific phase (e.g. reveal=1.5, pop-peak~2.77, settled=3.5).
        float shotT = 3.5f;
        const char* envt = std::getenv("FN_BONUS_TIMER");
        if (envt) shotT = (float)std::atof(envt);
        bs->m_Timer = shotT;
        h.RunComponentHeadless(4);
        h.ScreenshotPng();
        return h.Shutdown();
    }

    if (h.IsInteractive()) {
        // Keep the dialog up indefinitely in interactive mode: when the
        // reveal animation completes, restart it so the tester can look
        // at the screen. ESC / window close still exits via game.running.
        h.RunInteractive(BonusTick, bs, /*maxFrames=*/-1);
        return h.Shutdown();
    }

    // Headless: advance m_Timer manually per frame and capture metrics.
    int firstScoreFrame = -1, dismissFrame = -1, maxScore = 0;
    for (int frame = 0; frame < TIMEOUT_FRAMES; ++frame) {
        bs->m_Timer += kDtFixed;
        h.RunComponentHeadless(1);
        if (bs->m_DisplayedScore > maxScore) maxScore = bs->m_DisplayedScore;
        if (bs->m_DisplayedScore > 0 && firstScoreFrame < 0) firstScoreFrame = frame;
        if (bs->m_bPendingRemoval && dismissFrame < 0) { dismissFrame = frame; break; }
    }
    if (firstScoreFrame < 0) {
        std::fprintf(stderr, "FAIL: m_DisplayedScore stayed 0 in %d frames\n", TIMEOUT_FRAMES);
        return 4;
    }
    std::printf("OK: m_DisplayedScore first >0 at frame %d, peak=%d\n", firstScoreFrame, maxScore);
    if (dismissFrame < 0) {
        std::fprintf(stderr, "FAIL: m_bPendingRemoval never flipped in %d frames\n", TIMEOUT_FRAMES);
        return 5;
    }
    std::printf("OK: m_bPendingRemoval=1 at frame %d\n", dismissFrame);

    // Pin m_Timer past all 3 award reveals for the screenshot.
    bs->m_bPendingRemoval = 0;
    bs->m_Timer = 2.5f;
    h.RunHeadless(5);

    std::printf("DEBUG: pFontBlue2 valid=%d pFontMain valid=%d\n",
                (int)game_work.pFontBlue2.IsValid(),
                (int)game_work.pFontMain.IsValid());

    int ww = 0, wh = 0;
    unsigned char* px = h.ReadPixels(&ww, &wh);
    if (!px) { std::fprintf(stderr, "FAIL: glReadPixels unavailable\n"); return 6; }

    int bright = 0, darkInDialog = 0;
    const int cxLo = ww * 4 / 10, cxHi = ww * 6 / 10;
    const int cyLo = wh * 3 / 10, cyHi = wh * 7 / 10;
    for (int y = 0; y < wh; ++y) {
        for (int x = 0; x < ww; ++x) {
            int i = y * ww + x;
            unsigned char r = px[i*3 + 0], g = px[i*3 + 1], b = px[i*3 + 2];
            if (r > 200 || g > 200 || b > 200) ++bright;
            if (x >= cxLo && x < cxHi && y >= cyLo && y < cyHi
             && r < 60 && g < 60 && b < 60) ++darkInDialog;
        }
    }
    std::printf("DEBUG: bright=%d dark-in-dialog-center=%d\n", bright, darkInDialog);
    std::free(px);

    if (h.IsScreenshot()) h.ScreenshotPng();

    if (bright < 5000) {
        std::fprintf(stderr, "FAIL: only %d bright pixels (dialog likely invisible)\n", bright);
        return 7;
    }
    std::printf("OK: %d bright pixels rendered (dialog visible)\n", bright);
    std::printf("PASS: BonusScreen ctor + Update + dismiss completed cleanly\n");
    return h.Shutdown();
}
