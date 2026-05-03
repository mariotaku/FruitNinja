#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "render/Renderer.h"
#include "debug/CrashHandler.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Disable stdout buffering so log lines flush immediately. Without
    // this, line-buffered stdout silently drops the last few logs when
    // the process crashes (SEGV doesn't flush). Critical for debugging.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Win32 + _DEBUG only: register an unhandled-SEH filter that prints
    // exception code, faulting address, and a symbolised stack trace
    // to stderr before the OS terminates the process. No-op elsewhere.
    FN::InstallCrashHandler();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
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
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "Fruit Ninja",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "Window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fprintf(stderr, "GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    if (!gl_load_functions()) {
        fprintf(stderr, "Failed to load required GL functions\n");
        return 1;
    }

    printf("GL Vendor: %s\n", (const char*)glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("GL Version: %s\n", (const char*)glGetString(GL_VERSION));

    // Surface the "Microsoft 1.1 software ICD" fallback to the user --
    // rendering will be broken in that case, but the game would otherwise
    // silently start with a black screen.
    if (!gl_check_runtime()) {
        fprintf(stderr, "Aborting: rendering pipeline cannot proceed without a real GL driver.\n");
        return 1;
    }

    Game game;
    if (!game.init(window, gl)) {
        fprintf(stderr, "Failed to init game\n");
        return 1;
    }

    game.run();
    game.shutdown();

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
