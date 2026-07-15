// test_bonus_manager.cpp -- arcade bonus-scoring writer/reader hash-matching
// + formula invariants (high-fan-in scoring leaf).
//
// Three real bugs hid in this path this session:
//   1. BonusManager::AddCombo wrote combo_bonus/best_combo under the wrong
//      hash key and accumulated a flat per-call count instead of the scaled
//      per-length bonusAwards.xml value.
//   2. The "No-Bananas" bonus (frenzy+freeze+scorex2 all == 0) needs a
//      per-name AddToTotal call for EACH of frenzy/freeze/scorex2 so the
//      BonusType total-across-fruits sum reads back correctly.
//   3. FruitSaveData totals written on slice must go through _total (not
//      _drops); _drops is a KillFruit-drop-path-only counter.
//
// This test pins the writer(BonusManager/FruitSaveData) -> reader
// (Bonus/BonusType) hash-matching contract and the AddCombo running-sum
// formula so these regressions can't recur silently.
//
// Scope: BonusManager::AddCombo, Bonus::IsAchieved/BonusType::GetBest,
// FruitSaveData::AddToTotal/GetTotal/ClearTotals. Deliberately does NOT
// drive Fruit::CollisionResponse (needs WaveManager/particle/GameWork -- too
// heavy for this leaf-level test); see CASE 4 comment for the mirrored
// invariant check instead.
//
// Minimal stubbing: no SDL_Init, no GL context, no audio. A bare Game is
// constructed (like test_save_roundtrip.cpp) and its data_dir + a
// FileSystem_Direct are wired manually (mirrors GameInitialise.cpp Step 3)
// so BonusManager::Init() can load the REAL xml/bonusAwards.xml through
// FileManager -- this is the writer/reader hash source of truth, not a
// hand-typed duplicate. BonusType/Bonus objects for CASE 2 are built via
// direct field assignment mirroring Bonus::Parse's output for the exact
// bonusawards.xml snippets referenced in comments (no inline XML parser
// entry point is exposed by the port's TiXmlDocument wrapper).
//
// Run:
//   build/host/tests/test_bonus_manager
//   ctest -R bonus_manager

#include <cstdio>
#include <cstdint>

#include "game/BonusManager.h"
#include "game/Bonus.h"
#include "game/FruitSaveData.h"
#include "game/GameWork.h"
#include "Game.h"
#include "engine/util/StringHash.h"
#include "engine/asset/FileManager.h"
#include "engine/asset/FileSystem_Direct.h"
#include "config.h"

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL (line %d): %s\n", __LINE__, #cond); ++g_fail; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    long _va = (long)(a), _vb = (long)(b); \
    if (_va != _vb) { std::printf("FAIL (line %d): %s == %s : got %ld want %ld\n", \
        __LINE__, #a, #b, _va, _vb); ++g_fail; } \
} while (0)

int main() {
    std::setvbuf(stdout, NULL, _IONBF, 0);

    // --- Minimal environment: Game + FileSystem_Direct so BonusManager::Init
    // can load the real xml/bonusAwards.xml via FileManager (mirrors
    // GameInitialise.cpp Step 3; no SDL/GL/audio needed for that step). ---
    Game* game = new Game();  // intentionally leaked (test_save_roundtrip.cpp pattern)
    game->data_dir = FN_DATA_DIR;
    {
        Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
        fs->Initialise(game->data_dir.c_str(), /*writable=*/false);
        FileManager::GetInstance().AddSystem(fs, /*id=*/0, /*priority=*/0);
    }

    FruitSaveData sd;
    game_work.m_SaveData = &sd;

    BonusManager* bm = BonusManager::GetInstance();
    bm->Init();

    // ------------------------------------------------------------------
    // CASE 1: COMBO -- BonusManager::AddCombo running-sum + best-combo max.
    //
    // v1.6.1 BonusManager::AddCombo @0x0012e570: combo_bonus accumulates the
    // xml/bonusAwards.xml <combo length="N" bonus="X"/> value at
    // clamp(len-3, 0, size-1) EVERY call (running sum across multiple
    // combos in a game); best_combo tracks the max single combo length
    // (NOT a flat per-call increment -- that was bug #1).
    //
    // bonusAwards.xml combo table (length 3..10): 2,3,4,6,8,10,15,15.
    // length > 10 clamps to the last entry (15).
    // ------------------------------------------------------------------
    std::printf("\n[CASE 1] AddCombo running-sum + best-combo max\n");

    CHECK_EQ((long)bm->m_ComboTotalsByLevel.size(), 8);
    if (bm->m_ComboTotalsByLevel.size() == 8) {
        static const int kExpectedByLevel[8] = {2, 3, 4, 6, 8, 10, 15, 15};
        for (int i = 0; i < 8; ++i) {
            CHECK_EQ(bm->m_ComboTotalsByLevel[i], kExpectedByLevel[i]);
        }
    }

    const uint32_t hComboBonus = StringHash("combo_bonus");
    const uint32_t hBestCombo  = StringHash("best_combo");

    // Drive several combos of increasing AND decreasing length; combo_bonus
    // must be the RUNNING SUM of each call's scaled value, best_combo must
    // track the MAX single length seen (never decreases, never re-sums).
    int runningSum = 0;
    int bestSeen = 0;

    struct { int len; int bonus; } kCalls[] = {
        {3, 2}, {5, 4}, {4, 3}, {12, 15},  // 12 clamps to the len=10 slot (15)
        {2, 0},                            // len < 3 -> AddCombo no-ops entirely
    };
    for (size_t i = 0; i < sizeof(kCalls) / sizeof(kCalls[0]); ++i) {
        bm->AddCombo(kCalls[i].len);
        runningSum += kCalls[i].bonus;
        if (kCalls[i].len > bestSeen) bestSeen = kCalls[i].len;

        CHECK_EQ(sd.GetTotal(hComboBonus), runningSum);
        CHECK_EQ(sd.GetTotal(hBestCombo), bestSeen);
    }
    // Final expected: sum(2,4,3,15) = 24; best = 12.
    CHECK_EQ(sd.GetTotal(hComboBonus), 24);
    CHECK_EQ(sd.GetTotal(hBestCombo), 12);

    // Totals must land under the LITERAL hashes (no mode suffix) -- these
    // are the exact keys BonusType::Parse hashes out of bonusAwards.xml's
    // <bonusType total="combo_bonus"/> and <bonusType total="best_combo"/>.
    CHECK_EQ(sd.GetTotal(StringHash("combo_bonus")), 24);
    CHECK_EQ(sd.GetTotal(StringHash("best_combo")), 12);
    CHECK(sd.TotalExists("combo_bonus"));
    CHECK(sd.TotalExists("best_combo"));

    // A wrong-key write (e.g. mode-suffixed "arcade_combo_bonus") would leave
    // the literal key absent -- guard that explicitly.
    CHECK(!sd.TotalExists("arcade_combo_bonus"));
    CHECK(!sd.TotalExists("classic_combo_bonus"));

    // ------------------------------------------------------------------
    // CASE 2: BONUS CONDITION -- Bonus::IsAchieved / BonusType::GetBest tiers.
    //
    // 2a. combo_bonus BonusType (xml/bonusawards.xml lines 13-19): 5 tiers,
    //     min=15/30/40/55/70 -> points=10/15/25/35/50. GetBest picks the
    //     HIGHEST tier whose min is satisfied by the current combo_bonus total.
    // 2b. No-Bananas BonusType (xml/bonusawards.xml lines 50-56):
    //     total="frenzy,freeze,scorex2", first tier equals=0 -> points=50.
    //     Awards 50 ONLY when frenzy+freeze+scorex2 all read back as 0;
    //     as soon as ANY one is > 0 the equals=0 tier gate fails and (per
    //     this test's minimal 1-tier setup) GetBest returns null.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 2] Bonus/BonusType condition gates\n");

    // --- 2a: combo_bonus tiers, mirrors Bonus::Parse output for xml lines 13-19 ---
    {
        BonusType comboBonusType;
        comboBonusType.m_RequiredHashes[StringHash("combo_bonus")] = 0;

        static const int kMins[5]   = {15, 30, 40, 55, 70};
        static const int kPoints[5] = {10, 15, 25, 35, 50};
        for (int i = 0; i < 5; ++i) {
            Bonus b;
            b.m_MinSliced = kMins[i];
            b.m_Tier = kPoints[i];
            comboBonusType.m_Bonuses.push_back(b);
        }

        // combo_bonus total is 24 from CASE 1 -- satisfies min=15 tier (10) only.
        Bonus* best = comboBonusType.GetBest();
        CHECK(best != nullptr);
        if (best) CHECK_EQ(best->m_Tier, 10);

        // Push the total to 42 (>= min=40, < min=55) via AddCombo: needs a combo
        // whose scaled bonus adds exactly 18. Simpler: write directly through the
        // same writer path (AddToTotal) with the literal key, matching AddCombo's
        // own call shape.
        sd.AddToTotal("combo_bonus", hComboBonus, 18, false, false);  // 24+18=42
        CHECK_EQ(sd.GetTotal(hComboBonus), 42);
        best = comboBonusType.GetBest();
        CHECK(best != nullptr);
        if (best) CHECK_EQ(best->m_Tier, 25);  // min=40 tier
    }

    // --- 2b: No-Bananas, mirrors Bonus::Parse output for xml lines 50-56 (equals=0 tier only) ---
    {
        BonusType noBananasType;
        // BonusType::Parse's "total" CSV -> m_RequiredHashes keys (values seeded 0,
        // refreshed by GetBest's GetBonusTotal loop before IsAchieved runs).
        noBananasType.m_RequiredHashes[StringHash("frenzy")]   = 0;
        noBananasType.m_RequiredHashes[StringHash("freeze")]   = 0;
        noBananasType.m_RequiredHashes[StringHash("scorex2")]  = 0;

        Bonus noBananas;
        noBananas.m_MinSliced = 0;
        noBananas.m_MaxSliced = 0;  // equals="0"
        noBananas.m_Tier = 50;
        noBananasType.m_Bonuses.push_back(noBananas);

        // All three totals are absent (0 by GetTotal default) -- No-Bananas fires.
        Bonus* best = noBananasType.GetBest();
        CHECK(best != nullptr);
        if (best) CHECK_EQ(best->m_Tier, 50);

        // Per-name AddToTotal for EACH of frenzy/freeze/scorex2 (bug #2's fix):
        // a single slice of a frenzy power-up must write under "frenzy", not a
        // combined/aggregate key, so the BonusType's per-hash lookup (which reads
        // each of the 3 required names independently via GetBonusTotal) sees it.
        sd.AddToTotal("frenzy", StringHash("frenzy"), 1, false, false);
        CHECK_EQ(sd.GetTotal(StringHash("frenzy")), 1);
        // The other two names remain untouched by this write (writer-key isolation).
        CHECK_EQ(sd.GetTotal(StringHash("freeze")), 0);
        CHECK_EQ(sd.GetTotal(StringHash("scorex2")), 0);

        // As soon as ANY of the three is > 0, the equals=0 tier's min/max=0 gate
        // fails (score=1 > m_MaxSliced=0) -- No-Bananas must NOT fire.
        best = noBananasType.GetBest();
        CHECK(best == nullptr);
    }

    // ------------------------------------------------------------------
    // CASE 3: RESET semantics -- ClearTotals wipes m_Totals (per-game) but
    // NOT m_SessionTotals (lifetime). This is what makes No-Bananas/
    // combo_bonus correctly per-game: each new round starts from zero,
    // while session/lifetime trackers (achievements etc.) survive.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 3] ClearTotals per-game vs session\n");

    sd.AddToTotal("session_marker", StringHash("session_marker"), 7, /*trackSession=*/true, false);
    CHECK_EQ(sd.GetTotal(StringHash("session_marker")), 7);
    CHECK_EQ(sd.GetTotal(StringHash("combo_bonus")), 42);  // per-game total from CASE 2a

    sd.ClearTotals();

    // Per-game totals wiped.
    CHECK_EQ(sd.GetTotal(StringHash("combo_bonus")), 0);
    CHECK_EQ(sd.GetTotal(StringHash("best_combo")), 0);
    CHECK_EQ(sd.GetTotal(StringHash("frenzy")), 0);
    CHECK(!sd.TotalExists("combo_bonus"));

    // Session (lifetime) total survives.
    CHECK_EQ(sd.GetTotal(StringHash("session_marker")), 7);
    CHECK(sd.TotalExists("session_marker"));

    // A fresh AddCombo after ClearTotals starts the running sum over from
    // zero (proves per-game reset actually re-arms the writer path, not
    // just that the map read looks empty).
    bm->AddCombo(3);
    CHECK_EQ(sd.GetTotal(hComboBonus), 2);
    CHECK_EQ(sd.GetTotal(hBestCombo), 3);

    // ------------------------------------------------------------------
    // CASE 4: SLICE-WRITER invariant (_total vs _drops key separation).
    //
    // Bug #3 was: Fruit::CollisionResponse must write the per-fruit-name
    // "<name>" / "_total" counters on EVERY slice, but "_drops" ONLY on the
    // KillFruit drop path (missed fruit), never on slice. Driving the real
    // Fruit::CollisionResponse needs WaveManager/particle/GameWork wiring --
    // too heavy for this leaf-level bonus test (would defeat "minimal
    // stubbing"). Instead this pins the FruitSaveData-level half of the
    // invariant: the two call sites are DISTINCT AddToTotal calls under
    // DISTINCT hash keys, and neither call implicitly touches the other's
    // key. A slice-path helper (writes "<name>" + "_total" only) and a
    // drop-path helper (writes "<name>_drops" only) must never cross-pollute.
    //
    // GAP: this does NOT verify Fruit::CollisionResponse itself calls the
    // slice helper (not the drop helper) on a real slice -- that requires
    // driving the entity/collision pipeline and is out of scope here.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 4] _total vs _drops writer-key separation (FruitSaveData-level)\n");

    sd.ClearTotals();

    // Slice-path shape: AddToTotal("apple", hash, 1, ...) + AddToTotal("_total", hash, 1, ...)
    // mirrors the two-call pattern used by the real slice writer.
    sd.AddToTotal("apple", StringHash("apple"), 1, false, false);
    sd.AddToTotal("_total", StringHash("_total"), 1, false, false);
    CHECK_EQ(sd.GetTotal(StringHash("apple")), 1);
    CHECK_EQ(sd.GetTotal(StringHash("_total")), 1);
    CHECK_EQ(sd.GetTotal(StringHash("apple_drops")), 0);
    CHECK(!sd.TotalExists("apple_drops"));

    // Drop-path shape: AddToTotal("apple_drops", hash, 1, ...) only -- must NOT
    // touch "_total" or the plain "apple" slice counter.
    sd.AddToTotal("apple_drops", StringHash("apple_drops"), 1, false, false);
    CHECK_EQ(sd.GetTotal(StringHash("apple_drops")), 1);
    CHECK_EQ(sd.GetTotal(StringHash("apple")), 1);   // unchanged by the drop write
    CHECK_EQ(sd.GetTotal(StringHash("_total")), 1);  // unchanged by the drop write

    if (g_fail == 0) {
        std::printf("PASS: bonus-scoring writer/reader hash-matching + formula invariants\n");
        return 0;
    }
    std::printf("FAIL: %d assertion(s) failed\n", g_fail);
    return 1;
}
