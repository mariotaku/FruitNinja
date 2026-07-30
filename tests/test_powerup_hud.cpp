// test_powerup_hud -- isolated screenshot test for the in-game Arcade powerup
// HUD (PowerUpManager) for combinations of FROZEN / FRENZY / x2 (1..3 active).
//
// Usage: test_powerup_hud [--screenshot|--interactive|--headless] [--combo=<name>]
//   --combo one of: freeze | frenzy | x2 | freeze+frenzy | freeze+x2 |
//                   frenzy+x2 | all3 | intro   (default: all3)
//
// --combo=intro exercises a different power: "ready_set_go" (poweruplist.xml),
// the automatic="true" power activated by PowerUpManager::Reset(true) at Arcade
// start. Its <effect> spawns two EffectImages -- arcade_60seconds.tex and
// arcade_go.tex -- gated on PowerUp::m_LongestRemaining (counts DOWN from
// m_TotalTime=2.0 to 0) via ScreenEffect::Update's (wEnd,wStart] window (see
// ScreenEffect.cpp @0x00148844): "60 SECONDS" shows for elapsed in [0.3,1.1]s,
// "GO!!" for elapsed in [1.2,2.0]s. Unlike the other combos, we do NOT pin the
// timer -- we drive real dt so the two images cross their real windows, and
// screenshot at t=0.5s and t=1.5s (tmp/test/screenshots/powerup/intro_60seconds.png,
// powerup/intro_go.png). After both captures are written it keeps ticking to
// t=2.25s so the clone actually expires -- see the comment at that call for why
// teardown needs it.
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
#include "game/FruitSaveData.h"
#include "hud/HUD.h"
#include "engine/util/StringHash.h"
#include "math/_Vector3.h"
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

// Pre-HUD hook for --combo=intro: unlike PowerUpPreFrame, this does NOT pin
// m_BonusAccum -- ready_set_go's arcade_60seconds/arcade_go images are gated
// on the real, decreasing m_LongestRemaining, so we must let time actually
// advance for the two images to cross their windows.
static void IntroPreFrame(void* ud, float dt) {
    static_cast<PowerUpManager*>(ud)->Update(dt);
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
    fn::TestHarness h(argc, argv, "powerup");
    const char* combo = h.Opt("combo", "all3");

    // label = "powerup/<combo>"; kept alive for the harness lifetime (TestHarness
    // stores the const char* by pointer).
    std::string label = std::string("powerup/") + combo;
    h.label = label.c_str();
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

    if (std::strcmp(combo, "intro") == 0) {
        _Vector3<float> origin(0.0f, 0.0f, 0.0f);
        // Real arcade seeds time-remaining via TimeControl::Reset (v1.6.1 @0x001c0930)
        // BEFORE ready_set_go activates. Without it, GameModifier::OnDeferComplete
        // (v1.6.1 @0x00140890) clamps m_BonusAccum 2.0 -> 0.1 (target=0 < 2.0), collapsing
        // the 2s intro to ~6 frames so it's gone before the screenshots. Seed the precondition.
        if (game_work.m_SaveData) game_work.m_SaveData->m_TimeRemainingSave = 60.9f;
        PowerUp* p = pum->ActivatePower(StringHash("ready_set_go"), origin, NULL);
        std::printf("[%s] activate \"ready_set_go\" -> %p\n", label.c_str(), (void*)p);
        if (!p) {
            std::printf("SKIP: %s ready_set_go did not activate "
                        "(hash mismatch or template not loaded)\n", label.c_str());
            return h.Shutdown();
        }

        // Drive real time: 30 frames (0.5s) lands inside arcade_60seconds' window
        // (elapsed 0.3-1.1s); another 60 frames (total 90 = 1.5s) lands inside
        // arcade_go's window (elapsed 1.2-2.0s). See ScreenEffect::Update window math.
        h.RunComponentHeadlessHooked(30, IntroPreFrame, NULL, pum);
        std::printf("[%s] t=0.5s longest=%.2f total=%.2f\n",
                    label.c_str(), p->m_LongestRemaining, p->m_TotalTime);
        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("powerup/intro_60seconds")) return 2;
        }

        h.RunComponentHeadlessHooked(60, IntroPreFrame, NULL, pum);
        std::printf("[%s] t=1.5s longest=%.2f total=%.2f\n",
                    label.c_str(), p->m_LongestRemaining, p->m_TotalTime);
        if (h.IsScreenshot()) {
            if (!h.ScreenshotPng("powerup/intro_go")) return 2;
        }

        // Task #144: both captures are already written to PNG above, so these
        // extra frames change nothing that this test exists to produce -- they
        // exist to run the clone off the end of its life so teardown sees the
        // production steady state instead of a frozen mid-intro one.
        //
        // ready_set_go is a clone in PowerUpManager::m_ActivePowerUps and its
        // <effect> EffectImages (arcade_60seconds/arcade_go) are owned by the
        // CLONE's m_pScreenEffect. PowerUpManager::UnloadTextures @0x00140b10
        // walks m_AllPowerUps + m_ScreenEffectPool only -- it drops the
        // TEMPLATE's copies, never a clone's -- and has zero xrefs in v1.6.1
        // anyway. The only path that frees a clone's ScreenEffect is expiry:
        // PowerUpManager::Update @0x00141484 -> PowerUp::Deactivate(false)
        // @0x00140530, which deletes m_pScreenEffect.
        // Stopping at t=1.5s (m_LongestRemaining=0.50) parks the clone alive
        // forever, so its two textures survive GameDestroy and leak their GL
        // names once SDL_GL_DeleteContext runs.
        //
        // 45 frames = 0.75s covers it with margin: the 2.0s time_mod expires
        // 0.5s (30 frames) from here, then PowerUp::Update ramps m_BarRamp down
        // at 12/s (1/12s = 5 frames) before returning 1.
        h.RunComponentHeadlessHooked(45, IntroPreFrame, NULL, pum);
        std::printf("[%s] t=2.25s expired -- active powers=%d screen effects=%d\n",
                    label.c_str(), (int)pum->m_ActivePowerUps.size(),
                    (int)pum->m_ActiveScreenEffects.size());

        std::printf("PASS: %s rendered\n", label.c_str());
        return h.Shutdown();
    }

    const char* names[3] = { NULL, NULL, NULL };
    int count = ComboNames(combo, names);

    _Vector3<float> origin(0.0f, 0.0f, 0.0f);
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
