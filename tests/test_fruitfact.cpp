// test_fruitfact -- per-mode FruitFact board screenshot driver.
//
// Usage: test_fruitfact <mode> [--interactive|--screenshot|--headless]
//                              [--lang=<name|flag>] [--fact=<fruitIdx>]
//
// Drives the named game-over mode to STATE_MAIN_DISPLAY so the
// corresponding FruitFactPage is created and rendered, then captures a PNG.
//
// Supported modes:
//   classic  -> FruitFactClassicFactPage  -> tmp/test/screenshots/fruitfact/classic.png
//   arcade   -> FruitFactBonusFactPage    -> tmp/test/screenshots/fruitfact/arcade.png
//   zen      -> FruitFactZenPage          -> tmp/test/screenshots/fruitfact/zen.png
//
// --lang=<name>  Language name (english_us, japanese, chinese, korean, etc.)
//                or numeric flag (0=english_us, 11=japanese, 13=chinese).
//                Suffix appended to output filename: e.g. classic_zh.png
// --fact=<N>     Force a specific fruit fact (0=apple 1=banana 2=orange
//                3=watermelon 4=strawberry 5=kiwifruit 7=plum).
//                Suffix appended: e.g. classic_zh_strawberry.png

#include "test_harness.h"
#include "screens/GameOverScreen.h"
#include "screens/BaseScreen.h"
#include "screens/FruitFactPage.h"
#include "screens/FruitFactClassicFactPage.h"
#include "hud/FruitFactControl.h"
#include "hud/GenericHUDControl.h"
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
#include <iterator>
#include <set>
#include <list>
#include "hud/MenuButton.h"
#include "entities/Fruit.h"

// When true (--isolated flag), each mode generates an additional PNG that
// suppresses the game-over chrome (GameOverScreen board, RETRY/QUIT buttons,
// ScoreControl) and renders only the FruitFact board + its page children.
static bool g_Isolated = false;

// Fruit index to force for the fact board (--fact=N). -1 = use the game's
// default selection (Zen combo winner, or apple for Classic/Arcade).
static int g_FactOverride = -1;

// Language flag resolved from --lang= for label composition. -1 = not
// specified (no lang suffix in output filename). TestHarness also parses
// this arg independently; parsing twice is harmless.
static int g_LangFlag = -1;

// ---------------------------------------------------------------------------
// BuildShotLabel -- compose screenshot label from mode + optional lang + fact.
//
// Produces "fruitfact/<mode>" when no lang/fact override is active, so
// default runs keep the original output names intact.
// ---------------------------------------------------------------------------
static void BuildShotLabel(char* out, size_t outSize,
                           const char* mode, int langFlag, int factIdx) {
    // Short language tag table, indexed by languageFlag (matches kLanguageSuffix
    // order in StringTable.cpp).
    static const char* const kLangShort[] = {
        "en", "de", "nl", "fr", "es", "it", "sv", "da", "nb", "fi",
        "ko", "ja", "en_uk", "zh", "en"
    };
    // Sparse fruit name table (index matches fruitlist.xml order).
    static const char* const kFruitShort[] = {
        "apple", "banana", "orange", "watermelon", "strawberry", "kiwifruit",
        NULL /*6=none*/, "plum"
    };
    snprintf(out, outSize, "fruitfact/%s", mode);
    if (langFlag >= 0 && langFlag <= 14) {
        size_t n = strlen(out);
        snprintf(out + n, outSize - n, "_%s", kLangShort[langFlag]);
    }
    if (factIdx >= 0) {
        size_t n = strlen(out);
        const char* fname = (factIdx < 8) ? kFruitShort[factIdx] : NULL;
        if (fname)
            snprintf(out + n, outSize - n, "_%s", fname);
        else
            snprintf(out + n, outSize - n, "_fruit%d", factIdx);
    }
}

// ---------------------------------------------------------------------------
// ResetPageFactBoxes -- re-arm baked text boxes on a page after a fact
// override so the next DrawOrder rebuilds them from the patched controller.
//
// FruitFactClassicFactPage: the lazy m_pTitleBox and m_pBodyBox are deleted
//   and nulled so DrawOrder's NULL-gate fires again on the next frame.
// FruitFactZenPage / FruitFactBonusFactPage: the fact-title (index 2) and
//   fact-text (index 3) GenericHUDControl objects in m_HUDControls own
//   BakedStringBox labels that baked m_FactColour / m_FactText at Init() time.
//   SetColour/SetText marks the box dirty so Draw() rebuilds it.
//   Index 2 = CreateSenseisFruitFactTitle (colour needs update after override).
//   Index 3 = CreateSenseisFruitFactText  (text needs update after override).
//   This index is stable across both page types because both call the two
//   helpers at positions 3 and 4 in their Init() AddGenericControl sequence
//   (ZenPage: head, divider, title, text, ...; BonusPage: banner, divider, title, text, ...).
// ---------------------------------------------------------------------------
static void ResetPageFactBoxes(FruitFactPage* page, FruitFactControl* ctrl) {
    FruitFactClassicFactPage* classic = dynamic_cast<FruitFactClassicFactPage*>(page);
    if (classic) {
        classic->ResetBakedTextBoxes();
        return;
    }
    // Zen and Bonus pages: fact-title at index 2, fact-text at index 3.
    const std::list<HUDControl*>& controls = page->GetHUDControlsForTest();
    if (controls.size() < 4) return;
    std::list<HUDControl*>::const_iterator it = controls.begin();
    std::advance(it, 2);
    GenericHUDControl* titleCtrl = dynamic_cast<GenericHUDControl*>(*it);
    ++it;
    GenericHUDControl* textCtrl = dynamic_cast<GenericHUDControl*>(*it);
    if (titleCtrl && titleCtrl->m_pLabel)
        titleCtrl->m_pLabel->SetColour(ctrl->m_FactColour, 0);
    if (textCtrl && textCtrl->m_pLabel)
        textCtrl->m_pLabel->SetText(ctrl->m_FactText);
}

// ---------------------------------------------------------------------------
// ApplyFactOverride -- patch m_ComboA/B, m_FactText, m_FactColour,
// m_FactTexture on an already-Inited FruitFactControl to force a specific
// fruit fact. Also resets any already-baked text boxes on the pages currently
// in ctrl->m_Pages so the next settle frames rebuild from the overridden fields.
//
// Call AFTER FruitFactControl::Init() has run (i.e. after the first settle
// frame). The caller must run additional settle frames after this call so
// the overridden fields propagate into the page's Draw.
// ---------------------------------------------------------------------------
static void ApplyFactOverride(FruitFactControl* ctrl, int factIdx) {
    int comboB = -1;
    ctrl->m_ComboA   = (unsigned int)factIdx;
    ctrl->m_FactText = Fruit::GetFact(NULL, &comboB, factIdx, -1);
    ctrl->m_ComboB   = (unsigned int)comboB;
    ctrl->m_FactColour = Fruit::FruitFactColour(factIdx);
    const char* texName = Fruit::FruitFactTexture(factIdx);
    if (texName) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s.tex", texName);
        ctrl->m_FactTexture =
            Mortar::TextureManager::LoadLocalisedTexture(buf);
    }
    std::printf("[fact-override] fruit=%d text=%s comboB=%d"
                " colour=(r=%u,g=%u,b=%u)\n",
                factIdx,
                ctrl->m_FactText ? ctrl->m_FactText : "(null)",
                (int)ctrl->m_ComboB,
                (unsigned)ctrl->m_FactColour.r,
                (unsigned)ctrl->m_FactColour.g,
                (unsigned)ctrl->m_FactColour.b);
    // Reset baked text boxes on all pages already registered at this point.
    // Classic page2 (injected after ApplyFactOverride) is built fresh and
    // needs no reset. Zen/Bonus have only one page, added during settle.
    for (std::vector<FruitFactPage*>::iterator pit = ctrl->m_Pages.begin();
         pit != ctrl->m_Pages.end(); ++pit) {
        ResetPageFactBoxes(*pit, ctrl);
    }
}

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
        "                      [--lang=<name|flag>] [--fact=<fruitIdx>]\n"
        "  modes: classic arcade zen\n"
        "  lang examples: english_us japanese chinese korean (or numeric 0..13)\n"
        "  fact: 0=apple 1=banana 2=orange 3=watermelon 4=strawberry"
        " 5=kiwifruit 7=plum\n");
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
    for (int _ai = 2; _ai < argc; ++_ai) {
        if (std::strcmp(argv[_ai], "--isolated") == 0) {
            g_Isolated = true;
        } else if (std::strncmp(argv[_ai], "--fact=", 7) == 0) {
            g_FactOverride = std::atoi(argv[_ai] + 7);
        } else if (std::strncmp(argv[_ai], "--lang=", 7) == 0) {
            // Parse here for label composition; TestHarness::ParseFlags()
            // also parses this arg to apply the actual locale.
            g_LangFlag = ParseLanguageArg(argv[_ai] + 7);
        }
    }
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    return mc->run(h);
}

// ---------------------------------------------------------------------------
// RenderIsolated -- one isolated render + screenshot with chrome suppressed.
//
// Temporarily replaces game_work.mHud->controls with a minimal list containing
// only FruitFactControl and its registered FruitFactPage children, renders one
// multi-pass cycle, takes the screenshot, then splices the original list back.
//
// This is the only approach that works: flag-toggling (m_LayerFlags or m_Active)
// fails because GameOverScreen::BeginDraw and MenuButton::BeginDraw both
// unconditionally reset those fields on every frame before the HUD draw gate.
// MenuButton::AddPeice adds ring/backdrop HUDControl3d objects as independent
// HUD list entries with no pointer tracked here -- list erasure (not per-pointer
// zeroing) is the only way to exclude them cleanly.
// ---------------------------------------------------------------------------
static void RenderIsolated(fn::TestHarness& h, GameOverScreen* gos,
                            ScoreControl* /*sc*/, const char* isoLabel) {
    if (!game_work.mHud || !gos->m_pFruitFact) {
        h.ScreenshotPng(isoLabel);
        return;
    }
    FruitFactControl* ctrl = gos->m_pFruitFact;

    // Build the keep-set: the FruitFactControl, its pages, and each page's
    // registered child controls (sensei head, combo icons, title, fact text).
    // Those children are top-level HUD entries added via AddGenericControl --
    // NOT object children of the page -- so they must be kept explicitly or the
    // board renders empty. Everything else (SCORE, NEW BEST, RETRY/QUIT buttons
    // and their AddPeice ring children) is dropped.
    std::set<HUDControl*> keep;
    keep.insert(ctrl);
    for (std::vector<FruitFactPage*>::iterator it = ctrl->m_Pages.begin();
         it != ctrl->m_Pages.end(); ++it) {
        keep.insert(*it);
        const std::list<HUDControl*>& kids = (*it)->GetHUDControlsForTest();
        for (std::list<HUDControl*>::const_iterator k = kids.begin(); k != kids.end(); ++k)
            keep.insert(*k);
    }

    // Move the full list out, then re-add only the kept controls in original
    // order (preserves draw layering).
    std::list<HUDControl*> savedControls;
    savedControls.splice(savedControls.begin(), game_work.mHud->controls);
    for (std::list<HUDControl*>::iterator it = savedControls.begin();
         it != savedControls.end(); ++it) {
        if (keep.count(*it)) game_work.mHud->controls.push_back(*it);
    }

    h.RunComponentHeadlessMultiPass(2);
    h.ScreenshotPng(isoLabel);

    // Restore the original list (pointers only; no controls deleted).
    game_work.mHud->controls.clear();
    game_work.mHud->controls.splice(game_work.mHud->controls.begin(), savedControls);
}

// ---------------------------------------------------------------------------
// Classic mode: FruitFactClassicFactPage
//
// GameOverScreen (Classic, STATE_MAIN_DISPLAY) creates FruitFactControl +
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

    FruitFactControl* ctrl = gos->m_pFruitFact;
    if (!ctrl) {
        game_work.m_SaveData = prevSaveData;
        std::fprintf(stderr, "FAIL: m_pFruitFact not created\n");
        return 2;
    }

    // Apply fact override (if requested) before the 60-frame settle so the
    // overridden text + colour render in all subsequent frames.
    if (g_FactOverride >= 0) ApplyFactOverride(ctrl, g_FactOverride);

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

    char shotLabel[256];
    char isoLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), "classic",
                   g_LangFlag, g_FactOverride);
    snprintf(isoLabel, sizeof(isoLabel), "%s_isolated", shotLabel);

    std::printf("[%s] stable state (pages=%d, arrows=%s, gos_state=%d,"
                " sc_displayed=%d)\n",
                shotLabel,
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                (ctrl && ctrl->m_NextButton != NULL) ? "yes" : "no",
                gos->m_State,
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng(shotLabel)) return 3;
        if (g_Isolated) RenderIsolated(h, gos, sc, isoLabel);
    }

    std::printf("PASS: %s screenshot complete\n", shotLabel);
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

    FruitFactControl* ctrl = gos->m_pFruitFact;

    // Apply fact override (if requested) and settle a few extra frames so the
    // overridden fact text + colour propagate into the Draw cycle.
    if (g_FactOverride >= 0 && ctrl) {
        ApplyFactOverride(ctrl, g_FactOverride);
        for (int i = 0; i < 5; ++i) {
            game_work.m_GameDt     = 1.0f;
            game_work.currentScore = 789;
            h.RunComponentHeadlessMultiPass(1);
        }
    }

    char shotLabel[256];
    char isoLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), "arcade",
                   g_LangFlag, g_FactOverride);
    snprintf(isoLabel, sizeof(isoLabel), "%s_isolated", shotLabel);

    std::printf("[%s] stable state (pages=%d, gos_state=%d, bonuses=%d,"
                " sc_displayed=%d)\n",
                shotLabel,
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                gos->m_State,
                (int)bm->m_BestBonuses.size(),
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng(shotLabel)) return 3;
        if (g_Isolated) RenderIsolated(h, gos, sc, isoLabel);
    }

    std::printf("PASS: %s screenshot complete\n", shotLabel);
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

    FruitFactControl* ctrl = gos->m_pFruitFact;

    // Apply fact override (if requested) and settle a few extra frames so the
    // overridden fact text + colour propagate into the Draw cycle.
    if (g_FactOverride >= 0 && ctrl) {
        ApplyFactOverride(ctrl, g_FactOverride);
        for (int i = 0; i < 5; ++i) {
            game_work.m_GameDt     = 1.0f;
            game_work.currentScore = 456;
            h.RunComponentHeadlessMultiPass(1);
        }
    }

    char shotLabel[256];
    char isoLabel[256];
    BuildShotLabel(shotLabel, sizeof(shotLabel), "zen",
                   g_LangFlag, g_FactOverride);
    snprintf(isoLabel, sizeof(isoLabel), "%s_isolated", shotLabel);

    std::printf("[%s] stable state (pages=%d, combo=%d, gos_state=%d,"
                " sc_displayed=%d)\n",
                shotLabel,
                ctrl ? (int)ctrl->m_Pages.size() : -1,
                zenSave.m_BestComboLength,
                gos->m_State,
                sc->m_DisplayedScore);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng(shotLabel)) return 3;
        if (g_Isolated) RenderIsolated(h, gos, sc, isoLabel);
    }

    std::printf("PASS: %s screenshot complete\n", shotLabel);
    return h.Shutdown();
}
