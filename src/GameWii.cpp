// Port specific: Wii backend for Game -- the twin of GameSDL.cpp with no SDL.
// Provides the SAME Game:: methods GameSDL.cpp defines (init / pollInput /
// stepUpdate / setCurrentFps / renderFrame / frameTick / run / runFrames) so
// the fruit-ninja-game static lib links on the Wii target. The portable body
// of the game lives in Game.cpp; this file owns the platform plumbing.
//
// PASS 1 SCOPE: boot to a cleared screen. The render path runs the real
// GameTaskDraw call chain, but the GX draw shim (gl_funcsWii.cpp) no-ops the
// actual glDrawArrays/Elements this pass -- so the frame is just the clear
// colour. Input (WPAD IR) is driven from mainWii.cpp via InputTranslatorWii,
// not this file's pollInput (which is a no-op stub here).
//
// Only compiled when FRUIT_PLATFORM_WII is set (see root CMakeLists.txt's
// fruit-ninja-game target -- GameWii.cpp replaces GameSDL.cpp for Wii).
#ifdef FRUIT_PLATFORM_WII

#include "Game.h"
#include "game/GameWork.h"
#include "game/GameTaskState.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "asset/TextureManager.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "debug/OSD.h"
#include "config.h"
#include "render/gl_funcs.h"
#include "render/Layout.h"
#include "game/SettingsSave.h"
#include "platform/wii/WiiVideo.h"
#include "platform/wii/WiiPointer.h"
#include "platform/RenderInterp.h"

#include <gccore.h>
#include <ogc/conf.h>

// Port specific: FPS value shown by DebugFps_Draw (fed by mainWii's loop if it
// ever computes FPS; unused for the boot pass).
static float s_currentFps = 0.0f;

// Port specific: EFB dimensions, read from the real GXRModeObj chosen by
// mainWii's VIDEO_GetPreferredMode (via fn::wii::VideoMode()) in Game::init
// below. 640x480 are only the fallback/NTSC-progressive defaults -- PAL modes
// report a taller EFB (e.g. 640x528, xfbHeight 574), and hardcoding 640x480
// here previously clamped Renderer::InitGL's GX viewport/scissor to the top
// 480 rows of a taller real EFB (letterbox bars + scissored-off bottom rows),
// while mainWii's IR normalization already divided by the real (taller)
// xfbHeight -- the mismatch caused a vertical pointer offset.
static int s_efbW  = 640;
static int s_efbH  = 480;
static int s_xfbH  = 480;

// Port specific: the Wii console's own TV Aspect Ratio setting (System
// Settings > Screen), read once at boot via CONF_GetAspectRatio() in
// Game::init(). The EFB is ALWAYS a 4:3-shaped pixel grid (s_efbW/s_efbH
// above) regardless of this setting -- VIDEO's scan-out anamorphically
// stretches the WHOLE EFB horizontally to fill a 16:9 panel when the console
// is so configured. Independent of Layout::IsWideLayout() (the game-content
// widescreen CHECKBOX, a manual user choice on every platform) -- this is
// "what physical shape is the TV", not "how wide a field does the game
// render". Feeds the PAR (pixel-aspect-ratio) correction in renderFrame()'s
// ComputeViewport call so widescreen/letterbox content isn't anamorphically
// distorted on a 16:9-configured console. 4:3 (1.333) until Game::init() runs.
static float s_displayAspect = 4.0f / 3.0f;

// Matches GameSDL.cpp's Game::init flow (FruitNinja::OnAppInitializing) minus
// the SDL window/context handling -- GX has no window/context concept.
bool Game::init(void* win, void* gl) {
    window = win;        // unused on Wii (no SDL_Window*)
    gl_context = gl;     // unused on Wii (no SDL_GLContext)
    // inputTranslator (InputTranslatorSDL*) stays null on Wii; mainWii owns its
    // own InputTranslatorWii instance and drives input directly.

    data_dir = FN_DATA_DIR;
    Mortar::TextureManager::SetDataDir(data_dir.c_str());
    // Port specific: writable save dir, separate from the read-only asset
    // tree above (see Game.h save_dir comment). mainWii.cpp mkdir()s this
    // path at boot, before Game::init() runs. Wii is GX, not SDL -- it sets
    // its own save_dir directly rather than going through the shared SDL
    // Mortar_ResolveSaveDir() resolver (src/platform/SaveDirSDL.h) that the
    // host/webOS/Emscripten backends use.
    save_dir = FN_SAVE_DIR;

    // Port specific: load persisted settings (widescreen pref, motion mode,
    // sensitivity, FPS counter) -- mirrors mainSDL.cpp's LoadSettings() call.
    // No binary equivalent; must run after save_dir is set immediately above
    // (GetSettingsSavePath() reads Game::GetInstance()->save_dir on
    // FRUIT_PLATFORM_WII, see SettingsSave.cpp) and before GameInitialise
    // below so it sees the saved languageFlag rather than the zero-
    // initialised default. Previously never called on Wii at all -- the
    // widescreen pref, motion mode etc. were silently dropped every boot even
    // though SaveSettings() (mainWii.cpp's power/reset handler) wrote them.
    LoadSettings();

    // DisplayManager holds game-space dimensions (480x320), not EFB pixels.
    Mortar::DisplayManager::GetInstance().SetWindowSize(0, FN_SCREEN_H, 0, FN_SCREEN_W);

    if (!renderer.init()) {
        LOG_ERROR("GameWii", "Failed to init renderer");
        return false;
    }

    // Read the real EFB/XFB dimensions mainWii chose via
    // VIDEO_GetPreferredMode, instead of assuming the NTSC-progressive
    // 640x480 default -- PAL modes are taller (see s_efbW/s_efbH/s_xfbH
    // comment above).
    GXRModeObj* rm = (GXRModeObj*)fn::wii::VideoMode();
    if (rm) {
        s_efbW = rm->fbWidth;
        s_efbH = rm->efbHeight;
        s_xfbH = rm->xfbHeight;
    }

    // One-shot GL(->GX) state init -- matches FruitNinja::InitGL @0x00181e54.
    // Uses the real EFB dims so this doesn't re-clamp mainWii.cpp's own
    // GX_SetViewport/GX_SetScissor(0,0,fbWidth,efbHeight) call to a smaller
    // rect.
    renderer.InitGL(s_efbW, s_efbH);

    GamePreInitialise();
    SetHardware("BADA", true);
    GameInitialise(nullptr, nullptr);

    // Port specific: widescreen on Wii is a MANUAL user choice (the in-game
    // WIDESCREEN checkbox, SettingsScreen.cpp), same as every other platform
    // -- NOT auto-derived from the console's TV Aspect Ratio setting. Those
    // are two independent questions: "does the game render a wider FIELD"
    // (widescreen pref/active, Layout::SetWideLayout) vs "what physical shape
    // is the TV" (s_displayAspect below, read via CONF_GetAspectRatio() --
    // feeds the anamorphic PAR correction in renderFrame(), not this flag).
    // LoadSettings() (above) already seeded g_WideLayout/g_WideLayoutPref
    // from the save file (or left the zero-initialised default, false, on
    // first run) -- nothing to override here.
    //
    // Port specific: read the console's TV Aspect Ratio setting once here
    // (CONF_GetAspectRatio(), <ogc/conf.h> -- no binary equivalent, no NAND
    // config on Bada) into s_displayAspect. The Wii's EFB is ALWAYS a 4:3-
    // shaped pixel grid regardless of this setting -- VIDEO's scan-out
    // stretches the WHOLE EFB anamorphically (horizontally) to fill a 16:9
    // panel when the console is configured that way. renderFrame() uses
    // s_displayAspect to correct ComputeViewport's fit math for that
    // stretch (see PAR there) so widescreen/letterbox content isn't
    // horizontally distorted on a 16:9-configured console.
    s_displayAspect = (CONF_GetAspectRatio() == CONF_ASPECT_16_9) ? (16.0f / 9.0f) : (4.0f / 3.0f);

    // Port specific: pre-load pSplashTex here (same call GameInit.cpp's
    // GameUpdate makes lazily on first splashFadeTimer>0 tick) so it's already
    // resident before the main loop's first renderFrame. mainWii's boot splash
    // (SplashBootScreen) is still the visible XFB at this point, and the SD
    // texture load takes long enough that frame 1's DrawStartFade would
    // otherwise draw nothing -- DisplayManagerWii::SwapBuffers then overwrites
    // that same visible XFB with the black clear, producing a one-frame flash
    // between the boot splash and the logo fade-in. Loading now hides the
    // latency behind the still-displayed boot splash instead. Does not change
    // splash semantics: same texture, same DrawStartFade draw, same release at
    // splashFadeTimer<=0 (GameInit.cpp).
    if (!pSplashTex) {
        pSplashTex = Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex");
    }

    // Port specific: pre-load the IR hand-pointer texture (see WiiPointer.h)
    // now, alongside the other boot-common loads above, so the first
    // renderFrame's overlay draw doesn't stall on a lazy load.
    FN::wii::WiiPointer_Init();

    game_work.taskStateIndex = 0;
    running = true;
    return true;
}

// Port specific: input polling on Wii is driven from mainWii.cpp's loop
// (WPAD_ScanPads + InputTranslatorWii), not here. This method exists to match
// GameSDL's Game:: surface; quit-on-HOME is handled in mainWii.
void Game::pollInput() {
    // No-op: see file header. mainWii feeds WPAD IR into InputTranslatorWii and
    // sets `running = false` on HOME.
}

// Port specific: one simulation step. Identical to GameSDL.cpp's stepUpdate
// except inputTranslator is the SDL type (null on Wii), so the deferred-touch
// flush is skipped here -- mainWii's InputTranslatorWii::DispatchForSimTick
// runs the Wii equivalent before each stepUpdate() call.
void Game::stepUpdate() {
    ++Debug::g_LogTick;

    game_work.dt = 0.0f;
    SystemManager::GetInstance().Update(&game_work.dt);
    game_work.dt *= FN::g_DebugTimeScale;
    GameTaskUpdate(game_work.dt);
}

void Game::setCurrentFps(float fps) {
    s_currentFps = fps;
}

// Port specific: one render pass (no simulation). Mirrors GameSDL.cpp's
// renderFrame minus SDL drawable-size query + SDL_GL_SwapWindow: uses the
// real EFB dims read in Game::init (s_efbW/s_efbH) and
// DisplayManager::SwapBuffers (DisplayManagerWii.cpp) for the GX_CopyDisp/
// VIDEO present.
void Game::renderFrame(float alpha, int /*steps*/) {
    // Port specific: Layout::EffectiveAspect() -- the CONTENT aspect (1.5
    // when !IsWideLayout(), else clamped-to-[1.5,16/9] g_RawWindowAspect) --
    // must come out to EXACTLY 1.5 or EXACTLY 16/9 on Wii, a pure function of
    // the WIDESCREEN checkbox alone, decoupled from the console's physical TV
    // shape (s_displayAspect, read via CONF_GetAspectRatio() in Game::init()
    // -- a SEPARATE, independent input, see its own comment). Feeding literal
    // 16.0f/9.0f here (not s_displayAspect) makes EffectiveAspect()'s clamp a
    // no-op in both directions -- unlike host/web, where SetWindowAspect's
    // argument doubles as both "what to clamp EffectiveAspect() to" AND "what
    // ComputeViewport fits against" (the same real window there), those two
    // roles are DIFFERENT numbers on Wii and must not be conflated: the fit
    // target below uses s_displayAspect directly, independent of this call.
    Layout::SetWindowAspect(16.0f, 9.0f);

    // Port specific: fit targetAspect against the TV's TRUE physical aspect
    // (s_displayAspect) rather than the EFB's own pixel aspect -- then apply
    // an anamorphic PAR correction so the fit rect's width, expressed back in
    // real EFB pixel columns, produces that same physical aspect once
    // VIDEO's horizontal stretch is applied to it.
    //
    // Why: the EFB is ALWAYS a 4:3-ISH pixel grid (s_efbW/s_efbH -- not
    // exactly 4/3 on every mode, e.g. PAL non-interlace 640x528 is 1.212);
    // VIDEO's scan-out stretches the WHOLE EFB horizontally (anamorphically,
    // uniformly across every column) to fill a 16:9 panel when the console is
    // so configured. A full-EFB viewport's on-screen PHYSICAL aspect is
    // therefore (s_efbW*PAR)/s_efbH, which by construction equals
    // s_displayAspect exactly -- solving that for PAR gives the formula
    // below. A plain "EFB pixel aspect" fit (the SDL/web assumption, where
    // window dims ARE the true display shape) would fit against the WRONG
    // target and come out horizontally squashed/stretched whenever
    // s_displayAspect doesn't already equal s_efbW/s_efbH.
    //
    // par == 1.0 exactly when s_displayAspect == s_efbW/s_efbH (the console's
    // own EFB shape, reported as its own "native" CONF aspect) -- there is no
    // stretch to correct for, and this whole block reduces to fitting
    // directly against the EFB's real pixel dims (unchanged from before this
    // feature). Uses the real s_efbW/s_efbH ratio, NOT a hardcoded 4/3 --
    // generalises the confirmed "PAR = displayAspect/(4/3)" spec (which
    // assumes the common NTSC-progressive 640x480 EFB, exactly 4:3) to also
    // hold on PAL's non-4:3 EFB shapes (e.g. 640x528); the two formulas are
    // numerically identical on NTSC.
    const float efbAspect = (float)s_efbW / (float)s_efbH;
    const float par = s_displayAspect / efbAspect;

    int vpX, vpY, vpW, vpH;
    // ComputeViewportFitAlways (Wii-only -- see Layout.h/.cpp) fits whenever
    // IsLetterbox() is on, regardless of IsWideLayout() -- unlike the shared
    // ComputeViewport (used by host/web), which additionally requires
    // IsWideLayout() because host/web's non-wide default window is already
    // pre-sized to exactly 3:2. Wii's "window" is the TV, never pre-shaped to
    // match the content aspect, so a 3:2 game on a 16:9 TV still needs
    // fitting when LETTERBOX is on even with WIDESCREEN off.
    //
    // Feed a synthetic winW (not s_efbW) so the window-aspect ratio
    // ComputeViewportFitAlways computes internally (synthWinW/s_efbH) equals
    // s_displayAspect exactly: synthWinW = s_displayAspect * s_efbH. winH
    // stays the real s_efbH so vpH/vpY come out in true EFB pixel units
    // directly, needing no further correction (PAR is horizontal-only).
    //
    // vpW/vpX come out in "synthWinW pixel" units -- a DIFFERENT horizontal
    // scale than real EFB columns whenever par != 1 (synthWinW == s_efbW*par
    // by construction, so synthWinW > s_efbW when par>1). Converting a
    // synthWinW-space X measurement back to real EFB columns is a plain
    // proportional rescale by (s_efbW/synthWinW) = (s_efbW/(s_efbW*par)) =
    // 1/par -- so vpX/vpW are DIVIDED by par below. par == 1 (console's CONF
    // aspect matches its own EFB shape) makes synthWinW == s_efbW exactly, so
    // this whole block -- including this division -- is a no-op.
    const float synthWinW = s_displayAspect * (float)s_efbH;
    Layout::ComputeViewportFitAlways((int)(synthWinW + 0.5f), s_efbH, &vpX, &vpY, &vpW, &vpH);
    vpX = (int)((float)vpX / par + 0.5f);
    vpW = (int)((float)vpW / par + 0.5f);
    glViewport(vpX, vpY, vpW, vpH);
    Layout::SetActiveViewport(vpX, vpY, vpW, vpH, s_efbW, s_efbH);

    Mortar::DisplayManager::GetInstance().BeginFrame();

#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().ApplyForDraw(alpha);
#endif
    float savedDt = game_work.dt;
    GameTaskDraw(game_work.dt);
    game_work.dt = savedDt;
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().RestoreAfterDraw();
#endif

    FN::DebugFps_Draw(s_currentFps);
    OSD_Update(0.0f);
    OSD_Draw();

    // Port specific: Wiimote IR hand-pointer overlay, drawn topmost on every
    // screen (see WiiPointer.h). No binary equivalent.
    FN::wii::WiiPointer_Draw(fn::wii::GetInputTranslator());

    Mortar::DisplayManager::GetInstance().SwapBuffers(window);
}

// Port specific: one complete game tick -- poll, step, render. Thin wrapper,
// matches GameSDL's frameTick.
void Game::frameTick() {
    pollInput();
    stepUpdate();
    renderFrame(0.0f, 1);
}

// Port specific: main game loop. On Wii the loop lives in mainWii.cpp (it owns
// the VIDEO/GX/WPAD lifecycle + the elapsed-time clock), so Game::run() is not
// the entry point here. Provided for API symmetry with GameSDL; delegates to
// the same fixed-step shape mainWii uses, but mainWii calls the granular
// methods directly instead of this.
void Game::run() {
    while (running) {
        frameTick();
    }
}

// Test-only parity with GameSDL: run a fixed number of ticks. No wall clock.
void Game::runFrames(int frameCount) {
    for (int i = 0; i < frameCount && running; ++i) {
        stepUpdate();
        renderFrame(0.0f, 1);
    }
}

#endif // FRUIT_PLATFORM_WII
