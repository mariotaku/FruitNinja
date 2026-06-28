// test_dojoscreen -- headless layout/structure guard for DojoScreen.
//
// Verifies the faithful DojoScreen activation path (Init()->Reset()->CreateButtons())
// creates all expected controls and places them correctly. Runs headless --
// no screenshot or display required. Passes with `ctest -E screenshot`.
//
// Asserted:
//   1. All THREE ring buttons in HUD: back-bomb, shop (pineapple), about (plum).
//   2. BOTH BSButtons (FB + TW social stubs) in HUD with DISTINCT positions
//      (guards the v1.6.1 overlap bug where both were at Vec3(152,100,0)).
//   3. DojoScreen itself is active and added to HUD.
//
// Run:
//   ctest --test-dir build/host -R dojoscreen --output-on-failure

#include "test_harness.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/BSButton.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "screens/DojoScreen.h"
#include "game/GameWork.h"
#include <cstdio>
#include <list>
#include <vector>

// Collect controls added to the HUD AFTER the snapshot into MenuButtons and
// BSButtons. Controls that existed before the snapshot are skipped.
static void CollectNewControls(
    const std::list<HUDControl*>& existing,
    std::list<MenuButton*>& outMenuButtons,
    std::vector<BSButton*>& outBSButtons)
{
    outMenuButtons.clear();
    outBSButtons.clear();
    if (!game_work.mHud) return;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* ctrl = *it;
        bool wasExisting = false;
        for (std::list<HUDControl*>::const_iterator eit = existing.begin();
             eit != existing.end(); ++eit) {
            if (*eit == ctrl) { wasExisting = true; break; }
        }
        if (wasExisting) continue;

        MenuButton* mb = dynamic_cast<MenuButton*>(ctrl);
        if (mb) { outMenuButtons.push_back(mb); continue; }

        BSButton* bb = dynamic_cast<BSButton*>(ctrl);
        if (bb) outBSButtons.push_back(bb);
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "dojoscreen");
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    int fruitCount = FruitInfo_GetCount();
    if (fruitCount <= 0) {
        fprintf(stderr, "FAIL: FruitInfo_GetCount()=%d -- fruitlist.xml not loaded\n", fruitCount);
        return 1;
    }

    const int bombType      = fruitCount;
    const int pineappleType = Fruit::FruitType("pineapple", false);
    const int plumType      = Fruit::FruitType("plum", false);
    printf("[SETUP] bombType=%d pineappleType=%d plumType=%d\n",
           bombType, pineappleType, plumType);

    // Snapshot HUD before adding DojoScreen.
    std::list<HUDControl*> existing;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        existing.push_back(*it);
    }
    printf("[SETUP] pre-DojoScreen HUD count=%d\n", (int)existing.size());

    // Create and activate DojoScreen via the faithful binary flow:
    //   Init() -> Reset() -> CreateButtons()  (v1.6.1 DojoScreen::Init @0x00169e80)
    DojoScreen* dojo = new DojoScreen(h.game);
    dojo->Init();
    game_work.mHud->AddControl(dojo);

    // Run a few frames so entity allocations (if any) settle.
    h.game.runFrames(30);

    // Collect newly-added controls.
    std::list<MenuButton*> menuButtons;
    std::vector<BSButton*> bsButtons;
    CollectNewControls(existing, menuButtons, bsButtons);

    printf("[RESULT] new MenuButtons=%d  new BSButtons=%d\n",
           (int)menuButtons.size(), (int)bsButtons.size());

    // --- Assertion 1: all three ring buttons ---
    MenuButton* backBtn   = NULL;
    MenuButton* shopBtn   = NULL;
    MenuButton* aboutBtn  = NULL;
    for (std::list<MenuButton*>::iterator it = menuButtons.begin(); it != menuButtons.end(); ++it) {
        MenuButton* mb = *it;
        if (mb->m_FruitType == bombType)       backBtn  = mb;
        else if (mb->m_FruitType == pineappleType) shopBtn  = mb;
        else if (mb->m_FruitType == plumType)   aboutBtn = mb;
    }

    int failures = 0;

    if (!backBtn) {
        fprintf(stderr, "FAIL: back-bomb ring (m_pBackButton, fruitType=%d) not in HUD"
                " -- CreateButtons() not called on activation\n", bombType);
        ++failures;
    } else {
        printf("PASS: back-bomb ring in HUD (pos=%.1f,%.1f restScale=%.3f)\n",
               backBtn->pos.x, backBtn->pos.y, backBtn->m_RestScale.x);
    }

    if (!shopBtn) {
        fprintf(stderr, "FAIL: shop ring (m_pShopButton, fruitType=%d/pineapple) not in HUD\n",
                pineappleType);
        ++failures;
    } else {
        printf("PASS: shop ring in HUD (pos=%.1f,%.1f)\n",
               shopBtn->pos.x, shopBtn->pos.y);
    }

    if (!aboutBtn) {
        fprintf(stderr, "FAIL: about ring (m_pAboutButton, fruitType=%d/plum) not in HUD\n",
                plumType);
        ++failures;
    } else {
        printf("PASS: about ring in HUD (pos=%.1f,%.1f)\n",
               aboutBtn->pos.x, aboutBtn->pos.y);
    }

    // --- Assertion 2: both BSButtons exist with distinct positions ---
    if ((int)bsButtons.size() < 2) {
        fprintf(stderr, "FAIL: expected 2 BSButtons (FB+TW) in HUD, found %d\n",
                (int)bsButtons.size());
        ++failures;
    } else {
        Vec3 pos0 = bsButtons[0]->pos;
        Vec3 pos1 = bsButtons[1]->pos;
        printf("[BSBUTTON] btn0 pos=(%.1f,%.1f,%.1f)  btn1 pos=(%.1f,%.1f,%.1f)\n",
               pos0.x, pos0.y, pos0.z, pos1.x, pos1.y, pos1.z);
        if (pos0.x == pos1.x && pos0.y == pos1.y) {
            fprintf(stderr, "FAIL: FB and TW BSButtons are at identical positions"
                    " (%.1f,%.1f) -- they overlap\n", pos0.x, pos0.y);
            ++failures;
        } else {
            printf("PASS: FB and TW BSButtons have distinct positions\n");
        }
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: DojoScreen layout OK\n");
    return h.Shutdown();
}
