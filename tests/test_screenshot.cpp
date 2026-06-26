// test_screenshot -- per-screen golden-screenshot driver.
//
// Usage: test_screenshot <screen> [--interactive|--screenshot|--headless]
//
// Drives the named screen to a fully-drawn, stable state and captures the
// framebuffer to tmp/test/screenshots/<screen>_screen.png.
//
// Supported screens:
//   bonus    -- BonusScreen with 3 mock awards, timer past all reveals.
//   gameover -- Classic-mode GameOverScreen in STATE_MAIN_DISPLAY, score 123.
//
// Adding more screens later: add a ScreenCase entry to the dispatch table
// and implement its fixture function below.

#include "test_harness.h"
#include "screens/BonusScreen.h"
#include "screens/GameOverScreen.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "engine/math/Vec3.h"
#include "engine/asset/TextureManager.h"
#include "engine/util/StringTable.h"

// ---------------------------------------------------------------------------
// Forward declarations for per-screen fixture functions.
// ---------------------------------------------------------------------------
static int RunBonus(fn::TestHarness& h);
static int RunGameOver(fn::TestHarness& h);

// ---------------------------------------------------------------------------
// Dispatch table.
// ---------------------------------------------------------------------------
struct ScreenCase {
    const char* name;
    int (*run)(fn::TestHarness& h);
};

static const ScreenCase kScreens[] = {
    { "bonus",    RunBonus    },
    { "gameover", RunGameOver },
    // TODO: fixture for shop
    { NULL, NULL }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int FailUsage() {
    fprintf(stderr,
        "usage: test_screenshot <screen> [--interactive|--screenshot|--headless]\n"
        "  screens: bonus gameover\n");
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

    // Build a label for output paths: "<screen>_screen".
    char label[64];
    std::snprintf(label, sizeof(label), "%s_screen", screen_name);

    fn::TestHarness h(argc, argv, label);
    // 120 burn-in frames: lets GameInit run through the Splash->Game state
    // transition so fonts/textures are loaded before we strip the HUD.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    // InitComponent() boots normally then clears the HUD so GameDraw has
    // no controls and we get a clean background for component-only rendering.
    if (!h.InitComponent()) return 1;

    return sc->run(h);
}

// ---------------------------------------------------------------------------
// BonusScreen fixture.
// Uses component-isolation mode: only BonusScreen renders on a clean background.
// No MainScreen/menu, no background texture, no particles, no game state machine.
//
// Setup mirrors test_bonus_screen.cpp:
//   3 awards (names/tiers/multipliers), pos (0,-20,0), added to mHud.
//   m_Timer pinned to 2.5f (past all award reveals), m_bPendingRemoval=0.
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

    bs->pos = Vec3(0.0f, -20.0f, 0.0f);
    // Binary layer for BonusScreen is HUD_LAYER_POST_ACTOR (0x80), drawn at
    // GameDraw step 8. HUDControl ctor defaults m_LayerFlags=0x01 (DEFAULT),
    // so set the correct layer here -- mirrors what the binary sets during
    // the GameOver->BonusScreen transition.
    bs->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

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
    // Award names are localization keys (Bonus::Parse uses GETSTRING_CAST_0_STR):
    //   GAME_TEXTURE_17  = "COMBO GOD!!!!"
    //   GAME_TEXTURE_118 = "%i FRUIT COMBO?!?!" (bake with value 55)
    //   GAME_TEXTURE_23  = "NO FRUIT DROPPED!"
    // Route through GETSTRING_STR so --lang= override changes displayed names.
    // ApplyLanguageOverride() runs before InitComponent/burn-in so these
    // GETSTRING_STR calls reflect the chosen language.
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

    bs->AddAward(Colour(0xAD, 0x7E, 0x00, 0xFF), icon0, name0,      50);  // slot0 gold
    bs->AddAward(Colour(0xA0, 0x05, 0x05, 0xFF), icon1, name1buf,   55);  // slot1 red
    bs->AddAward(Colour(0x01, 0x5C, 0x95, 0xFF), icon2, name2,      50);  // slot2 blue
    // Representative total score so m_ScoreBox shows a number.
    // Note: AddAward already sums tier into m_TotalScore (50+55+50=155); override to 1234.
    bs->m_TotalScore = 1234;
    // m_bSkipIntro triggers BuildBonusText on first Update call.
    bs->m_bSkipIntro = true;

    // Add BonusScreen as the ONLY control in the isolated HUD.
    // InitComponent() already cleared the game-state controls (MainScreen, etc.)
    // so nothing else will draw on top.
    game_work.mHud->AddControl(bs);

    if (h.IsInteractive()) {
        // Interactive: advance timer via tick callback, draw only BonusScreen.
        h.RunComponentInteractive(BonusTick, bs, /*maxFrames=*/-1,
                                  Mortar::HUD_LAYER_POST_ACTOR);
        return h.Shutdown();
    }

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

    // Settle 5 frames in isolation mode. RunComponentHeadless calls HUD::Update
    // each frame, so m_Timer advances slightly -- keep suppressing m_bPendingRemoval.
    // Since HUD::Update may delete the control if m_bPendingRemoval is set, we
    // drive frames one-at-a-time and reset the flag each frame.
    for (int i = 0; i < 5; ++i) {
        h.RunComponentHeadless(1, Mortar::HUD_LAYER_POST_ACTOR);
        bs->m_bPendingRemoval = 0;
    }

    std::printf("[bonus_screen] stable state reached (timer=%.2f, isolation mode)\n",
                bs->m_Timer);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: bonus_screen screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// GameOverScreen fixture (Classic mode, STATE_MAIN_DISPLAY, score 123).
//
// Component-isolation mode: only GameOverScreen renders on a clean background.
// No BonusManager, no live entities required.
//
// Setup:
//   gameMode = CLASSIC (0), currentScore = 123, m_GameDt = 1.0f (fully faded).
//   Construct in fast-path (param2=STATE_MAIN_DISPLAY, param3=0.0f) which sets
//   m_bScoreSubmitted=1 and immediately calls Update(0.0f) inside Initialise
//   (fast-path gate: param2>5 && m_GameDt>0.999).
//   Add to isolated HUD, run 60 frames to create Retry/Quit buttons + settle.
//
// Stubbed:
//   FruitFactPageControl (Classic page) is created by Update state-6 on the first
//   tick after entering STATE_MAIN_DISPLAY -- it renders fine as long as textures
//   loaded during 120-frame burn-in. No manual BonusManager setup needed for
//   Classic mode.
// ---------------------------------------------------------------------------

static int RunGameOver(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Classic mode, score 123.
    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 123;
    game_work.m_GameDt     = 1.0f;  // fully faded; required by fast-path gate

    // Fast-path ctor: param2=STATE_MAIN_DISPLAY(6)>5, param3=0.0f satisfies
    // the gate (param2>=0 && param3>=0.0f && param2>5 && m_GameDt>0.999).
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

    // Add as the ONLY control -- component mode already cleared the HUD.
    game_work.mHud->AddControl(gos);

    if (h.IsInteractive()) {
        h.RunComponentInteractive(NULL, NULL, /*maxFrames=*/-1,
                                  Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT);
        return h.Shutdown();
    }

    // Settle 60 frames: drives BeginDraw -> sets final m_LayerFlags, creates
    // FruitFactPageControl (Classic page), creates Retry + Quit buttons.
    // Keep m_GameDt=1.0f each frame so alpha ramp is already done.
    for (int i = 0; i < 60; ++i) {
        game_work.m_GameDt = 1.0f;
        h.RunComponentHeadless(1, Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT);
    }

    std::printf("[gameover_screen] stable state reached (m_State=%d, score=%d)\n",
                gos->m_State, game_work.currentScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng("gameover_screen")) return 3;
    }

    std::printf("PASS: gameover_screen screenshot complete\n");
    return h.Shutdown();
}
