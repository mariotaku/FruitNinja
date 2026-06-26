// test_screenshot -- per-screen golden-screenshot driver.
//
// Usage: test_screenshot <screen> [--interactive|--screenshot|--headless]
//
// Drives the named screen to a fully-drawn, stable state and captures the
// framebuffer to tmp/test/screenshots/<screen>_screen.png.
//
// Supported screens:
//   bonus    -- BonusScreen with 3 mock awards, timer past all reveals.
//
// Adding more screens later: add a ScreenCase entry to the dispatch table
// and implement its fixture function below.

#include "test_harness.h"
#include "screens/BonusScreen.h"
#include "engine/math/Vec3.h"
#include "engine/asset/TextureManager.h"

// ---------------------------------------------------------------------------
// Forward declarations for per-screen fixture functions.
// ---------------------------------------------------------------------------
static int RunBonus(fn::TestHarness& h);

// ---------------------------------------------------------------------------
// Dispatch table.
// ---------------------------------------------------------------------------
struct ScreenCase {
    const char* name;
    int (*run)(fn::TestHarness& h);
};

static const ScreenCase kScreens[] = {
    { "bonus", RunBonus },
    // TODO: fixture for gameover
    // TODO: fixture for shop
    { NULL, NULL }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int FailUsage() {
    fprintf(stderr,
        "usage: test_screenshot <screen> [--interactive|--screenshot|--headless]\n"
        "  screens: bonus\n");
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

    // Load the canonical award-star icon so the star-draw path is exercised.
    // star_awards.tex is the game's general award-star badge texture.
    Mortar::SmartPtr<Mortar::Texture> starTex =
        Mortar::TextureManager::LoadLocalisedTexture("star_awards.tex");
    if (!starTex.IsValid()) {
        std::fprintf(stderr, "WARN: star_awards.tex failed to load -- star quads will be skipped\n");
    }

    bs->AddAward(Colour(0xAD, 0x7E, 0x00, 0xFF), starTex, "ALL_APPLES",   150);
    bs->AddAward(Colour(0x00, 0xAD, 0x7E, 0xFF), starTex, "STRAIGHT_3",   300);
    bs->AddAward(Colour(0x7E, 0xAD, 0x00, 0xFF), starTex, "FRUIT_FRENZY", 500);
    bs->m_Awards[0].m_Multiplier = 2;
    bs->m_Awards[1].m_Multiplier = 3;
    bs->m_Awards[2].m_Multiplier = 4;

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
