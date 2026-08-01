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
// Ticking runs in two phases:
//   A. Natural ticking (t = 0 -> ~0.57s, fixed 1/60): m_StateTimer is NOT
//      pinned, so NotificationControl::Update (v1.6.1 @0x001a3c7c) crosses the
//      three 1/8s confetti ticks (t=0.125/0.25/0.375) one Update apart and
//      spawns three "confettif" PSPParticleManager emitters (Type_Named only).
//      A pinned/jumped timer would cross all three ticks in ONE Update and
//      spawn at most once -- that's exactly the regression this phase guards.
//      The particle manager is driven per frame via RunComponentHeadlessHooked
//      (Update pre-HUD, Draw(-1/0/1) post-HUD -- same idiom as
//      test_widescreen_render.cpp segment 2); templates themselves were
//      already loaded by the InitComponent boot (GameInitialise step 12,
//      pm.LoadFile("particles", "particles/particles_fast.xml"), which
//      defines "confettif"). A per-frame probe accumulates positive deltas of
//      PSPParticleManager::CountActiveEmitters() and the test asserts the
//      total == 3 for Type_Named (0 for Type_Numeric). Positive-delta
//      accumulation (not a single before/after count) because each confetti
//      emitter's MaxLifetime is 5/60s (~0.083s, <life> 5 in
//      particles_fast.xml) -- Update reaps it ~5 frames after its burst, so
//      the three are never alive simultaneously.
//   B. Settle + hold (existing behaviour): the popup's m_StateTimer is pinned
//      into the settled window (0.2..2.7s) and m_bPendingRemoval forced to 0
//      before each HUD::Update, so the slide-in/slide-out animation settles
//      and holds instead of dismissing (pin tick = 8, >= 4, so no further
//      confetti spawns). The particle hooks stay attached so the burst
//      particles (life 100-130/60s) keep integrating and render in the capture.
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
#include "particle/PSPParticleManager.h"
#include <cstdio>
#include <cstring>

// ---- Confetti probe (see phase A in the header comment) ----
// Counts confetti emitter SPAWNS as positive deltas of the live-emitter count,
// sampled once per frame after HUD::Update has run (post-draw hook). Reaps
// (negative deltas) are ignored; spawn frames (t=0.125/0.25/0.375) and reap
// frames (~5 frames after each burst, MaxLifetime 5/60s) never coincide, so
// the accumulated total equals the number of successful AddEmitter calls.
struct ConfettiProbe {
    int lastCount;
    int spawns;
};

// Pre-HUD hook: emitters only SPAWN particles on PSPParticleManager::Update
// (v1.6.1 @0x00105ed8, walks m_pActiveEmitters -> UpdateEmitter) -- and it is
// also what advances emitter timers / reaps ended emitters. Mirrors
// test_widescreen_render.cpp's RunPowerUpFrameWidescreenAware wiring.
static void ConfettiPreFrame(void* /*ud*/, float dt) {
    PSPParticleManager::GetInstance().Update(dt, false);
}

// Post-HUD hook: fused integrate+render (Draw, v1.6.1 @0x0013eccc), one call
// per depth layer matching GameDraw's pm.Draw(-1)/Draw(0)/Draw(1) triple
// ("confettif" is useDepth=1, but mirror the real triple rather than assuming
// the layer). Then sample the live emitter count for the spawn probe --
// sampled here so a same-frame spawn (HUD::Update ran just before) is seen
// before any later Update can reap it.
static void ConfettiPostFrame(void* ud, float dt) {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    pm.Draw(dt, false, -1);
    pm.Draw(dt, false, 0);
    pm.Draw(dt, false, 1);
    ConfettiProbe* p = static_cast<ConfettiProbe*>(ud);
    int c = pm.CountActiveEmitters();
    if (c > p->lastCount) p->spawns += c - p->lastCount;
    p->lastCount = c;
}

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
        // First entry in load order. Every achievementList.xml entry carries a
        // "texture" attribute and LoadAchievementInfo now appends ".tex" the way
        // the binary does, so the icon is always valid -- no scan needed. (This
        // used to scan for a valid-icon entry and warn when none was found, which
        // masked the missing-extension bug in LoadAchievementInfo.)
        pickedHash = am->m_All.begin()->first;
        a = am->m_All.begin()->second;
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

    // --- Phase A: natural ticking through the confetti spawn window ---
    // (See header comment.) m_StateTimer advances 1/60 per frame from 0, so the
    // three 1/8s tick crossings (frames ~8/15/23) each fire in their OWN Update
    // call -- jumping the timer straight into the settled window would cross
    // all three ticks in one Update and spawn at most once. 34 frames reach
    // t ~= 0.567s: past the last crossing (0.375s), well short of slide-out
    // (2.7s). ClearEmitters first so the probe's count deltas are confetti-only
    // (boot burn-in can leave menu-ambience emitters live, and a same-frame
    // reap of one of those would mask a spawn's +1).
    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    pm.ClearEmitters();
    ConfettiProbe probe;
    probe.lastCount = pm.CountActiveEmitters();  // 0 after ClearEmitters
    probe.spawns = 0;
    h.RunComponentHeadlessHooked(34, ConfettiPreFrame, ConfettiPostFrame,
                                 &probe, Mortar::HUD_LAYER_FADE_MODAL);

    // Regression guard: the confetti spawn block is Type_Named-gated
    // (NotificationControl::Update @0x001a3c7c), so Named banners must have
    // spawned exactly 3 emitters and Numeric banners exactly 0. This is the
    // real coverage -- the screenshot alone can't distinguish "no confetti
    // drawn" from "confetti never spawned".
    int expectedSpawns =
        (notifType == NotificationControl::Type_Named) ? 3 : 0;
    std::printf("[achievement/unlock] confetti emitter spawns=%d expected=%d m_StateTimer=%.3f\n",
                probe.spawns, expectedSpawns, n->m_StateTimer);
    if (probe.spawns != expectedSpawns) {
        std::fprintf(stderr, "FAIL: confetti emitter spawns=%d, expected %d\n",
                     probe.spawns, expectedSpawns);
        h.Shutdown();
        return 1;
    }

    // --- Phase B: settle into the resting window (0.2..2.7s per
    // NotificationControl::Update @0x001a3c7c) and hold there: pin m_StateTimer
    // just inside the settled range before each frame's HUD::Update so the
    // slide-in/out never fires (pin tick = 8, >= 4 -- no further confetti
    // spawns). Hooks stay attached so the burst particles keep integrating and
    // render under the banner in the capture.
    for (int i = 0; i < 20; ++i) {
        n->m_StateTimer = 1.0f;
        n->m_bPendingRemoval = 0;
        h.RunComponentHeadlessHooked(1, ConfettiPreFrame, ConfettiPostFrame,
                                     &probe, Mortar::HUD_LAYER_FADE_MODAL);
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
