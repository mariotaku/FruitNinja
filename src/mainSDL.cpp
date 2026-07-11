#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/CrashHandler.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "engine/util/LanguageArgs.h"
#include "engine/util/Localisation.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
    // Port specific: parse launch parameters for debug flags.
    //   --fps / --show-fps  : enable the FPS counter overlay (same as F3 at runtime)
    //   --osd-sfx           : enable the per-SFX OSD readout (same as F4 at runtime)
    //   --motion            : enable velocity-gated pointer slash (same as F5 at runtime)
    //   --motion-threshold=<f> : set the motion-mode cut speed threshold (px/sim-tick)
    //   --lang=<code|num>   : override language (e.g. --lang=french or --lang=3)
    int g_langOverride = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--fps") == 0 || strcmp(argv[i], "--show-fps") == 0) {
            FN::g_ShowFps = true;
        } else if (strcmp(argv[i], "--osd-sfx") == 0) {
            FN::g_bOsdSfx = true;
        } else if (strcmp(argv[i], "--motion") == 0) {
            FN::g_MotionMode = true;
        } else if (strncmp(argv[i], "--motion-threshold=", 20) == 0) {
            FN::g_MotionSpeedThreshold = strtof(argv[i] + 20, nullptr);
        } else if (strncmp(argv[i], "--lang=", 7) == 0) {
            g_langOverride = ParseLanguageArg(argv[i] + 7);
        }
    }

    // Disable stdout buffering so log lines flush immediately. Without
    // this, line-buffered stdout silently drops the last few logs when
    // the process crashes (SEGV doesn't flush). Critical for debugging.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

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

    // Fixed-function GL — matches the binary's pipeline. Picked at
    // CMake configure via FRUIT_GL_API.
#if defined(FRUIT_GL_API_ES1)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
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
        960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) {
        LOG_ERROR("mainSDL", "Window failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

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

    // Port specific: set languageFlag BEFORE game.init() so GameInitialise
    // loads the correct string table before ItemManager::LoadItemData parses
    // item titles (titles are baked at parse time, not draw time).
    if (g_langOverride >= 0) {
        static const char* const kLangNames[] = {
            "english_us", "english_uk", "french", "spanish", "german", "italian",
            "dutch", "swedish", "danish", "norwegian", "finnish", "korean",
            "japanese", "chinese", "traditional chinese", "latin spanish",
            "polish", "portuguese (pt)", "portuguese (br)", "russian",
            "arabic", "fake debug language"
        };
        game_work.languageFlag = (uint8_t)g_langOverride;
        LOG_INFO("lang", "override: flag=%d (%s)",
                 g_langOverride, kLangNames[g_langOverride]);
    }

    Game game;
    if (!game.init(window, gl)) {
        LOG_ERROR("mainSDL", "Failed to init game");
        return 1;
    }

    game.run();
    game.shutdown();

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
