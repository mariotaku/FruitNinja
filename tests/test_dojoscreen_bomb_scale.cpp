// test_dojoscreen_bomb_scale -- regression guard for Dojo back-bomb SIZE via REAL DojoScreen flow.
//
// The manual test (test_menubutton_scale) proved the math is correct when
// m_pTrackedFruit is non-null at shrink time. This test drives the REAL
// DojoScreen activation flow (Init()->Reset()->CreateButtons()) to confirm
// whether m_pTrackedFruit is actually set when the 0.825 shrink block inside
// CreateButtons() executes (m_pBackButton null guard).
//
// Hypothesis: if m_pTrackedFruit is null at shrink time (e.g. because
// CreateFruit defers entity allocation past the current frame), then only
// m_RestScale gets shrunk -- but MenuButton::Update captures m_BaseScale
// from entity->scale on its first tick, which was NOT shrunk.  In steady
// state, entity->scale = m_BaseScale (unchanged), so the Dojo bomb renders
// the same size as Home.
//
// Expected (faithful): dojoBackBomb scale ~= 0.825 * homeBomb scale (ratio ~0.825).
// If the assert FAILS with ratio ~1.0, the bug is reproduced.
//
// Run:
//   ctest --test-dir build/host -R dojoscreen_bomb_scale --output-on-failure

#include "test_harness.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"
#include "screens/DojoScreen.h"
#include "game/GameWork.h"
#include <cmath>
#include <cstdio>
#include <list>

// --- Constants ---
static const float BACK_SCALE     = 0.825f;  // DojoScreen::BACK_SCALE
static const float ALPHA_BUTTON_CREATE = 0.95f;
static const float ALPHA_LERP_IN       = 0.25f;

// Number of frames to advance past button creation and grow-in.
// alpha reaches 0.95 in ~10 frames (0.25 lerp), then button creates.
// MenuButton grow-in saturates in <10 more frames.  300 frames is generous.
static const int SETTLE_FRAMES = 300;

// ---
// Snapshot the set of HUD control pointers currently in the HUD list.
// Used to find NEW controls added by DojoScreen vs pre-existing MainScreen controls.
static void SnapshotHUD(std::list<HUDControl*>& out) {
    out.clear();
    if (!game_work.mHud) return;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        out.push_back(*it);
    }
}

// Find the back-bomb MenuButton in the HUD control list that was NOT in the
// pre-existing snapshot. The back button is a bomb (m_FruitType == FruitInfo_GetCount())
// added by DojoScreen, not any pre-existing MainScreen button.
static MenuButton* FindDojoBackBomb(const std::list<HUDControl*>& existingControls) {
    if (!game_work.mHud) return NULL;
    int bombType = FruitInfo_GetCount();
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        HUDControl* ctrl = *it;
        // Skip controls that existed before DojoScreen was added.
        bool wasExisting = false;
        for (std::list<HUDControl*>::const_iterator eit = existingControls.begin();
             eit != existingControls.end(); ++eit) {
            if (*eit == ctrl) { wasExisting = true; break; }
        }
        if (wasExisting) continue;

        MenuButton* mb = dynamic_cast<MenuButton*>(ctrl);
        if (mb && mb->m_FruitType == bombType) {
            return mb;
        }
    }
    return NULL;
}

// Run MenuButton::MeasureBombScale for a plain (non-Dojo) bomb to establish
// the Home baseline.  This replicates test_menubutton_scale's MeasureBombScale
// with dojoShrink=false, kept local so this test is self-contained.
static float MeasureHomeBombScale(fn::TestHarness& h) {
    int bombType = FruitInfo_GetCount();
    _Vector3<float> spawnPos(185.0f, -106.0f, 0.0f);
    _Vector3<float> hitBounds(0.0f, 0.0f, 0.0f);

    Mortar::Delegate0<void> clickCb;
    Mortar::Delegate0<void> deleteCb;

    MenuButton* btn = new MenuButton();
    btn->Init(spawnPos, clickCb, bombType, hitBounds, deleteCb);

    if (game_work.mHud) {
        btn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        game_work.mHud->AddControl(btn);
    }

    for (int i = 0; i < 200; ++i) {
        h.game.runFrames(1);
    }

    float result = 0.0f;
    if (btn->m_pEntity) {
        result = btn->m_pEntity->scale.x;
    } else if (btn->m_pTrackedFruit) {
        result = btn->m_pTrackedFruit->scale.x;
    }
    printf("[HOME-BOMB] entityPtr=%p trackedFruitPtr=%p baseScale=%.6f restScale=%.6f entityScale=%.6f\n",
           (void*)btn->m_pEntity,
           (void*)btn->m_pTrackedFruit,
           btn->m_BaseScale.x,
           btn->m_RestScale.x,
           result);

    // Remove from HUD.
    btn->m_bPendingRemoval = 1;
    h.game.runFrames(1);
    return result;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "dojoscreen_bomb_scale");
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "FAIL: game_work.mHud is null after init\n");
        return 1;
    }

    // Verify FruitInfo was loaded.
    int fruitCount = FruitInfo_GetCount();
    if (fruitCount <= 0) {
        fprintf(stderr, "FAIL: FruitInfo_GetCount()=%d -- fruitlist.xml not loaded\n", fruitCount);
        return 1;
    }
    printf("[SETUP] fruitCount=%d bombType=%d\n", fruitCount, fruitCount);

    // --- Step 1: Measure Home bomb baseline. ---
    float homeScale = MeasureHomeBombScale(h);
    printf("[RESULT] homeScale=%.6f\n", homeScale);
    if (homeScale <= 0.0f) {
        fprintf(stderr, "FAIL: homeScale=%.6f -- Home bomb entity never got a scale\n", homeScale);
        return 1;
    }

    // --- Step 2: Boot a REAL DojoScreen and drive it to button-creation. ---
    // Deactivate pre-existing controls so they don't interfere.
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        (*it)->m_Active = 0;
    }

    // Snapshot HUD BEFORE adding DojoScreen so we can distinguish DojoScreen's
    // buttons from pre-existing MainScreen buttons (which also have bomb MenuButtons).
    std::list<HUDControl*> existingControls;
    SnapshotHUD(existingControls);
    printf("[SETUP] pre-DojoScreen HUD control count: %d\n",
           (int)existingControls.size());

    DojoScreen* dojo = new DojoScreen();
    // Match the real game's activation flow (MainScreen @ 0x197494):
    //   Init() is called BEFORE AddControl; Init() -> Reset() -> CreateButtons().
    dojo->Init();
    game_work.mHud->AddControl(dojo);

    // --- Step 3: Tick enough frames for the back-bomb grow-in to saturate. ---
    // CreateButtons() fires during Init() (v1.6.1 faithful path: Init->Reset->CreateButtons).
    // We drive SETTLE_FRAMES total so MenuButton grow-in fully saturates.
    //
    // After each frame, check if the back-bomb has been added to HUD.
    // When it first appears in the HUD list, snapshot the diagnostic state.
    MenuButton* dojoBtn       = NULL;
    int         creationFrame = -1;

    // Snapshot values at the exact frame button is first found.
    void*  snap_entityPtr      = NULL;
    void*  snap_trackedFruitPtr = NULL;
    float  snap_restScale      = 0.0f;

    for (int frame = 0; frame < SETTLE_FRAMES; ++frame) {
        h.game.runFrames(1);
        // Port specific: m_TransitionAlpha (state 0 fade-in that gates the
        // back-bomb button creation) now eases in DojoScreen::UpdateRealtime
        // (per-presented-frame, dt-scaled), not the 60Hz Update() that
        // runFrames drives. The real game loop pumps both per presented frame
        // (GameSDL.cpp); without the paired call here alpha never crosses
        // ALPHA_IN_DONE and the back-bomb button is never created.
        h.game.tickRealtimeUi(1.0f / 60.0f);

        if (dojoBtn == NULL) {
            dojoBtn = FindDojoBackBomb(existingControls);
            if (dojoBtn != NULL) {
                creationFrame       = frame;
                snap_entityPtr      = (void*)dojoBtn->m_pEntity;
                snap_trackedFruitPtr = (void*)dojoBtn->m_pTrackedFruit;
                snap_restScale      = dojoBtn->m_RestScale.x;
                printf("[CREATION f=%d] entityPtr=%p trackedFruitPtr=%p restScale=%.6f\n",
                       creationFrame,
                       snap_entityPtr,
                       snap_trackedFruitPtr,
                       snap_restScale);
            }
        }
    }

    if (dojoBtn == NULL) {
        fprintf(stderr,
            "FAIL: DojoScreen never created its back-bomb after %d frames.\n"
            "  -> m_pBackButton was never added to HUD (Init()->Reset()->CreateButtons() path broken?)\n",
            SETTLE_FRAMES);
        h.Shutdown();
        return 1;
    }

    // Read final steady-state values.
    float  finalEntityScale     = 0.0f;
    void*  final_entityPtr      = (void*)dojoBtn->m_pEntity;
    void*  final_trackedFruit   = (void*)dojoBtn->m_pTrackedFruit;
    float  final_baseScale      = dojoBtn->m_BaseScale.x;
    float  final_restScale      = dojoBtn->m_RestScale.x;

    if (dojoBtn->m_pEntity) {
        finalEntityScale = dojoBtn->m_pEntity->scale.x;
    } else if (dojoBtn->m_pTrackedFruit) {
        finalEntityScale = dojoBtn->m_pTrackedFruit->scale.x;
    }

    printf("[DOJO-BOMB FINAL]\n");
    printf("  entityPtr       = %p (at creation: %p)\n",
           final_entityPtr, snap_entityPtr);
    printf("  trackedFruitPtr = %p (at creation: %p)\n",
           final_trackedFruit, snap_trackedFruitPtr);
    printf("  baseScale.x     = %.6f\n", final_baseScale);
    printf("  restScale.x     = %.6f (at creation: %.6f)\n",
           final_restScale, snap_restScale);
    printf("  entityScale.x   = %.6f\n", finalEntityScale);
    printf("[DIAGNOSIS]\n");
    printf("  homeScale       = %.6f\n", homeScale);
    printf("  dojoScale       = %.6f\n", finalEntityScale);

    // Derive expected dojo scale = homeScale * 0.825.
    float expectedDojoScale = homeScale * BACK_SCALE;
    float ratio = (homeScale > 0.0f && finalEntityScale > 0.0f)
                  ? (finalEntityScale / homeScale)
                  : 0.0f;
    printf("  expected_ratio  = %.4f\n", BACK_SCALE);
    printf("  actual_ratio    = %.6f\n", ratio);
    printf("  expected_scale  = %.6f\n", expectedDojoScale);

    // Diagnostic: was trackedFruit null at creation time?
    if (snap_trackedFruitPtr == NULL) {
        printf("[DIAGNOSIS] trackedFruitPtr was NULL at creation frame -- "
               "shrink skipped entity scale (BUG PATH)\n");
    } else {
        printf("[DIAGNOSIS] trackedFruitPtr was non-null at creation frame -- "
               "shrink applied entity scale (CORRECT PATH)\n");
    }

    // Core assertion.
    if (finalEntityScale <= 0.0f) {
        fprintf(stderr,
            "FAIL: finalEntityScale=%.6f -- Dojo bomb entity never got a scale\n",
            finalEntityScale);
        h.Shutdown();
        return 1;
    }

    float diff = fabsf(ratio - BACK_SCALE);
    if (diff > 1e-3f) {
        fprintf(stderr,
            "FAIL: ratio=%.6f expected=%.4f diff=%.6f > 1e-3\n"
            "  -> Dojo bomb is NOT 0.825x the Home bomb size (BUG REPRODUCED via real DojoScreen)\n",
            ratio, BACK_SCALE, diff);
        h.Shutdown();
        return 1;
    }

    printf("PASS: dojoScale/homeScale = %.6f (expected %.4f, diff=%.6f)\n",
           ratio, BACK_SCALE, diff);
    return h.Shutdown();
}
