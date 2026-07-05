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
//       fn::TestHarness h(argc, argv, "bonus/default");
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
//       if (h.IsScreenshot()) h.Screenshot();       // writes tmp/test/screenshots/<suite>/<case>.ppm
//
//       return h.Shutdown();                        // SDL/GL teardown + final PASS line
//   }
//
// Screenshot paths use a <suite>/<case> scheme: the name (or nameOverride)
// passed to Screenshot/ScreenshotPng/ScreenshotJpg may contain a single '/'
// separating suite from case. The intermediate subdirectory is created
// automatically (e.g. "gameover/classic" -> tmp/test/screenshots/gameover/classic.png).
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
#include "hud/HUD.h"
#include "hud/HUDControl.h"
#include "hud/HUDLayer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "engine/util/LanguageArgs.h"
#include "engine/util/Localisation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#endif
#include <SDL_image.h>

// glReadPixels isn't in the thin gl_funcs.h wrapper -- pull via
// SDL_GL_GetProcAddress so we don't need to link opengl32 statically.
typedef int          GLint_t;
typedef unsigned int GLsizei_t;
typedef unsigned int GLenum_t;
typedef void         GLvoid_t;
// Windows GL entry points are __stdcall (APIENTRY). On x86, calling glReadPixels
// through a default (__cdecl) pointer corrupts ESP -> _RTC_CheckEsp abort in debug
// builds. Match the platform GL calling convention (harmless/no-op off Win32 and
// on x64 where conventions are unified).
#if defined(_WIN32)
#  define FN_GL_APIENTRY __stdcall
#else
#  define FN_GL_APIENTRY
#endif
typedef void (FN_GL_APIENTRY *PFN_glReadPixels)(GLint_t, GLint_t, GLsizei_t, GLsizei_t,
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
          frames(-1),
          m_langOverride(-1),
          m_interactiveDefault(false),
          m_componentMode(false),
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
    // Parses --interactive / --screenshot / --headless / --frames N / --duration S.
    // Ignores unknown flags so the caller's main can do its own parsing pass.
    // Call once before Init().
    //
    // Flag semantics:
    //   default (no flags)     -- interactive=m_interactiveDefault, screenshot=false
    //   --interactive          -- force visible window (overrides --headless)
    //   --screenshot           -- one-shot: render (hidden window), dump image, exit
    //   --headless             -- force hidden window, no screenshot dump
    //   --frames N             -- override headless run frame count (stored in frames)
    //   --duration S           -- override headless run in seconds at 60fps (frames = S*60)
    //
    // Both --frames and --duration populate the public `frames` field. The test
    // reads frames to know how many RunHeadless iterations to drive. If neither
    // flag is given, frames remains -1 (no override; test uses its own default).
    bool ParseFlags() {
        interactive = m_interactiveDefault;
        for (int i = 1; i < argc; ++i) {
            if      (std::strcmp(argv[i], "--interactive") == 0) { interactive = true;  screenshot = false; }
            else if (std::strcmp(argv[i], "--screenshot")  == 0) { screenshot  = true;  interactive = false; }
            else if (std::strcmp(argv[i], "--headless")    == 0) { interactive = false; }
            else if (std::strncmp(argv[i], "--frames=", 9) == 0) {
                frames = std::atoi(argv[i] + 9);
            } else if (i + 1 < argc && std::strcmp(argv[i], "--frames") == 0) {
                frames = std::atoi(argv[++i]);
            } else if (std::strncmp(argv[i], "--duration=", 11) == 0) {
                float secs = (float)std::atof(argv[i] + 11);
                frames = (int)(secs * 60.0f + 0.5f);
            } else if (i + 1 < argc && std::strcmp(argv[i], "--duration") == 0) {
                float secs = (float)std::atof(argv[++i]);
                frames = (int)(secs * 60.0f + 0.5f);
            } else if (std::strncmp(argv[i], "--lang=", 7) == 0) {
                m_langOverride = ParseLanguageArg(argv[i] + 7);
            }
        }
        return true;
    }

    // Apply --lang= override: set languageFlag and reload translations.
    // Call after game.init() succeeds, before burn-in frames consume strings.
    // No-op if --lang= was not specified or the value was invalid.
    void ApplyLanguageOverride() {
        if (m_langOverride < 0) return;
        game_work.languageFlag = (uint8_t)m_langOverride;
        Localisation::Load(game.data_dir.c_str(), m_langOverride);
        // Log the applied language name for verification.
        // kLanguageSuffix is private to StringTable.cpp; use flag 0..13 directly.
        static const char* const kNames[] = {
            "english_us", "german", "dutch", "french", "spanish", "italian",
            "swedish", "danish", "norwegian", "finnish", "korean", "japanese",
            "english_uk", "chinese", "english_us"
        };
        const char* name = (m_langOverride >= 0 && m_langOverride <= 14)
                           ? kNames[m_langOverride] : "?";
        std::printf("[%s] lang override applied: flag=%d (%s)\n",
                    label, m_langOverride, name);
    }

    bool IsInteractive() const { return interactive;  }
    bool IsScreenshot()  const { return screenshot;   }
    // Returns true if --frames or --duration was parsed; test should use frames
    // instead of its own default frame count.
    bool HasFramesOverride() const { return frames >= 0; }

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

        // Set languageFlag BEFORE game.init() so GameInitialise loads the
        // correct string table before ItemManager::LoadItemData parses item
        // titles (titles are baked at parse time, not draw time).
        if (m_langOverride >= 0) {
            game_work.languageFlag = (uint8_t)m_langOverride;
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

    // -------- Component-isolation mode --------
    //
    // InitComponent() boots SDL+GL+game.init() identically to Init(), then
    // strips the HUD so NO game-state drawing happens by default. The caller
    // adds only the component under test before calling RunComponentHeadless.
    //
    // This suppresses the full GameTaskDraw / GameDraw path (background tex,
    // 3D actors, particles, slashes, MainScreen logo/shade, etc.) and renders
    // ONLY what the caller explicitly puts into the isolated HUD, on a clean
    // clear-colour background.
    //
    // How it works:
    //   1. game.init() as normal (boots game, runs initFrames burn-in so HUD is live).
    //   2. Clear game_work.mHud->controls -- removes MainScreen and all other
    //      controls the game boot added, WITHOUT deleting them (m_bNoDestructor guard).
    //   3. Set m_componentMode=true so RunComponentHeadless uses its own frame loop.
    //
    // The existing RunHeadless / RunInteractive paths are NOT affected; they call
    // game.runFrames() which calls the full GameTaskDraw as before.
    bool InitComponent() {
        if (!Init()) return false;
        m_componentMode = true;

        // Strip the game-state controls from mHud. Mark each control with
        // m_bNoDestructor=1 before clearing so HUD::Release doesn't free them
        // (they're owned by the game subsystems, not by us). We just want the
        // draw list empty so RunComponentHeadless sees a blank canvas.
        int stripped = 0;
        if (game_work.mHud) {
            std::list<HUDControl*>& ctrls = game_work.mHud->controls;
            for (std::list<HUDControl*>::iterator it = ctrls.begin();
                 it != ctrls.end(); ++it) {
                if (*it) { (*it)->m_bNoDestructor = 1; ++stripped; }
            }
            ctrls.clear();
        }
        std::printf("[%s] component mode: HUD cleared (%d controls stripped)\n",
                    label, stripped);
        return true;
    }

    // Run n frames in component-isolation mode. Each frame:
    //   - DisplayManager::BeginFrame (GL clear)
    //   - MatrixManager::SetupOrtho (same ortho as the HUD path in FruitCamera)
    //   - mHud->BeginDraw(dt) + mHud->Draw(layerMask) -- draws only what the
    //     caller added to mHud
    //   - SDL_GL_SwapWindow
    //
    // dt is fixed at 1/60 per frame (matches binary fixed-step timing).
    // layerMask defaults to all layers so any HUDControl layer is drawn.
    //
    // Does NOT call game.stepUpdate() so the game state machine does not run.
    void RunComponentHeadless(int n, int layerMask = 0x7FFFFFFF) {
        static const float kDt = 1.0f / 60.0f;
        for (int i = 0; i < n; ++i) {
            // Drain quit events so the window close button works in interactive use.
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) return;
            }

            int ww = 0, wh = 0;
            SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
            glViewport(0, 0, ww, wh);

            // Clear to the game's default clear colour (black).
            Mortar::DisplayManager::GetInstance().BeginFrame();

            // Set up the same ortho projection the HUD path uses.
            // Matches FruitCamera::SetupPerspective(PT_STANDARD) ortho branch
            // and Renderer::SetupOrtho.
            MatrixManager::GetInstance().SetupOrtho(
                160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

            // Drive the component: Update propagates animation state (phase
            // timers, per-award scales, score counters), then Draw renders it.
            // HUD::Update may mark controls for removal (m_bPendingRemoval) and
            // delete them -- callers that want to hold a screen past its dismiss
            // point should suppress m_bPendingRemoval before each call.
            if (game_work.mHud) {
                game_work.mHud->Update(kDt);
                game_work.mHud->BeginDraw(kDt);
                game_work.mHud->Draw(layerMask);
            }

            SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
        }
    }

    // Like RunComponentHeadless but issues the game's per-layer ordered passes each
    // frame, matching GameDraw's separate HUD::Draw(layer) calls. This is required
    // for multi-pass self-demoting controls like MenuButton: BeginDraw re-arms 0x40,
    // the 0x40 pass draws the scratch backdrop and demotes m_LayerFlags to 0x80,
    // then the 0x80 pass draws the button face and label. A single all-bits-mask
    // pass misses the second visit -- the face/label never renders.
    //
    // Per-frame order: ONE Update + ONE BeginDraw, then one Draw() per layer bit.
    //
    // Scale reset (mirrors GameDraw @0x001cd720 FIX 1): after the 0x01 tinted pass and
    // before the 0x08/0x100/0x200/0x400 overlay passes, reset scales[0..2] = 1.0f.
    // RunComponentHeadlessMultiPass does not call PowerUpManager::Update/SetDefaults,
    // so scales from the burn-in frames persist. If a ScreenEffect was active during
    // the last burn-in frame (fade=0 -> scales *= 0), overlay controls (combo icons,
    // sensei head) would render black. The reset matches the state GameDraw establishes
    // for overlay rendering.
    void RunComponentHeadlessMultiPass(int n) {
        static const float kDt = 1.0f / 60.0f;
        // Pre-overlay layers: 0x40 (MenuButton scratch bg), 0x80 (PostActor / face+label),
        // 0x01 (DEFAULT -- GameOverScreen body, MainScreen logo).
        static const int kLayersPre[]  = { 0x40, 0x80, 0x01 };
        // Overlay layers: 0x08 (combo icons / buttons), 0x100, 0x200, 0x400.
        static const int kLayersPost[] = { 0x08, 0x100, 0x200, 0x400 };
        for (int i = 0; i < n; ++i) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) return;
            }

            int ww = 0, wh = 0;
            SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
            glViewport(0, 0, ww, wh);

            Mortar::DisplayManager::GetInstance().BeginFrame();
            MatrixManager::GetInstance().SetupOrtho(
                160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

            if (game_work.mHud) {
                game_work.mHud->Update(kDt);
                game_work.mHud->BeginDraw(kDt);
                // Pre-overlay passes (may be tinted by ScreenEffect scales).
                for (int L = 0; L < (int)(sizeof(kLayersPre) / sizeof(kLayersPre[0])); ++L) {
                    game_work.mHud->Draw(kLayersPre[L]);
                }
                // Mirror GameDraw @0x001cd720: reset scales to 1.0f before overlay passes.
                game_work.mHud->scales[0] = 1.0f;
                game_work.mHud->scales[1] = 1.0f;
                game_work.mHud->scales[2] = 1.0f;
                // Overlay passes (combo icons, sensei head, pause overlays, fade modal).
                for (int L = 0; L < (int)(sizeof(kLayersPost) / sizeof(kLayersPost[0])); ++L) {
                    game_work.mHud->Draw(kLayersPost[L]);
                }
            }

            SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
        }
    }

    // Interactive component-isolation loop. ESC / close exits. on_tick runs
    // once per frame before the component draw. layerMask defaults to all layers.
    void RunComponentInteractive(OnTickFn on_tick, void* userdata = NULL,
                                 int maxFrames = -1,
                                 int layerMask = 0x7FFFFFFF) {
        static const float kDt = 1.0f / 60.0f;
        int frame = 0;
        bool running_ = true;
        while (running_) {
            if (maxFrames >= 0 && frame >= maxFrames) break;
            if (on_tick && !on_tick(game, frame, userdata)) break;

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) { running_ = false; break; }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) { running_ = false; break; }
            }
            if (!running_) break;

            int ww = 0, wh = 0;
            SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &ww, &wh);
            glViewport(0, 0, ww, wh);

            Mortar::DisplayManager::GetInstance().BeginFrame();
            MatrixManager::GetInstance().SetupOrtho(
                160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

            if (game_work.mHud) {
                game_work.mHud->Update(kDt);
                game_work.mHud->BeginDraw(kDt);
                game_work.mHud->Draw(layerMask);
            }

            SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
            SDL_Delay(16); // ~60 Hz pace for interactive viewing
            ++frame;
        }
        std::printf("[%s] component interactive exit after %d frames\n", label, frame);
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
    // Writes tmp/test/screenshots/<suite>/<case>.ppm. Returns true on success.
    // The name (nameOverride or label) may contain a '/' to place the file in
    // a suite subdirectory, e.g. "gameover/classic" ->
    // tmp/test/screenshots/gameover/classic.ppm.
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

        const char* name = nameOverride ? nameOverride : label;
        MakeScreenshotDir_(name);
        char path[256];
        std::snprintf(path, sizeof(path), "tmp/test/screenshots/%s.ppm", name);
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

    // Writes tmp/test/screenshots/<suite>/<case>.png (compressed PNG, RGB, top-down).
    // The name (nameOverride or label) may contain a '/' for suite/case nesting.
    // Uses SDL2_image IMG_SavePNG. Returns true on success.
    bool ScreenshotPng(const char* nameOverride = NULL) {
        unsigned char* pixels = _ReadPixelsFlipped(NULL, NULL);
        if (!pixels) return false;
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);

        const char* name = nameOverride ? nameOverride : label;
        MakeScreenshotDir_(name);
        char path[256];
        std::snprintf(path, sizeof(path), "tmp/test/screenshots/%s.png", name);

        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
            pixels, ww, wh,
            24,            // bits per pixel
            ww * 3,        // pitch (bytes per row)
            0x000000FFu,   // Rmask
            0x0000FF00u,   // Gmask
            0x00FF0000u,   // Bmask
            0u);           // Amask (none -- 24-bit RGB)
        if (!surf) {
            std::fprintf(stderr, "[%s] SDL_CreateRGBSurfaceFrom failed: %s\n",
                         label, SDL_GetError());
            std::free(pixels);
            return false;
        }
        int rc = IMG_SavePNG(surf, path);
        SDL_FreeSurface(surf);
        std::free(pixels);
        if (rc != 0) {
            std::fprintf(stderr, "[%s] IMG_SavePNG failed: %s\n", label, IMG_GetError());
            return false;
        }
        std::printf("[%s] wrote %s (%dx%d)\n", label, path, ww, wh);
        return true;
    }

    // Writes tmp/test/screenshots/<suite>/<case>.jpg (JPEG, RGB, top-down).
    // The name (nameOverride or label) may contain a '/' for suite/case nesting.
    // quality: 0-100 (default 90). Uses SDL2_image IMG_SaveJPG.
    bool ScreenshotJpg(const char* nameOverride = NULL, int quality = 90) {
        unsigned char* pixels = _ReadPixelsFlipped(NULL, NULL);
        if (!pixels) return false;
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);

        const char* name = nameOverride ? nameOverride : label;
        MakeScreenshotDir_(name);
        char path[256];
        std::snprintf(path, sizeof(path), "tmp/test/screenshots/%s.jpg", name);

        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
            pixels, ww, wh,
            24,
            ww * 3,
            0x000000FFu,
            0x0000FF00u,
            0x00FF0000u,
            0u);
        if (!surf) {
            std::fprintf(stderr, "[%s] SDL_CreateRGBSurfaceFrom failed: %s\n",
                         label, SDL_GetError());
            std::free(pixels);
            return false;
        }
        int rc = IMG_SaveJPG(surf, path, quality);
        SDL_FreeSurface(surf);
        std::free(pixels);
        if (rc != 0) {
            std::fprintf(stderr, "[%s] IMG_SaveJPG failed: %s\n", label, IMG_GetError());
            return false;
        }
        std::printf("[%s] wrote %s (%dx%d, quality=%d)\n", label, path, ww, wh, quality);
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
    // Set by --frames N or --duration S in ParseFlags(). -1 = no override.
    // Tests should check HasFramesOverride() and use frames instead of their
    // own default when it is >= 0.
    int          frames;

private:
    // Language flag resolved from --lang=<code|num>. -1 = no override.
    int  m_langOverride;
    bool m_interactiveDefault;
    // Set by InitComponent(); signals that the HUD was cleared for isolation mode.
    // Informational only -- the actual isolation is enforced by not calling
    // game.runFrames() in RunComponentHeadless.
    bool m_componentMode;
    // Per-instance glReadPixels pointer. Loaded lazily via
    // SDL_GL_GetProcAddress on first use; cached for subsequent calls.
    PFN_glReadPixels m_glReadPixels;

    // Read the framebuffer and flip bottom-up -> top-down.
    // Returns a malloc'd (ww*wh*3)-byte RGB buffer; caller free()s.
    // Writes dimensions to *outW / *outH if non-null.
    unsigned char* _ReadPixelsFlipped(int* outW, int* outH) {
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);
        if (outW) *outW = ww;
        if (outH) *outH = wh;
        if (!m_glReadPixels) {
            m_glReadPixels = (PFN_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
        }
        if (!m_glReadPixels) {
            std::fprintf(stderr, "[%s] glReadPixels unavailable\n", label);
            return NULL;
        }
        const unsigned int GL_RGB_           = 0x1907;
        const unsigned int GL_UNSIGNED_BYTE_ = 0x1401;
        size_t rowBytes = (size_t)ww * 3;
        size_t totalBytes = rowBytes * (size_t)wh;
        // Read bottom-up into a temp buffer, then flip into the final buffer.
        unsigned char* tmp  = (unsigned char*)std::malloc(totalBytes);
        unsigned char* flip = (unsigned char*)std::malloc(totalBytes);
        if (!tmp || !flip) {
            std::free(tmp);
            std::free(flip);
            return NULL;
        }
        m_glReadPixels(0, 0, ww, wh, GL_RGB_, GL_UNSIGNED_BYTE_, tmp);
        for (int y = 0; y < wh; ++y) {
            std::memcpy(flip + (size_t)y * rowBytes,
                        tmp  + (size_t)(wh - 1 - y) * rowBytes,
                        rowBytes);
        }
        std::free(tmp);
        return flip;
    }

    // Create tmp/test/screenshots/ and any subdirectory implied by 'name'.
    // If 'name' contains a '/' (e.g. "gameover/classic"), the part before
    // the slash is treated as a suite subdirectory and is created under
    // tmp/test/screenshots/. Names without '/' only create the base dir.
    static void MakeScreenshotDir_(const char* name = NULL) {
#ifdef _WIN32
        _mkdir("tmp"); _mkdir("tmp/test"); _mkdir("tmp/test/screenshots");
#else
        mkdir("tmp", 0755); mkdir("tmp/test", 0755); mkdir("tmp/test/screenshots", 0755);
#endif
        if (!name) return;
        const char* slash = std::strchr(name, '/');
        if (!slash) return;
        // Build "tmp/test/screenshots/<suite>" and mkdir it.
        char suiteDir[256];
        int suiteLen = (int)(slash - name);
        std::snprintf(suiteDir, sizeof(suiteDir), "tmp/test/screenshots/%.*s", suiteLen, name);
#ifdef _WIN32
        _mkdir(suiteDir);
#else
        mkdir(suiteDir, 0755);
#endif
    }
};

} // namespace fn

#endif // FN_TEST_HARNESS_H
