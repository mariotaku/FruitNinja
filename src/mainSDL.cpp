#include <SDL.h>
#include "config.h"
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/CrashHandler.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "game/SettingsSave.h"
#include "game/GameTaskState.h"   // GameTaskSaveOnExit (save on window-close)
#include "render/Layout.h"
#include "platform/AppDirSDL.h"
#include "platform/SaveDirSDL.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#if defined(_WIN32) && defined(_MSC_VER)
#include <crtdbg.h> // _CrtSetReportMode / _CrtSetReportFile
#include <windows.h> // SetErrorMode
#endif

// Port specific: SDL's default log output function writes to stderr on every
// platform. On Emscripten, stderr maps to console.error, which prints a full
// JS call stack per line -- useless noise for routine SDL_Log/LOG_* traffic.
// Route SDL's own logs to stdout (console.log) instead, on every platform
// (harmless on native); genuine JS errors/aborts still hit stderr/console.error
// since they never go through this callback. Format is
// "[NNNNNN][LEVEL][TAG] message" -- the 6-digit zero-padded prefix is the
// sim-tick counter (Debug::g_LogTick); [TAG] is already embedded in
// `message` by Debug::Log (src/debug/LoggerSDL.cpp).
static void FnSdlLogToStdout(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message) {
    const char* p = "INFO";
    switch (priority) {
        case SDL_LOG_PRIORITY_VERBOSE:  p = "VERBOSE"; break;
        case SDL_LOG_PRIORITY_DEBUG:    p = "DEBUG";   break;
        case SDL_LOG_PRIORITY_INFO:     p = "INFO";    break;
        case SDL_LOG_PRIORITY_WARN:     p = "WARN";    break;
        case SDL_LOG_PRIORITY_ERROR:    p = "ERROR";   break;
        case SDL_LOG_PRIORITY_CRITICAL: p = "CRITICAL";break;
        default: break;
    }
    fprintf(stdout, "[%06u][%s]%s\n", Debug::g_LogTick, p, message);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    // Port specific: construct the Game singleton and resolve data_dir/save_dir
    // up front (mirrors Game::init(), GameSDL.cpp) so GetSettingsSavePath() --
    // which reads Game::GetInstance() -- resolves to the same path
    // SaveSettings() writes to. Without this, LoadSettings() below would run
    // before any Game exists, GetInstance() would return nullptr, and settings
    // would silently load from (and only ever save to) two different paths --
    // i.e. never actually load. save_dir goes through the same shared
    // Mortar_ResolveSaveDir() resolver Game::init calls, so the two can't drift.
    Game game;
#if defined(FRUIT_PLATFORM_WEBOS)
    std::string appDir = fn_webos_app_dir();
    game.data_dir = appDir + "/Data";
#else
    game.data_dir = FN_DATA_DIR;
    std::string appDir = FN_DATA_DIR;
#endif
    game.save_dir = Mortar_ResolveSaveDir(appDir.c_str());

    // Port specific: load persisted settings. Language, motion mode,
    // sensitivity, and the FPS counter are user-settable via the in-game
    // Settings UI and persisted through SettingsSave/LoadSettings. Must run
    // after data_dir is resolved (above) and before game.init() so
    // GameInitialise's Localisation::Load step (GameInitialise.cpp) sees the
    // saved languageFlag rather than the zero-initialised default.
    LoadSettings();

    // Port specific: parse launch parameters for debug flags.
    //   --osd-sfx           : enable the per-SFX OSD readout (same as F4 at runtime)
    //   --fps               : enable the FPS counter overlay (same as F3 at runtime)
    //   --widescreen        : DIFFERS: opt-in widescreen layout enhancement (Layout::HalfWidth);
    //                         faithful 3:2 (240 half-width) stays the default when omitted.
    //   --window WxH         : Port specific: explicit initial window size, overrides
    //                         the aspect-from-setting default below (e.g. 1024x600).
    int winW = 960, winH = 640;
    bool winExplicit = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--osd-sfx") == 0) {
            FN::g_bOsdSfx = true;
        } else if (strcmp(argv[i], "--fps") == 0) {
            FN::g_ShowFps = true;
        } else if (strcmp(argv[i], "--widescreen") == 0) {
#ifndef __bada__
            Layout::SetWideLayout(true);
#endif
        } else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[i + 1], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                winW = w;
                winH = h;
                winExplicit = true;
            }
            ++i;
        }
    }

#ifndef __bada__
    // Port specific: the widescreen setting (persisted via LoadSettings above, or
    // set by --widescreen) drives the DEFAULT desktop window aspect -- otherwise
    // enabling the setting has no visible effect on the fixed 3:2 window and reads
    // as "the setting is ignored". 16:9 when on, 3:2 (960x640) when off. An explicit
    // --window WxH still wins. On mobile/web the drawable is the device screen, so
    // this desktop-window default is moot there.
    if (!winExplicit && Layout::IsWideLayout()) {
        winW = 1136;   // ~16:9 at 640 tall (1136/640 = 1.775; even width)
        winH = 640;
    }
#endif

    // Disable stdout buffering so log lines flush immediately. Without
    // this, line-buffered stdout silently drops the last few logs when
    // the process crashes (SEGV doesn't flush). Critical for debugging.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Port specific: an unattended/remote run (CI, overnight, webosbrew user
    // running headless) has nobody there to click a dialog. A crash/assert/
    // abort() must terminate the process, never HANG it behind an invisible
    // "Microsoft Visual C++ Runtime Library" modal. Route the Debug CRT's
    // error/assert/warn reports to stderr instead of a MessageBox, and stop
    // the OS's own WER crash dialog too. Do NOT remove this to "restore" the
    // dialogs -- a hang is worse than a visible crash for unattended runs.
#if defined(_WIN32) && defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    // Win32 + _DEBUG only: register an unhandled-SEH filter that prints
    // exception code, faulting address, and a symbolised stack trace
    // to stderr before the OS terminates the process. No-op elsewhere.
    FN::InstallCrashHandler();

    // Synthesize SDL_FINGER* events from SDL_MOUSE* so the InputTranslator
    // only needs to handle the touch path. SDL produces the synthetic
    // events with finger id = SDL_TOUCH_MOUSEID.
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    // ...and DISABLE the reverse (touch -> mouse). SDL's default is "1",
    // which makes a real touch ALSO emit synthetic mouse events; combined
    // with MOUSE_TOUCH_EVENTS=1 above that round-trips a single physical
    // touch back into a SECOND synthetic SDL_FINGER* event, doubling input.
    // We want exactly one touch per physical pointer action: mouse is
    // converted to touch (one-way) and the input path is touch-only.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    // Port specific: route SDL_Log output to stdout (see FnSdlLogToStdout above).
    // Registered as early as possible so every subsequent SDL log goes through it.
    SDL_LogSetOutputFunction(FnSdlLogToStdout, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("mainSDL", "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // ASM-spec v1.6.1 FruitNinja::OnAppInitializing @0x001ef9e4:
    //   calls Osp::System::PowerManager::KeepScreenOnState(true, true) once at end of
    //   app init, unconditionally, never released -- screen held on for app lifetime.
    // Port specific: SDL2 has no direct equivalent; SDL_DisableScreenSaver() once at
    //   startup matches keepScreenOn=true.
    SDL_DisableScreenSaver();

    // Fixed-function GL — matches the binary's pipeline. Picked at
    // CMake configure via FRUIT_GL_API.
#if defined(FRUIT_GL_API_ES1)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#elif defined(FRUIT_GL_API_ES2)
    // webOS TV: real GLES2 context (see gl_compat.h / engine CMakeLists.txt).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else  // FRUIT_GL_API_GL_COMPAT
    // Desktop compatibility profile — ES 1.x fixed-function superset.
    // Mesa llvmpipe provides software fallback (LIBGL_ALWAYS_SOFTWARE=1
    // + GALLIUM_DRIVER=llvmpipe) on systems without an ICD.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Port specific: Bada got a depth buffer from the platform EGL surface.
    // SDL/GLES2 must request + verify it explicitly. Request 24-bit —
    // many drivers offer no 16-bit-depth pixel format and silently fall back
    // to 0 bits rather than rounding up, which makes GL_DEPTH_TEST a no-op
    // and causes splats (z=-50) to paint over fruits (z>=+32).
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "Fruit Ninja",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) {
        LOG_ERROR("mainSDL", "Window failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Logged BEFORE the call: on Windows a software-GL fallback (Mesa
    // llvmpipe) hitting the real display adapter instead can hard-terminate
    // the process from inside SDL_GL_CreateContext with no SEH exception and
    // no chance to reach the !gl check below -- this line is the last thing
    // that survives in that case, so a bare exit code isn't silent about
    // where it died (see tests/README.md / tests/CMakeLists.txt for the
    // equivalent ctest-side env wiring).
    LOG_INFO("mainSDL", "Creating GL context (SDL_GL_CreateContext)...");
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        LOG_ERROR("mainSDL", "GL context failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    if (!gl_load_functions()) {
        LOG_ERROR("mainSDL", "Failed to load required GL functions");
        return 1;
    }

    LOG_INFO("GL", "GL Vendor: %s", (const char*)glGetString(GL_VENDOR));
    LOG_INFO("GL", "GL Renderer: %s", (const char*)glGetString(GL_RENDERER));
    LOG_INFO("GL", "GL Version: %s", (const char*)glGetString(GL_VERSION));

    // Surface the "Microsoft 1.1 software ICD" fallback to the user --
    // rendering will be broken in that case, but the game would otherwise
    // silently start with a black screen.
    if (!gl_check_runtime()) {
        LOG_ERROR("GL", "Aborting: rendering pipeline cannot proceed without a real GL driver.");
        return 1;
    }

    // Port specific: verify the driver actually granted a depth buffer.
    // SDL_GL_SetAttribute is a hint; the driver may silently grant 0 bits.
    // Without a real depth buffer GL_DEPTH_TEST is a no-op and later-drawn
    // splats would paint over fruits regardless of z values.
    {
        int gotDepth = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gotDepth);
        LOG_INFO("GL", "depth buffer bits: %d (requested 24)", gotDepth);
        if (gotDepth == 0) {
            LOG_ERROR("GL", "FATAL: context has 0 depth bits; depth test cannot occlude (splats would cover fruits).");
            return 1;
        }
    }

    if (!game.init(window, gl)) {
        LOG_ERROR("mainSDL", "Failed to init game");
        return 1;
    }

    game.run();

    // Port specific: save on normal window-close, BEFORE shutdown(). The binary
    // saves on app suspend/exit via GameTaskSaveOnExit (v1.6.1 @0x001ce170),
    // which persists without tearing the session down; the port already routes
    // SDL_APP_WILLENTERBACKGROUND there (GameSDL.cpp), so do the same for a
    // plain quit. It self-guards on the saving flag and a null HUD, so it is
    // safe if the game never booted.
    //
    // Still required now that shutdown() routes through Game::End() ->
    // GameTaskExit() -> GameExit(): GameExit deletes the HUD *before* its own
    // SaveCurrentData() call, so it can never persist mHud->Save() (per-control
    // state). This call is the only one that does, and it must run first.
    GameTaskSaveOnExit();

    game.shutdown();

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
