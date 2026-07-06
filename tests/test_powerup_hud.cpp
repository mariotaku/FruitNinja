// test_powerup_hud -- isolated screenshot test for the in-game Arcade powerup
// HUD (PowerUpManager) for combinations of FROZEN / FRENZY / x2 (1..3 active).
//
// Usage: test_powerup_hud [--screenshot|--interactive|--headless] [--combo=<name>]
//   --combo one of: freeze | frenzy | x2 | freeze+frenzy | freeze+x2 |
//                   frenzy+x2 | all3   (default: all3)
//
// The three arcade "banana" powerups are activated by StringHash of their XML
// name (FruitNinjaBada/Data/xml/poweruplist.xml):
//   FROZEN  -> "freeze"      (7s time_mod + wave_mod; effect: ice_cover vignette
//                             + clock_freeze overlay images; meter bar
//                             arcade_banana_meter_freeze)
//   FRENZY  -> "speed"       (5s wave_mod; effect: particle emitters + screen tint,
//                             NO persistent overlay image; meter bar
//                             arcade_banana_meter_frenzy)
//   x2      -> "score_mult"  (8s score_mod; effect: hud_x2_sign overlay image +
//                             emitter + tint; meter bar arcade_banana_meter_scorex2)
//
// Two draw paths (both driven per frame):
//   (A) Meter bars      -- PowerUpManager::Draw() walks m_ActivePowerUps and calls
//                          PowerUp::DrawBar(). These are NOT HUDControls; they draw
//                          directly through MatrixManager. Issued AFTER the HUD so
//                          bars sit on top.
//   (B) Overlay images  -- freeze's ice_cover + clock_freeze and x2's hud_x2_sign
//                          are HUDControl3d's that ScreenEffect::Activate() adds to
//                          game_work.mHud, drawn by mHud->Draw(). FRENZY spawns no
//                          persistent overlay image, so its only visible artefact is
//                          the meter bar (plus a screen tint we don't capture here).
//
// Isolation approach mirrors test_achievement_notification: InitComponent() boots
// the full game then strips game_work.mHud so only what we add renders. We set
// Arcade mode, call PowerUpManager::LoadTextures() (WaveManager normally does this
// on wave start; the MainScreen boot path may not), then ActivatePower for the
// selected combo. Each ActivatePower clones the template, applies modifiers, spawns
// the overlay HUDControl3d's into mHud, and a transient word popup.
//
// Per-frame settle (RunComponentHeadlessHooked):
//   pre-HUD hook  : pin each active power into its steady window (so Update neither
//                   fades it out nor expires/erases it), then PowerUpManager::Update
//   HUD           : mHud->Update / BeginDraw / Draw  (overlay images ramp in)
//   post-HUD hook : PowerUpManager::Draw()  (meter bars on top)
//
// FIDELITY NOTE: we deliberately do NOT touch m_BarXPos. For these non-special
// (single="true", automatic absent) powers, PowerUpManager::Update computes every
// bar's x-target from GetNumActiveTimedPowers() (which counts m_bIsSpecial powers ==
// 0 here), so all bars converge to the same x and OVERLAP. That collapse is the real
// in-game layout for this powerup class (known unresolved TODO at PowerUpManager.h:77)
// -- the screenshot shows it as-is rather than hiding it with a port-side offset.
//
// Output PNG (--screenshot): tmp/test/screenshots/powerup/<combo>.png

#include "test_harness.h"
#include "game/PowerUpManager.h"
#include "game/PowerUp.h"
#include "game/GameModifier.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "engine/util/StringHash.h"
#include "math/Vec3.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <list>

// Pre-HUD hook: pin every active power into its steady window, then tick the
// manager. PowerUp::Update recomputes m_LongestRemaining from each modifier's
// m_BonusAccum and expires the power when a modifier's m_BonusAccum hits 0, so
// the load-bearing pin is m_BonusAccum; m_LongestRemaining / m_BarRamp are set
// for good measure (m_BarRamp also ramps up on its own). We hold at 0.6 *
// m_TotalTime -- inside the "on" window, past the fade-in, before the fade-out.
static void PowerUpPreFrame(void* ud, float dt) {
    PowerUpManager* pum = static_cast<PowerUpManager*>(ud);
    for (std::list<PowerUp*>::iterator it = pum->m_ActivePowerUps.begin();
         it != pum->m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        float hold = p->m_TotalTime * 0.6f;
        p->m_BarRamp = 1.0f;
        p->m_LongestRemaining = hold;
        for (std::list<GameModifier*>::iterator mit = p->m_ModList.begin();
             mit != p->m_ModList.end(); ++mit) {
            (*mit)->m_BonusAccum = hold;
        }
    }
    pum->Update(dt);
}

// Post-HUD hook: draw the meter bars on top of the HUD overlays.
static void PowerUpPostFrame(void* ud, float /*dt*/) {
    static_cast<PowerUpManager*>(ud)->Draw();
}

// Resolve a --combo key to the set of powerup XML names to activate.
// Returns the count; fills names[0..count-1]. Unknown -> all3.
static int ComboNames(const char* combo, const char* names[3]) {
    if (std::strcmp(combo, "freeze") == 0)             { names[0] = "freeze"; return 1; }
    if (std::strcmp(combo, "frenzy") == 0)             { names[0] = "speed";  return 1; }
    if (std::strcmp(combo, "x2") == 0)                 { names[0] = "score_mult"; return 1; }
    if (std::strcmp(combo, "freeze+frenzy") == 0)      { names[0] = "freeze"; names[1] = "speed"; return 2; }
    if (std::strcmp(combo, "freeze+x2") == 0)          { names[0] = "freeze"; names[1] = "score_mult"; return 2; }
    if (std::strcmp(combo, "frenzy+x2") == 0)          { names[0] = "speed";  names[1] = "score_mult"; return 2; }
    // all3 (default)
    names[0] = "freeze"; names[1] = "speed"; names[2] = "score_mult";
    return 3;
}

int main(int argc, char* argv[]) {
    const char* combo = "all3";
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--combo=", 8) == 0) combo = argv[i] + 8;
    }

    // label = "powerup/<combo>"; kept alive for the harness lifetime (TestHarness
    // stores the const char* by pointer).
    std::string label = std::string("powerup/") + combo;

    fn::TestHarness h(argc, argv, label.c_str());
    // 120 burn-in frames: lets GameInitialise -> PowerUpManager::Load parse
    // poweruplist.xml (templates + screen effects) before we clone/activate.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: mHud null after boot\n");
        return 1;
    }

    // Arcade is the mode that runs the banana-powerup HUD.
    game_work.gameMode = Mortar::GAME_MODE_ARCADE;

    PowerUpManager* pum = PowerUpManager::GetInstance();
    // WaveManager::LoadTextures normally primes the powerup icon/bar textures on
    // wave start; the isolated boot may not have reached a wave, so do it here.
    pum->LoadTextures();

    const char* names[3] = { NULL, NULL, NULL };
    int count = ComboNames(combo, names);

    Vec3 origin(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < count; ++i) {
        PowerUp* p = pum->ActivatePower(StringHash(names[i]), origin, NULL);
        std::printf("[%s] activate \"%s\" -> %p\n", label.c_str(), names[i], (void*)p);
    }

    // Settle: ~90 frames (1.5s) so overlay images fully fade in (transitionTime
    // 0.5..0.75s) and meter bars ramp to full, held in the steady window.
    h.RunComponentHeadlessHooked(90, PowerUpPreFrame, PowerUpPostFrame, pum);

    // Report the resting HUD state per active power.
    int active = 0;
    for (std::list<PowerUp*>::iterator it = pum->m_ActivePowerUps.begin();
         it != pum->m_ActivePowerUps.end(); ++it, ++active) {
        PowerUp* p = *it;
        std::printf("[%s] active[%d] name=\"%s\" total=%.2f longest=%.2f ramp=%.3f barX=%.2f\n",
                    label.c_str(), active, p->m_Name, p->m_TotalTime,
                    p->m_LongestRemaining, p->m_BarRamp, p->m_BarXPos);
    }
    if (active == 0) {
        std::printf("[%s] NOTE: no active powerups after settle (activation failed?)\n",
                    label.c_str());
    }

    if (h.IsScreenshot()) {
        if (!h.ScreenshotPng()) return 2;  // uses label -> powerup/<combo>.png
    }
    std::printf("PASS: %s rendered (%d active)\n", label.c_str(), active);

    return h.Shutdown();
}
