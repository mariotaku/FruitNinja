// test_achievement_notification -- isolated screenshot test for the achievement
// unlock popup (NotificationControl, Type_Named).
//
// Usage: test_achievement_notification [--screenshot|--interactive|--headless]
//
// There is no achievements-list screen in v1.6.1 (AchievementsScreen is a
// defunct stub) -- the only on-screen "achievement drawing" is the slide-in
// unlock popup that AchievementManager::UnlockedAchievement (binary
// @0x001090d0) constructs:
//
//   NotificationControl* ctrl = new NotificationControl(
//       a->m_Name, a->m_Score, a->m_Texture, notifType);
//   ctrl->Init();
//   hud->AddControl(ctrl, false);
//
// (see src/game/AchievementManager.cpp UnlockedAchievement). notifType is
// Type_Numeric if a->m_Name[0] is a digit, else Type_Named -- this test forces
// Type_Named (the achievement-unlock case) regardless of the picked entry's name.
//
// Isolation approach: InitComponent() boots the full game (which runs
// AchievementManager::LoadAchievementInfo -> populates m_All with real XML
// entries + icon textures, and sets NotificationControl::s_banner /
// s_unlockBanner statics), then strips game_work.mHud so only the popup we add
// renders. A real AchievementInfo entry is pulled from m_All (public member)
// for the title/points/icon.
//
// Per-frame the popup's m_StateTimer is pinned into the settled window
// (0.2..2.7s) and m_bPendingRemoval forced to 0 before each HUD::Update, so
// the slide-in/slide-out animation settles and holds instead of dismissing.
//
// Output PNG (--screenshot mode):
//   tmp/test/screenshots/achievement/unlock.png

#include "test_harness.h"
#include "hud/NotificationControl.h"
#include "hud/HUDLayer.h"
#include "game/AchievementManager.h"
#include "game/GameWork.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "achievement/unlock");
    // 120 burn-in frames: lets GameInitialise -> AchievementManager::LoadAchievementInfo
    // run so xml/achievementList.xml entries + icon textures + the TTF font are loaded
    // before we pull an entry and draw.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    AchievementManager* am = AchievementManager::GetInstance();
    if (!am || am->m_All.empty()) {
        std::printf("SKIP: AchievementManager::m_All empty after boot (no achievementList.xml entries loaded)\n");
        return h.Shutdown();
    }

    // Pick the first entry with a valid icon texture (m_All.begin() is
    // BAMBOO_BLADE whose icon fails to load -> popup renders with no badge).
    uint32_t pickedHash = am->m_All.begin()->first;
    AchievementInfo* a = am->m_All.begin()->second;
    for (std::map<uint32_t, AchievementInfo*>::iterator it = am->m_All.begin();
         it != am->m_All.end(); ++it) {
        if (it->second->m_Texture.IsValid()) {
            pickedHash = it->first;
            a = it->second;
            break;
        }
    }
    if (!a->m_Texture.IsValid()) {
        std::printf("[achievement/unlock] NOTE: no entry has a valid icon texture; falling back to first entry\n");
    }
    std::printf("[achievement/unlock] picked id-hash=0x%08x name=\"%s\" score=%d icon-valid=%d\n",
                pickedHash, a->m_Name, a->m_Score, a->m_Texture.IsValid() ? 1 : 0);

    // Force Type_Named -- the achievement-unlock popup variant (as opposed to
    // Type_Numeric, which UnlockedAchievement only picks when m_Name starts
    // with a digit; see AchievementManager::UnlockedAchievement @0x001090d0).
    NotificationControl* n = new NotificationControl(
        a->m_Name, a->m_Score, a->m_Texture, NotificationControl::Type_Named);
    n->Init();
    n->m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;
    game_work.mHud->AddControl(n, false);

    // Settle into the resting window (0.2..2.7s per NotificationControl::Update
    // @0x001a3c7c) and hold there: pin m_StateTimer just inside the settled
    // range before each frame's HUD::Update so the slide-in/out never fires.
    for (int i = 0; i < 20; ++i) {
        n->m_StateTimer = 1.0f;
        n->m_bPendingRemoval = 0;
        h.RunComponentHeadless(1, Mortar::HUD_LAYER_FADE_MODAL);
    }

    std::printf("[achievement/unlock] pos=(%.2f, %.2f, %.2f) m_StateTimer=%.3f\n",
                n->pos.x, n->pos.y, n->pos.z, n->m_StateTimer);

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng("achievement/unlock")) return 2;
    }
    std::printf("PASS: achievement/unlock rendered\n");

    return h.Shutdown();
}
