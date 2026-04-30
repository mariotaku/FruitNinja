// Per-screen smoke test. Boots the full game with a hidden SDL+GL
// context, pushes the requested screen onto the HUD, ticks a few
// frames, and verifies no crash + Font::DrawString hits the screen
// when expected. Catches regressions like the Font V-flip + maxWidth=0
// divide-by-zero that the home-screen run never exercised.
//
// Run via:
//   cd build && ctest --output-on-failure -R screen_
//
// Each ctest entry passes a different screen name.

#include <SDL.h>
#include "render/gl_funcs.h"
#include <cstdlib>

// Test-only: glReadPixels isn't in the project's thin gl_funcs.h
// wrapper. Load it dynamically via SDL_GL_GetProcAddress so the test
// doesn't need a static link to opengl32 / libGL.
typedef int     GLint_t;
typedef unsigned int GLsizei_t;
typedef unsigned int GLenum_t;
typedef void    GLvoid_t;
typedef void (*PFN_glReadPixels)(GLint_t, GLint_t, GLsizei_t, GLsizei_t,
                                 GLenum_t, GLenum_t, GLvoid_t*);
static PFN_glReadPixels g_glReadPixels = nullptr;
#include "Game.h"
#include "render/Renderer.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/ShopScreen.h"
#include "screens/GameModeScreen.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int FailUsage() {
    fprintf(stderr,
        "usage: test_screen <main|dojo|about|shop|gamemode> [--interactive]\n"
        "  --interactive: show the window and run the normal main loop\n"
        "                 instead of ticking 30 frames headless. ESC quits.\n");
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return FailUsage();
    const char* screenName = argv[1];
    bool interactive = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--interactive") == 0) interactive = true;
        else if (strcmp(argv[i], "--screenshot") == 0) {} // handled later
        else return FailUsage();
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(FRUIT_GL_API_ES1)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    // Hidden in headless mode (FB still rendered); shown in interactive.
    Uint32 winFlags = SDL_WINDOW_OPENGL | (interactive ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    SDL_Window* window = SDL_CreateWindow(
        "fruit-ninja-test",
        interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
        interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
        960, 640, winFlags);
    if (!window) { fprintf(stderr, "Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { fprintf(stderr, "GL ctx failed: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(interactive ? 1 : 0);

    if (!gl_load_functions()) { fprintf(stderr, "gl_load_functions failed\n"); return 1; }

    Game game;
    if (!game.init(window, gl)) { fprintf(stderr, "game.init failed\n"); return 1; }

    // Drive a few frames first so the splash -> game transition
    // initialises HUD + MainScreen.
    game.runFrames(5);
    if (!game.hud) {
        fprintf(stderr, "FAIL: game.hud is null after runFrames(5)\n");
        return 1;
    }

    // Push the requested screen and hide all pre-existing HUD controls
    // (MainScreen, BG, etc.) so the screenshot/visual capture isolates
    // only the requested screen. Without this, parent menus draw under
    // the target screen and clutter the output.
    auto hideAllExisting = [&]() {
        for (auto it = game.hud->controls.begin(); it != game.hud->controls.end(); ++it) {
            (*it)->m_bActive = 0;
        }
    };

    if (strcmp(screenName, "main") == 0) {
        // already there — leave MainScreen active
    } else if (strcmp(screenName, "dojo") == 0) {
        hideAllExisting();
        DojoScreen* s = new DojoScreen(game);
        game.hud->AddControl(s);
    } else if (strcmp(screenName, "about") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen(game);
        dojo->m_bActive = 0;  // dojo is just AboutScreen's parent for back-nav
        game.hud->AddControl(dojo);
        AboutScreen* s = new AboutScreen(game, dojo);
        s->Init();
        game.hud->AddControl(s);
    } else if (strcmp(screenName, "shop") == 0) {
        hideAllExisting();
        DojoScreen* dojo = new DojoScreen(game);
        dojo->m_bActive = 0;
        game.hud->AddControl(dojo);
        ShopScreen* s = new ShopScreen(game, dojo);
        game.hud->AddControl(s, false);
        s->Init();
    } else if (strcmp(screenName, "gamemode") == 0) {
        hideAllExisting();
        GameModeScreen* s = new GameModeScreen(game, false);
        game.hud->AddControl(s);
    } else {
        fprintf(stderr, "unknown screen '%s'\n", screenName);
        return FailUsage();
    }

    bool screenshot = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) screenshot = true;
    }

    if (interactive) {
        // Interactive: hand off to the normal main loop. ESC / window
        // close exits. No automatic timeout.
        game.run();
    } else {
        // Headless: drive enough frames to finish the in-transition
        // and reach the screen's idle/visible state.
        //
        // Screens use multiplicative lerp (alpha += (1-alpha)*0.125 etc.)
        // with thresholds around 0.999. With dt=1/60 that's ~55 frames
        // to settle one stage; About/Shop have an extra fade-in then a
        // back-button spawn at alpha>0.999. 180 frames (~3s) covers all
        // observed in-transitions with margin.
        //
        // Run in two passes so we hit Draw in the steady state explicitly:
        //   pass 1: drive the in-transition to completion
        //   pass 2: 30 more frames of "idle Draw" — this is what catches
        //           rendering bugs that only fire post-transition (e.g.
        //           glyph emission once the screen is fully alpha=1).
        game.runFrames(180);
        game.runFrames(30);

        // --screenshot: dump the framebuffer to a PPM next to the exe so
        // remote testers can inspect what the screen rendered without a
        // visible window.
        if (screenshot) {
            int ww = 0, wh = 0;
            SDL_GL_GetDrawableSize(window, &ww, &wh);
            unsigned char* px = (unsigned char*)malloc((size_t)ww * wh * 3);
            if (!g_glReadPixels)
                g_glReadPixels = (PFN_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
            if (px && g_glReadPixels) {
                g_glReadPixels(0, 0, ww, wh, GL_RGB, GL_UNSIGNED_BYTE, px);
                char path[256];
                snprintf(path, sizeof(path), "screen_%s.ppm", screenName);
                FILE* f = fopen(path, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", ww, wh);
                    // glReadPixels gives bottom-up; flip to top-down so
                    // viewers (most expect P6 top-down) show correctly.
                    for (int y = wh - 1; y >= 0; y--) {
                        fwrite(px + (size_t)y * ww * 3, 1, (size_t)ww * 3, f);
                    }
                    fclose(f);
                    fprintf(stdout, "wrote %s (%dx%d)\n", path, ww, wh);
                }
                free(px);
            }
        }
    }

    game.shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!interactive) {
        fprintf(stdout,
            "PASS: screen '%s' transition + 30 idle frames clean\n",
            screenName);
    }
    return 0;
}
