// test_dojo_navigation -- "child screen blank after transition" bug reproducer.
//
// Usage: test_dojo_navigation --mode=about|shop [--screenshot]
//
// MOTIVATION
// The existing isolated tests (test_aboutscreen, test_shopscreen) construct the
// child screen directly and pass fine. The bug only surfaces through the REAL
// navigation path: DojoScreen's state machine must fire the transition callback,
// wait for the entity-count gate to clear, then create the child screen itself.
//
// ROOT CAUSE UNCOVERED WHILE WRITING THIS TEST
// In the real game, slicing a ring button calls:
//   MenuButton::Update -> ClearMenuItems() -> m_ClickCallback (= AboutCallback/ShopCallback)
// ClearMenuItems() flings every live menu entity (type-0 fruit + type-1 bomb) so they
// fly off-screen and get reaped. Only then does the DojoScreen gate:
//   GetNumEntities(1)==0 && GetNumEntities(0)==0
// become true, allowing the child screen to be created.
//
// If the callback is fired WITHOUT ClearMenuItems() first (as direct AboutCallback()
// invocations would do), the shop/about ring fruit entities stay alive -> gate stuck ->
// child screen never created -> blank screenshot.
//
// This test uses TestFireAboutSlice() / TestFireShopSlice() which replicate the full
// real path: ClearMenuItems() + callback. After running 240 frames for all flung
// entities to be reaped and the child screen to fade in, a screenshot is taken.
//
// Screenshots:
//   tmp/test/screenshots/dojo_nav/about.png  (dojo -> aboutscreen)
//   tmp/test/screenshots/dojo_nav/shop.png   (dojo -> shopscreen)
//
// Run:
//   ctest --test-dir build/host -R dojo_navigation --output-on-failure
//   ctest --test-dir build/host -R dojo_nav_about  --output-on-failure
//   ctest --test-dir build/host -R dojo_nav_shop   --output-on-failure

#include "test_harness.h"
#include "screens/DojoScreen.h"
#include "hud/HUD.h"
#include "hud/HUDControl.h"
#include "entities/FruitInfo.h"
#include "entities/ActorManager.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <list>

// Scan the HUD for controls NOT in 'existing'. Returns the count found.
static int CountNewControls(const std::list<HUDControl*>& existing) {
    int count = 0;
    if (!game_work.mHud) return 0;
    std::list<HUDControl*>::iterator it;
    for (it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* ctrl = *it;
        bool found = false;
        std::list<HUDControl*>::const_iterator eit;
        for (eit = existing.begin(); eit != existing.end(); ++eit) {
            if (*eit == ctrl) { found = true; break; }
        }
        if (!found) ++count;
    }
    return count;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "dojo_nav");
    const char* mode = h.Opt("mode", NULL);
    bool doAbout = mode && strcmp(mode, "about") == 0;
    bool doShop  = mode && strcmp(mode, "shop")  == 0;
    if (!doAbout && !doShop) {
        fprintf(stderr, "FAIL: pass --mode=about or --mode=shop\n");
        return 1;
    }

    const char* modeName = doAbout ? "about" : "shop";
    char shotLabel[256];
    snprintf(shotLabel, sizeof(shotLabel), "dojo_nav/%s", modeName);
    h.label = shotLabel;
    // 60 burn-in frames: game boots, fonts/textures live, MainScreen initialises its
    // menu entities. Full game init (NOT component mode) so ActorManager::Update()
    // runs each frame and can process entity physics / reap flung entities.
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null\n");
        return 1;
    }
    int fruitCount = g_FruitInfoCount;
    if (fruitCount <= 0) {
        fprintf(stderr, "FAIL: g_FruitInfoCount=%d -- fruitlist.xml not loaded\n",
                fruitCount);
        return 1;
    }

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();

    // Step 1: deactivate all pre-existing HUD controls (MainScreen etc.) so they
    // don't draw over DojoScreen, and so their Update() does not interfere.
    // We do NOT clear the HUD list -- the controls own their entities and we need
    // the game infrastructure live. m_Active=0 suppresses their HUD callbacks.
    {
        std::list<HUDControl*>::iterator it;
        for (it = game_work.mHud->controls.begin();
             it != game_work.mHud->controls.end(); ++it) {
            (*it)->m_Active = 0;
        }
    }

    // Step 1b: reap MainScreen ring entities before DojoScreen so the entity-count
    // gate (GetNumEntities(0)==0 && (1)==0) can fire correctly during the transition.
    // In the real game DojoScreen is only shown after MainScreen's fruits are
    // flung+reaped. Replicate that here by calling ClearMenuItems() then waiting.
    {
        ::ClearMenuItems();
        int capFrames = 0;
        while (capFrames < 120) {
            int nFruit = am ? am->GetNumEntities(0) : 0;
            int nBomb  = am ? am->GetNumEntities(1) : 0;
            if (nFruit == 0 && nBomb == 0) break;
            h.game.runFrames(1);
            // Port specific: no DojoScreen exists yet here, but MainScreen's own
            // transition alpha (same UpdateRealtime split) needs the paired pump
            // to keep behaving like the real per-presented-frame game loop.
            h.game.tickRealtimeUi(1.0f / 60.0f);
            ++capFrames;
        }
        int entFruit = am ? am->GetNumEntities(0) : -1;
        int entBomb  = am ? am->GetNumEntities(1) : -1;
        printf("[dojo_nav/%s] pre-dojo entity clear: fruit=%d bomb=%d (after %d frames)\n",
               modeName, entFruit, entBomb, capFrames);
        if (entFruit != 0 || entBomb != 0) {
            fprintf(stderr,
                "FAIL: entity count not zero after ClearMenuItems + 120 frames: "
                "fruit=%d bomb=%d\n", entFruit, entBomb);
            h.Shutdown();
            return 1;
        }
    }

    // Step 2: snapshot HUD before DojoScreen to identify new controls later.
    std::list<HUDControl*> preDojoControls;
    {
        std::list<HUDControl*>::iterator it;
        for (it = game_work.mHud->controls.begin();
             it != game_work.mHud->controls.end(); ++it) {
            preDojoControls.push_back(*it);
        }
    }
    printf("[dojo_nav/%s] pre-dojo HUD count=%d\n", modeName,
           (int)preDojoControls.size());
    if (am) {
        printf("[dojo_nav/%s] pre-dojo entities: fruit=%d bomb=%d\n", modeName,
               am->GetNumEntities(0), am->GetNumEntities(1));
    }

    // Step 3: create DojoScreen via the faithful binary flow:
    //   ctor (adds BSButtons to HUD) -> Init() -> Reset() -> CreateButtons() -> AddControl.
    DojoScreen* dojo = new DojoScreen();
    dojo->Init();
    game_work.mHud->AddControl(dojo);

    // Step 4: settle 60 frames so DojoScreen reaches idle state 1 (alpha 0->1,
    // ring buttons grow in, bomb/fruit entities allocated by CreateFruit).
    // Port specific: m_TransitionAlpha now eases in DojoScreen::UpdateRealtime
    // (per-presented-frame, dt-scaled), not the 60Hz Update() that runFrames
    // drives. The real game loop pumps both per presented frame (GameSDL.cpp);
    // without the paired call here alpha never advances and the state-0 ->
    // state-1 transition never fires.
    for (int i = 0; i < 60; ++i) {
        h.game.runFrames(1);
        h.game.tickRealtimeUi(1.0f / 60.0f);
    }

    int stateAfterSettle = dojo->TestGetState();
    printf("[dojo_nav/%s] after 60-frame settle: state=%d\n",
           modeName, stateAfterSettle);
    if (am) {
        printf("[dojo_nav/%s] entity counts after settle: fruit=%d bomb=%d\n",
               modeName, am->GetNumEntities(0), am->GetNumEntities(1));
    }
    if (stateAfterSettle != 1) {
        fprintf(stderr,
            "WARN: DojoScreen state=%d after settle (expected 1 idle) -- "
            "alpha may not have converged\n", stateAfterSettle);
    }

    // Step 5: snapshot HUD after DojoScreen settle (before child screen appears).
    // Controls added after this point are the child screen(s) created by DojoScreen.
    std::list<HUDControl*> preChildControls;
    {
        std::list<HUDControl*>::iterator it;
        for (it = game_work.mHud->controls.begin();
             it != game_work.mHud->controls.end(); ++it) {
            preChildControls.push_back(*it);
        }
    }
    printf("[dojo_nav/%s] pre-child HUD count=%d\n", modeName,
           (int)preChildControls.size());

    // Step 6: fire the REAL transition callback path.
    // TestFireAboutSlice() / TestFireShopSlice() replicate what MenuButton::Update
    // does when a ring is sliced:
    //   ClearMenuItems()  -- flings all live menu entities (fruit + bombs) so they
    //                        fly off-screen and get reaped by ActorManager::Update;
    //                        this clears the GetNumEntities(0)==0 && (1)==0 gate.
    //   AboutCallback()/ShopCallback() -- sets DojoScreen state to 3/2, gives the
    //                        back-bomb its outward velocity.
    if (doAbout) {
        dojo->TestFireAboutSlice();
        printf("[dojo_nav/about] TestFireAboutSlice() called -> state=%d\n",
               dojo->TestGetState());
    } else {
        dojo->TestFireShopSlice();
        printf("[dojo_nav/shop] TestFireShopSlice() called -> state=%d\n",
               dojo->TestGetState());
    }
    if (am) {
        printf("[dojo_nav/%s] entity counts immediately after fire: fruit=%d bomb=%d\n",
               modeName, am->GetNumEntities(0), am->GetNumEntities(1));
    }

    // Step 7: run 240 frames.
    // Timeline:
    //   ~0-80 frames: flung menu entities fly off-screen, ActorManager reaps them.
    //   ~80-100 frames: GetNumEntities(0)==0 && (1)==0 -> entity gate clears.
    //   ~100-112 frames: m_TransitionDelay 0.2s / (1/60) = ~12 frames -> countdown fires.
    //   frame ~112: DojoScreen creates child screen, calls Init(), adds to HUD.
    //   ~112-130 frames: child screen alpha lerps 0->1 (0.125 step, ~17 frames to 0.999).
    //   frame ~130+: child screen fully visible.
    // 240 frames total = generous margin.
    bool transitionFired = false;
    int  transitionFrame = -1;
    int  childControlCount = 0;
    for (int frame = 0; frame < 240; ++frame) {
        h.game.runFrames(1);
        // Port specific: pumps DojoScreen's (and the child screen's, once created)
        // UpdateRealtime-eased m_TransitionAlpha -- see Step 4 comment above.
        h.game.tickRealtimeUi(1.0f / 60.0f);

        // Poll for transition once per frame. transition fired = m_pBackButton nulled
        // inside the state-2/3 block AFTER all conditions cleared.
        if (!transitionFired && dojo->TestTransitionFired()) {
            transitionFired   = true;
            transitionFrame   = frame;
            childControlCount = CountNewControls(preChildControls);
            printf("[dojo_nav/%s] transition fired at frame %d: "
                   "new HUD controls=%d\n",
                   modeName, transitionFrame, childControlCount);
            if (am) {
                printf("[dojo_nav/%s]   entities at transition: fruit=%d bomb=%d\n",
                       modeName,
                       am->GetNumEntities(0), am->GetNumEntities(1));
            }
            if (doAbout) {
                AboutScreen* as = dojo->TestGetAboutScreen();
                printf("[dojo_nav/about]   m_pAboutScreen=%p %s\n",
                       (void*)as, (as ? "non-null" : "NULL"));
            }
        }

        // Periodic entity count log to help diagnose gate stalls.
        if ((frame % 30 == 0) && !transitionFired) {
            if (am) {
                printf("[dojo_nav/%s] f=%d entities fruit=%d bomb=%d state=%d\n",
                       modeName, frame,
                       am->GetNumEntities(0), am->GetNumEntities(1),
                       dojo->TestGetState());
            }
        }
    }

    // --- Results ---
    int failures = 0;

    if (!transitionFired) {
        fprintf(stderr,
            "FAIL: DojoScreen transition did NOT fire after 240 frames. "
            "state=%d, m_pBackButton still live.\n",
            dojo->TestGetState());
        if (am) {
            fprintf(stderr,
                "FAIL: entity counts at 240f: fruit=%d bomb=%d -- "
                "gate stuck (flung entities not reaped or ClearMenuItems missed)\n",
                am->GetNumEntities(0), am->GetNumEntities(1));
        }
        ++failures;
    } else {
        printf("[dojo_nav/%s] PASS: transition fired at frame %d, "
               "%d new HUD control(s) added\n",
               modeName, transitionFrame, childControlCount);

        // For About: child screen must be non-null in DojoScreen's own pointer.
        if (doAbout) {
            AboutScreen* as = dojo->TestGetAboutScreen();
            if (!as) {
                fprintf(stderr,
                    "FAIL: m_pAboutScreen is null after transition -- "
                    "AboutScreen not created by DojoScreen state-3 path\n");
                ++failures;
            } else {
                printf("[dojo_nav/about] PASS: m_pAboutScreen=%p\n", (void*)as);
            }
        }

        // For Shop: verify at least one new HUD control appeared.
        if (doShop && childControlCount == 0) {
            fprintf(stderr,
                "FAIL: no new HUD controls after Shop transition -- "
                "ShopScreen not added to HUD\n");
            ++failures;
        } else if (doShop) {
            printf("[dojo_nav/shop] PASS: %d new control(s) in HUD after transition\n",
                   childControlCount);
        }
    }

    // Screenshot: captures the final rendered state.
    // With the bug (gate never clears): blank -- DojoScreen never created child screen.
    // Without the bug: child screen content is visible (About board / Shop panel).
    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng(shotLabel)) {
            fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", shotLabel);
            ++failures;
        } else {
            printf("[%s] screenshot written\n", shotLabel);
        }
    }

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("PASS: dojo -> %s navigation OK\n", modeName);
    return h.Shutdown();
}
