// test_harness.h -- shared scaffolding for screen / gameplay smoke tests.
//
// Factors out the ~80 lines of duplicated SDL+GL+window+Game boilerplate
// each test was carrying, plus the --interactive / --screenshot flag
// parsing and the glReadPixels-based PPM dumper.
//
// Usage:
//   #include "test_harness.h"
//
//   int main(int argc, char* argv[]) {
//       fn::TestHarness h(argc, argv, "bonus_screen");
//       if (!h.ParseFlags()) return 1;        // handles --interactive, --screenshot, --extra-flag if you AddFlag()'d
//       if (!h.Init()) return 1;              // SDL + GL + game.init + 5 burn-in frames
//
//       // ...test-specific state setup using h.game...
//
//       if (h.IsInteractive()) {
//           h.RunInteractive(/*on_tick=*/nullptr);  // or pass a callback that runs once per frame
//       } else {
//           h.RunHeadless(210);                     // 180 transition + 30 idle frames
//           // ...assertions...
//       }
//       if (h.IsScreenshot()) h.Screenshot();       // writes tmp/test/screenshots/<label>.ppm
//
//       return h.Shutdown();                        // SDL/GL teardown + final PASS line
//   }
//
// The harness is header-only / inline; no separate .cpp. Header-only keeps
// link-line surgery off the test CMake list. Cross-build (asm-verify
// toolchain) doesn't see this file -- tests stay out of the symbol diff.

#ifndef FN_TEST_HARNESS_H
#define FN_TEST_HARNESS_H

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "engine/audio/SoundManager.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#endif

// glReadPixels isn't in the thin gl_funcs.h wrapper -- pull via
// SDL_GL_GetProcAddress so we don't need to link opengl32 statically.
typedef int          GLint_t;
typedef unsigned int GLsizei_t;
typedef unsigned int GLenum_t;
typedef void         GLvoid_t;
typedef void (*PFN_glReadPixels)(GLint_t, GLint_t, GLsizei_t, GLsizei_t,
                                 GLenum_t, GLenum_t, GLvoid_t*);

namespace fn {

// Per-tick callback for interactive mode. Returns false to break the loop
// early (e.g. internal state-machine condition). Returning true continues.
typedef bool (*OnTickFn)(Game& game, int frame, void* userdata);

struct TestHarness {
    // -------- ctor / opts --------
    TestHarness(int argc_, char** argv_, const char* label_)
        : argc(argc_), argv(argv_), label(label_),
          interactive(false), screenshot(false),
          window(NULL), gl(NULL),
          initFrames(5),
          m_interactiveDefault(false),
          m_glReadPixels(NULL)
    {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    // Port specific: standalone scene tool.
    // Call before ParseFlags() to opt a scene into visible-by-default mode.
    // With interactiveDefault=true: the window starts visible unless --screenshot
    // or --headless is passed. The standard test workflow (no flags = headless
    // assertions) is unchanged for callers that never call this.
    void SetInteractiveDefault(bool v) { m_interactiveDefault = v; }

    ~TestHarness() {
        Shutdown();
    }

    // Set custom number of init burn-in frames before the test body runs.
    // Default 5 covers the splash transition. test_screen needs ~180 for
    // the in-transition; test_bonus_phase needs ~120 for HUD live.
    void SetInitFrames(int n) { initFrames = n; }

    // -------- arg parsing --------
    // Parses --interactive / --screenshot / --headless.
    // Ignores unknown flags so the caller's main can do its own parsing pass.
    // Call once before Init().
    //
    // Flag semantics:
    //   default (no flags)     -- interactive=m_interactiveDefault, screenshot=false
    //   --interactive          -- force visible window (overrides --headless)
    //   --screenshot           -- headless one-shot: run hidden, dump PPM, exit
    //   --headless             -- force hidden window, no screenshot dump
    bool ParseFlags() {
        interactive = m_interactiveDefault;
        for (int i = 1; i < argc; ++i) {
            if      (std::strcmp(argv[i], "--interactive") == 0) { interactive = true;  screenshot = false; }
            else if (std::strcmp(argv[i], "--screenshot")  == 0) { screenshot  = true;  interactive = false; }
            else if (std::strcmp(argv[i], "--headless")    == 0) { interactive = false; }
        }
        return true;
    }

    bool IsInteractive() const { return interactive;  }
    bool IsScreenshot()  const { return screenshot;   }

    // -------- init --------
    bool Init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
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
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   16);

        Uint32 winFlags = SDL_WINDOW_OPENGL | (interactive ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
        char title[64];
        std::snprintf(title, sizeof(title), "fruit-ninja-test:%s", label);
        window = SDL_CreateWindow(
            title,
            interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
            interactive ? SDL_WINDOWPOS_CENTERED : SDL_WINDOWPOS_UNDEFINED,
            960, 640, winFlags);
        if (!window) {
            std::fprintf(stderr, "Window failed: %s\n", SDL_GetError());
            SDL_Quit();
            return false;
        }

        gl = SDL_GL_CreateContext(window);
        if (!gl) {
            std::fprintf(stderr, "GL ctx failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window); window = NULL;
            SDL_Quit();
            return false;
        }
        SDL_GL_SetSwapInterval(interactive ? 1 : 0);

        if (!gl_load_functions()) {
            std::fprintf(stderr, "gl_load_functions failed\n");
            return false;
        }

        if (!game.init(window, gl)) {
            std::fprintf(stderr, "game.init failed\n");
            return false;
        }

        // Headless tests have no human listener; silence the SFX channel.
        // Interactive tests keep audio so the tester can hear what fires.
        if (!interactive) {
            Mortar::SoundManager::GetInstance().SetSFXVolume(0.0f);
        }

        game.runFrames(initFrames);
        return true;
    }

    // -------- run --------
    void RunHeadless(int frames) {
        for (int i = 0; i < frames; ++i) game.runFrames(1);
    }

    // Interactive event loop. ESC / close exits. on_tick (if non-null) runs
    // once per frame before game.runFrames(1) and may return false to break
    // out early. Caps at maxFrames so a stuck-window test doesn't hang CI;
    // pass -1 for no cap.
    void RunInteractive(OnTickFn on_tick, void* userdata = NULL, int maxFrames = -1) {
        int frame = 0;
        while (game.running) {
            if (maxFrames >= 0 && frame >= maxFrames) break;
            if (on_tick && !on_tick(game, frame, userdata)) break;
            game.runFrames(1);
            ++frame;
        }
        std::printf("[%s] interactive exit after %d frames (%s)\n",
                    label, frame, game.running ? "max-frames cap" : "window closed");
    }

    // -------- screenshot --------
    // Writes tmp/test/screenshots/<label>.ppm. Returns true on success.
    bool Screenshot(const char* nameOverride = NULL) {
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        unsigned char* pixels = (unsigned char*)std::malloc((size_t)ww * wh * 3);
        if (!pixels) return false;
        if (!m_glReadPixels) {
            m_glReadPixels = (PFN_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
        }
        if (!m_glReadPixels) {
            std::fprintf(stderr, "[%s] glReadPixels unavailable\n", label);
            std::free(pixels);
            return false;
        }
        const unsigned int GL_RGB_           = 0x1907;
        const unsigned int GL_UNSIGNED_BYTE_ = 0x1401;
        m_glReadPixels(0, 0, ww, wh, GL_RGB_, GL_UNSIGNED_BYTE_, pixels);

#ifdef _WIN32
        _mkdir("tmp"); _mkdir("tmp/test"); _mkdir("tmp/test/screenshots");
#else
        mkdir("tmp", 0755); mkdir("tmp/test", 0755); mkdir("tmp/test/screenshots", 0755);
#endif
        char path[256];
        std::snprintf(path, sizeof(path), "tmp/test/screenshots/%s.ppm",
                      nameOverride ? nameOverride : label);
        FILE* f = std::fopen(path, "wb");
        if (!f) { std::free(pixels); return false; }
        std::fprintf(f, "P6\n%d %d\n255\n", ww, wh);
        // Flip bottom-up -> top-down for viewers.
        for (int y = wh - 1; y >= 0; --y) {
            std::fwrite(pixels + (size_t)y * ww * 3, 1, (size_t)ww * 3, f);
        }
        std::fclose(f);
        std::printf("[%s] wrote %s (%dx%d)\n", label, path, ww, wh);
        std::free(pixels);
        return true;
    }

    // Pixel readback for assertion-style checks. Caller manages pixels buffer.
    // Returns (ww * wh * 3)-byte RGB buffer or NULL. Caller free()s.
    unsigned char* ReadPixels(int* outW, int* outH) {
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        if (outW) *outW = ww;
        if (outH) *outH = wh;
        unsigned char* pixels = (unsigned char*)std::malloc((size_t)ww * wh * 3);
        if (!pixels) return NULL;
        if (!m_glReadPixels) {
            m_glReadPixels = (PFN_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
        }
        if (!m_glReadPixels) { std::free(pixels); return NULL; }
        const unsigned int GL_RGB_           = 0x1907;
        const unsigned int GL_UNSIGNED_BYTE_ = 0x1401;
        m_glReadPixels(0, 0, ww, wh, GL_RGB_, GL_UNSIGNED_BYTE_, pixels);
        return pixels;
    }

    // -------- shutdown --------
    int Shutdown() {
        // Port specific: game teardown must precede SDL_Quit so that
        // SoundManager::SFXStop (called from GameSound::KillAll inside
        // GameDestroy) does not call SDL_LockAudioDevice on a dead audio
        // device. Game::shutdown() is idempotent (static flag guard) so the
        // subsequent ~Game() call from ~TestHarness is a safe no-op.
        game.shutdown();
        if (gl)     { SDL_GL_DeleteContext(gl); gl = NULL; }
        if (window) { SDL_DestroyWindow(window); window = NULL; }
        SDL_Quit();
        return 0;
    }

    // -------- public state --------
    int          argc;
    char**       argv;
    const char*  label;
    bool         interactive;
    bool         screenshot;
    SDL_Window*  window;
    SDL_GLContext gl;
    Game         game;
    int          initFrames;

private:
    bool m_interactiveDefault;
    // Per-instance glReadPixels pointer. Loaded lazily via
    // SDL_GL_GetProcAddress on first use; cached for subsequent calls.
    PFN_glReadPixels m_glReadPixels;
};

} // namespace fn

#endif // FN_TEST_HARNESS_H
