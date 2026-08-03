// test_shoplistitem -- headless box-creation guard for ShopListItem v1.6.1 TTF path.
//
// Verifies: after ShopListItem::NewDraw():
//   (a) m_pBox0 (title BakedStringBox) is non-null    -- TTF title path taken
//   (b) m_pBox1 (category BakedStringBox) is non-null -- TTF category path taken
//   (c) m_TintA == 0 -- caches m_Type=0 (BLADE) after first category box build
//
// These assertions confirm the structural port from the monolith v1.5.x bitmap
// Font::DrawString path to the v1.6.1 BakedStringBox+TTF dispatcher is correct.
//
// Runs headless (no --screenshot flag); passes in ctest -E screenshot.
// Uses fn_add_game_test (SDL+GL context) so game_work.m_pTTFFontMain is live.

#include "test_harness.h"
#include "hud/ShopListItem.h"
#include "game/ItemInfo.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cassert>

static const char* PASS = "PASS";
static const char* FAIL = "FAIL";

static bool g_ok = true;

static void check(bool cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "  FAIL: %s\n", msg);
        g_ok = false;
    } else {
        fprintf(stdout, "  ok:   %s\n", msg);
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "shoplistitem/boxes");
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // Run a few frames to warm up the GL+game state (shaders bound, etc.).
    h.RunHeadless(5);

    fprintf(stdout, "[test_shoplistitem] TTF box-creation guard\n");

    // -------------------------------------------------------------------------
    // Check that game_work.m_pTTFFontMain is live (prerequisite for box build).
    // -------------------------------------------------------------------------
    check(game_work.m_pTTFFontMain != nullptr,
          "game_work.m_pTTFFontMain non-null after game init");
    if (!game_work.m_pTTFFontMain) {
        // Cannot continue without the TTF font.
        fprintf(stderr, "Skipping box-build assertions (no TTF font available)\n");
        return h.Shutdown();
    }

    // -------------------------------------------------------------------------
    // Build a minimal stub ItemInfo (type=0, BLADE, unlocked, no requirement).
    // -------------------------------------------------------------------------
    // Scope item+info so the ShopListItem (and its BakedStringBox GL resources)
    // are destroyed while the GL context is still alive. Destroying them after
    // h.Shutdown() (which tears down GL) hangs the process at exit.
    {
    ItemInfo info;
    info.m_Type            = (int8_t)ITEM_TYPE_BLADE;  // 0
    info.m_Cost            = 0;       // unlocked: IsLocked() == false
    info.m_pTitle          = (char*)"Test Blade";
    info.m_pDescText       = (char*)"A test blade item.";
    info.m_pLockedText     = nullptr;
    info.m_pProgressFmt    = nullptr;
    info.m_pTotalStatKey   = nullptr;
    info.m_CountDownFrom   = 0;
    info.m_pTextureName    = nullptr; // no icon texture in headless context
    info.m_bSeen           = 1;       // seen -> no "new" badge
    info.m_RequirementType = (int8_t)0; // no requirement

    // -------------------------------------------------------------------------
    // Construct ShopListItem and Create() it (pShopScreen=nullptr is safe;
    // DrawDescription() early-returns when m_pShopScreen==nullptr).
    // -------------------------------------------------------------------------
    ShopListItem item;
    item.Create(&info, nullptr /* pShopScreen */);
    // NewDraw's head gate is m_bOnscreen (+0x2D), matching the binary @0x001b5910.
    // ScrollingMenu::Update normally sets it; this fixture drives NewDraw directly.
    item.SetOnscreen(true);

    // Preconditions.
    check(item.m_pBox0 == nullptr, "m_pBox0 null before NewDraw (lazy-build guard)");
    check(item.m_pBox1 == nullptr, "m_pBox1 null before NewDraw (lazy-build guard)");
    check(item.m_TintA == 0xFF,    "m_TintA == 0xFF (sentinel) before NewDraw");

    // -------------------------------------------------------------------------
    // Call NewDraw() directly (bypasses Draw()'s dispatch; the item was marked
    // onscreen above so NewDraw's own head gate passes).
    // Boxes are built before any GL Draw() call, so assertions hold even if
    // GL state is not perfect in headless context.
    // -------------------------------------------------------------------------
    item.NewDraw();

    // -------------------------------------------------------------------------
    // Structural assertions: verify boxes were allocated via the TTF path.
    // -------------------------------------------------------------------------
    check(item.m_pBox0 != nullptr,
          "m_pBox0 (title BakedStringBox) built by NewDraw -- TTF path taken");
    check(item.m_pBox1 != nullptr,
          "m_pBox1 (category BakedStringBox) built by NewDraw -- TTF path taken");
    check(item.m_TintA == (uint8_t)ITEM_TYPE_BLADE,
          "m_TintA caches m_Type=0 (BLADE) after category box build");

    // Calling NewDraw() a second time should NOT rebuild box0 (lazy-build).
    // Rebuild of box1 only happens when m_TintA != m_Type (which it now doesn't).
    Mortar::BakedStringBox* box0_before = item.m_pBox0;
    Mortar::BakedStringBox* box1_before = item.m_pBox1;
    item.NewDraw();
    check(item.m_pBox0 == box0_before, "m_pBox0 not rebuilt on second NewDraw (lazy-build)");
    check(item.m_pBox1 == box1_before, "m_pBox1 not rebuilt on second NewDraw (type unchanged)");

    // ItemInfo::~ItemInfo() free()s its string fields, but this test assigned string
    // LITERALS (and left m_pName unset) -- free()ing non-heap pointers corrupts the
    // heap and hangs at exit. The boxes already copied the text via SetText, so null
    // the pointers before info's dtor runs (free(nullptr) is a no-op). In the real
    // game these strings are strdup'd from XML, so the dtor's free() is correct there.
    info.m_pName = nullptr; info.m_pTitle = nullptr; info.m_pDescText = nullptr;
    info.m_pLockedText = nullptr; info.m_pProgressFmt = nullptr;
    info.m_pTotalStatKey = nullptr; info.m_pTextureName = nullptr;
    }  // item + info destroyed here, before h.Shutdown()

    // -------------------------------------------------------------------------
    // Summary.
    // -------------------------------------------------------------------------
    fprintf(stdout, "[test_shoplistitem] %s\n", g_ok ? PASS : FAIL);
    if (!g_ok) return 1;

    return h.Shutdown();
}
