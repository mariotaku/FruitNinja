// test_screenshot -- per-screen screenshot CAPTURE driver.
//
// Capture-only: it writes a PNG and exits. No reference image is loaded and
// nothing is compared, so a green run means "the screen booted and drew",
// not "the output matches a reference". The PNGs are for human eyeballing.
// (The suite's only real golden comparison is tests/test_renderer.cpp
// against tests/golden/renderer/.)
//
// Usage: test_screenshot <screen> [--interactive|--screenshot|--headless]
//                                  [--content=<key>] [--lang=<code>]
//
// Drives the named screen to a fully-drawn, stable state and captures the
// framebuffer to tmp/test/screenshots/<suite>/<case>.png.
//
// Supported screens (suite/case mapping):
//   bonus               -> bonus/default (no --content given)
//                       -> bonus/<content>_<lang> otherwise (task #32 matrix;
//                          --content=one|two|three|big, --lang=<code>; see RunBonus)
//   gameover            -> gameover/classic    (score 266, highscore 200, NEW BEST, ScoreControl)
//   gameover_zen        -> gameover/zen        (score 456, highscore 400, NEW BEST, ScoreControl)
//   gameover_arcade     -> gameover/arcade     (score 789, highscore 700, NEW BEST, ScoreControl)
//   drawquad            -> drawquad/default
//   mesh                -> mesh/default
//   gameover_withscore  -> temp/gameover_with_scorecontrol  (classic 266, kept for reference)
//
// FruitFact per-mode screenshots have moved to test_fruitfact (classic/arcade/zen).
//
// Adding more screens later: add a ScreenCase entry to the dispatch table
// and implement its fixture function below.

#include "test_harness.h"
#include "screens/BonusScreen.h"
#include "screens/GameOverScreen.h"
#include "hud/ScoreControl.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/BonusManager.h"
#include "game/Bonus.h"
#include "game/FruitSaveData.h"
#include "engine/math/_Vector3.h"
#include "engine/math/Colour.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Texture.h"
#include "engine/asset/Mesh.h"
#include "engine/asset/MeshManager.h"
#include "engine/asset/Model.h"
#include "engine/render/MatrixStack.h"
#include "engine/util/SmartPtr.h"
#include "engine/util/StringTable.h"
#include "engine/math/Quaternion.h"
#include "engine/math/math3d.h"
#include "debug/DebugFlags.h"
#include <cstring>

// Port specific: --debug-textbounds flag. When set, FN::g_DebugHitboxes is
// forced on before the screenshot render so the text-bounds overlay fires.
// Output goes to debug/bonus_textbounds.png instead of the normal label.
static bool g_DebugTextBounds = false;

// Port specific: --content=<key> selects the BonusScreen content x locale
// matrix case (task #32). NULL (no flag) keeps the original single-case
// "bonus/default" fixture byte-for-byte for back-compat. See RunBonus.
static const char* g_ContentArg = NULL;

// ---------------------------------------------------------------------------
// Forward declarations for per-screen fixture functions.
// ---------------------------------------------------------------------------
static int RunBonus(fn::TestHarness& h);
static int RunGameOver(fn::TestHarness& h);
static int RunGameOverZen(fn::TestHarness& h);
static int RunGameOverArcade(fn::TestHarness& h);
static int RunDrawQuad(fn::TestHarness& h);
static int RunMesh(fn::TestHarness& h);
static int RunGameOverWithScore(fn::TestHarness& h);

// ---------------------------------------------------------------------------
// Dispatch table.
// ---------------------------------------------------------------------------
struct ScreenCase {
    const char* name;       // command-line argument (e.g. "gameover")
    const char* label;      // screenshot path suffix: "<suite>/<case>" (e.g. "gameover/classic")
    int (*run)(fn::TestHarness& h);
};

static const ScreenCase kScreens[] = {
    { "bonus",              "bonus/default",               RunBonus             },
    { "gameover",           "gameover/classic",            RunGameOver          },
    { "gameover_zen",       "gameover/zen",                RunGameOverZen       },
    { "gameover_arcade",    "gameover/arcade",             RunGameOverArcade    },
    { "drawquad",           "drawquad/default",            RunDrawQuad          },
    { "mesh",               "mesh/default",                RunMesh              },
    { "gameover_withscore", "temp/gameover_with_scorecontrol",    RunGameOverWithScore },
    // TODO: fixture for shop
    { NULL, NULL, NULL }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int FailUsage() {
    fprintf(stderr,
        "usage: test_screenshot <screen> [--interactive|--screenshot|--headless]\n"
        "                                [--content=<key>] [--lang=<code>]\n"
        "  screens: bonus gameover gameover_zen gameover_arcade drawquad mesh\n"
        "           gameover_withscore\n"
        "  bonus --content=one|two|three|big  (default: original 3-award fixture)\n"
        "  (fruitfact per-mode: see test_fruitfact classic|arcade|zen)\n");
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* screen_name = argv[1];

    // Find the screen case.
    const ScreenCase* sc = NULL;
    for (int i = 0; kScreens[i].name != NULL; ++i) {
        if (std::strcmp(kScreens[i].name, screen_name) == 0) {
            sc = &kScreens[i];
            break;
        }
    }
    if (!sc) {
        std::fprintf(stderr, "test_screenshot: unknown screen '%s'\n", screen_name);
        return FailUsage();
    }

    fn::TestHarness h(argc, argv, sc->label);

    // Read --debug-textbounds / --content= / --lang= via the harness accessors.
    // --lang= is read here (independently of TestHarness::ParseFlags, which
    // applies it to languageFlag/StringTable) purely to build the composite
    // screenshot label -- same pattern as test_achievement_notification.cpp.
    g_DebugTextBounds   = h.OptFlag("debug-textbounds");
    g_ContentArg        = h.Opt("content", NULL);
    const char* langArg = h.Opt("lang", "default");

    // When --debug-textbounds is set, redirect the output label so the overlay
    // screenshot goes to a separate path and doesn't overwrite the normal capture.
    // Otherwise, an explicit --content= for the bonus screen builds a
    // "bonus/<content>_<lang>" label; no --content keeps "bonus/default" verbatim.
    char contentLabelBuf[128];
    if (g_DebugTextBounds) {
        if (std::strcmp(sc->name, "bonus") == 0) {
            h.label = "debug/bonus_textbounds";
        } else if (std::strcmp(sc->name, "gameover") == 0) {
            h.label = "debug/gameover_textbounds";
        }
    } else if (std::strcmp(sc->name, "bonus") == 0 && g_ContentArg != NULL) {
        std::snprintf(contentLabelBuf, sizeof(contentLabelBuf), "bonus/%s_%s",
                      g_ContentArg, langArg);
        h.label = contentLabelBuf;
    }
    // 120 burn-in frames: lets GameInit run through the Splash->Game state
    // transition so fonts/textures are loaded before we strip the HUD.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    // InitComponent() boots normally then clears the HUD so GameDraw has
    // no controls and we get a clean background for component-only rendering.
    if (!h.InitComponent()) return 1;

    // Enable text-bounds overlay after init so burn-in frames don't trigger it.
    // Level 3 = entity + HUD + font overlays (the font-box overlay is what
    // --debug-textbounds actually wants; g_DebugHitboxes is now a 4-level int).
    if (g_DebugTextBounds) {
        FN::g_DebugHitboxes = 3;
    }

    return sc->run(h);
}

// ---------------------------------------------------------------------------
// BonusScreen fixture.
// Uses component-isolation mode: only BonusScreen renders on a clean background.
// No MainScreen/menu, no background texture, no particles, no game state machine.
//
// Setup mirrors test_bonus_screen.cpp:
//   1-3 awards (names/tiers/multipliers), pos (0,0,0), added to mHud.
//   m_Timer pinned past every award's reveal gate, m_bPendingRemoval=0.
//
// g_ContentArg selects the content x locale matrix (task #32) -- see the
// content-selection block inside RunBonus for the one/two/three/big cases.
// ---------------------------------------------------------------------------
static const float kDtFixed = 1.0f / 60.0f;

// Tick callback for interactive component mode. Advances m_Timer and stops
// once the dismiss flag fires.
static bool BonusTick(Game& /*game*/, int /*frame*/, void* userdata) {
    BonusScreen* bs = (BonusScreen*)userdata;
    if (!bs) return false;
    bs->m_Timer += kDtFixed;
    return bs->m_bPendingRemoval == 0;
}

static int RunBonus(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    BonusScreen* bs = new BonusScreen();
    if (bs->size.x <= 0.0f || bs->size.y <= 0.0f) {
        std::fprintf(stderr, "FAIL: BonusScreen ctor size=(%.1f,%.1f) -- texture load failed\n",
                     bs->size.x, bs->size.y);
        delete bs;
        return 2;
    }

    bs->pos = _Vector3<float>(0.0f, 0.0f, 0.0f); // binary ctor @0x162d1c settles pos = Vec3::Zero
    // m_LayerFlags = HUD_LAYER_POST_ACTOR is now set by the BonusScreen ctor
    // itself (v1.6.1 BonusScreen::BonusScreen @0x00162d1c); no manual patch
    // needed here anymore.

    // Per-award icons (bonus_icon_*.tex). NOTE: the real award icon is the
    // bonusType's m_StarTexture, a small per-award bonus_icon -- NOT star_awards.tex
    // (that is a 128px star sprite STRIP which scales to a huge quad over the name).
    Mortar::SmartPtr<Mortar::Texture> icon0 =
        Mortar::TextureManager::LoadLocalisedTexture("bonus_icon_total_combo.tex");
    Mortar::SmartPtr<Mortar::Texture> icon1 =
        Mortar::TextureManager::LoadLocalisedTexture("bonus_icon_max_combo.tex");
    Mortar::SmartPtr<Mortar::Texture> icon2 =
        Mortar::TextureManager::LoadLocalisedTexture("bonus_icon_no_fruit_dropped.tex");

    // Real award data per spec (#212). Colour ctor is (r,g,b,a); spec gives BGRA memory layout:
    // slot0 gold: b=0x00,g=0x7E,r=0xAD -> Colour(0xAD,0x7E,0x00,0xFF)
    // slot1 red:  b=0x05,g=0x05,r=0xA0 -> Colour(0xA0,0x05,0x05,0xFF)
    // slot2 blue: b=0x95,g=0x5C,r=0x01 -> Colour(0x01,0x5C,0x95,0xFF)
    //
    // NOTE (#34): this fixture calls BonusScreen::AddAward directly with its
    // own alpha=0xFF colours -- it does NOT exercise BonusManager::SetUpBonusScreen,
    // so it never caught the alpha=0x00 bug that lived in BonusManager.cpp's
    // hardcoded k_TierColours (fixed separately). Production alpha coverage
    // for that path is BonusManager.cpp's own constants, not this test.
    //
    // Award names are localization keys (Bonus::Parse uses GETSTRING_CAST_0_STR):
    //   GAME_TEXTURE_17  = "COMBO GOD!!!!"
    //   GAME_TEXTURE_118 = "%i FRUIT COMBO?!?!" (bake with value 55)
    //   GAME_TEXTURE_23  = "NO FRUIT DROPPED!"
    // Route through GETSTRING_STR so --lang= override changes displayed names.
    // languageFlag is set before game.init() so string table and item parse
    // both see the chosen language; these GETSTRING_STR calls follow suit.
    const char* name0 = GETSTRING_STR("GAME_TEXTURE_17",  0);
    if (!name0 || !name0[0]) name0 = "COMBO GOD!!!!";

    // GAME_TEXTURE_118 contains %i; bake with the tier value (55).
    const char* tmpl1 = GETSTRING_STR("GAME_TEXTURE_118", 0);
    char name1buf[64];
    if (tmpl1 && tmpl1[0]) {
        std::snprintf(name1buf, sizeof(name1buf), tmpl1, 55);
    } else {
        std::snprintf(name1buf, sizeof(name1buf), "55 FRUIT COMBO?!?!");
    }

    const char* name2 = GETSTRING_STR("GAME_TEXTURE_23",  0);
    if (!name2 || !name2[0]) name2 = "NO FRUIT DROPPED!";

    // -------------------------------------------------------------------
    // Content selection (task #32 content x locale matrix).
    // g_ContentArg == NULL reproduces the original single-case fixture
    // byte-for-byte (3 awards, m_TotalScore overridden to 1234, m_Timer
    // pinned to 2.5f) so the plain `screenshot_bonus` ctest is unaffected.
    // An explicit --content=<key> selects a curated award count / total:
    //   three -- 3 awards (gold/red/blue), natural total (50+55+50=155)
    //   two   -- first 2 awards (gold, red), natural total (105)
    //   one   -- first 1 award (gold), natural total (50)
    //   big   -- 3 awards, m_DisplayedScore/m_TotalScore forced to a
    //            6-digit value post-Update to exercise the TOTAL number's
    //            size-scale formula (26 + 14*displayed/total; BonusScreen.cpp
    //            Draw @0x00164a68 area) at its ratio=1.0 max (scale 40px)
    //            and confirm a 6-digit string still fits the bottom band.
    // -------------------------------------------------------------------
    int nAwards = 3;
    bool bigTotal = false;
    if (g_ContentArg != NULL) {
        if      (std::strcmp(g_ContentArg, "one")   == 0) nAwards = 1;
        else if (std::strcmp(g_ContentArg, "two")   == 0) nAwards = 2;
        else if (std::strcmp(g_ContentArg, "three") == 0) nAwards = 3;
        else if (std::strcmp(g_ContentArg, "big")   == 0) { nAwards = 3; bigTotal = true; }
        else {
            std::fprintf(stderr, "FAIL: unknown --content=%s (want one|two|three|big)\n", g_ContentArg);
            delete bs;
            return 5;
        }
    }

    if (g_ContentArg == NULL) {
        bs->AddAward(Colour(0xAD, 0x7E, 0x00, 0xFF), icon0, name0,      50);  // slot0 gold
        bs->AddAward(Colour(0xA0, 0x05, 0x05, 0xFF), icon1, name1buf,   55);  // slot1 red
        bs->AddAward(Colour(0x01, 0x5C, 0x95, 0xFF), icon2, name2,      50);  // slot2 blue
        // Representative total score so m_ScoreBox shows a number.
        // Note: AddAward already sums tier into m_TotalScore (50+55+50=155); override to 1234.
        bs->m_TotalScore = 1234;
    } else {
        const Colour kColours[3] = {
            Colour(0xAD, 0x7E, 0x00, 0xFF),   // slot0 gold
            Colour(0xA0, 0x05, 0x05, 0xFF),   // slot1 red
            Colour(0x01, 0x5C, 0x95, 0xFF),   // slot2 blue
        };
        Mortar::SmartPtr<Mortar::Texture> icons[3] = { icon0, icon1, icon2 };
        const char* names[3] = { name0, name1buf, name2 };
        const int   tiers[3] = { 50, 55, 50 };
        for (int i = 0; i < nAwards; ++i) {
            bs->AddAward(kColours[i], icons[i], names[i], tiers[i]);
        }
        // No m_TotalScore override here: AddAward's natural sum is left in
        // place for one/two/three (m_ScoreBox shows the real accumulated tier
        // total); "big" overrides it below, after the finale fires.
    }
    // BuildBonusText is now called unconditionally every Update tick (fixed
    // create-once latch, task #26) -- no manual trigger needed here.

    // Add BonusScreen as the ONLY control in the isolated HUD.
    // InitComponent() already cleared the game-state controls (MainScreen, etc.)
    // so nothing else will draw on top.
    game_work.mHud->AddControl(bs);

    if (h.IsInteractive()) {
        // Interactive: advance timer via tick callback, draw only BonusScreen.
        h.RunComponentInteractive(BonusTick, bs, /*maxFrames=*/-1,
                                  Mortar::HUD_LAYER_POST_ACTOR);
        // Drop the award-icon refs BEFORE Shutdown(): GameDestroy's GL-handle
        // leak check runs inside game.shutdown(), and these locals would still
        // be strong refs at that point (they only die when RunBonus returns).
        icon0.SetNull();
        icon1.SetNull();
        icon2.SetNull();
        return h.Shutdown();
    }

    if (g_ContentArg == NULL) {
        // Pin to fully-revealed stable state: timer 2.5s covers all 3 awards
        // (~0.6s each, revealEnd=2.0s), finale already fired, well before dismiss(3.0s).
        // HUD::Update advances m_Timer by 1/60 each frame; after 10 frames the timer
        // is ~2.67s, still below the 3.0s dismiss threshold. Reset m_bPendingRemoval
        // each frame defensively (in case timer drifts past dismiss).
        bs->m_Timer = 2.5f;
        bs->m_bPendingRemoval = 0;
        // Manually run one Update cycle to populate per-award scales/scores before
        // any draw call, without relying on RunComponentHeadless's own Update.
        // This primes m_Awards[i].m_DisplayedScore = tierBase * multiplier.
        bs->Update(kDtFixed);
        bs->m_bPendingRemoval = 0; // suppress dismiss in case Update crossed the threshold
    } else {
        // Reveal-gate formula (BonusScreen.cpp Update @0x00163dd0 / Draw's per-row
        // gate): row i is revealed once m_Timer - FIRST_AWARD >= i*TIME_PER_AWARD,
        // and the one-shot finale (which snaps m_DisplayedScore to the natural sum)
        // fires once m_Timer >= FIRST_AWARD + TIME_PER_AWARD*(count+0.25). FIRST_AWARD/
        // TIME_PER_AWARD are file-static in BonusScreen.cpp (not exported); the
        // literal values are duplicated here from that file's ASM-verified constants.
        const float kFirstAward   = 0.666667f;  // FIRST_AWARD
        const float kTimePerAward = 1.0f;       // TIME_PER_AWARD
        // +nAwards*kTimePerAward comfortably clears both the last row's per-row gate
        // (needs (nAwards-1)*kTimePerAward) and revealEnd (needs (nAwards+0.25)*kTimePerAward
        // minus the +0.5 margin below already covers the +0.25 slack); still well under
        // the TOTAL_TIME+TRANSITION_OUT_TIME=7.25s dismiss threshold for nAwards<=3.
        bs->m_Timer = kFirstAward + kTimePerAward * (float)nAwards + 0.5f;
        bs->m_bPendingRemoval = 0;
        bs->Update(kDtFixed);
        bs->m_bPendingRemoval = 0;

        if (bigTotal) {
            // Synthetic content override (test fixture, not RE'd binary data): force
            // a 6-digit TOTAL to exercise the size-scale formula's worst case
            // (displayed/total ratio=1.0 -> max scale 40px) and confirm the digit
            // string still fits the bottom band. Safe to poke post-Update: the
            // one-shot finale latch (m_FinaleFired) already fired above, so Update
            // never rewrites m_DisplayedScore again on the settle ticks below.
            bs->m_DisplayedScore = 123456;
            bs->m_TotalScore     = 123456;
        }
    }

    // Content-built guard (task #26 regression check): BuildBonusText must have
    // fired through the production Update() call above, not a hand-set latch.
    if (!bs->m_bBonusTextBuilt || !bs->m_ScoreBox || !bs->m_TotalBox
     || !bs->m_RankLabelBoxes[0] || !bs->m_RankValueBoxes[0]) {
        std::fprintf(stderr,
            "FAIL: BuildBonusText did not populate content boxes via production Update() "
            "(built=%d score=%p total=%p rank0=%p val0=%p)\n",
            (int)bs->m_bBonusTextBuilt, (void*)bs->m_ScoreBox, (void*)bs->m_TotalBox,
            (void*)bs->m_RankLabelBoxes[0], (void*)bs->m_RankValueBoxes[0]);
        return 4;
    }
    std::printf("OK: content boxes built via production Update() (m_ScoreBox/m_TotalBox/rank boxes non-null)\n");

    // Settle 5 frames in isolation mode. RunComponentHeadless calls HUD::Update
    // each frame, so m_Timer advances slightly -- keep suppressing m_bPendingRemoval.
    // Since HUD::Update may delete the control if m_bPendingRemoval is set, we
    // drive frames one-at-a-time and reset the flag each frame.
    for (int i = 0; i < 5; ++i) {
        h.RunComponentHeadless(1, Mortar::HUD_LAYER_POST_ACTOR);
        bs->m_bPendingRemoval = 0;
    }

    std::printf("[%s] stable state reached (timer=%.2f, isolation mode)\n",
                h.label, bs->m_Timer);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: %s screenshot complete\n", h.label);
    // See the interactive path above: the leak check lives inside Shutdown(),
    // so the icon refs must be gone before it runs.
    icon0.SetNull();
    icon1.SetNull();
    icon2.SetNull();
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// GameOverScreen fixture (Classic mode, STATE_MAIN_DISPLAY).
//
// ScoreControl is kept present alongside GameOverScreen (same pattern as
// RunGameOverWithScore) so SCORE + number + NEW BEST render top-left.
//
// Setup:
//   gameMode = CLASSIC (0), currentScore = 266, highscore = 200 -> NEW BEST.
//   m_PauseAmount = 1.0f (fully faded; required by fast-path gate).
//   Construct in fast-path (param2=STATE_MAIN_DISPLAY, param3=0.0f) which sets
//   m_bScoreSubmitted=1 and immediately calls Update(0.0f) inside Initialise
//   (fast-path gate: param2>5 && m_PauseAmount>0.999).
//   Add GameOverScreen + ScoreControl to HUD, run 60 frames to settle.
//
// Stubbed:
//   FruitFactControl (Classic page) is created by Update state-6 on the first
//   tick after entering STATE_MAIN_DISPLAY -- it renders fine as long as textures
//   loaded during 120-frame burn-in. No manual BonusManager setup needed for
//   Classic mode.
// ---------------------------------------------------------------------------

static int RunGameOver(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Classic mode, score 266.  Highscore 200 < 266 -> NEW BEST fires.
    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 266;
    game_work.m_PauseAmount     = 1.0f;  // fully faded; required by fast-path gate
    game_work.bM_bPaused   = 0;

    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 200;
    saveData.newBestThisGame = 1;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    // Fast-path ctor: param2=STATE_MAIN_DISPLAY(6)>5, param3=0.0f satisfies
    // the gate (param2>=0 && param3>=0.0f && param2>5 && m_PauseAmount>0.999).
    // Initialise calls Update(0.0f) and sets m_bScoreSubmitted=1 immediately.
    GameOverScreen* gos = new GameOverScreen(
        "Classic",
        GameOverScreen::STATE_MAIN_DISPLAY,   // param2
        0.0f,                                  // param3
        1,                                     // expressionIdx
        1,                                     // bgPatternIdx
        0,                                     // tabIndex
        0);                                    // starCount

    gos->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT;
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);

    // ScoreControl: kept present so SCORE + number + NEW BEST render top-left.
    ScoreControl* sc = new ScoreControl();
    sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
    sc->Skip();
    game_work.mHud->AddControl(sc);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        game_work.m_SaveData = prevSaveData;
        return h.Shutdown();
    }

    // Settle 60 frames with per-layer passes matching GameDraw's HUD::Draw order
    // (0x40->0x80->0x01->0x08->0x100->0x200->0x400). This is required for
    // MenuButton: BeginDraw re-arms 0x40; the 0x40 pass draws+demotes to 0x80;
    // the 0x80 pass then draws the button face + Retry/Quit label. A single
    // all-bits pass misses the 0x80 visit so only the scratch backdrop renders.
    for (int i = 0; i < 60; ++i) {
        game_work.m_PauseAmount     = 1.0f;
        game_work.currentScore = 266;
        h.RunComponentHeadlessMultiPass(1);
    }

    std::printf("[gameover/classic] stable state reached (m_State=%d, score=%d, sc_displayed=%d)\n",
                gos->m_State, game_work.currentScore, sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) {
            game_work.m_SaveData = prevSaveData;
            return 3;
        }
    }

    game_work.m_SaveData = prevSaveData;
    std::printf("PASS: gameover/classic screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// GameOverScreen fixture -- Zen mode, STATE_MAIN_DISPLAY.
//
// FruitFactZenPage::Init reads game_work.m_SaveData->m_BestComboLength
// and m_BestComboFruits[]. A combo of 4 (> 2 threshold) forces the
// hasCombo branch that displays fruit icons + combo star.
// Fruit type indices 0,1,2,3 are the first four fruits in the fruit table
// (watermelon/apple/pear/orange by default); Init calls Fruit::FruitInfo()
// on each but ignores the return value, so any valid index works.
//
// The fixture creates a local FruitSaveData, seeds the combo fields, and
// points game_work.m_SaveData at it for the duration of the settle frames.
// ---------------------------------------------------------------------------
static int RunGameOverZen(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // A local save-data object; seeded with a 4-fruit combo + highscore.
    // Highscore 400 < 456 -> NEW BEST fires.
    FruitSaveData zenSave;
    zenSave.m_BestComboLength = 4;
    zenSave.m_BestComboFruits[0] = 0;  // fruit type index 0
    zenSave.m_BestComboFruits[1] = 1;  // fruit type index 1
    zenSave.m_BestComboFruits[2] = 2;  // fruit type index 2
    zenSave.m_BestComboFruits[3] = 0;  // fruit type index 0 again (mixed combo)
    zenSave.m_ModeHighScores[Mortar::GAME_MODE_ZEN] = 400;
    zenSave.newBestThisGame = 1;

    // Point game_work at our local save-data so FruitFactZenPage::Init reads it.
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &zenSave;

    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_ZEN;
    game_work.currentScore = 456;
    game_work.m_PauseAmount     = 1.0f;
    game_work.bM_bPaused   = 0;

    GameOverScreen* gos = new GameOverScreen(
        "Zen",
        GameOverScreen::STATE_MAIN_DISPLAY,   // param2
        0.0f,                                  // param3
        1,                                     // expressionIdx
        1,                                     // bgPatternIdx
        0,                                     // tabIndex
        0);                                    // starCount

    gos->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT;
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);

    // ScoreControl: kept present so SCORE + number + NEW BEST render top-left.
    ScoreControl* sc = new ScoreControl();
    sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
    sc->Skip();
    game_work.mHud->AddControl(sc);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        game_work.m_SaveData = prevSaveData;
        return h.Shutdown();
    }

    // Per-layer passes matching GameDraw's HUD::Draw order so Retry/Quit MenuButtons
    // render their face+label (0x80 pass) after the scratch backdrop (0x40 pass).
    for (int i = 0; i < 60; ++i) {
        game_work.m_PauseAmount     = 1.0f;
        game_work.currentScore = 456;
        h.RunComponentHeadlessMultiPass(1);
    }

    game_work.m_SaveData = prevSaveData;

    std::printf("[gameover/zen] stable state reached (m_State=%d, combo=%d, sc_displayed=%d)\n",
                gos->m_State, zenSave.m_BestComboLength, sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: gameover/zen screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// GameOverScreen fixture -- Arcade mode, STATE_MAIN_DISPLAY.
//
// FruitFactBonusFactPage::Init iterates BonusManager::m_BestBonuses via
// GetFirstBestBonus / GetNextBestBonus. We push 2 hand-crafted Bonus objects
// directly into m_BestBonuses; each carries a DisplayName, Tier, and a
// star-icon texture (result_board_star.tex, matching what Init loads).
//
// starCount=3 is passed to the GameOverScreen ctor to show 3 stars on the
// arcade results header (a common post-round state).
// ---------------------------------------------------------------------------
static int RunGameOverArcade(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Seed BonusManager::m_BestBonuses with 2 representative awards.
    BonusManager* bm = BonusManager::GetInstance();
    bm->ClearBestBonuses();

    // Award 0: "COMBO GOD!!!!" tier 50 (gold).
    {
        Bonus b;
        const char* name = GETSTRING_STR("GAME_TEXTURE_17", 0);
        if (!name || !name[0]) name = "COMBO GOD!!!!";
        std::strncpy(b.m_DisplayName, name, sizeof(b.m_DisplayName) - 1);
        b.m_DisplayName[sizeof(b.m_DisplayName) - 1] = '\0';
        b.m_Tier = 50;
        b.m_StarTexture = Mortar::TextureManager::LoadLocalisedTexture("result_board_star.tex");
        bm->m_BestBonuses.push_back(b);
    }

    // Award 1: "NO FRUIT DROPPED!" tier 50 (blue).
    {
        Bonus b;
        const char* name = GETSTRING_STR("GAME_TEXTURE_23", 0);
        if (!name || !name[0]) name = "NO FRUIT DROPPED!";
        std::strncpy(b.m_DisplayName, name, sizeof(b.m_DisplayName) - 1);
        b.m_DisplayName[sizeof(b.m_DisplayName) - 1] = '\0';
        b.m_Tier = 50;
        b.m_StarTexture = Mortar::TextureManager::LoadLocalisedTexture("result_board_star.tex");
        bm->m_BestBonuses.push_back(b);
    }

    // Highscore 700 < 789 -> NEW BEST fires.
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_ARCADE] = 700;
    saveData.newBestThisGame = 1;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_ARCADE;
    game_work.currentScore = 789;
    game_work.m_PauseAmount     = 1.0f;
    game_work.bM_bPaused   = 0;

    // starCount=3: shows 3 stars on the arcade results board header.
    GameOverScreen* gos = new GameOverScreen(
        "Arcade",
        GameOverScreen::STATE_MAIN_DISPLAY,   // param2
        0.0f,                                  // param3
        1,                                     // expressionIdx
        1,                                     // bgPatternIdx
        0,                                     // tabIndex
        3);                                    // starCount (3 stars)

    gos->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT;
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);

    // ScoreControl: kept present so SCORE + number + NEW BEST render top-left.
    ScoreControl* sc = new ScoreControl();
    sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
    sc->Skip();
    game_work.mHud->AddControl(sc);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        game_work.m_SaveData = prevSaveData;
        return h.Shutdown();
    }

    // Per-layer passes matching GameDraw's HUD::Draw order so Retry/Quit MenuButtons
    // render their face+label (0x80 pass) after the scratch backdrop (0x40 pass).
    for (int i = 0; i < 60; ++i) {
        game_work.m_PauseAmount     = 1.0f;
        game_work.currentScore = 789;
        h.RunComponentHeadlessMultiPass(1);
    }

    std::printf("[gameover/arcade] stable state reached (m_State=%d, score=%d, sc_displayed=%d)\n",
                gos->m_State, game_work.currentScore, sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) {
            game_work.m_SaveData = prevSaveData;
            return 3;
        }
    }

    game_work.m_SaveData = prevSaveData;
    std::printf("PASS: gameover/arcade screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// DrawQuad fixture -- isolates the Mesh::DrawQuadUnCached / textured-quad path.
//
// Draws one known-opaque texture (sensei_head_01.tex) as a 200x200 quad
// centered at the screen origin, bypassing HUD/screen machinery entirely.
// Purpose: determine whether the textured-quad path renders at all.
// ---------------------------------------------------------------------------
static int RunDrawQuad(fn::TestHarness& h) {
    Mortar::SmartPtr<Mortar::Texture> tex =
        Mortar::TextureManager::LoadLocalisedTexture("sensei_head_01.tex");
    if (!tex.IsValid()) {
        std::fprintf(stderr, "FAIL: drawquad -- could not load sensei_head_01.tex\n");
        return 1;
    }

    for (int i = 0; i < 5; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto done;
        }

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(h.window), &ww, &wh);
        glViewport(0, 0, ww, wh);

        Mortar::DisplayManager::GetInstance().BeginFrame();

        MatrixManager::GetInstance().SetupOrtho(
            160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

        {
            MatrixManager& mm = MatrixManager::GetInstance();
            tex->SetUnCached();
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().Scale(_Vector3<float>(200.0f, 200.0f, 1.0f));
            mm.GetWorldStack().Translate(_Vector3<float>(0.0f, 0.0f, 0.0f));
            mm.UploadModelViewOnly();
            // Faithful board UV convention: DrawQuadUnCached(colour, uMin, uMax, vMin, vMax).
            // (0,1,0,1) = full texture; mirrors FruitFactClassicFactPage::DrawOrder's board.
            Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255),
                                           0.0f, 1.0f, 0.0f, 1.0f, NULL);
            tex->UnSetUnCached();
        }

        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

done:
    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: drawquad/default complete (texValid=%d)\n", (int)tex.IsValid());
    return 0;
}

// ---------------------------------------------------------------------------
// Mesh fixture -- renders a single 3D fruit mesh (apple_single.mmd) in
// isolation, exercising the 3D Geometry::Render path (Model::Draw ->
// Mesh::Draw -> Geometry::Render), separate from the 2D quad path.
//
// No live Fruit entity or physics state needed. MeshManager::Load is called
// directly; if the model is already cached from the burn-in it returns the
// cached SmartPtr.
//
// Camera replicates FruitCamera::SetupPerspective (v1.6.1 @0x001810ac):
//   SetupLookAt(eye=(0,0,1), upHint=(0,1,0), target=(0,0,0))
//   SetupOrtho(160, -160, -240, 240, 2000, -6000)
//   (despite the name, FruitCamera::SetupPerspective uses ortho -- ASM-verified)
//
// World transform replicates DrawOneModel in Fruit.cpp (v1.6.1 @0x00179216):
//   MakeScale(s,s,s) -> rotate by quaternion -> GlobalTranslate44(pos)
//   Apple scale: fruitlist.xml scale="60" -> s = 60 * 0.01 = 0.60f
//   Apple Z: GetFruitZPosition initial value = -500.0f (Fruit.cpp @0x00169108)
//   Tilt: 45-degree Y-axis rotation so 3D form is visible (not a flat disc)
//
// Depth test: GameDraw enables SetDepthBuffer(1)+SetDepthBufferWrite(1) before
//   ActorManager::Draw (@0x0016ba10). BeginFrame disables it. We replicate that
//   sequence so GL_LESS depth test orders front/back faces correctly.
// ---------------------------------------------------------------------------
static int RunMesh(fn::TestHarness& h) {
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (!meshMgr) {
        std::fprintf(stderr, "FAIL: mesh -- MeshManager singleton is null\n");
        return 1;
    }

    Mortar::SmartPtr<Mortar::Model> model = meshMgr->Load("models/fruit/apple_single.mmd");
    if (!model.IsValid()) {
        std::fprintf(stderr, "FAIL: mesh -- apple_single.mmd load failed\n");
        return 1;
    }

    std::printf("[mesh] apple_single.mmd loaded (nodes=%d)\n", model->NodeCount());

    // Apple scale from fruitlist.xml (scale="60"): s = 60 * 0.01 = 0.60f.
    // Matches Fruit::Init -> SetFruitType path: scale = Vec3::One() * (m_Scale * 0.01f).
    // Apple collision radius = 5 + 0.52*60 = 36.2 ortho units -- reasonable for the
    // 160-unit half-height of the game ortho.
    const float kAppleScale = 0.60f;

    // 45-degree Y-axis tilt so the 3D form is visible rather than a flat disc.
    // 45 deg == 65536/8 == 0x2000 in the binary's 16-bit angle index. Same rotation
    // as the old radians helper (which was a port invention with no binary
    // counterpart and has been removed): its half-angle pi/8 and this one's
    // half-index 0x1000 both land on sin 0.38268 / cos 0.92388.
    Quaternion rot;
    rot.CreateFromAxisAngle(0.0f, 1.0f, 0.0f, 0x2000u);

    // Replicate DrawOneModel (Fruit.cpp @0x00179216):
    //   mat = MakeScale(s,s,s)
    //   mat = quat.ToMatrix44() * mat   (rotation applied after scale)
    //   mat.GlobalTranslate44(pos)
    Matrix44 world = Matrix44::MakeScale(kAppleScale, kAppleScale, kAppleScale);
    {
        Matrix44 qmat = rot.ToMatrix44();
        float temp[16];
        mat4_multiply(temp, qmat.ptr(), world.ptr());
        for (int i = 0; i < 16; ++i) world.m[i] = temp[i];
    }
    // Z = -500 is the initial GetFruitZPosition value (Fruit.cpp s_FruitZCounter init).
    // In ortho this doesn't affect apparent size but puts the fruit within
    // the near/far range [2000, -6000] used by the game camera.
    world.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, -500.0f));

    for (int i = 0; i < 5; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto mesh_done;
        }

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(h.window), &ww, &wh);
        glViewport(0, 0, ww, wh);

        // BeginFrame disables depth test. Re-enable it exactly as GameDraw does
        // before ActorManager::Draw (binary @ 0x0016ba10).
        Mortar::DisplayManager::GetInstance().BeginFrame();
        Mortar::DisplayManager::GetInstance().SetDepthBuffer(true);
        Mortar::DisplayManager::GetInstance().SetDepthBufferWrite(true);

        {
            MatrixManager& mm = MatrixManager::GetInstance();
            // Replicate FruitCamera::SetupPerspective (v1.6.1 @0x001810ac):
            //   eye=(0,0,1) target=(0,0,0) up=(0,1,0)
            //   SetupOrtho(160, -160, -240, 240, 2000, -6000)
            mm.SetupLookAt(_Vector3<float>(0.0f, 0.0f, 1.0f),
                           _Vector3<float>(0.0f, 1.0f, 0.0f),
                           _Vector3<float>(0.0f, 0.0f, 0.0f));
            mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
            // Model::Draw(const Matrix44&) -- same call as DrawOneModel in Fruit.cpp.
            model->Draw(world);
        }

        SDL_GL_SwapWindow(static_cast<SDL_Window*>(h.window));
    }

mesh_done:
    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: mesh/default complete\n");
    return 0;
}

// ---------------------------------------------------------------------------
// GameOverWithScore fixture -- Classic game-over WITH ScoreControl present.
//
// Purpose: verify ScoreControl renders at the correct position in the full
// game-over flow (same top-left position as the isolated test_scorecontrol).
// Settled state confirmed matching: m_PauseAmount=1.0, pos=(-418,138),
// m_DrawPosX=-160.71 (lerped to anchor), m_DrawPosY=80.
//
// Output screenshot: tmp/test/screenshots/temp/gameover_with_scorecontrol.png
// ---------------------------------------------------------------------------
static int RunGameOverWithScore(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 266;
    game_work.m_PauseAmount     = 1.0f;
    game_work.bM_bPaused   = 0;

    // FruitSaveData with highscore 200 < 266 -> triggers NEW BEST path.
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 200;
    saveData.newBestThisGame = 1;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    // Create GameOverScreen (fast-path to STATE_MAIN_DISPLAY).
    // Its Update() in STATE_MAIN_DISPLAY always writes game_work.m_PauseAmount=1.0f
    // at step 5 (line: "game_work.m_PauseAmount = 1.0f; m_State = STATE_MAIN_DISPLAY").
    GameOverScreen* gos = new GameOverScreen(
        "Classic",
        GameOverScreen::STATE_MAIN_DISPLAY,
        0.0f,
        1,   // expressionIdx
        1,   // bgPatternIdx
        0,   // tabIndex
        0);  // starCount
    gos->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT;
    game_work.pGameOverScreen = gos;
    game_work.mHud->AddControl(gos);

    // Create ScoreControl with the same seeding as the isolated test.
    ScoreControl* sc = new ScoreControl();
    sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
    sc->Skip();
    game_work.mHud->AddControl(sc);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1, 0x7FFFFFFF);
        game_work.m_SaveData = prevSaveData;
        return h.Shutdown();
    }

    // Settle 60 frames using the multi-pass layer order (same as RunGameOver).
    // GameOverScreen::Update writes game_work.m_PauseAmount=1.0f each frame in
    // STATE_MAIN_DISPLAY, so ScoreControl::Update sees m_PauseAmount=1.0 each tick.
    for (int i = 0; i < 60; ++i) {
        game_work.m_PauseAmount     = 1.0f;
        game_work.currentScore = 266;
        h.RunComponentHeadlessMultiPass(1);
    }

    std::printf("[gameover_with_scorecontrol] stable state reached"
                " (gos_state=%d, sc_displayedScore=%d)\n",
                gos->m_State, sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) {
            game_work.m_SaveData = prevSaveData;
            return 3;
        }
    }

    game_work.m_SaveData = prevSaveData;
    std::printf("PASS: temp/gameover_with_scorecontrol screenshot complete\n");
    return h.Shutdown();
}
