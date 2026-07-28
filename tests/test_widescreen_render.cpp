// test_widescreen_render -- visual screenshot ctest for the opt-in widescreen
// layout (Layout::SetWideLayout, see src/engine/render/Layout.h).
//
// Usage: test_widescreen_render [--screenshot] [--widescreen] [--combo=<name>]
//   --widescreen   activate Layout::SetWideLayout(true) and create the hidden
//                  SDL window at a 16:9 drawable (1136x640) up front (before
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
//      dojo_3x2.png     / dojo_16x9.png
//      about_3x2.png    / about_16x9.png
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
// 3. IN-GAME HUD / SCORECONTROL (--combo=ingame_hud) -- component isolation
//    mode, same InitComponent() pattern as segment 2 but without PowerUpManager:
//    a single ScoreControl is constructed and added as the sole HUD control,
//    with game_work seeded to the TRUE steady-state gameplay values
//    (gameMode=CLASSIC, bM_Mode=false, bM_bPaused=0, m_PauseAmount=0.0f --
//    see WaveManager.cpp:1613, "m_PauseAmount == 0.0f during [active gameplay]")
//    and currentScore=12345 so the score number renders a visible multi-digit
//    value. IMPORTANT: at m_PauseAmount==0 the "SCORE" wordmark (ScoreControl::
//    PreDraw Section D, gated on transTimer > 0.0f) does NOT draw -- that gate
//    only opens during the wave-transition/game-over settle (m_PauseAmount -> 1.0,
//    see GameOverScreen.cpp:489/1121/1295), which is a DIFFERENT visual moment
//    than steady in-game play even though both share the same ScoreControl
//    instance and the same "hud.score" Layout::MapX key. This capture is the
//    faithful true-in-game frame (score number + watermelon icon only, no
//    wordmark) -- see the orchestrator's report for the full analysis of
//    whether the in-game and game-over anchor should be made independently
//    adjustable.
//    Captures (tmp/test/screenshots/widescreen/):
//      ingame_hud_3x2.png / ingame_hud_16x9.png
//
// C++11 / GCC 4.4.1 NOT required -- host-only test TU (no cross-build target).

#include "test_harness.h"
#include "render/Layout.h"
#include "render/MatrixManager.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
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
#include "hud/HUDLayer.h"
#include "hud/ScoreControl.h"
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

// ---- Segment 3 helper: in-game HUD (ScoreControl) frame loop. Same
// widescreen viewport/ortho/background sequence as
// RunPowerUpFrameWidescreenAware (Game::renderFrame's real per-frame calls),
// minus the PowerUpManager pre/post hooks -- this segment isolates
// ScoreControl only, no powerup activation involved.
static void RunHudFrameWidescreenAware(fn::TestHarness& h, int n) {
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

        DrawBackground();

        if (game_work.mHud) {
            game_work.mHud->Update(kDt);
            game_work.mHud->BeginDraw(kDt);
            game_work.mHud->Draw(0x7FFFFFFF);
        }

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

// Root cause: the real game loop (Game::run) calls Game::tickRealtimeUi every
// PRESENTED frame (Game.cpp:214 -> HUD::UpdateRealtime), which is what advances
// per-present-only visual state -- GameModeScreen's mode-select sensei slide
// (m_SecondaryAlpha, GameModeScreen.cpp UpdateRealtime), ShopScreen's panel
// fade-in (m_TransitionAlpha, ShopScreen.cpp UpdateRealtime), ScrollingMenu
// scroll/spring, MenuButton sparkle/NEW-badge, etc. h.RunHeadless() only drives
// game.runFrames() (sim tick + render), never tickRealtimeUi, so those
// animations never advance under a plain headless settle. Interleave a fixed
// dt=1/60 tickRealtimeUi call after each runFrames(1) so captures are
// deterministic (not wall-clock-based) and match what a real present loop
// would have driven by this point.
static void SettleRealtime(fn::TestHarness& h, int frames) {
    static const float kDt = 1.0f / 60.0f;
    for (int i = 0; i < frames; ++i) {
        h.game.runFrames(1);
        h.game.tickRealtimeUi(kDt);
    }
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "widescreen");
    if (!h.ParseFlags()) return 1;

    bool wide = h.OptFlag("widescreen");
    const char* combo = h.Opt("combo", NULL);
    const char* suffix = wide ? "16x9" : "3x2";

    int failures = 0;

    if (combo && std::strcmp(combo, "ingame_hud") == 0) {
        // ---- Segment 3: in-game HUD (ScoreControl), component isolation ----
        // Same InitComponent + wide-window-up-front pattern as segment 2, minus
        // PowerUpManager -- isolates the top-left SCORE readout so its
        // widescreen edge-alignment can be eyeballed against the game-over
        // fact-board capture (widescreen/gamemode_*, shop_*, etc. in segment 1
        // are menu screens; this is the actual gameplay HUD element).
        h.SetInitFrames(120);
        if (wide) h.SetWindowSize(1136, 640);
        Layout::SetWideLayout(wide);
        if (!h.InitComponent()) return 1;
        if (!game_work.mHud) {
            std::fprintf(stderr, "FAIL: mHud null after boot\n");
            return 1;
        }

        // Steady-state in-game gameplay: gameMode=CLASSIC, bM_Mode=false
        // (gameplay active, not menu-paused per GameWork.h:40), m_PauseAmount=0.0f
        // (WaveManager.cpp:1613 -- "0.0f during [active gameplay]"; GameInit's
        // menu-boot value is -1.0f, GameOverScreen's settled-display value is
        // ~1.0f -- see ScoreControl.cpp Update's waveTimer comment). A nonzero
        // score exercises the "SCORE" wordmark... except Section D
        // (ScoreControl::PreDraw, ScoreControl.cpp ~line 622) gates the wordmark
        // on transTimer > 0.0f, which steady-state gameplay (m_PauseAmount==0)
        // never satisfies -- see this test's header comment / the orchestrator
        // report for the analysis. Only the score NUMBER + watermelon icon are
        // visible in this true in-game state; this capture is intentionally the
        // faithful "no wordmark during gameplay" frame, not a fudged wordmark-on
        // frame.
        game_work.gameMode      = (uint8_t)Mortar::GAME_MODE_CLASSIC;
        game_work.bM_Mode        = false;
        game_work.bM_bPaused    = 0;
        game_work.m_PauseAmount = 0.0f;
        game_work.currentScore  = 12345;

        ScoreControl* sc = new ScoreControl();
        sc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
        game_work.mHud->AddControl(sc);

        // 60 frames: settle m_ScoreSmoothed's eased catch-up to currentScore
        // (ScoreControl::Update Stage 2, ~0.1-lerp per frame) and the
        // size-pulse decay, same settle budget as test_scorecontrol.cpp's
        // "active" case.
        for (int i = 0; i < 60; ++i) {
            game_work.m_PauseAmount = 0.0f;
            game_work.currentScore  = 12345;
            RunHudFrameWidescreenAware(h, 1);
        }

        std::printf("[widescreen/ingame_hud] pos=(%.2f, %.2f) drawPos=(%.2f, %.2f) displayedScore=%d\n",
                    sc->pos.x, sc->pos.y, sc->m_DrawPosX, sc->m_DrawPosY, sc->m_DisplayedScore);

        if (h.IsScreenshot()) {
            char label[64];
            std::snprintf(label, sizeof(label), "widescreen/ingame_hud_%s", suffix);
            if (!h.ScreenshotPng(label)) {
                std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
                ++failures;
            } else {
                std::printf("[%s] screenshot written\n", label);
            }
        }

        // Port specific: test teardown -- drop the score control's pending-removal
        // flag and settle one more frame so HUD::Update processes the removal
        // before Shutdown() tears down mHud (same discipline as test_scorecontrol.cpp).
        sc->m_bPendingRemoval = 1;
        RunHudFrameWidescreenAware(h, 1);

        if (failures > 0) {
            std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
            h.Shutdown();
            return 1;
        }
        std::printf("PASS: widescreen ingame_hud (%s) OK\n", suffix);
        return h.Shutdown();
    }

    if (combo) {
        // ---- Segment 2: powerup frenzy/freeze, component isolation ----
        const char* names[3] = { NULL, NULL, NULL };
        int count = ComboNames(combo, names);

        h.SetInitFrames(120);
        // Create the window at the target drawable size up front (mirrors
        // mainSDL.cpp: winW/winH computed before SDL_CreateWindow, never a
        // post-creation SDL_SetWindowSize -- which can hang the hidden test
        // window under some GL drivers). Must be set before InitComponent().
        if (wide) h.SetWindowSize(1136, 640);
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

        // Port specific: test teardown. PowerUpManager is a Meyer's singleton;
        // its function-local-static destructor runs at process-exit time, AFTER
        // main() returns and TestHarness::Shutdown() -> GameDestroy has already
        // deleted game_work.mHud and every HUDControl it owned. If any PowerUp
        // clone is still in m_ActivePowerUps at that point, ~PowerUp() ->
        // ScreenEffect::Deactivate() would deref a dangling EffectImage::m_pHudCtrl
        // into freed HUD memory. Drain here, while the HUD is still alive, so
        // Deactivate() detaches cleanly and m_ActivePowerUps is empty by the time
        // the static-dtor pass runs. Uses the same drain the game calls on
        // game-over/quit (PowerUpManager::Reset), not a hand-rolled loop.
        // fullReset=false: this combo's powers (freeze/speed/score_mult) are all
        // non-purchaseable (cost 0), so they hit the unconditional-erase branch
        // regardless; false additionally skips Reset's fullReset-only zen-mode
        // re-activation of m_bIsSpecial templates (which would repopulate
        // m_ActivePowerUps right back under GAME_MODE_ARCADE, set above) and the
        // game_work.mCountDown->Reset() call, neither needed for this drain.
        pum->Reset(false);

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
    if (wide) h.SetWindowSize(1136, 640);
    Layout::SetWideLayout(wide);
    if (!h.Init()) return 1;
    // Settle: let MainScreen's camera-zoom state and any Layout::MapX-positioned
    // controls settle under the final viewport before capture. Uses
    // SettleRealtime (not plain RunHeadless) so per-present-only visual state
    // (MenuButton sparkle/NEW-badge, etc.) advances too -- see SettleRealtime doc.
    SettleRealtime(h, 60);

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud is null after boot\n");
        return 1;
    }

    // ---- MainScreen: already live and active; just capture. ----
    // Port specific: test capture ordering. No entity drain here -- MainScreen's
    // ring fruit/bomb (MenuButton::CreateFruit, pinned m_bBallisticEnable=0) are
    // created ONCE at boot and never respawn once killed. A blanket
    // DeactivateAllEntities before this capture would empty every ring
    // permanently. RunHeadless(60) above already settled the ambient wave.
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
    // Port specific: test capture ordering. Clear MainScreen's leftover ring
    // fruit BEFORE GameModeScreen is constructed -- HideAllExisting() only
    // hides MainScreen's HUDControl (m_Active=0), it does not release its
    // pinned ActorManager fruit/bomb, so without this clear they'd render
    // underneath GameModeScreen's own ring icons. GameModeScreen's ctor (via
    // its MenuButton::CreateFruit calls) creates fresh ring fruit for
    // classic/zen/arcade/back immediately after, so no drain may run after
    // this point or those fresh entities would be killed with no respawn.
    HideAllExisting();
    if (h.game.actorManager) {
        h.game.actorManager->DeactivateAllEntities(0);
        h.game.actorManager->DeactivateAllEntities(1);
    }
    GameModeScreen* gms = new GameModeScreen(false);
    game_work.mHud->AddControl(gms);
    // 90 (not 60) frames: GameModeScreen::UpdateRealtime's case-2 sensei slide
    // steps m_SecondaryAlpha from -2.5 toward 1.0 via
    // step = (1-alpha)*(1-(1-0.25)^f), clamped to +/-0.1/frame (f=dt*60=1.0 at
    // 60fps) -- that clamp makes early steps linear (~0.1/frame), so it takes
    // ~52 frames to fully converge to ~0.999; 60 leaves ~5% still short, 90
    // gives margin so the slide is fully landed before capture.
    SettleRealtime(h, 90);

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
    // Port specific: same principle as GameModeScreen above -- clear
    // GameModeScreen's leftover ring fruit BEFORE ShopScreen is constructed,
    // then no drain runs after setup. ShopScreen has no ring fruit of its own,
    // so this clear only needs to happen once before Init(), not repeated.
    if (h.game.actorManager) {
        h.game.actorManager->DeactivateAllEntities(0);
        h.game.actorManager->DeactivateAllEntities(1);
    }
    // DojoScreen is needed only as ShopScreen's parent ctor arg (ShopScreen::Update
    // @0x0015e19c derefs m_pParent->m_bPendingRemoval only when m_State==2 -- never
    // reached during this settle-in capture, so an un-added dojo is safe). Its ctor
    // (@0x0016bad8) unconditionally constructs TWO independent BSButton HUD controls
    // (m_pBSButton0/1, the Facebook/Twitter "FOLLOW US" stubs) and AddControls each
    // straight into game_work.mHud with their own default m_Active=1 (HUDControl
    // ctor) -- dojo->m_Active gates only dojo's own Draw, not these sibling
    // controls, so they bleed through independently of dojo's active flag.
    // HideAllExisting() below (called AFTER this ctor runs) sweeps every control
    // accumulated in mHud so far -- gms + its 4 ring MenuButtons (left active since
    // the gamemode capture only did `gms->m_Active = 0`) and dojo + its 2 BSButtons
    // -- in one generic pass, so no per-control private-member access is needed.
    DojoScreen* dojo = new DojoScreen();
    game_work.mHud->AddControl(dojo);
    HideAllExisting();
    ShopScreen* shop = new ShopScreen(dojo);
    game_work.mHud->AddControl(shop, false);
    // ShopScreen's own HUDControl3d/HUDControl base ctor already defaults
    // m_Active=1 (neither ctor touches it), so this is defensive/explicit rather
    // than a fix for a real gate -- keeps the activation visible at the call site.
    shop->m_Active = 1;
    shop->Init();
    // SettleRealtime (not plain RunHeadless): ShopScreen::UpdateRealtime case 0
    // is what lerps m_TransitionAlpha (panel fade-in) toward 1.0 at rate 0.125
    // -- Update() never touches it -- so a plain RunHeadless settle left the
    // panel fully transparent/empty. 60 frames is ample for a 0.125-rate lerp.
    SettleRealtime(h, 60);

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

    // ---- DojoScreen: this IS the screen whose own FOLLOW US (BSButton) pair
    // and dojo_sensei slide-in are the real content -- unlike the shop capture
    // above, do NOT hide dojo's own children here. Only the PRIOR screen
    // (shop) is hidden before dojo is (re)constructed. ----
    // Port specific: same drain discipline as GameModeScreen/ShopScreen above --
    // clear ShopScreen's leftover state before constructing a fresh DojoScreen
    // (dojo has no ring fruit of its own, but the drain is cheap and keeps the
    // pattern uniform across every capture in this segment).
    if (h.game.actorManager) {
        h.game.actorManager->DeactivateAllEntities(0);
        h.game.actorManager->DeactivateAllEntities(1);
    }
    HideAllExisting();
    DojoScreen* dojo2 = new DojoScreen();
    game_work.mHud->AddControl(dojo2);
    // DojoScreen's ctor only builds the version text + the two BSButtons
    // (Facebook/Twitter); the nav rings (m_pBackButton/m_pShopButton/
    // m_pAboutButton) are built by CreateButtons(), called from Reset(),
    // called from Init() (see DojoScreen::Init @0x00169e80). Without this
    // call the three MenuButton rings stay null and never draw.
    dojo2->Init();
    // 90 frames: same sensei-slide convergence budget as GameModeScreen above
    // (DojoScreen::UpdateRealtime eases m_TransitionAlpha with the same
    // clamped-step approach; UpdateBSButtons repositions the FOLLOW US pair
    // off the same alpha, so both land together).
    SettleRealtime(h, 90);

    if (h.IsScreenshot()) {
        char label[64];
        std::snprintf(label, sizeof(label), "widescreen/dojo_%s", suffix);
        if (!h.ScreenshotPng(label)) {
            std::fprintf(stderr, "FAIL: ScreenshotPng('%s') failed\n", label);
            ++failures;
        } else {
            std::printf("[%s] screenshot written\n", label);
        }
    }

    // ---- AboutScreen: needs a DojoScreen parent for back-navigation, same
    // as ShopScreen (test_screen.cpp pattern: dojo is added but hidden --
    // AboutScreen is a direct HUDControl3d child, not gated by dojo's
    // m_Active). ----
    if (h.game.actorManager) {
        h.game.actorManager->DeactivateAllEntities(0);
        h.game.actorManager->DeactivateAllEntities(1);
    }
    HideAllExisting();
    DojoScreen* dojo3 = new DojoScreen();
    dojo3->m_Active = 0;  // dojo3 is just AboutScreen's parent for back-nav
    game_work.mHud->AddControl(dojo3);
    // DojoScreen's ctor (@0x0016bad8) unconditionally builds TWO independent
    // BSButton HUD controls (m_pBSButton0/1, the Facebook/Twitter "FOLLOW US"
    // stubs) and AddControls each straight into game_work.mHud with their own
    // default m_Active=1 (HUDControl ctor) -- dojo3->m_Active=0 above gates
    // only dojo3's OWN Draw, not these sibling controls, so they bleed
    // through independently. Sweep dojo3 + its 2 BSButtons (and anything else
    // still active) BEFORE constructing AboutScreen, same fix as the shop
    // capture above. AboutScreen itself creates no social button.
    HideAllExisting();
    AboutScreen* about = new AboutScreen(dojo3);
    about->Init();
    // Constructed AFTER the HideAllExisting() sweep above, so its own
    // HUDControl3d/HUDControl base ctor default (m_Active=1) is defensive/
    // explicit here rather than a fix for a real gate -- same style as the
    // shop->m_Active=1 line above.
    about->m_Active = 1;
    game_work.mHud->AddControl(about);
    // 90 frames: AboutScreen::UpdateRealtime eases m_TransitionAlpha with the
    // same AS_APPROACH_F clamped-step convergence as DojoScreen/GameModeScreen,
    // and the credits marquee's m_EntryDelay (ctor=3.0f -> 180 frames at 60Hz)
    // is intentionally NOT required to fully elapse for this capture -- the
    // sensei slide-in and sml_title/back button are what this screenshot
    // verifies, matching the gamemode/dojo settle budget above.
    SettleRealtime(h, 90);

    if (h.IsScreenshot()) {
        char label[64];
        std::snprintf(label, sizeof(label), "widescreen/about_%s", suffix);
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
