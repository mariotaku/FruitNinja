// test_gamemode_destructor -- ASAN regression guard for GameModeScreen heap crash.
//
// DIAGNOSIS TARGET: UAF / double-free in GameModeScreen::~GameModeScreen()
// triggered via GameModeScreen::RemoveButtons() in the dtor.
//
// ROOT CAUSE (RE spec v1.6.1):
//   Binary GameModeScreen::Release @0x001835a4 does NOT call RemoveButtons().
//   Port divergence: port Release() and ~GameModeScreen() both call RemoveButtons(),
//   which sets m_bPendingRemoval=1 on the 4 MenuButtons and nulls the screen's
//   cached pointers.
//
//   UAF scenario: a MenuButton self-removes (shrink-out: m_bPendingRemoval=1 after
//   m_AnimPhase drains to 0), HUD::Update() deletes it, but m_pBackButton etc. in
//   GameModeScreen is still non-null (m_DeletedCallback is never fired via the
//   shrink-out path -- only TouchReleased() fires it). When GameModeScreen is later
//   reaped, ~GameModeScreen() -> RemoveButtons() calls SetPendingRemoval() on an
//   already-freed MenuButton pointer = use-after-free.
//
// CRASH PATH:
//   1. GameModeScreen creates buttons (state 2 after alpha lerps).
//   2. One button self-removes (m_bPendingRemoval=1 via shrink-out).
//   3. HUD::Update() reaps + deletes the button.
//   4. GameModeScreen's m_pClassicButton still points to the freed memory.
//   5. GameModeScreen sets m_bPendingRemoval=1 (mode-pick or back-out).
//   6. HUD::Update() reaps + deletes GameModeScreen.
//   7. ~GameModeScreen() calls RemoveButtons().
//   8. RemoveButtons() reads m_pClassicButton (freed) -> SetPendingRemoval() -> UAF.
//   9. Then deletes m_pTitleBox / m_pDescBox / m_pInfoBox -> heap corruption ->
//      emscripten_builtin_free OOB (web) / ASAN heap-use-after-free (local).
//
// TEST STRATEGY (deterministic, no real touch input):
//   Sub-test A (PRE-REAP PATH):
//     1. Deactivate existing HUD controls.
//     2. Create GameModeScreen, add to HUD.
//     3. Run frames until GMS reaches state 2 (alpha > 0.999, buttons appear).
//        Detect via m_pClassicButton becoming non-null.
//     4. Force m_pClassicButton->m_bPendingRemoval = 1 (simulates shrink-out).
//     5. Run one tick: HUD reaps + deletes the classic button.
//        Now m_pClassicButton in GMS points to freed memory.
//     6. Force GMS->m_bPendingRemoval = 1.
//     7. Run one tick: HUD reaps + deletes GMS.
//        ~GMS() -> RemoveButtons() -> UAF on freed classic button pointer.
//        Under ASAN this catches the bug. After the fix it runs clean.
//
//   Sub-test B (BACK-OUT PATH):
//     Same setup but triggers QuitCallback -> state 0xf -> natural alpha decay
//     -> m_bPendingRemoval. Tests the realistic user-facing crash path.

#include "test_harness.h"
#include "screens/GameModeScreen.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/HUDControl.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>
#include <list>

// Returns true if any GameModeScreen is still live in game_work.mHud.
static bool GameModeScreenLive() {
    if (!game_work.mHud) return false;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        if (dynamic_cast<GameModeScreen*>(*it)) return true;
    }
    return false;
}

static GameModeScreen* FindGameModeScreen() {
    if (!game_work.mHud) return 0;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        GameModeScreen* s = dynamic_cast<GameModeScreen*>(*it);
        if (s) return s;
    }
    return 0;
}

// Returns true if the given pointer is still in game_work.mHud->controls.
static bool ControlInHUD(HUDControl* target) {
    if (!game_work.mHud || !target) return false;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        if (*it == target) return true;
    }
    return false;
}

// Deactivate all existing HUD controls so they don't interact with the test.
static void DeactivateExisting() {
    if (!game_work.mHud) return;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        (*it)->m_Active = 0;
    }
}

// Wait for GameModeScreen to create its buttons (state 2: alpha > 0.999).
// Returns true when m_pClassicButton is non-null, false on timeout.
static bool WaitForGMSButtons(fn::TestHarness& h, int maxFrames) {
    for (int i = 0; i < maxFrames; ++i) {
        h.game.runFrames(1);
        GameModeScreen* cur = FindGameModeScreen();
        if (cur && cur->m_pClassicButton) {
            printf("[gamemode_dtor] buttons created at frame %d (classic=%p back=%p zen=%p arcade=%p)\n",
                   i,
                   (void*)cur->m_pClassicButton,
                   (void*)cur->m_pBackButton,
                   (void*)cur->m_pZenButton,
                   (void*)cur->m_pArcadeButton);
            return true;
        }
    }
    return false;
}

// ---- Sub-test A: pre-reap UAF path ------------------------------------------
// Force the exact crash scenario: delete a button BEFORE deleting the screen,
// while the screen still holds a non-null (stale) pointer to the freed button.

static int RunPreReapTest(fn::TestHarness& h) {
    printf("[gamemode_dtor] === Sub-test A: pre-reap UAF path ===\n");

    DeactivateExisting();

    GameModeScreen* gms = new GameModeScreen(false);
    game_work.mHud->AddControl(gms);
    printf("[gamemode_dtor] A: GMS=%p\n", (void*)gms);

    // Wait for state 2 (buttons created). Takes ~55 frames for alpha lerp.
    if (!WaitForGMSButtons(h, 120)) {
        fprintf(stderr, "[gamemode_dtor] FAIL A: buttons not created in 120 frames\n");
        return 1;
    }

    GameModeScreen* cur = FindGameModeScreen();
    if (!cur) {
        fprintf(stderr, "[gamemode_dtor] FAIL A: GMS gone before buttons appeared\n");
        return 1;
    }

    // Pick the classic button to pre-reap.
    MenuButton* preReapBtn = cur->m_pClassicButton;
    if (!preReapBtn) preReapBtn = cur->m_pZenButton;
    if (!preReapBtn) preReapBtn = cur->m_pArcadeButton;
    if (!preReapBtn) {
        fprintf(stderr, "[gamemode_dtor] FAIL A: no mode button found\n");
        return 1;
    }

    printf("[gamemode_dtor] A: pre-reap btn=%p (classic=%p)\n",
           (void*)preReapBtn, (void*)cur->m_pClassicButton);

    // Step: force button pending-removal (simulates post-slice shrink-out completing).
    preReapBtn->m_bPendingRemoval = 1;

    // Run ONE tick: HUD reaps + deletes the button. GMS still has m_pClassicButton
    // pointing to the now-freed memory (m_DeletedCallback never fires on self-reap).
    h.game.runFrames(1);
    bool btnStillInHUD = ControlInHUD(preReapBtn);
    printf("[gamemode_dtor] A: after btn reap tick; btn still in HUD: %s\n",
           btnStillInHUD ? "YES" : "NO (reaped)");

    // Verify GMS is still live and still holds the stale pointer.
    cur = FindGameModeScreen();
    if (!cur) {
        fprintf(stderr, "[gamemode_dtor] FAIL A: GMS gone unexpectedly\n");
        return 1;
    }

    // Note: m_pClassicButton is non-null (stale pointer to freed memory).
    // Under the buggy code, RemoveButtons() in ~GMS() will dereference it.
    printf("[gamemode_dtor] A: GMS still live; m_pClassicButton=%p (stale=%s)\n",
           (void*)cur->m_pClassicButton,
           (!btnStillInHUD && cur->m_pClassicButton) ? "YES - UAF target" : "no");

    // Step: force GMS pending-removal.
    cur->m_bPendingRemoval = 1;

    // Run ONE tick: HUD reaps + deletes GMS.
    // Under buggy code: ~GMS() -> RemoveButtons() -> SetPendingRemoval() on freed ptr.
    // ASAN reports: heap-use-after-free on the freed MenuButton.
    // After the fix: RemoveButtons() not called in dtor; runs clean.
    h.game.runFrames(1);

    if (GameModeScreenLive()) {
        fprintf(stderr, "[gamemode_dtor] FAIL A: GMS still live after forced removal\n");
        return 1;
    }

    // Drain any remaining buttons (let the HUD finish cleaning up).
    for (int i = 0; i < 10; ++i) h.game.runFrames(1);

    printf("[gamemode_dtor] A: PASS\n");
    return 0;
}

// ---- Sub-test B: back-out path ----------------------------------------------
// Realistic path: QuitCallback -> state 0xf -> alpha decays -> m_bPendingRemoval.

static int RunBackOutTest(fn::TestHarness& h) {
    printf("[gamemode_dtor] === Sub-test B: back-out (QuitCallback) path ===\n");

    DeactivateExisting();

    GameModeScreen* gms = new GameModeScreen(false);
    game_work.mHud->AddControl(gms);
    printf("[gamemode_dtor] B: GMS=%p\n", (void*)gms);

    if (!WaitForGMSButtons(h, 120)) {
        fprintf(stderr, "[gamemode_dtor] FAIL B: buttons not created in 120 frames\n");
        return 1;
    }

    GameModeScreen* cur = FindGameModeScreen();
    if (!cur) {
        fprintf(stderr, "[gamemode_dtor] FAIL B: GMS gone before test\n");
        return 1;
    }

    // Pre-reap the zen button to create a stale pointer scenario.
    MenuButton* preReapBtn = cur->m_pZenButton;
    if (preReapBtn) {
        printf("[gamemode_dtor] B: pre-reaping zen btn=%p\n", (void*)preReapBtn);
        preReapBtn->m_bPendingRemoval = 1;
        h.game.runFrames(1);  // HUD reaps it; GMS m_pZenButton still non-null
    }

    cur = FindGameModeScreen();
    if (!cur) {
        fprintf(stderr, "[gamemode_dtor] FAIL B: GMS gone after btn pre-reap\n");
        return 1;
    }

    // Trigger back-out.
    cur->QuitCallback();
    printf("[gamemode_dtor] B: QuitCallback fired\n");

    // Run until GMS is reaped (state 0xf fades out to < 0.001, ~28 frames).
    for (int i = 0; i < 60; ++i) {
        h.game.runFrames(1);
        if (!GameModeScreenLive()) {
            printf("[gamemode_dtor] B: GMS reaped at tick %d\n", i);
            break;
        }
    }

    // Force reap if still live (mMainScreen may be inactive in test context).
    cur = FindGameModeScreen();
    if (cur) {
        printf("[gamemode_dtor] B: forcing GMS pending removal (state machine stalled)\n");
        cur->m_bPendingRemoval = 1;
        h.game.runFrames(1);
    }

    if (GameModeScreenLive()) {
        fprintf(stderr, "[gamemode_dtor] FAIL B: GMS still live\n");
        return 1;
    }

    for (int i = 0; i < 10; ++i) h.game.runFrames(1);

    printf("[gamemode_dtor] B: PASS\n");
    return 0;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "gamemode_dtor");
    h.SetInitFrames(5);
    if (!h.ParseFlags()) return 1;
    if (!h.Init()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "[gamemode_dtor] FAIL: mHud null after init\n");
        return 1;
    }

    int failures = 0;
    failures += RunPreReapTest(h);
    failures += RunBackOutTest(h);

    if (failures > 0) {
        fprintf(stderr, "[gamemode_dtor] FAILED %d sub-test(s)\n", failures);
        h.Shutdown();
        return 1;
    }

    printf("[gamemode_dtor] PASS: all sub-tests clean\n");
    return h.Shutdown();
}
