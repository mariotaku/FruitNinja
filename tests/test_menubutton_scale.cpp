// test_menubutton_scale -- regression guard for per-screen menu-bomb SIZE.
//
// Proves that a Dojo-style menu bomb ends up 0.825x the steady-state size
// of a Home-style menu bomb, by driving MenuButton::Update headlessly.
//
// Background:
//   MenuButton::CreateFruit (bomb branch) sets:
//     entity->scale *= 0.85 (BOMB_MENU_SCALE)
//     m_RestScale = Vec3(bombRawSize*2, ...)
//   MenuButton::Update first frame: m_BaseScale = entity->scale
//   MenuButton::Update steady state: entity->scale = m_BaseScale * (size.y/m_RestScale.y)
//     -> at full grow-in (size.y == m_RestScale.y) -> entity->scale = m_BaseScale
//   DojoScreen after Init: m_RestScale *= 0.825; entity->scale *= 0.825
//     -> m_BaseScale is NOT yet captured (captured in first Update frame after DojoScreen shrinks)
//     -> so first Update: m_BaseScale = entity->scale * 0.825 = shrunken scale
//     -> steady state: entity->scale = m_BaseScale = shrunken scale
//
// Expected: dojoScale ~= 0.825 * homeScale (tolerance 1e-3).
// This test is expected to FAIL if the bug is present (Dojo renders same size as Home).
//
// Run:
//   ctest --test-dir build/host -R menubutton_scale --output-on-failure
//   ./build/host/tests/Debug/test_menubutton_scale.exe

#include "test_harness.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"
#include "engine/util/Delegate.h"
#include "game/GameWork.h"
#include <cmath>
#include <cstdio>

static const int   SETTLE_FRAMES = 200;
static const float BACK_SCALE = 0.825f;  // DojoScreen::BACK_SCALE

// No-op callback for Init.
static void NoOpCb() {}

// Run one MenuButton grow-in cycle and return the steady-state entity scale.x.
// If dojoShrink==true, apply the DojoScreen 0.825 shrink after Init (before
// any Update tick), matching DojoScreen::CreateButtons.
static float MeasureBombScale(fn::TestHarness& h, bool dojoShrink) {
    int bombType = g_FruitInfoCount;

    // Position chosen from DojoScreen POS_PLAY_BUTTON: doesn't matter for scale.
    _Vector3<float> spawnPos(16.0f, -66.0f, 0.0f);
    _Vector3<float> hitBounds(0.0f, 0.0f, 0.0f);

    Mortar::Delegate0<void> clickCb  = Mortar::Delegate0<void>::MakeFree(NoOpCb);
    Mortar::Delegate0<void> deleteCb = Mortar::Delegate0<void>::MakeFree(NoOpCb);

    MenuButton* btn = new MenuButton();
    btn->Init(spawnPos, clickCb, bombType, hitBounds, deleteCb);

    // Add to HUD so Update path doesn't fault on HUD traversal.
    if (game_work.mHud) {
        btn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        game_work.mHud->AddControl(btn);
    }

    if (dojoShrink) {
        // Replicate DojoScreen::CreateButtons exactly (DojoScreen.cpp ~215-218):
        //   m_pPlayButton->m_RestScale *= BACK_SCALE;
        //   if (m_pPlayButton->m_pTrackedFruit) m_pTrackedFruit->scale *= BACK_SCALE;
        btn->m_RestScale = btn->m_RestScale * BACK_SCALE;
        if (btn->m_pTrackedFruit) {
            btn->m_pTrackedFruit->scale = btn->m_pTrackedFruit->scale * BACK_SCALE;
        }
    }

    // Tick enough frames for full grow-in (AnimPhase saturates to 0x3ffc in <10 frames).
    for (int i = 0; i < SETTLE_FRAMES; ++i) {
        h.game.runFrames(1);
    }

    float result = 0.0f;
    if (btn->m_pEntity) {
        result = btn->m_pEntity->scale.x;
    } else if (btn->m_pTrackedFruit) {
        // Entity may have been cleared on a fling/disable; tracked fruit scale is the fallback.
        result = btn->m_pTrackedFruit->scale.x;
    }

    // Remove from HUD to avoid stale-pointer crashes in subsequent test steps.
    if (game_work.mHud) {
        btn->m_bPendingRemoval = 1;
        h.game.runFrames(1);
    }

    return result;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "menubutton_scale");
    h.SetInitFrames(60);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    // Verify FruitInfo was loaded.
    int fruitCount = g_FruitInfoCount;
    if (fruitCount <= 0) {
        fprintf(stderr, "FAIL: g_FruitInfoCount=%d -- fruitlist.xml not loaded\n", fruitCount);
        return 1;
    }
    printf("[SETUP] fruitCount=%d bombType=%d bombRawSize=%.2f\n",
           fruitCount, fruitCount, FruitInfo_GetBombSize());

    float homeScale = MeasureBombScale(h, false);
    float dojoScale = MeasureBombScale(h, true);
    float ratio     = (homeScale > 0.0f) ? (dojoScale / homeScale) : 0.0f;

    printf("[RESULT] homeScale=%.6f  dojoScale=%.6f  ratio=%.6f  expected_ratio=%.4f\n",
           homeScale, dojoScale, ratio, BACK_SCALE);

    // Sanity: home scale must be positive.
    if (homeScale <= 0.0f) {
        fprintf(stderr, "FAIL: homeScale=%.6f -- bomb entity never got a scale\n", homeScale);
        return 1;
    }
    if (dojoScale <= 0.0f) {
        fprintf(stderr, "FAIL: dojoScale=%.6f -- dojo bomb entity never got a scale\n", dojoScale);
        return 1;
    }

    // Core assertion: Dojo bomb must be 0.825x Home bomb (within 1e-3).
    float diff = fabsf(ratio - BACK_SCALE);
    if (diff > 1e-3f) {
        fprintf(stderr,
            "FAIL: ratio=%.6f expected=%.4f diff=%.6f > 1e-3\n"
            "  -> Dojo bomb is NOT 0.825x the Home bomb size (BUG REPRODUCED)\n",
            ratio, BACK_SCALE, diff);
        h.Shutdown();
        return 1;
    }

    printf("[PASS] dojoScale / homeScale = %.6f (expected %.4f, diff=%.6f)\n",
           ratio, BACK_SCALE, diff);
    return h.Shutdown();
}
