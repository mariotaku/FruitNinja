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
#include "platform/wii/WiiVideo.h"

#include <gccore.h>

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

// Matches GameSDL.cpp's Game::init flow (FruitNinja::OnAppInitializing) minus
// the SDL window/context handling -- GX has no window/context concept.
bool Game::init(void* win, void* gl) {
    window = win;        // unused on Wii (no SDL_Window*)
    gl_context = gl;     // unused on Wii (no SDL_GLContext)
    // inputTranslator (InputTranslatorSDL*) stays null on Wii; mainWii owns its
    // own InputTranslatorWii instance and drives input directly.

    data_dir = FN_DATA_DIR;
    Mortar::TextureManager::SetDataDir(data_dir.c_str());

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

    // Wii is a fixed 4:3 display: force the native 3:2 layout (widescreen is a
    // host/web-only enhancement) so the UI uses HalfWidth==240 positions and
    // Layout::ComputeViewport returns the FULL viewport -- the native 3:2 frame
    // then stretches to fill the real EFB (no letterbox bars) rather than being
    // aspect-fit. Overrides whatever the loaded save's widescreen pref was.
    Layout::SetWideLayout(false);

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
void Game::renderFrame(float /*alpha*/, int /*steps*/) {
    Layout::SetWindowAspect((float)s_efbW, (float)s_efbH);
    int vpX, vpY, vpW, vpH;
    Layout::ComputeViewport(s_efbW, s_efbH, &vpX, &vpY, &vpW, &vpH);
    glViewport(vpX, vpY, vpW, vpH);
    Layout::SetActiveViewport(vpX, vpY, vpW, vpH, s_efbW, s_efbH);

    Mortar::DisplayManager::GetInstance().BeginFrame();

    float savedDt = game_work.dt;
    GameTaskDraw(game_work.dt);
    game_work.dt = savedDt;

    FN::DebugFps_Draw(s_currentFps);
    OSD_Update(0.0f);
    OSD_Draw();

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
