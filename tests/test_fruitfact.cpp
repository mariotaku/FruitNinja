// test_fruitfact -- per-mode FruitFact board screenshot driver.
//
// Usage: test_fruitfact <mode> [--interactive|--screenshot|--headless]
//
// Drives the named game-over mode to STATE_MAIN_DISPLAY so the
// corresponding FruitFactPage is created and rendered, then captures a PNG.
//
// Supported modes:
//   classic  -> FruitFactClassicFactPage  -> tmp/test/screenshots/fruitfact/classic.png
//   arcade   -> FruitFactBonusFactPage    -> tmp/test/screenshots/fruitfact/arcade.png
//   zen      -> FruitFactZenPage          -> tmp/test/screenshots/fruitfact/zen.png

#include "test_harness.h"
#include "screens/GameOverScreen.h"
#include "screens/FruitFactClassicFactPage.h"
#include "hud/FruitFactPageControl.h"
#include "hud/ScoreControl.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/BonusManager.h"
#include "game/Bonus.h"
#include "game/FruitSaveData.h"
#include "engine/util/StringTable.h"
#include "engine/asset/TextureManager.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Forward declarations for per-mode fixture functions.
// ---------------------------------------------------------------------------
static int RunFruitFactClassic(fn::TestHarness& h);
static int RunFruitFactArcade(fn::TestHarness& h);
static int RunFruitFactZen(fn::TestHarness& h);

// ---------------------------------------------------------------------------
// Dispatch table.
// ---------------------------------------------------------------------------
struct ModeCase {
    const char* name;
    const char* label;
    int (*run)(fn::TestHarness& h);
};

static const ModeCase kModes[] = {
    { "classic", "fruitfact/classic", RunFruitFactClassic },
    { "arcade",  "fruitfact/arcade",  RunFruitFactArcade  },
    { "zen",     "fruitfact/zen",     RunFruitFactZen     },
    { NULL, NULL, NULL }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int FailUsage() {
    std::fprintf(stderr,
        "usage: test_fruitfact <mode> [--interactive|--screenshot|--headless]\n"
        "  modes: classic arcade zen\n");
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* mode_name = argv[1];

    if (mode_name[0] == '-') return FailUsage();

    const ModeCase* mc = NULL;
    for (int i = 0; kModes[i].name != NULL; ++i) {
        if (std::strcmp(kModes[i].name, mode_name) == 0) {
            mc = &kModes[i];
            break;
        }
    }
    if (!mc) {
        std::fprintf(stderr, "test_fruitfact: unknown mode '%s'\n", mode_name);
        return FailUsage();
    }

    fn::TestHarness h(argc, argv, mc->label);
    // 120 burn-in frames: lets GameInit run through the Splash->Game state
    // transition so fonts/textures are loaded before we strip the HUD.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    return mc->run(h);
}

// ---------------------------------------------------------------------------
// Classic mode: FruitFactClassicFactPage
//
// GameOverScreen (Classic, STATE_MAIN_DISPLAY) creates FruitFactPageControl +
// FruitFactClassicFactPage via Update state-6 on the first post-Initialise tick.
// A second FruitFactClassicFactPage is injected directly to m_Pages so the
// left/right arrow MenuButtons appear (pages.size() > 1 gate in Update).
// ---------------------------------------------------------------------------
static int RunFruitFactClassic(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Highscore 280 < 321 -> NEW BEST fires.
    FruitSaveData saveData;
    saveData.m_ModeHighScores[Mortar::GAME_MODE_CLASSIC] = 280;
    saveData.newBestThisGame = 1;
    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &saveData;

    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_CLASSIC;
    game_work.currentScore = 321;
    game_work.m_GameDt     = 1.0f;
    game_work.bM_bPaused   = 0;

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

    // One settle frame to wire up the HUD after the Initialise fast-path.
    {
        game_work.m_GameDt     = 1.0f;
        game_work.currentScore = 321;
        h.RunComponentHeadlessMultiPass(1);
    }

    FruitFactPageControl* ctrl = gos->m_pFruitFact;
    if (!ctrl) {
        game_work.m_SaveData = prevSaveData;
        std::fprintf(stderr, "FAIL: m_pFruitFact not created\n");
        return 2;
    }

    // Inject a second page so Update creates the nav arrow MenuButtons.
    FruitFactClassicFactPage* page2 = new FruitFactClassicFactPage(ctrl, 0, 0);
    ctrl->m_Pages.push_back(page2);
    page2->HidePage();
    page2->size = ctrl->size;

    // Settle 60 frames with the per-layer multi-pass so MenuButton scratch
    // backdrop (0x40 pass) and face+label (0x80 pass) both render correctly.
    for (int i = 0; i < 60; ++i) {
        game_work.m_GameDt     = 1.0f;
        game_work.currentScore = 321;
        h.RunComponentHeadlessMultiPass(1);
    }

    game_work.m_SaveData = prevSaveData;

    std::printf("[fruitfact/classic] stable state (pages=%d, arrows=%s, gos_state=%d, sc_displayed=%d)\n",
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                (ctrl && ctrl->m_NextButton != NULL) ? "yes" : "no",
                gos->m_State,
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: fruitfact/classic screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// Arcade mode: FruitFactBonusFactPage
//
// FruitFactBonusFactPage::Init iterates BonusManager::m_BestBonuses via
// GetFirstBestBonus / GetNextBestBonus. Two hand-crafted Bonus objects are
// pushed into m_BestBonuses before GameOverScreen is created. starCount=3
// shows 3 stars on the arcade results header.
// ---------------------------------------------------------------------------
static int RunFruitFactArcade(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Seed BonusManager::m_BestBonuses with 2 representative awards.
    BonusManager* bm = BonusManager::GetInstance();
    bm->ClearBestBonuses();

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
    game_work.m_GameDt     = 1.0f;
    game_work.bM_bPaused   = 0;

    GameOverScreen* gos = new GameOverScreen(
        "Arcade",
        GameOverScreen::STATE_MAIN_DISPLAY,
        0.0f,
        1,   // expressionIdx
        1,   // bgPatternIdx
        0,   // tabIndex
        3);  // starCount (3 stars)

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

    // 60 frames with per-layer passes so MenuButton scratch/face/label render.
    for (int i = 0; i < 60; ++i) {
        game_work.m_GameDt     = 1.0f;
        game_work.currentScore = 789;
        h.RunComponentHeadlessMultiPass(1);
    }

    game_work.m_SaveData = prevSaveData;

    FruitFactPageControl* ctrl = gos->m_pFruitFact;
    std::printf("[fruitfact/arcade] stable state (pages=%d, gos_state=%d, bonuses=%d, sc_displayed=%d)\n",
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                gos->m_State,
                (int)bm->m_BestBonuses.size(),
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: fruitfact/arcade screenshot complete\n");
    return h.Shutdown();
}

// ---------------------------------------------------------------------------
// Zen mode: FruitFactZenPage
//
// FruitFactZenPage::Init reads game_work.m_SaveData->m_BestComboLength and
// m_BestComboFruits[]. A combo of 4 (> 2 threshold) forces the hasCombo
// branch that displays fruit icons and a combo star.
// ---------------------------------------------------------------------------
static int RunFruitFactZen(fn::TestHarness& h) {
    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Seeded with 4-fruit combo + highscore 400 < 456 -> NEW BEST fires.
    FruitSaveData zenSave;
    zenSave.m_BestComboLength = 4;
    zenSave.m_BestComboFruits[0] = 0;
    zenSave.m_BestComboFruits[1] = 1;
    zenSave.m_BestComboFruits[2] = 2;
    zenSave.m_BestComboFruits[3] = 0;
    zenSave.m_ModeHighScores[Mortar::GAME_MODE_ZEN] = 400;
    zenSave.newBestThisGame = 1;

    FruitSaveData* prevSaveData = game_work.m_SaveData;
    game_work.m_SaveData = &zenSave;

    game_work.gameMode     = (uint8_t)Mortar::GAME_MODE_ZEN;
    game_work.currentScore = 456;
    game_work.m_GameDt     = 1.0f;
    game_work.bM_bPaused   = 0;

    GameOverScreen* gos = new GameOverScreen(
        "Zen",
        GameOverScreen::STATE_MAIN_DISPLAY,
        0.0f,
        1,   // expressionIdx
        1,   // bgPatternIdx
        0,   // tabIndex
        0);  // starCount

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

    // 60 frames with per-layer passes.
    for (int i = 0; i < 60; ++i) {
        game_work.m_GameDt     = 1.0f;
        game_work.currentScore = 456;
        h.RunComponentHeadlessMultiPass(1);
    }

    game_work.m_SaveData = prevSaveData;

    FruitFactPageControl* ctrl = gos->m_pFruitFact;
    std::printf("[fruitfact/zen] stable state (pages=%d, combo=%d, gos_state=%d, sc_displayed=%d)\n",
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                zenSave.m_BestComboLength,
                gos->m_State,
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 3;
    }

    std::printf("PASS: fruitfact/zen screenshot complete\n");
    return h.Shutdown();
}
