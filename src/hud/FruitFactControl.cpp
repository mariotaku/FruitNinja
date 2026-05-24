// FruitFactControl implementation
// Binary: ctor 0x0013cb60, Init 0x0013a278, Release 0x00139d24, etc.

#include "hud/FruitFactControl.h"
#include "game/GameMode.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "game/BonusManager.h"
#include "game/Bonus.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/LeaderboardList.h"
#include "game/LeaderboardManager.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "engine/asset/TextureManager.h"
#include "engine/network/NetworkManager.h"
#include "game/LeaderboardManager.h"
#include "engine/audio/GameSound.h"
#include "engine/math/Colour.h"
#include "engine/util/Delegate.h"
#include "engine/render/Font.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Renderer.h"
#include "engine/math/Matrix44.h"
#include "engine/math/MathUtil.h"
#include "engine/util/StringTable.h"
#include <cstring>
#include <cstdio>
#include <list>
#include "game/GameWork.h"

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// CheckCombo (binary @ 0x00110cb0) -- pattern matcher for the BestCombo slice
// array. Input is a 1..11-slot array of fruit type indices (m_ComboSliceArr
// values stored as int). Returns 0..24 = combo-name index (see GetComboName
// at GameOverScreen.cpp:944), or 0xFF for "no combo".
//
// Algorithm (ASM-verified 2026-05-22, re-analyst):
//   - Build unique-fruit work table tracking (type, count) per slot.
//   - Track `alternating` = true while consecutive same-fruit hits never
//     match the *newest* entry (ABAB... checkerboard pattern).
//   - Compute `unique` count + dominant fruit.
//   - Branch on (unique, count):
//       unique==1   -> ALL_<FRUIT> lookup by type-index
//       unique==2 && alternating -> CHECKERS (combo 24)
//       unique==2 && count==5 && (n0==2 || n1==2) -> FULLHOUSE (20)
//       unique==3 && count==5 && (n0==2 || n1==2) -> 2_PAIR (21)
//       unique==count && unique>4 -> ALL_DIFFERENT (4)
//       X-OF-A-KIND scan: any n==3 -> 22, any n==4 -> 23 (4OAK overrides 3OAK)
//       fallback by count: 3->0, 4->1, 5->2, 6->3, 7+->5; <3 -> 0xFF
// ---------------------------------------------------------------------------

// Fruit name -> ALL_<FRUIT> combo byte. Resolved to type indices lazily on
// first CheckCombo call (Fruit::FruitType doesn't depend on game state).
// Binary's pair table at DAT_00110f88 is hash-keyed; port uses type indices
// since m_ComboSliceArr stores indices, not hashes.
struct AllFruitEntry { const char* name; uint8_t combo; int typeIdx; };
static AllFruitEntry s_AllFruitTable[13] = {
    { "apple_red",     6, -1 }, { "orange",       7, -1 },
    { "pineapple",     8, -1 }, { "watermelon",   9, -1 },
    { "kiwifruit",    10, -1 }, { "mango",       11, -1 },
    { "strawberry",   12, -1 }, { "pear",        13, -1 },
    { "banana",       14, -1 }, { "lime",        15, -1 },
    { "lemon",        16, -1 }, { "coconut",     17, -1 },
    { "passionfruit", 18, -1 },
};
static bool s_AllFruitResolved = false;

static void ResolveAllFruitIndices() {
    if (s_AllFruitResolved) return;
    s_AllFruitResolved = true;
    for (int i = 0; i < 13; ++i) {
        s_AllFruitTable[i].typeIdx = Fruit::FruitType(s_AllFruitTable[i].name, false);
    }
}

static uint8_t CheckCombo(int* hashes, int count, int* outFruitIdx) {
    if (!hashes || count <= 0) return 0xFF;
    ResolveAllFruitIndices();

    struct Entry { int type; int n; };
    Entry work[16];
    int unique = 0;
    int domType = -1;
    int domCount = 0;
    bool alternating = true;

    for (int i = 0; i < count; ++i) {
        bool found = false;
        for (int j = 0; j < unique; ++j) {
            if (work[j].type == hashes[i]) {
                found = true;
                ++work[j].n;
                if (work[j].n > domCount) { domCount = work[j].n; domType = work[j].type; }
                // alternating gate: hit on NEWEST entry breaks the alternating
                // pattern (means same-fruit-back-to-back somewhere).
                if (j == unique - 1) alternating = false;
                break;
            }
        }
        if (!found) {
            if (unique < 16) { work[unique].type = hashes[i]; work[unique].n = 1; ++unique; }
            if (domCount == 0) { domType = hashes[i]; domCount = 1; }
        }
    }
    if (outFruitIdx) *outFruitIdx = domType;

    if (unique == 1) {
        // ALL_<FRUIT> -- match by type-index.
        for (int i = 0; i < 13; ++i) {
            if (s_AllFruitTable[i].typeIdx >= 0 && hashes[0] == s_AllFruitTable[i].typeIdx) {
                return s_AllFruitTable[i].combo;
            }
        }
        // Fruit not in the all-fruit table (e.g. moose, special) -> fall
        // through to count-based fallback below.
    } else if (unique == 2) {
        if (alternating) return 24; // 5_OF_A_KIND / CHECKERS pattern
        if (count == 5 && (work[0].n == 2 || work[1].n == 2)) return 20; // FULLHOUSE
    } else if (unique == 3 && count == 5 &&
               (work[0].n == 2 || work[1].n == 2)) {
        return 21; // 2_PAIR
    } else if (unique == count && unique > 4) {
        return 4; // ALL_DIFFERENT (5+ slices, all distinct)
    }

    // X-OF-A-KIND scan: priority 4OAK > 3OAK.
    if (unique > 1) {
        int8_t r = -1;
        for (int i = 0; i < unique; ++i) {
            if (work[i].n == 3 && r == -1) r = 22;
            else if (work[i].n == 4 && r < 23) r = 23;
        }
        if (r != -1) return (uint8_t)r;
    }

    // Count-based fallback (binary table at DAT_00110f60).
    if (count < 3) return 0xFF;
    if (count == 3) return 0;
    if (count == 4) return 1;
    if (count == 5) return 2;
    if (count == 6) return 3;
    return 5; // 7_FRUIT_PLUS
}

// ---------------------------------------------------------------------------
// GetComboStarTexture (binary @ 0x001112e0)
// Returns the star-burst texture for the given combo type. Some combos have
// multiple tier-textures; binary picks one uniformly at random per call
// using Math::g_Random. Port mirrors via Math::g_Random.Rand32(count).
// ---------------------------------------------------------------------------
struct ComboStarEntry { uint8_t count; const char* tex[3]; };
static const ComboStarEntry kComboStars[25] = {
    { 2, { "star_fruity.tex",         "star_juicy.tex",          nullptr } },             // 0  3_FRUIT
    { 2, { "star_yummy.tex",          "star_tasty.tex",          nullptr } },             // 1  4_FRUIT
    { 2, { "star_lush.tex",           "star_delicious.tex",      nullptr } },             // 2  5_FRUIT
    { 2, { "star_succulent.tex",      "star_succulent.tex",      nullptr } },             // 3  6_FRUIT (binary stores dup)
    { 3, { "star_fruit_salad.tex",    "star_fruits_basket.tex",  "star_megamix.tex" } },  // 4  ALL_DIFFERENT
    { 2, { "star_amazing.tex",        "star_exquisite.tex",      nullptr } },             // 5  7_FRUIT_PLUS
    { 1, { "star_its_apples.tex",     nullptr,                   nullptr } },             // 6  ALL_APPLES
    { 1, { "star_vitamin_c.tex",      nullptr,                   nullptr } },             // 7  ALL_ORANGES
    { 1, { "star_got_the_sweats.tex", nullptr,                   nullptr } },             // 8  ALL_PINEAPPLES
    { 1, { "star_melon_mania.tex",    nullptr,                   nullptr } },             // 9  ALL_WATERMELONS
    { 1, { "star_flightless_bird.tex",nullptr,                   nullptr } },             // 10 ALL_KIWIS
    { 1, { "star_mango_smoothie.tex", nullptr,                   nullptr } },             // 11 ALL_MANGOES
    { 1, { "star_full_punnet.tex",    nullptr,                   nullptr } },             // 12 ALL_STRAWBERRIES
    { 1, { "star_pear_tree.tex",      nullptr,                   nullptr } },             // 13 ALL_PEARS
    { 1, { "star_banana_cake.tex",    nullptr,                   nullptr } },             // 14 ALL_BANANAS
    { 1, { "star_scurvy_cure.tex",    nullptr,                   nullptr } },             // 15 ALL_LIMES
    { 1, { "star_lemon_line_up.tex",  nullptr,                   nullptr } },             // 16 ALL_LEMONS
    { 1, { "star_lovely_bunch.tex",   nullptr,                   nullptr } },             // 17 ALL_COCONUTS
    { 1, { "star_passion_punch.tex",  nullptr,                   nullptr } },             // 18 ALL_PASSIONFRUITS
    { 1, { "star_alphabetic.tex",     nullptr,                   nullptr } },             // 19 ALPHABETICAL
    { 1, { "star_full_house.tex",     nullptr,                   nullptr } },             // 20 FULLHOUSE
    { 1, { "star_two_pairs.tex",      nullptr,                   nullptr } },             // 21 2_PAIR
    { 1, { "star_three_of_a_kind.tex",nullptr,                   nullptr } },             // 22 3_OF_A_KIND
    { 1, { "star_four_of_a_kind.tex", nullptr,                   nullptr } },             // 23 4_OF_A_KIND
    { 1, { "star_checkers.tex",       nullptr,                   nullptr } },             // 24 5_OF_A_KIND (CHECKERS)
};

static Mortar::SmartPtr<Mortar::Texture> GetComboStarTexture(uint8_t comboType) {
    if (comboType >= 25) return Mortar::SmartPtr<Mortar::Texture>();
    const ComboStarEntry& e = kComboStars[comboType];
    uint32_t tier = (e.count > 1) ? Math::g_Random.Rand32(e.count) : 0;
    return Mortar::TextureManager::LoadLocalisedTexture(e.tex[tier]);
}

// ---------------------------------------------------------------------------
// Static (class-level) content -- 17 shared textures.
// Binary @ 0x001399fc (LoadContent), 0x00139f84 (UnLoadContent).
// ---------------------------------------------------------------------------

static bool s_bLoaded = false;

// Slot order matches binary @ 0x001399fc
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexClassic;      // slot1  "fact_board.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexZen;           // slot2  "diolog_box_big.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ComboDescTex;          // slot3  "combo_description.tex"
static Mortar::SmartPtr<Mortar::Texture> s_SenseiHeadTex;         // slot4  "sensei_head.tex"  -- used as LoadContent proxy in BeginDraw
static Mortar::SmartPtr<Mortar::Texture> s_BlurryBackingTex;      // slot5  "blurry_backing.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeArrowTex;        // slot6  "arcade_results_arrow.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeScoreBoxTex;     // slot7  "arcade_results_score_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexArcade;        // slot8  "arcade_results_diolog_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeBonusBoxTex;     // slot9  "arcade_results_bonus_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_OFTitleTex;            // slot10 "op_title.tex"           -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_OFConnectTex;          // slot11 "op_connect_button.tex"  -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_OFAddFriendsTex;       // slot12 "op_add_friends_button.tex" -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_GCTitleTex;            // slot13 "gc_title.tex"           -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_GCConnectTex;          // slot14 "gc_connect_button.tex"  -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_GCNoScoreTex;          // slot15 "gc_no_score_this_week.tex" -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_ScoreYouTex;           // slot16 "score_you.tex"
static Mortar::SmartPtr<Mortar::Texture> s_NoScoreThisWeekTex;    // slot17 "no_score_this_week.tex"

void FruitFactControl::LoadContent() {
    if (s_bLoaded) return;
    s_bLoaded = true;

    s_PanelTexClassic   = TextureManager::LoadLocalisedTexture("fact_board.tex");
    s_PanelTexZen       = TextureManager::LoadLocalisedTexture("diolog_box_big.tex");
    s_ComboDescTex      = TextureManager::LoadLocalisedTexture("combo_description.tex");
    s_SenseiHeadTex     = TextureManager::LoadLocalisedTexture("sensei_head.tex");
    s_BlurryBackingTex  = TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_ArcadeArrowTex    = TextureManager::LoadLocalisedTexture("arcade_results_arrow.tex");
    s_ArcadeScoreBoxTex = TextureManager::LoadLocalisedTexture("arcade_results_score_box.tex");
    s_PanelTexArcade    = TextureManager::LoadLocalisedTexture("arcade_results_diolog_box.tex");
    s_ArcadeBonusBoxTex = TextureManager::LoadLocalisedTexture("arcade_results_bonus_box.tex");
    s_OFTitleTex        = TextureManager::LoadLocalisedTexture("op_title.tex");
    s_OFConnectTex      = TextureManager::LoadLocalisedTexture("op_connect_button.tex");
    s_OFAddFriendsTex   = TextureManager::LoadLocalisedTexture("op_add_friends_button.tex");
    s_GCTitleTex        = TextureManager::LoadLocalisedTexture("gc_title.tex");
    s_GCConnectTex      = TextureManager::LoadLocalisedTexture("gc_connect_button.tex");
    s_GCNoScoreTex      = TextureManager::LoadLocalisedTexture("gc_no_score_this_week.tex");
    s_ScoreYouTex       = TextureManager::LoadLocalisedTexture("score_you.tex");
    s_NoScoreThisWeekTex= TextureManager::LoadLocalisedTexture("no_score_this_week.tex");
}

void FruitFactControl::UnLoadContent() {
    if (!s_bLoaded) return;
    s_bLoaded = false;

    s_PanelTexClassic.SetNull();
    s_PanelTexZen.SetNull();
    s_ComboDescTex.SetNull();
    s_SenseiHeadTex.SetNull();
    s_BlurryBackingTex.SetNull();
    s_ArcadeArrowTex.SetNull();
    s_ArcadeScoreBoxTex.SetNull();
    s_PanelTexArcade.SetNull();
    s_ArcadeBonusBoxTex.SetNull();
    s_OFTitleTex.SetNull();
    s_OFConnectTex.SetNull();
    s_OFAddFriendsTex.SetNull();
    s_GCTitleTex.SetNull();
    s_GCConnectTex.SetNull();
    s_GCNoScoreTex.SetNull();
    s_ScoreYouTex.SetNull();
    s_NoScoreThisWeekTex.SetNull();
}

// ---------------------------------------------------------------------------
// FruitFactControl ctor  (Binary @ 0x0013cb60)
// ---------------------------------------------------------------------------

FruitFactControl::FruitFactControl()
    : HUDControl3d()
    , m_AnimTimer(0.0f)
    , m_pCurFactString(nullptr)
    , m_FruitIdx(-1)
    , m_FactIdx(-1)
    , m_FactTexture()
    , m_FactPosOffset(-69.0f, 53.0f, 0.0f)
    , m_FactColour(0x74, 0x5d, 0x3b, 0xff)
    , m_ComboLength(0)
    , m_StarTimer(0.0f)
    , m_bConnectPressed(0)
    , m_ComboStarTex()
    , m_ComboType(0xFF)
    , m_TabIndex(0)
    , m_pLeaderboardMenu(nullptr)
    , m_pConnectButton(nullptr)
    , m_LBVisitedCount(0)
    , m_LBProgressTimer(0.0f)
    , m_LBState(1)
    , m_pLeftButton(nullptr)
    , m_pRightButton(nullptr)
    , m_StarType(0)
{
    memset(m_ComboHashArray, 0, sizeof(m_ComboHashArray));
    m_ComboActiveFlag = 0;
    memset(_pad_factColour, 0, sizeof(_pad_factColour));
    memset(_pad_D9, 0, sizeof(_pad_D9));
    memset(_pad_E1, 0, sizeof(_pad_E1));
    memset(_pad_E5, 0, sizeof(_pad_E5));
    memset(_pad_LocalScore, 0, sizeof(_pad_LocalScore));
    memset(_pad_FriendScore1, 0, sizeof(_pad_FriendScore1));
    memset(_pad_FriendScore2, 0, sizeof(_pad_FriendScore2));
    memset(_pad_201, 0, sizeof(_pad_201));

    // Binary @ 0x0013cb60: snapshots game_work.gameMode (gw+4) into m_StarType at construction.
    // ASM-verified: 2026-05-24 binary @ 0x0013cb60 (re-analyst)
    // Defunct stub: gameMode is 0 at pre-game construction time, so m_StarType stays 0 -- identical effect.

    // Binary: IsProviderOnline() -> m_LBState (1 = always offline branch)
    // Defunct: always offline; m_LBState = 1 set in initializer list above

    m_bNoDestructor = 1;
    m_Timer = 0.0f;

    LoadContent();

    // Binary calls Reset() at end of ctor (via vtable)
    Reset();
}

// ---------------------------------------------------------------------------
// FruitFactControl dtor  (Binary @ 0x00139e6c)
// ---------------------------------------------------------------------------

FruitFactControl::~FruitFactControl() {
    Release();
    // FNHighscore dtors fire automatically (trivial)
    // SmartPtr dtors fire automatically
}

// ---------------------------------------------------------------------------
// Init  (Binary @ 0x0013a278)
// ---------------------------------------------------------------------------

void FruitFactControl::Init() {
    m_StarTimer = -0.5f;
    m_ComboStarTex.SetNull();

    uint8_t savedTabIndex = m_TabIndex;
    m_TabIndex = 0;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game_work.gameMode : 0;

    // Per-mode backplate stored in m_Texture (binary @ 0x0013a2a6..0x0013a2ba)
    if (gameMode == 2) {
        if (s_PanelTexArcade.IsValid()) m_Texture = s_PanelTexArcade;
    } else if (gameMode == 3) {
        if (s_PanelTexZen.IsValid()) m_Texture = s_PanelTexZen;
    } else {
        if (s_PanelTexClassic.IsValid()) m_Texture = s_PanelTexClassic;
    }
    if (m_Texture.IsValid()) {
        size.x = (float)(m_Texture->m_Width + 1);
        size.y = (float)(m_Texture->m_Height + 1);
        size.z = 0.0f;
        // ASM-verified: 2026-05-11 binary @ 0x0013a31e (re-analyst)
        // 1.37f scale (DAT_0013a500) applies only in Classic (gameMode == 0).
        if (gameMode == 0) {
            size.x *= 1.37f;
            size.y *= 1.37f;
            size.z *= 1.37f;
        }
    }

    // ASM-verified: 2026-05-18 binary @ 0x0013a34e..0x0013a3ac (re-analyst)
    // TWO distinct gates in the binary:
    //   1. comboFlag (m_ComboActiveFlag / field_0xa0): Zen ONLY (mode==3 && cnt>=3).
    //      Stored in m_ComboActiveFlag; consumed by Update/Draw layout paths.
    //   2. comboPath (text+hash+offset): Arcade OR Zen (mode in {2,3} && cnt>=3).
    //      Selects BEST COMBO snippet over plain fact text.
    int comboFlag = 0;
    int comboPath = 0;
    if (game && game_work.m_SaveData) {
        // ASM-verified: 2026-05-18 binary @ 0x0013a398 (re-analyst)
        int count = game_work.m_SaveData->m_BestComboLength;
        if (gameMode == 3 && count >= 3) {
            comboFlag = 1;
        }
        if (comboFlag || (gameMode == 2 && count >= 3)) {
            comboPath = 1;
        }
    }
    m_ComboActiveFlag = (uint8_t)comboFlag; // field_0xa0 -- Zen-only; consumed by Update/Draw

    // NB: field_0xe0 = m_ComboType (uint8). 0xFF = "no combo" default per binary @ 0x0013a3ae
    m_ComboType = 0xFF;

    // Per-mode m_FactPosOffset: non-combo default (-69, 53, 0)
    m_FactPosOffset = Vec3(-69.0f, 53.0f, 0.0f);

    // TODO: 0x0013a3ae -- non-combo path: strcpy LSTR_FRUIT_FACT_INTRO (id 0xb1) into BakedString buf @ DAT_0013a528; BakedString unported
    if (comboPath) {
        // Combo path (binary @ 0x0013a3ae, comboPath branch).
        // Binary: snprintf(buf, "%s", Mortar::GETSTRING_CAST_0(LSTR_BEST_COMBO))
        // where LSTR_BEST_COMBO = 0x98 = "BEST COMBO: %i FRUIT!". The single
        // BakedString slot gets the formatted-with-count string. Port renders
        // it via the Font::DrawString path -- the BakedString optimisation
        // is unported (it caches the rendered glyph quads).
        char comboBuf[128];
        snprintf(comboBuf, sizeof(comboBuf),
                 Mortar::GETSTRING_CAST_0(LSTR_BEST_COMBO),
                 game_work.m_SaveData->m_BestComboLength);
        m_ComboLength = game_work.m_SaveData->m_BestComboLength;
        for (int i = 0; i < m_ComboLength && i < 11; i++) {
            m_ComboHashArray[i] = game_work.m_SaveData->m_BestComboFruits[i];
        }
        int localFruitIdx = 0;
        m_ComboType = CheckCombo(m_ComboHashArray, m_ComboLength, &localFruitIdx);
        m_ComboStarTex = GetComboStarTexture(m_ComboType);
        if (m_FruitIdx != localFruitIdx) m_FruitIdx = localFruitIdx;
        m_FactPosOffset = Vec3(140.0f, -72.0f, 0.0f);
    }

    // Always: GetFact with current fruit/fact indices
    m_pCurFactString = Fruit::GetFact(&m_FruitIdx, &m_FactIdx, m_FruitIdx, m_FactIdx);

    // Always: colour from fruit (binary always assigns; Fruit::FruitFactColour clamps internally)
    // ASM-verified: 2026-05-24 binary @ 0x0013a278 (re-analyst) -- unconditional, no m_FruitIdx guard
    m_FactColour = Fruit::FruitFactColour(m_FruitIdx);

    // ASM-verified: 2026-05-14 binary @ 0x0013a278..0x0013a4f6 (asm-inspector)
    //   GOT base = 0x001ec130 (ldr r5,[pc,#0x000b1eb0]; adds r5,r5,r3)
    //   GOT off  = 0xfffcdf83 (ldr r2,[pc,...])  -> r2 = 0x001ba0b3
    //   bytes @ 0x001ba0b3 = 25 73 2E 74 65 78 00 = "%s.tex"
    //   blx 0x001032b4 (OS_SPrintf)
    // Port previously had "%s_facts.tex" which produced filenames that
    // don't exist (sml_ap_facts.tex). FruitFactTexture returns the XML
    // factTexture attr directly (e.g. "sml_ap" for apple) which resolves
    // to the small per-fruit icon in FruitNinjaBada/Data/textures/.
    // Binary always calls FruitFactTexture + sprintf unconditionally (@ 0x0013a434..0x0013a47c)
    {
        const char* fruitBase = Fruit::FruitFactTexture(m_FruitIdx);
        if (fruitBase && *fruitBase) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.tex", fruitBase);
            m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
        }
    }

    m_LBVisitedCount = 0;
    m_bConnectPressed = 0;
    m_LBProgressTimer = 0.0f;

    m_TabIndex = savedTabIndex;
    m_LayerFlags = 0x80;

    Reset();
}

// ---------------------------------------------------------------------------
// Release  (Binary @ 0x00139d24)
// ---------------------------------------------------------------------------

void FruitFactControl::Release() {
    m_FactTexture.SetNull();
    m_ComboStarTex.SetNull();

    Game* game = Game::GetInstance();
    HUD* hud = game ? game_work.mHud : nullptr;

    // Order from binary: m_pLeaderboardMenu, m_pLeftButton, m_pRightButton, m_pConnectButton.
    if (m_pLeaderboardMenu) {
        if (hud) hud->RemoveControl(m_pLeaderboardMenu);
        m_pLeaderboardMenu->Release();
        delete m_pLeaderboardMenu;
        m_pLeaderboardMenu = nullptr;
    }
    if (m_pLeftButton) {
        if (hud) hud->RemoveControl(m_pLeftButton);
        m_pLeftButton->Release();
        delete m_pLeftButton;
        m_pLeftButton = nullptr;
    }
    if (m_pRightButton) {
        if (hud) hud->RemoveControl(m_pRightButton);
        m_pRightButton->Release();
        delete m_pRightButton;
        m_pRightButton = nullptr;
    }
    if (m_pConnectButton) {
        if (hud) hud->RemoveControl(m_pConnectButton);
        m_pConnectButton->Release();
        delete m_pConnectButton;
        m_pConnectButton = nullptr;
    }
}

// ---------------------------------------------------------------------------
// BeginDraw  (Binary @ 0x0013a0bc)
// ---------------------------------------------------------------------------

void FruitFactControl::BeginDraw(float /*dt*/) {
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Validity check uses STATIC s_SenseiHeadTex (slot #4 in LoadContent) as
    // a proxy for "did LoadContent run successfully".
    if (!s_SenseiHeadTex.IsValid()) return;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game_work.gameMode : 0;

    if (gameMode == Mortar::GAME_MODE_ZEN || (gameMode == Mortar::GAME_MODE_ARCADE && m_TabIndex <= 1)) {
        m_LayerFlags |= Mortar::HUD_LAYER_BUTTONS;
    }
}

// ---------------------------------------------------------------------------
// Update  (Binary @ 0x0013b604)
// ---------------------------------------------------------------------------

void FruitFactControl::Update(float dt) {
    m_AnimTimer += dt * 8.0f;
    if (m_AnimTimer >= 8.0f) m_AnimTimer = 0.0f;

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    Game* game = Game::GetInstance();
    if (!game) return;

    uint8_t gameMode = game_work.gameMode;

    if (gameMode == Mortar::GAME_MODE_ZEN) {
        // SecondaryTex = combo-star tex
        m_Texture = m_ComboStarTex;

        // ASM-verified: 2026-05-24 binary @ 0x0013b604 (re-analyst)
        // Path A: post-combo single-shot "achievement" SFX, dt*2 step.
        // Path B: during-combo per-beat "popup-%i" SFX, dt*4 step.
        // Gate: (gw.m_GameDt > 0.75f && m_StarTimer < ComboLength) -> Path B; else Path A.
        if (game_work.m_GameDt <= 0.75f || m_StarTimer >= (float)m_ComboLength) {
            // Path A: post-combo single-shot crossing trigger
            float old = m_StarTimer;
            m_StarTimer += 2.0f * dt;
            if (m_StarTimer > 0.0f && m_ComboStarTex.IsValid()) {
                float oldFrac = old - (float)(int)old;
                float newFrac = m_StarTimer - (float)(int)m_StarTimer;
                if (oldFrac < 0.5f && newFrac >= 0.5f) {
                    if (game_work.mGameSound) {
                        // DAT_0013b92c = 0xfffcd195 -> "achievement" @ 0x001b92c5
                        game_work.mGameSound->SFXPlay("achievement", 1.0f, 1.0f,
                            Mortar::Delegate1<bool, Mortar::MortarSound*>());
                    }
                }
            }
        } else {
            // Path B: during-combo per-beat
            float old = m_StarTimer;
            m_StarTimer += 4.0f * dt;
            if (m_StarTimer > 0.0f) {
                float oldFrac = old - (float)(int)old;
                float newFrac = m_StarTimer - (float)(int)m_StarTimer;
                if (oldFrac < 0.5f && newFrac >= 0.5f) {
                    int idx_n = (int)m_StarTimer;
                    int idx = (idx_n <= 6) ? (idx_n + 1) : 8;
                    char sfx[16];
                    snprintf(sfx, sizeof(sfx), "popup-%i", idx);  // DAT_0013b928 -> "popup-%i" @ 0x001bb438
                    if (game_work.mGameSound) {
                        game_work.mGameSound->SFXPlay(sfx, 1.0f, 1.0f,
                            Mortar::Delegate1<bool, Mortar::MortarSound*>());
                    }
                }
            }
        }
    } else if (gameMode == Mortar::GAME_MODE_ARCADE) {
        // Arcade mode: lazy-create leaderboard menu ONCE (binary @ 0x0013b604)
        if (!m_pLeaderboardMenu) {
            m_pLeaderboardMenu = new LeaderboardList();
            m_pLeaderboardMenu->Init();
            m_pLeaderboardMenu->pos = Vec3(75.0f, pos.y - 8.0f, 0.0f);
            // Defunct: online-services -- SetItemHeight/SetWidth/SetHeight preserved for call shape.
            m_pLeaderboardMenu->SetItemHeight(47.0f);
            m_pLeaderboardMenu->SetWidth(240.0f);
            m_pLeaderboardMenu->SetHeight(141.0f);
            if (game_work.mHud) game_work.mHud->AddControl(m_pLeaderboardMenu, false);
        }
        if (m_pLeaderboardMenu) m_pLeaderboardMenu->m_Active = 0;
        if (m_pConnectButton) m_pConnectButton->m_Active = 0;

        if (m_TabIndex == 1) {
            UpdateLeaderboard(dt);
            return;
        }
        if (m_TabIndex == 0) {
            std::list<Bonus>::iterator it;
            BonusManager* bm = BonusManager::GetInstance();
            Bonus* bonus = bm->GetFirstBestBonus(it);
            if (!bonus) {
                bm->SetUpBonusScreen(nullptr);
            }
        }
    }
    // Classic: no per-frame dispatch
}

// ---------------------------------------------------------------------------
// UpdateLeaderboard  (Binary @ 0x0013afbc)
// ---------------------------------------------------------------------------

void FruitFactControl::UpdateLeaderboard(float dt) {
    // Binary @ 0x0013afbc -- 5-state machine for online leaderboard flow.
    // All branches are defunct (online services are not ported).
    Game* game = Game::GetInstance();
    if (!game) return;

    int provider = Mortar::GetSocialNetworkProvider();
    (void)provider;

    switch (m_LBState) {
        case 0:
            // Defunct: online-services -- offline-prompt: spawn connect button bound to ConnectPressed.
            // binary @ 0x0013afbc: creates m_pConnectButton at (pos.x-8, pos.y-8, 0),
            // shows provider title texture, shows connect button.
            // no observable effect; binary @ 0x0013afbc
            break;

        case 1:
            // Defunct: online-services -- initiating connection: NetworkManager::ConnectGameCenter().
            // binary @ 0x0013b0e4: calls ConnectGameCenter(), polls IsGameCenterAttemptingToConnect(),
            // transitions to state 2 or state 4 on timeout.
            // no observable effect; binary @ 0x0013b0e4
            (void)dt;
            break;

        case 2:
            // Defunct: online-services -- fetching: LeaderboardManager::RefreshLeaderboard() poll.
            // binary @ 0x0013b1e4: calls RefreshLeaderboard(gameMode, boardId),
            // draws download spinner via DrawDownloadIcon(), populates leaderboard rows.
            // no observable effect; binary @ 0x0013b1e4
            LeaderboardManager::GetInstance()->RefreshLeaderboard(0, 0);
            DrawDownloadIcon();
            if (m_pLeaderboardMenu) {
                // Defunct: online-services -- no rows to populate; list stays empty.
                // binary @ 0x0013b244: iterates FNHighscoreList rows into LeaderboardList items.
            }
            break;

        case 3:
            // Defunct: online-services -- showing: make leaderboard visible.
            // binary @ 0x0013b39c: sets m_pLeaderboardMenu->m_Active = 1.
            // no observable effect; binary @ 0x0013b39c
            if (m_pLeaderboardMenu) m_pLeaderboardMenu->m_Active = 1;
            break;

        case 4:
            // Defunct: online-services -- error/offline fallback.
            // binary @ 0x0013b418: shows no-score texture, hides leaderboard.
            // no observable effect; binary @ 0x0013b418
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// DrawOrder  (Binary @ 0x0013b95c, vtable slot 9)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawOrder(const Vec3& hudScale, int layerMask) {
    (void)hudScale;

    Game* game = Game::GetInstance();
    if (!game || !game_work.pFontMain.IsValid()) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    if (layerMask == 8) {
        // Zen combo-bead overlay (HUD layer 0x08, binary @ 0x0013b95c)
        if (game_work.gameMode != Mortar::GAME_MODE_ZEN) return;
        if (!m_ComboStarTex.IsValid()) return;
        if (m_StarTimer <= (float)m_ComboLength) return;

        // Normalise elapsed post-combo time to [0..1]
        float t = (m_StarTimer - (float)m_ComboLength) * 2.0f - 1.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        // Sin-pulse scale: SinIdx maps [0..65535] -> full sin period.
        uint16_t idx = (uint16_t)(int)(t * 135.0f * 182.0f);
        float pulse = SinIdx(idx) * 1.41421356f;

        // Per-bead stride along the panel
        float stride = ((float)(m_ComboLength - 1)) * 40.0f;
        if (m_ComboLength > 1 && stride > 220.0f) {
            stride = 220.0f / (float)(m_ComboLength - 1);
        }

        // Draw the combo-star texture quad with sin-pulse scale
        // ASM-verified: 2026-05-18 binary @ 0x0013a06c (re-analyst)
        const float tw = (float)m_ComboStarTex->m_Width;
        const float th = (float)m_ComboStarTex->m_Height;
        m_ComboStarTex->Set();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale((tw + 1.0f) * pulse, (th + 1.0f) * pulse, 0.0f);
        mat.GlobalTranslate44(Vec3(pos.x + stride * 0.5f, pos.y, pos.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_ComboStarTex->UnSet();
        return;
    }

    if (game_work.gameMode == Mortar::GAME_MODE_ZEN) {
        // ---- Zen body ----
        // Backplate from m_Texture (set to combo-star or PanelTexZen in Init/Update)
        if (m_Texture.IsValid()) {
            m_Texture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_Texture->UnSet();
        }

        // Title: pos.x - 8.0
        {
            const char* title = Mortar::GETSTRING_CAST_0(LSTR_FRUIT_FACT_TITLE);
            if (!title) title = "FRUIT FACT";
            const float titleX = pos.x - 8.0f;
            const float titleY = pos.y;
            // Triple-pass: shadow + dark-brown stroke at scale 20 + main colour at scale 20
            // Shadow pass
            game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                Colour(0, 0, 0, 128), 0x0F);
            // Dark-brown stroke
            game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                Colour(0x4B, 0x32, 0x28, 200), 0x0F);
            // Main colour
            game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                m_FactColour, 0x0F);
        }

        // Fact body: wrap 127/89, step 0.25
        if (m_pCurFactString) {
            float scale = 16.0f;
            {
                Mortar::Utf8StringIterator iter(m_pCurFactString);
                while (game_work.pFontMain->GetStringHeight(iter, scale, 127.0f) > 89.0f) {
                    if (scale <= 0.5f) break;
                    scale -= 0.25f;
                    iter = Mortar::Utf8StringIterator(m_pCurFactString);
                }
            }
            game_work.pFontMain->DrawStringWrapped(scale, 127.0f, 0.0f,
                m_pCurFactString, Vec3(pos.x - 8.0f, pos.y, 0.0f),
                m_FactColour, 0x0D);
        }

        // Fact icon: does NOT use m_FactPosOffset
        if (m_FactTexture.IsValid()) {
            const float w = (float)m_FactTexture->m_Width;
            const float h = (float)m_FactTexture->m_Height;
            m_FactTexture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(w, h, 0.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 8.0f, pos.y + 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_FactTexture->UnSet();
        }

    } else if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        // ---- Arcade body ----
        if (m_TabIndex == 1) {
            DrawLeaderboard();
            return;
        }

        if (m_TabIndex == 0) {
            // Backplate from m_FactTexture (per-mode secondary backplate)
            if (m_FactTexture.IsValid()) {
                m_FactTexture->Set();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
                mat.GlobalTranslate44(Vec3(pos.x - 8.0f, pos.y + 8.0f, pos.z));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                r->DrawQuad(Colour(255, 255, 255, 255));
                m_FactTexture->UnSet();
            }

            // Three bonus rows via BonusManager (binary @ 0x0013b95c, m_TabIndex==0 path)
            {
                const Colour rowColours[3] = {
                    Colour(0xAD, 0x7E, 0x00, 0xFF),  // gold  (1st)
                    Colour(0xA0, 0x05, 0x05, 0xFF),  // red   (2nd)
                    Colour(0x01, 0x5C, 0x95, 0xFF),  // blue  (3rd)
                };
                Vec3 rowPos(pos.x - 98.0f, pos.y + 58.0f, 0.0f);
                BonusManager* bm = BonusManager::GetInstance();
                std::list<Bonus>::iterator it;
                Bonus* bonus = bm->GetFirstBestBonus(it);
                for (int i = 0; i < 3 && bonus; i++) {
                    Colour col = rowColours[i];
                    // m_DisplayName at +0x80 (char[64])
                    if (bonus->m_DisplayName[0] != '\0') {
                        game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                            bonus->m_DisplayName,
                            Vec3(rowPos.x + 16.0f, rowPos.y, 0.0f),
                            col, 0x0F);
                    }
                    // m_Tier at +0x3C holds display score value
                    char scoreBuf[32];
                    snprintf(scoreBuf, sizeof(scoreBuf), "%d", bonus->m_Tier);
                    game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                        scoreBuf,
                        Vec3(rowPos.x + 193.0f, rowPos.y, 0.0f),
                        col, 0x0F);
                    if (bonus->m_StarTexture.IsValid()) {
                        // ASM-verified: 2026-05-18 binary @ 0x0013a06c (re-analyst)
                        bonus->m_StarTexture->Set();
                        mm.GetWorldStack().Reset();
                        Matrix44 starMat = Matrix44::MakeScale(
                            (float)bonus->m_StarTexture->m_Width * 0.5f,
                            (float)bonus->m_StarTexture->m_Height * 0.5f,
                            0.0f);
                        starMat.GlobalTranslate44(rowPos);
                        mm.GetWorldStack().SetCurrentMatrix(starMat);
                        mm.UploadModelViewOnly();
                        r->DrawQuad(Colour(255, 255, 255, 255));
                        bonus->m_StarTexture->UnSet();
                    }
                    rowPos.y -= 20.0f;
                    bonus = bm->GetNextBestBonus(it);
                }
            }

            // Title body (Zen-equivalent structure, 127/89/0.25)
            if (m_pCurFactString) {
                float scale = 16.0f;
                {
                    Mortar::Utf8StringIterator iter(m_pCurFactString);
                    while (game_work.pFontMain->GetStringHeight(iter, scale, 127.0f) > 89.0f) {
                        if (scale <= 0.5f) break;
                        scale -= 0.25f;
                        iter = Mortar::Utf8StringIterator(m_pCurFactString);
                    }
                }
                game_work.pFontMain->DrawStringWrapped(scale, 127.0f, 0.0f,
                    m_pCurFactString, Vec3(pos.x - 8.0f, pos.y, 0.0f),
                    m_FactColour, 0x0D);
            }
        }

    } else {
        // ---- Classic body ----

        // 1. Backplate quad
        // Offset from GOT[DAT_0013cb1c] + (-1, -8, 0); const unknown, treat as (0,0,0)
        if (m_Texture.IsValid()) {
            m_Texture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_Texture->UnSet();
        }

        // 2. Title: dispatch on game_work.languageFlag (field_0x3)
        {
            const char* title = Mortar::GETSTRING_CAST_0(LSTR_FRUIT_FACT_TITLE);
            if (!title) title = "FRUIT FACT";

            if (!game_work.languageFlag) {
                // clear branch: pos.x - 66, pos.y + 42, maxWH=(42, 0), scale 16, align 0xF
                game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                    title, Vec3(pos.x - 66.0f, pos.y + 42.0f, 0.0f),
                    m_FactColour, 0x0F);
            } else {
                // set branch: pos.x - 64, maxWH=(128, 0), scale 16, align 0xF
                game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                    title, Vec3(pos.x - 64.0f, pos.y + 42.0f, 0.0f),
                    m_FactColour, 0x0F);
            }
        }

        // 3. Fact body: wrap 128/96, step 0.125
        if (m_pCurFactString) {
            const Colour brown(0x74, 0x5D, 0x3B, 0xFF);
            const float bodyX = pos.x - 64.0f;
            const float bodyY = pos.y - 14.0f;
            float scale = 16.0f;
            {
                Mortar::Utf8StringIterator iter(m_pCurFactString);
                while (game_work.pFontMain->GetStringHeight(iter, scale, 128.0f) > 96.0f) {
                    if (scale <= 0.5f) break;
                    scale -= 0.125f;
                    iter = Mortar::Utf8StringIterator(m_pCurFactString);
                }
            }
            game_work.pFontMain->DrawStringWrapped(scale, 128.0f, 0.0f,
                m_pCurFactString, Vec3(bodyX, bodyY, 0.0f),
                brown, 0x0D);
        }

        // 4. Per-fruit fact icon with m_FactPosOffset
        if (m_FactTexture.IsValid()) {
            const float w = (float)m_FactTexture->m_Width;
            const float h = (float)m_FactTexture->m_Height;
            m_FactTexture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(w, h, 0.0f);
            mat.GlobalTranslate44(Vec3(
                pos.x + m_FactPosOffset.x - 8.0f,
                pos.y + m_FactPosOffset.y + 8.0f,
                pos.z + m_FactPosOffset.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_FactTexture->UnSet();
        }
    }
}

// ---------------------------------------------------------------------------
// DrawLeaderboard  (Binary @ 0x0013aac0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawLeaderboard() {
    // Defunct: online-services -- no-op stub; binary @ 0x0013aac0
}

// ---------------------------------------------------------------------------
// DrawDownloadIcon  (Binary @ 0x001395d0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawDownloadIcon() {
    // Defunct: online-services -- no-op stub; binary @ 0x001395d0
    // Binary @ 0x001395d0 builds an 8-segment ring with per-segment alpha
    // animation (48 verts, Mesh::DrawTriList). The only caller is
    // UpdateLeaderboard state 2 which is itself defunct.
    // Mesh::DrawTriList is not yet wired in port; ring draw omitted.
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

// Binary @ 0x001394ec
bool FruitFactControl::LeftPressed(InputEvent* /*ev*/) {
    --m_FactIdx;
    if (m_FactIdx < 0) m_FactIdx = Fruit::FruitInfo(m_FruitIdx)->m_FactCount - 1;
    m_pCurFactString = Fruit::GetFact(nullptr, nullptr, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x001394b0
bool FruitFactControl::RightPressed(InputEvent* /*ev*/) {
    ++m_FactIdx;
    if (m_FactIdx >= Fruit::FruitInfo(m_FruitIdx)->m_FactCount) m_FactIdx = 0;
    m_pCurFactString = Fruit::GetFact(nullptr, nullptr, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x0013993c
bool FruitFactControl::UpPressed(InputEvent* /*ev*/) {
    do {
        ++m_FruitIdx;
        if (m_FruitIdx >= FruitInfo_GetCount()) m_FruitIdx = 0;
    } while (Fruit::FruitInfo(m_FruitIdx)->m_FactCount < 1);
    m_pCurFactString = Fruit::GetFact(nullptr, &m_FactIdx, m_FruitIdx, -1);
    m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    char buf[128];
    const char* base = Fruit::FruitFactTexture(m_FruitIdx);
    snprintf(buf, sizeof(buf), "%s.tex", base);  // binary format DAT_001399f8 = "%s.tex"
    m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
    return true;
}

// Binary @ 0x0013987c
bool FruitFactControl::DownPressed(InputEvent* /*ev*/) {
    do {
        --m_FruitIdx;
        if (m_FruitIdx < 0) m_FruitIdx = FruitInfo_GetCount() - 1;
    } while (Fruit::FruitInfo(m_FruitIdx)->m_FactCount < 1);
    m_pCurFactString = Fruit::GetFact(nullptr, &m_FactIdx, m_FruitIdx, -1);
    m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    char buf[128];
    const char* base = Fruit::FruitFactTexture(m_FruitIdx);
    snprintf(buf, sizeof(buf), "%s.tex", base);  // binary format DAT_001399f8 = "%s.tex"
    m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
    return true;
}

// ---------------------------------------------------------------------------
// Button callbacks
// ---------------------------------------------------------------------------

// Binary @ 0x0013a130
void FruitFactControl::LeftButton() {
    Game* g = Game::GetInstance();
    if (g && game_work.mGameSound) {
        // ASM-verified: 2026-05-23 binary @ 0x0013a160 (re-analyst)
        game_work.mGameSound->SFXPlay("Next-screen-button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    --m_TabIndex;
    if ((int)m_TabIndex < 0) m_TabIndex = 1;
    if (g && game_work.m_SaveData) {
        game_work.m_SaveData->SetTotal("factMode", (int)m_TabIndex + 1, true, true);
    }
}

// Binary @ 0x0013a1d4
void FruitFactControl::RightButton() {
    Game* g = Game::GetInstance();
    if (g && game_work.mGameSound) {
        // ASM-verified: 2026-05-23 binary @ 0x0013a204 (re-analyst)
        game_work.mGameSound->SFXPlay("Next-screen-button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    ++m_TabIndex;
    if ((int)m_TabIndex > 1) m_TabIndex = 0;
    if (g && game_work.m_SaveData) {
        game_work.m_SaveData->SetTotal("factMode", (int)m_TabIndex + 1, true, true);
    }
}

// Binary @ 0x00139440
void FruitFactControl::ConnectPressed() {
    // Defunct: online-services -- no-op stub; binary @ 0x00139440
    // NetworkManager::ConnectGameCenter() / NetworkManager::LaunchDashboard() are no-op stubs.
    (void)Mortar::NetworkManager::GetInstance();
}
