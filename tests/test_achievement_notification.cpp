// test_achievement_notification -- isolated screenshot test for the achievement
// unlock popup (NotificationControl, Type_Named/Type_Numeric).
//
// Usage: test_achievement_notification [--screenshot|--interactive|--headless]
//                                       [--lang=<code>] [--achievement=<id>]
//
// There is no achievements-list screen in v1.6.1 (AchievementsScreen is a
// defunct stub) -- the only on-screen "achievement drawing" is the slide-in
// unlock popup that AchievementManager::UnlockedAchievement (v1.6.1
// @0x001180a8) constructs:
//
//   NotificationControl* ctrl = new NotificationControl(
//       a->m_DisplayName, a->m_Score, a->m_Texture, notifType);
//   ctrl->Init();
//   hud->AddControl(ctrl, false);
//
// (see src/game/AchievementManager.cpp UnlockedAchievement). notifType is
// Type_Numeric if a->m_Name[0] (the raw id, e.g. "261524") is a digit, else
// Type_Named (e.g. "DISCO_SLASH"). The popup label itself draws
// a->m_DisplayName -- the localized name loaded from the XML "name" attribute
// (e.g. ACHIEVEMENT_NEWBLADE -> "New Blade!") -- NOT the raw id.
//
// --achievement=<id> selects a specific AchievementInfo by its raw XML "id"
// attribute (hashed via StringHash, matching AchievementManager::m_All's key
// space). Combined with --lang=<code> (parsed by TestHarness::ParseFlags(),
// applied before game.init() so the string table + TTF font load for that
// language), this lets the same popup be rendered per language x achievement
// to verify localized text actually differs.
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
//   no --achievement given : tmp/test/screenshots/achievement/unlock.png
//   --achievement=<id>     : tmp/test/screenshots/achievement/<id>_<lang>.png
//                            (<lang> is the raw --lang= value, or "default"
//                            if --lang was not passed)

#include "test_harness.h"
#include "hud/NotificationControl.h"
#include "hud/HUDLayer.h"
#include "game/AchievementManager.h"
#include "game/GameWork.h"
#include "engine/util/StringHash.h"
#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "achievement/unlock");

    // Read independently of TestHarness::ParseFlags() (which only handles --lang=
    // for its own purposes). --lang= is also read here to compose the label.
    const char* achArg  = h.Opt("achievement", NULL);
    const char* langArg = h.Opt("lang", "default");
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

    uint32_t pickedHash = 0;
    AchievementInfo* a = NULL;

    if (achArg) {
        uint32_t hash = StringHash(achArg);
        std::map<uint32_t, AchievementInfo*>::iterator it = am->m_All.find(hash);
        if (it == am->m_All.end()) {
            std::printf("SKIP: achievement %s not found\n", achArg);
            return h.Shutdown();
        }
        pickedHash = it->first;
        a = it->second;
    } else {
        // Pick the first entry with a valid icon texture (m_All.begin() is
        // BAMBOO_BLADE whose icon fails to load -> popup renders with no badge).
        pickedHash = am->m_All.begin()->first;
        a = am->m_All.begin()->second;
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
    }
    std::printf("[achievement/unlock] picked id-hash=0x%08x id=\"%s\" name=\"%s\" lang=%s score=%d icon-valid=%d\n",
                pickedHash, a->m_Name, a->m_DisplayName, langArg, a->m_Score, a->m_Texture.IsValid() ? 1 : 0);

    // Binary: m_Name[0] (raw id) in '0'..'9' => Type_Numeric, else Type_Named
    // (mirrors AchievementManager::UnlockedAchievement @0x001180a8 exactly).
    NotificationControl::NotificationType notifType =
        (a->m_Name[0] >= '0' && a->m_Name[0] <= '9')
        ? NotificationControl::Type_Numeric
        : NotificationControl::Type_Named;
    // v1.6.1 @0x001180a8: ctor arg 1 is m_DisplayName (localized), not the raw id.
    NotificationControl* n = new NotificationControl(
        a->m_DisplayName, a->m_Score, a->m_Texture, notifType);
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

    std::printf("[achievement/unlock] name=\"%s\" lang=%s pos=(%.2f, %.2f, %.2f) m_StateTimer=%.3f\n",
                a->m_DisplayName, langArg, n->pos.x, n->pos.y, n->pos.z, n->m_StateTimer);

    if (h.IsScreenshot()) {
        if (achArg) {
            char name[256];
            std::snprintf(name, sizeof(name), "achievement/%s_%s", achArg, langArg);
            if (!h.ScreenshotPng(name)) return 2;
        } else {
            if (!h.ScreenshotPng("achievement/unlock")) return 2;
        }
    }
    std::printf("PASS: achievement/unlock rendered\n");

    return h.Shutdown();
}
