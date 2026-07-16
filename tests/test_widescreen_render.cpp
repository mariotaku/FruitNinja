// test_widescreen_render -- visual screenshot ctest for the opt-in widescreen
// layout (Layout::SetWideLayout, see src/engine/render/Layout.h).
//
// Usage: test_widescreen_render [--screenshot] [--widescreen] [--combo=<name>]
//   --widescreen   activate Layout::SetWideLayout(true) and create the hidden
//                  SDL window at a 16:9 drawable (1138x640) up front (before
//                  Init()/InitComponent() -- mirrors mainSDL.cpp, which sizes
//                  the window at SDL_CreateWindow time rather than resizing an
//                  existing one), so Layout::EffectiveAspect()/HalfWidth()
//                  actually widen and Game::renderFrame's real
//                  Layout::ComputeViewport pillarbox logic (GameSDL.cpp)
//                  engages exactly as it does in the real app. Without the
//                  flag: window stays at the harness default 960x640 (3:2),
//                  SetWideLayout(false) -- byte-identical to pre-widescreen
//                  output (baseline).
//   --combo=<name> selects the powerup-effect segment (see below); default
//                  runs the menu-screen segment only.
//
// Two independently-selectable segments (kept in one file per widescreen
// flag/combo parameterisation, but using two different boot modes -- see
// per-segment comment -- so a single process runs exactly one):
//
// 1. MENU SCREENS (default; --combo not passed) -- full h.Init() boot, same
//    pattern as test_screen.cpp: MainScreen is already live; GameModeScreen /
//    ShopScreen are pushed the same way test_screen.cpp does, with sibling
//    screens hidden (m_Active=0) so the capture isolates the target. Driven
//    via game.runFrames() -> Game::renderFrame() (GameSDL.cpp), which is the
//    REAL widescreen code path (Layout::SetWindowAspect + ComputeViewport +
//    glViewport), not a re-derivation -- this is the strongest possible
//    evidence the opt-in layout actually engages.
//    Captures (tmp/test/screenshots/widescreen/):
//      mainmenu_3x2.png / mainmenu_16x9.png
//      gamemode_3x2.png / gamemode_16x9.png
//      shop_3x2.png     / shop_16x9.png
//
// 2. POWERUP FRENZY/FREEZE/X2 (--combo=frenzy|freeze|x2|freeze+frenzy|all) --
//    component isolation mode (InitComponent(), same pattern as test_powerup_hud.cpp):
//    GameTaskUpdate/WaveManager never tick in this mode (PowerUpManager::Update
//    is only ever called from WaveManager::Update, gated on an active wave --
//    see WaveManager.cpp:1193), so PowerUpManager is driven manually exactly
//    like test_powerup_hud.cpp's PowerUpPreFrame/PowerUpPostFrame hooks.
//    RunComponentHeadlessHooked's own per-frame viewport/ortho setup is fixed
//    at the faithful 240-wide bounds (test_harness.h does not know about
//    Layout), so this segment reapplies the SAME three calls
//    Game::renderFrame uses (Layout::SetWindowAspect/ComputeViewport/
//    glViewport) plus FruitCamera's Layout::HalfWidth()-driven ortho, applied
//    locally per frame here -- not a new harness feature, just inlining the
//    real widescreen call sequence around the existing hook API.
//    Captures (tmp/test/screenshots/widescreen/):
//      freeze_16x9.png / frenzy_16x9.png / x2_16x9.png /
//      freeze+frenzy_16x9.png / all_16x9.png
//      (widescreen only -- the 3:2 case is already covered by
//      screenshot_powerup_freeze / screenshot_powerup_frenzy in
//      test_powerup_hud.cpp; this segment exists to prove the powerup HUD
//      still renders correctly under the wider viewport, not to re-baseline 3:2)
//
// C++11 / GCC 4.4.1 NOT required -- host-only test TU (no cross-build target).

#include "test_harness.h"
#include "render/Layout.h"
#include "render/MatrixManager.h"
#include "screens/DojoScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/MainScreen.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"      // DrawBackground (factored out of GameDraw)
#include "game/PowerUpManager.h"
#include "game/PowerUp.h"
#include "game/GameModifier.h"
#include "game/GameMode.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"   // DeactivateAllEntities (drain menu fruit)
#include "particle/PSPParticleManager.h"
#include "engine/util/StringHash.h"
#include "math/_Vector3.h"
#include <cstdio>
#include <cstring>
#include <list>

// ---- Segment 2 helpers (mirror test_powerup_hud.cpp exactly) ----

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

static void PowerUpPostFrame(void* ud, float /*dt*/) {
    static_cast<PowerUpManager*>(ud)->Draw();
}

static int ComboNames(const char* combo, const char* names[3]) {
    if (std::strcmp(combo, "freeze") == 0) { names[0] = "freeze"; return 1; }
    if (std::strcmp(combo, "frenzy") == 0) { names[0] = "speed";  return 1; }
    if (std::strcmp(combo, "x2") == 0) { names[0] = "score_mult"; return 1; }
    if (std::strcmp(combo, "freeze+frenzy") == 0) {
        names[0] = "freeze"; names[1] = "speed"; return 2;
    }
    if (std::strcmp(combo, "all") == 0) {
        names[0] = "freeze"; names[1] = "speed"; names[2] = "score_mult"; return 3;
    }
    names[0] = "freeze"; names[1] = "speed";
    return 2;
}

// Runs the widescreen-aware per-frame viewport/ortho setup that
// Game::renderFrame (GameSDL.cpp) applies on the real render path, then the
// PowerUpManager pre/post hooks around one HUD update+draw. Component mode's
// own RunComponentHeadlessHooked always uses the fixed 240-wide ortho and the
// full drawable as the viewport (it has no Layout awareness), so this segment
// drives its own frame loop instead of calling that helper, matching
// Game::renderFrame's exact call sequence:
//   Layout::SetWindowAspect -> Layout::ComputeViewport -> glViewport ->
//   Layout::SetActiveViewport -> DisplayManager::BeginFrame -> ortho(HalfWidth) ->
//   HUD update/draw -> PSPParticleManager::Draw(-1/0/1) -> PowerUpManager::Draw
//
// PSPParticleManager wiring (GameUpdate/GameDraw's real per-frame calls, not
// reached by this component-isolation loop otherwise): PowerUpManager::Update
// (inside PowerUpPreFrame) drives ScreenEffect::Activate, which calls
// PSPParticleManager::AddEmitter -- the frenzy "speed" screen effect's two
// "star" emitters anchored at +-HalfWidth() (screen edges). Emitters only
// SPAWN particles on PSPParticleManager::Update(dt) (v1.6.1 @0x00105ed8,
// walks m_pActiveEmitters -> UpdateEmitter); the per-particle age/velocity/
// position integration is fused into PSPParticleManager::Draw(dt, paused,
// layer) itself (v1.6.1 @0x0013eccc, "fused integrate+render" -- see
// src/engine/particle/PSPParticleManager.h). So both calls are required every
// frame for particles to move and render; Draw must be called once per used
// depth layer, mirroring GameDraw's pm.Draw(-1)/pm.Draw(0)/pm.Draw(1) triple
// (src/game/GameInit.cpp GameDraw, passes 7/9/12) since we don't know a priori
// which m_UseDepth the "speed" emitter's star template uses.
static void RunPowerUpFrameWidescreenAware(fn::TestHarness& h, PowerUpManager* pum, int n) {
    static const float kDt = 1.0f / 60.0f;
    for (int i = 0; i < n; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) { if (ev.type == SDL_QUIT) return; }

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(h.window, &ww, &wh);
        Layout::SetWindowAspect((float)ww, (float)wh);
        int vpX, vpY, vpW, vpH;
        Layout::ComputeViewport(ww, wh, &vpX, &vpY, &vpW, &vpH);
        glViewport(vpX, vpY, vpW, vpH);
        Layout::SetActiveViewport(vpX, vpY, vpW, vpH, ww, wh);

        Mortar::DisplayManager::GetInstance().BeginFrame();
        MatrixManager::GetInstance().SetupOrtho(
            160.0f, -160.0f, -Layout::HalfWidth(), Layout::HalfWidth(), 2000.0f, -6000.0f);

        // Real game background (wooden dojo panel), matching GameDraw's draw
        // order (background drawn first, before HUD/actors/particles). The
        // background texture is already loaded -- InitComponent() calls
        // Init() -> game.init() -> GameInit(), whose Step 6 runs
        // ChangeBackground(nullptr) unconditionally; InitComponent() only
        // clears mHud's control list afterwards, it never touches the
        // g_BackgroundTexture global. DrawBackground() already scales by
        // Layout::HalfWidth() internally (see src/game/GameInit.cpp), so it
        // widens correctly under the widescreen ortho set up above.
        DrawBackground();

        PowerUpPreFrame(pum, kDt);
        PSPParticleManager::GetInstance().Update(kDt, false);

        if (game_work.mHud) {
            game_work.mHud->Update(kDt);
            game_work.mHud->BeginDraw(kDt);
            game_work.mHud->Draw(0x7FFFFFFF);
        }

        // Same three depth-layer draws as GameDraw (background/mid/foreground).
        PSPParticleManager::GetInstance().Draw(kDt, false, -1);
        PSPParticleManager::GetInstance().Draw(kDt, false, 0);
        PSPParticleManager::GetInstance().Draw(kDt, false, 1);

        PowerUpPostFrame(pum, kDt);

        SDL_GL_SwapWindow(h.window);
    }
}

// ---- Segment 1 helper: hide every existing HUD control (mirrors
// test_screen.cpp's hideAllExisting lambda, spelled as an explicit loop so
// this TU stays lambda-free like the rest of the render-test suite). ----
static void HideAllExisting() {
    if (!game_work.mHud) return;
    std::list<HUDControl*>::iterator it;
    for (it = game_work.mHud->controls.begin(); it != game_work.mHud->controls.end(); ++it) {
        (*it)->m_Active = 0;
    }
}

// Drains ambient fruit/bomb entities so menu screenshots capture clean UI
// (the menu screens keep their own ambient ActorManager spawner running,
// which otherwise clutters every capture with thrown fruit). Only targets
// ActorManager entities (fruit/bombs), not HUDControls -- menu buttons and
// other HUD widgets are untouched. Re-drains every settle frame because the
// ambient spawner can fire again mid-settle (mirrors test_bonus_crash.cpp's
// TickFrame(..., drainEntities=true) pattern, looped instead of one-shot so
// respawns during the settle window don't slip back into frame).
static void DrainAndSettle(fn::TestHarness& h, int settleFrames) {
    for (int i = 0; i < settleFrames; ++i) {
        if (h.game.actorManager) {
            h.game.actorManager->DeactivateAllEntities(0);
            h.game.actorManager->DeactivateAllEntities(1);
        }
        h.RunHeadless(1);
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "widescreen");
    if (!h.ParseFlags()) return 1;

    bool wide = h.OptFlag("widescreen");
    const char* combo = h.Opt("combo", NULL);
    const char* suffix = wide ? "16x9" : "3x2";

    int failures = 0;

    if (combo) {
        // ---- Segment 2: powerup frenzy/freeze, component isolation ----
        const char* names[3] = { NULL, NULL, NULL };
        int count = ComboNames(combo, names);

        h.SetInitFrames(120);
        // Create the window at the target drawable size up front (mirrors
        // mainSDL.cpp: winW/winH computed before SDL_CreateWindow, never a
        // post-creation SDL_SetWindowSize -- which can hang the hidden test
        // window under some GL drivers). Must be set before InitComponent().
        if (wide) h.SetWindowSize(1138, 640);
        Layout::SetWideLayout(wide);
        if (!h.InitComponent()) return 1;
        if (!game_work.mHud) {
            std::fprintf(stderr, "FAIL: mHud null after boot\n");
            return 1;
        }

        game_work.gameMode = Mortar::GAME_MODE_ARCADE;
        PowerUpManager* pum = PowerUpManager::GetInstance();
        pum->LoadTextures();

        _Vector3<float> origin(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < count; ++i) {
            PowerUp* p = pum->ActivatePower(StringHash(names[i]), origin, NULL);
            std::printf("[widescreen/%s] activate \"%s\" -> %p\n", combo, names[i], (void*)p);
        }

        // 90 frames (1.5s): overlay images fade in (0.5-0.75s) and meter bars
        // ramp to full, held in the steady window (matches test_powerup_hud.cpp).
        RunPowerUpFrameWidescreenAware(h, pum, 90);

        int active = 0;
        for (std::list<PowerUp*>::iterator it = pum->m_ActivePowerUps.begin();
             it != pum->m_ActivePowerUps.end(); ++it, ++active) {}
        if (active == 0) {
            std::printf("[widescreen/%s] NOTE: no active powerups after settle "
                        "(activation failed?)\n", combo);
        }

        if (h.IsScreenshot()) {
            char label[128];
            std::snprintf(label, sizeof(label), "widescreen/%s_%s", combo, suffix);
            if (!h.ScreenshotPng(label)) {
                std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
                ++failures;
            } else {
                std::printf("[%s] screenshot written (%d active)\n", label, active);
            }
        }

        if (failures > 0) {
            std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
            h.Shutdown();
            return 1;
        }
        std::printf("PASS: widescreen powerup (%s, %s) OK\n", combo, suffix);
        return h.Shutdown();
    }

    // ---- Segment 1: menu screens, full boot (matches test_screen.cpp) ----
    h.SetInitFrames(5);

    // --widescreen must be applied BEFORE Init()'s burn-in frames so every
    // subsequent game.runFrames() -> Game::renderFrame() call (real widescreen
    // code path, GameSDL.cpp) sees the wide viewport from the first frame.
    // Create the window at the target drawable size up front (mirrors
    // mainSDL.cpp: winW/winH computed before SDL_CreateWindow) instead of a
    // post-creation SDL_SetWindowSize, which can hang the hidden test window
    // under some GL drivers.
    if (wide) h.SetWindowSize(1138, 640);
    Layout::SetWideLayout(wide);
    if (!h.Init()) return 1;
    // Settle: let MainScreen's camera-zoom state and any Layout::MapX-positioned
    // controls settle under the final viewport before capture.
    h.RunHeadless(60);

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud is null after boot\n");
        return 1;
    }

    // ---- MainScreen: already live and active; just capture. ----
    DrainAndSettle(h, 10);
    if (h.IsScreenshot()) {
        char label[64];
        std::snprintf(label, sizeof(label), "widescreen/mainmenu_%s", suffix);
        if (!h.ScreenshotPng(label)) {
            std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
            ++failures;
        } else {
            std::printf("[%s] screenshot written\n", label);
        }
    }

    // ---- GameModeScreen: push on top, hide MainScreen, settle, capture. ----
    HideAllExisting();
    GameModeScreen* gms = new GameModeScreen(h.game, false);
    game_work.mHud->AddControl(gms);
    h.RunHeadless(60);
    DrainAndSettle(h, 10);

    if (h.IsScreenshot()) {
        char label[64];
        std::snprintf(label, sizeof(label), "widescreen/gamemode_%s", suffix);
        if (!h.ScreenshotPng(label)) {
            std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
            ++failures;
        } else {
            std::printf("[%s] screenshot written\n", label);
        }
    }
    gms->m_Active = 0;

    // ---- ShopScreen: needs a DojoScreen parent (test_screen.cpp pattern). ----
    DojoScreen* dojo = new DojoScreen(h.game);
    dojo->m_Active = 0;
    game_work.mHud->AddControl(dojo);
    ShopScreen* shop = new ShopScreen(dojo);
    game_work.mHud->AddControl(shop, false);
    shop->Init();
    h.RunHeadless(60);
    DrainAndSettle(h, 10);

    if (h.IsScreenshot()) {
        char label[64];
        std::snprintf(label, sizeof(label), "widescreen/shop_%s", suffix);
        if (!h.ScreenshotPng(label)) {
            std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
            ++failures;
        } else {
            std::printf("[%s] screenshot written\n", label);
        }
    }

    if (h.IsInteractive()) {
        h.RunInteractive(NULL);
    }

    if (failures > 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        h.Shutdown();
        return 1;
    }

    std::printf("PASS: widescreen_render (%s) OK\n", suffix);
    return h.Shutdown();
}
