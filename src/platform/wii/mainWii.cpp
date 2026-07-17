// Port specific: Wii entry point -- SCAFFOLDING ONLY, not a working build.
//
// Mirrors mainSDL.cpp / mainEmscripten.cpp's structure: fixed-timestep loop
// driving Game::pollInput/stepUpdate/renderFrame via the shared
// fn::FixedStepDriver (src/platform/FixedStepDriver.h). Unlike those two
// backends there is no SDL_Window*/SDL_GLContext -- GX has no windowing or
// GL-context concept, so Game::init(void*, void*) is called with placeholder
// opaque pointers (both parameters are already `void*`/opaque per Game.h's
// comment "win = SDL_Window*, gl = SDL_GLContext (opaque to header)").
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt) and only links successfully with
// devkitPPC + libogc2 present -- neither is available in this repo/session.
// This file will NOT compile as-is; every libogc2 call is a
// // TODO(wii): marker, not a real header include, so it does not
// accidentally get pulled into a non-Wii TU scan.
#ifdef FRUIT_PLATFORM_WII

// TODO(wii): #include <gccore.h>      -- GX, VIDEO, core libogc types
// TODO(wii): #include <wiiuse/wpad.h> -- Wiimote input
// TODO(wii): #include <fat.h>         -- libfat filesystem init
// TODO(wii): #include <ogc/lwp_watchdog.h> -- gettime()-based real elapsed ms

#include "Game.h"
#include "platform/FixedStepDriver.h"
#include "platform/wii/gx/GxRenderer.h"
#include "platform/wii/InputTranslatorWii.h"
#include "debug/Logger.h"

// Port specific: Game instance lives as a file-static, matching
// mainEmscripten.cpp's g_game (the C main-loop callback needs a stable
// pointer; here it's just main()'s own local loop, but keeping the same
// shape as the other backends eases future diffing).
static Game g_game;
static fn::FixedStepDriver g_driver;

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // TODO(wii): VIDEO_Init(); read VIDEO_GetPreferredMode into an
    // GXRModeObj*; allocate + clear double-buffered external framebuffers
    // (MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)) x2); VIDEO_Configure;
    // VIDEO_SetNextFramebuffer; VIDEO_SetBlack(FALSE); VIDEO_Flush;
    // VIDEO_WaitVSync (x2 if interlaced) -- standard libogc2 boot sequence.

    // TODO(wii): fatInitDefault() -- mounts sd:/ and usb:/ via libfat before
    // any Mortar::IFileSystem is registered (FileSystemWii.cpp's
    // FileSystem_Direct subclass needs this to have already run).

    // TODO(wii): WPAD_Init(); WPAD_SetDataFormat(WPAD_CHAN_ALL,
    // WPAD_FMT_BTNS_ACC_IR); WPAD_SetVRes(chan, screenWidth, screenHeight)
    // for each of the 4 channels -- see InputTranslatorWii.h for the IR ->
    // finger-channel mapping this feeds.

    // TODO(wii): GX fifo setup -- allocate a ~256KB fifo buffer
    // (MEM_K0_TO_K1(memalign(32, fifoSize))), GX_Init(fifo, fifoSize),
    // then fn::wii::GxInit(xfb, rmode) (src/platform/wii/gx/GxRenderer.h)
    // for the GX_SetCopyClear/GX_SetViewport/TEV-stage one-time setup.

    LOG_INFO("mainWii", "Wii entry point reached (scaffolding -- no GX/VIDEO init yet)");

    // TODO(wii): once GX/VIDEO/WPAD/libfat are actually initialised above,
    // pass real opaque handles here if the Wii Game::init path ever needs
    // them; today Game::init(void*, void*) only uses its params to pass
    // through to SDL-specific setup that doesn't exist on this backend, so
    // nullptr is correct for scaffolding purposes.
    if (!g_game.init(nullptr, nullptr)) {
        LOG_ERROR("mainWii", "Failed to init game");
        return 1;
    }

    static InputTranslatorWii inputTranslator;
    inputTranslator.Init();

    // TODO(wii): real elapsed-time source. libogc2 provides gettime()/
    // ticks_to_millisecs() (ogc/lwp_watchdog.h) for a monotonic clock;
    // mainEmscripten.cpp's emscripten_get_now()-driven accumulator is the
    // model to follow (same fn::FixedStepDriver, same maxSteps=5 spiral-of-
    // death guard).
    double lastTimeMs = 0.0;

    while (g_game.running) {
        // TODO(wii): WPAD_ScanPads(); read each channel's IR data via
        // WPAD_IR(chan, &ir) and feed inputTranslator.DrainWiimoteIR(chan, ir)
        // -- see InputTranslatorWii.h. Also poll WPAD_ButtonsDown(WPAD_CHAN_0)
        // for HOME-button-quit (binary has no equivalent; host-only affordance
        // like the SDL backend's window-close handling).
        g_game.pollInput();
        if (!g_game.running) {
            break;
        }

        // TODO(wii): real elapsed ms since last frame (see clock-source TODO
        // above). Placeholder 0.0 below means the driver never advances --
        // this loop body is structural only until a real clock is wired in.
        double nowMs = lastTimeMs;
        double elapsedMs = nowMs - lastTimeMs;
        lastTimeMs = nowMs;

        int steps = g_driver.advance(elapsedMs);
        for (int i = 0; i < steps && g_game.running; ++i) {
            inputTranslator.DispatchForSimTick();
            g_game.stepUpdate();
        }

        // TODO(wii): fn::wii::GxBeginFrame() (gx/GxRenderer.h) before
        // Game::renderFrame() so the GX equivalent of the GL frame-begin
        // state reset runs; GxSwapBuffers() after, replacing
        // SDL_GL_SwapWindow (which renderFrame() calls internally on the
        // SDL backend and which does not exist here).
        g_game.renderFrame(static_cast<float>(g_driver.alpha()), steps);
    }

    g_game.shutdown();

    // TODO(wii): WPAD_Shutdown(); GX/VIDEO teardown is normally skipped on
    // Wii homebrew (the loader/HBC handles it), matching how mainSDL.cpp's
    // post-loop teardown is "kept for symmetry" rather than load-bearing.
    return 0;
}

#endif // FRUIT_PLATFORM_WII
