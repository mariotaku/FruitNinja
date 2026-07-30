// test_harness.h -- shared scaffolding for screen / gameplay smoke tests.
//
// Factors out the ~80 lines of duplicated SDL+GL+window+Game boilerplate
// each test was carrying, plus the --interactive / --screenshot flag
// parsing and the glReadPixels-based PNG dumper.
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
//       if (h.IsScreenshot()) h.ScreenshotPng();    // writes <FN_TEST_SCREENSHOT_DIR>/<suite>/<case>.png
//
//       return h.Shutdown();                        // SDL/GL teardown + final PASS line
//   }
//
// PNG is the single framebuffer-capture format:
//   ScreenshotPng(name)  -- capture the live framebuffer to a PNG.
//   SavePng(surf, name)  -- save a caller-composed SDL_Surface to a PNG
//                           (font/text grid tests that build their own image).
// Both share the <suite>/<case> path convention below.
//
// Screenshot paths use a <suite>/<case> scheme: the name (or nameOverride)
// passed to ScreenshotPng/SavePng may contain a single '/' separating suite
// from case. The intermediate subdirectory is created automatically
// (e.g. "gameover/classic" -> <FN_TEST_SCREENSHOT_DIR>/gameover/classic.png).
//
// FN_TEST_SCREENSHOT_DIR is a compile definition (see tests/CMakeLists.txt's
// fn_add_game_test macro) carrying the absolute path
// "${CMAKE_SOURCE_DIR}/tmp/test/screenshots" -- the project's gitignored
// tmp/ tree, NOT the CMake binary/build dir. Falls back to the relative
// "tmp/test/screenshots" (resolved against the process CWD) if undefined.
//
// The harness is header-only / inline; no separate .cpp. Header-only keeps
// link-line surgery off the test CMake list. Cross-build (asm-verify
// toolchain) doesn't see this file -- tests stay out of the symbol diff.

#ifndef FN_TEST_HARNESS_H
#define FN_TEST_HARNESS_H

#include <SDL.h>
#include "render/gl_funcs.h"
#include "Game.h"
#include "debug/CrashHandler.h"
#include "engine/audio/SoundManager.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "hud/HUDControl.h"
#include "hud/HUDLayer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/Renderer.h"   // Flush2D readback barrier (stage-2 2D batching)
#include "engine/util/LanguageArgs.h"
#include "engine/util/Localisation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  include <process.h>  // _getpid
#else
#  include <unistd.h>   // getpid
#endif
#include <SDL_image.h>

// Base directory screenshots are written under. FN_TEST_SCREENSHOT_DIR is
// an absolute path supplied by tests/CMakeLists.txt's fn_add_game_test macro
// (${CMAKE_SOURCE_DIR}/tmp/test/screenshots), keeping captures in the
// project's gitignored tmp/ tree instead of the CMake binary dir. The
// relative fallback covers a non-CMake / unknown build of this header.
#ifndef FN_TEST_SCREENSHOT_DIR
#define FN_TEST_SCREENSHOT_DIR "tmp/test/screenshots"
#endif

// Base directory the per-test hermetic save-dir override (task #124) is
// created under. FN_TEST_SAVE_DIR is an absolute path supplied by
// tests/CMakeLists.txt's fn_add_game_test macro (${CMAKE_CURRENT_BINARY_DIR}/save,
// colocated with FN_TEST_SCREENSHOT_DIR). Relative fallback for a non-CMake
// build of this header (resolved against the process CWD).
#ifndef FN_TEST_SAVE_DIR
#define FN_TEST_SAVE_DIR "tmp/test/save"
#endif

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

// Per-frame hook for RunComponentHeadlessHooked. Runs once per frame either
// before or after the isolated HUD draw. userdata is passed through opaquely.
typedef void (*ComponentFrameFn)(void* userdata, float dt);

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
          m_glReadPixels(NULL),
          m_winW(960), m_winH(640)
    {
        // Port specific: MSVC fully buffers stdout/stderr when the stream is
        // a pipe (which ctest always uses) -- a hard crash never flushes the
        // buffer, so the last printed line lies about where the process died.
        // _IONBF (not _IOLBF -- MSVC documents line-buffering as behaving
        // like full buffering) must run before ANY logging, so it lives here
        // in the ctor rather than Init(): the ctor always runs first.
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

    // Set the window/drawable size the harness creates in Init()/InitComponent().
    // Must be called BEFORE Init()/InitComponent() -- SDL_CreateWindow reads it
    // at creation time. Default 960x640 (3:2), matching the game's default
    // desktop window (mainSDL.cpp). There is no post-creation resize path here
    // on purpose: mirrors mainSDL.cpp, which computes winW/winH before
    // SDL_CreateWindow rather than calling SDL_SetWindowSize afterwards.
    // A live SDL_SetWindowSize() on a window that already has a GL context can
    // hang under some drivers/WMs (observed on the hidden test window); create-
    // at-size sidesteps that entirely.
    void SetWindowSize(int w, int h) { m_winW = w; m_winH = h; }

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

    // -------- generic option accessors --------
    // Centralised per-test CLI parsing so individual tests stop hand-rolling
    // strncmp(argv[i], "--key=", ...) loops. CLI spelling is the conventional
    // "--key=value" (Opt / OptInt) or a bare "--key" boolean (OptFlag). These
    // scan argv on each call -- argv is tiny for tests, so no caching needed.
    // They are independent of ParseFlags(); a caller may use them with or
    // without having called ParseFlags().
    //
    //   Opt("content", "default")   -> value of --content=VALUE, else the default.
    //   OptInt("fact", 3)           -> atoi of --fact=VALUE, else 3.
    //   OptFlag("debug-textbounds") -> true if a bare --debug-textbounds is present.
    //
    // Opt returns a pointer INTO argv (valid for the process lifetime) or the
    // caller-supplied default pointer. It never returns a dangling pointer.
    const char* Opt(const char* key, const char* def = NULL) const {
        size_t klen = std::strlen(key);
        for (int i = 1; i < argc; ++i) {
            const char* a = argv[i];
            if (a[0] == '-' && a[1] == '-' &&
                std::strncmp(a + 2, key, klen) == 0 && a[2 + klen] == '=') {
                return a + 2 + klen + 1;
            }
        }
        return def;
    }

    // atoi of Opt(key); returns def when --key=VALUE is absent.
    int OptInt(const char* key, int def) const {
        const char* v = Opt(key, NULL);
        return v ? std::atoi(v) : def;
    }

    // True if a bare "--key" argument is present (exact match, no '=').
    bool OptFlag(const char* key) const {
        for (int i = 1; i < argc; ++i) {
            const char* a = argv[i];
            if (a[0] == '-' && a[1] == '-' && std::strcmp(a + 2, key) == 0) return true;
        }
        return false;
    }

    // -------- init --------
    bool Init() {
        // Win32 + _DEBUG only: register an unhandled-SEH filter that prints
        // exception code, faulting address, and a symbolised stack trace to
        // stderr before the OS terminates the process (no-op elsewhere --
        // see CrashHandler.h). Matches mainSDL.cpp's call shape/order: after
        // stdout/stderr are unbuffered (done in the ctor above, which always
        // runs before Init()) so a crash mid-test doesn't also lose this.
        FN::InstallCrashHandler();

        // Synthesize SDL_FINGER* events from the mouse (must be set BEFORE
        // SDL_Init) so interactive tests get touch input the widgets hit-test,
        // matching mainSDL.cpp. TOUCH_MOUSE_EVENTS=0 stops the reverse round-trip.
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
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
            m_winW, m_winH, winFlags);
        if (!window) {
            std::fprintf(stderr, "Window failed: %s\n", SDL_GetError());
            SDL_Quit();
            return false;
        }

        // Logged (and flushed -- stderr is unbuffered, see the ctor) BEFORE the
        // call: a missing GALLIUM_DRIVER/LIBGL_ALWAYS_SOFTWARE/MESA_GL_VERSION_OVERRIDE
        // env (see tests/README.md, and the ctest ENVIRONMENT wiring in
        // tests/CMakeLists.txt's fn_add_game_test) can make Mesa's opengl32.dll
        // hit the real display adapter and hard-terminate the process from inside
        // SDL_GL_CreateContext with no SEH exception and no chance to reach the
        // !gl check below -- this line is the last thing that survives in that
        // case, so a bare exit code is no longer silent about where it died.
        std::fprintf(stderr, "Creating GL context (SDL_GL_CreateContext)...\n");
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

        // Task #124: hermetic per-test save directory. Point Mortar_ResolveSaveDir
        // (src/platform/SaveDirSDL.h) at a throwaway directory scoped to this test's
        // label instead of the machine-global SDL_GetPrefPath location, so a save
        // left behind by one test (e.g. an active powerup, which scales dt in
        // GameUpdate) can never leak into a later test's start state. Reset the
        // known save files (not just create the dir) so a rerun of the SAME test
        // starts from a known state even if a prior run left files behind.
        SetupHermeticSaveDir_();

        // Port specific: pin Math::g_Random's boot seed so every TestHarness
        // run is reproducible instead of wall-clock-seeded (see FN_RNG_SEED in
        // src/engine/core/SystemManager.cpp). Must be set before game.init(),
        // which calls SystemManager::Init() (GameInitialise.cpp) that reads it.
        // A fixed value only makes a run's outcome REPEATABLE, not "correct" --
        // it does not change what the binary's RNG algorithm computes.
        //
        // MUST be set through the C RUNTIME, not SDL_setenv: the reader in
        // SystemManager.cpp is std::getenv, and on Windows SDL_setenv writes the
        // Win32 environment block while the MSVC CRT's getenv reads its OWN cached
        // copy -- so an SDL_setenv here is silently invisible to std::getenv and the
        // seed stays wall-clock. (SetupHermeticSaveDir_ above may use SDL_setenv
        // because SaveDirSDL.cpp reads it with SDL_getenv; that pair is consistent.
        // Do not "unify" these two on SDL_* without moving the reader too.)
#if defined(_WIN32)
        _putenv_s("FN_RNG_SEED", "1");
#else
        setenv("FN_RNG_SEED", "1", 1);
#endif

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
    //   2. Splice game_work.mHud->controls into m_strippedControls -- moves
    //      MainScreen and all other boot controls OUT of the draw list without
    //      destroying them (they stay owned by the game subsystems). Shutdown()
    //      splices them back before game.shutdown() so HUD::Release still walks
    //      and frees the full ownership graph, matching the binary's teardown.
    //   3. Set m_componentMode=true so RunComponentHeadless uses its own frame loop.
    //
    // The existing RunHeadless / RunInteractive paths are NOT affected; they call
    // game.runFrames() which calls the full GameTaskDraw as before.
    bool InitComponent() {
        if (!Init()) return false;
        m_componentMode = true;

        // Move the boot-time controls aside instead of destroying the list:
        // splicing (not clear()) means the nodes -- and the HUDControl*
        // ownership they represent -- survive intact in m_strippedControls
        // until Shutdown() splices them back, right before HUD::Release()
        // walks and deletes them for real.
        if (game_work.mHud) {
            m_strippedControls.splice(m_strippedControls.begin(), game_work.mHud->controls);
        }
        std::printf("[%s] component mode: HUD cleared (%d controls stripped)\n",
                    label, (int)m_strippedControls.size());
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

    // Like RunComponentHeadless but wraps the per-frame HUD update+draw with two
    // caller-supplied hooks, so a test can drive extra per-frame subsystems that
    // draw OUTSIDE the HUD control list (e.g. PowerUpManager::Draw, which renders
    // meter bars directly through MatrixManager rather than via HUDControl).
    //
    // Per-frame order (single BeginFrame / ortho / SwapWindow, matching
    // RunComponentHeadless):
    //   preDraw(userdata, dt)   -- run BEFORE the HUD (advance/settle subsystem state)
    //   mHud->Update / BeginDraw / Draw(layerMask)
    //   postDraw(userdata, dt)  -- run AFTER the HUD (draw overlays on top)
    // Either hook may be NULL. dt is fixed 1/60. Same ortho as the HUD path.
    void RunComponentHeadlessHooked(int n,
                                    ComponentFrameFn preDraw,
                                    ComponentFrameFn postDraw,
                                    void* userdata,
                                    int layerMask = 0x7FFFFFFF) {
        static const float kDt = 1.0f / 60.0f;
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

            if (preDraw) preDraw(userdata, kDt);

            if (game_work.mHud) {
                game_work.mHud->Update(kDt);
                game_work.mHud->BeginDraw(kDt);
                game_work.mHud->Draw(layerMask);
            }

            if (postDraw) postDraw(userdata, kDt);

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
    // Writes <FN_TEST_SCREENSHOT_DIR>/<suite>/<case>.png (compressed PNG, RGB, top-down)
    // by capturing the live framebuffer. PNG is the ONE framebuffer-capture format.
    // The name (nameOverride or label) may contain a '/' for suite/case nesting.
    // Uses SDL2_image IMG_SavePNG. Returns true on success.
    // CAPTURE ONLY: no reference image is loaded, nothing is diffed, no tolerance
    // is applied. A `true` return means "the PNG was written", never "the frame
    // matches a golden". Tests built on this assert only that boot/draw ran.
    bool ScreenshotPng(const char* nameOverride = NULL) {
        unsigned char* pixels = _ReadPixelsFlipped(NULL, NULL);
        if (!pixels) return false;
        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(window, &ww, &wh);

        const char* name = nameOverride ? nameOverride : label;
        char path[256];
        BuildScreenshotPath_(name, "png", path, sizeof(path));

        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
            pixels, ww, wh,
            24,            // bits per pixel
            (int)((((size_t)ww * 3u) + 3u) & ~(size_t)3u),  // pitch, 4-byte aligned (matches _ReadPixelsFlipped)
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

    // Save a caller-composed SDL_Surface as a PNG, using the SAME <suite>/<case>
    // path convention + auto directory creation as ScreenshotPng ('name' may
    // contain one '/'). For tests that build their own image (font/text grids)
    // instead of capturing the live framebuffer.
    //
    // Does NOT take ownership of 'surf' -- the caller still frees it (and any
    // backing pixel buffer). Returns true on success.
    bool SavePng(SDL_Surface* surf, const char* name) {
        if (!surf || !name) return false;
        char path[256];
        BuildScreenshotPath_(name, "png", path, sizeof(path));
        int rc = IMG_SavePNG(surf, path);
        if (rc != 0) {
            std::fprintf(stderr, "[%s] IMG_SavePNG(%s) failed: %s\n",
                         label, path, IMG_GetError());
            return false;
        }
        std::printf("[%s] wrote %s\n", label, path);
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
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        if (Renderer* r = Renderer::GetInstance()) r->Flush2D();
        m_glReadPixels(0, 0, ww, wh, GL_RGB_, GL_UNSIGNED_BYTE_, pixels);
        return pixels;
    }

    // -------- shutdown --------
    int Shutdown() {
        // Splice the boot controls stripped by InitComponent() back into
        // mHud->controls BEFORE game.shutdown() so GameExit's
        // game_work.mHud->Release() (GameInit.cpp) walks the full list and
        // frees them for real, instead of an empty list orphaning 30 boot
        // controls' textures to atexit (task #146). Naturally idempotent --
        // splicing an empty m_strippedControls is a no-op -- which matters
        // because ~TestHarness() calls Shutdown() a second time.
        if (game_work.mHud) {
            game_work.mHud->controls.splice(game_work.mHud->controls.begin(), m_strippedControls);
        }

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
    // Boot-time HUD controls (MainScreen etc.) spliced out of game_work.mHud->controls
    // by InitComponent() so RunComponentHeadless sees a blank draw list. Spliced back
    // into mHud->controls by Shutdown() before game.shutdown() so HUD::Release still
    // owns and frees them (task #146 -- clear() used to orphan these, leaking their
    // textures to atexit).
    std::list<HUDControl*> m_strippedControls;
    // Per-instance glReadPixels pointer. Loaded lazily via
    // SDL_GL_GetProcAddress on first use; cached for subsequent calls.
    PFN_glReadPixels m_glReadPixels;
    // Window/drawable size Init()/InitComponent() creates the SDL window at.
    // Set via SetWindowSize() before Init(); default 960x640 (3:2).
    int m_winW, m_winH;

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
        // 4-byte-align the row stride: glReadPixels' default GL_PACK_ALIGNMENT=4
        // pads each row up to a 4-byte boundary, so for odd widths where ww*3 is
        // not a multiple of 4 (e.g. 1138*3=3414) a tight ww*3 buffer would be
        // overrun by the padded write -> heap corruption / hang. 960*3=2880 is
        // already aligned, so 3:2 captures are byte-identical.
        size_t rowBytes = (((size_t)ww * 3u) + 3u) & ~(size_t)3u;
        size_t totalBytes = rowBytes * (size_t)wh;
        // Read bottom-up into a temp buffer, then flip into the final buffer.
        unsigned char* tmp  = (unsigned char*)std::malloc(totalBytes);
        unsigned char* flip = (unsigned char*)std::malloc(totalBytes);
        if (!tmp || !flip) {
            std::free(tmp);
            std::free(flip);
            return NULL;
        }
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        if (Renderer* r = Renderer::GetInstance()) r->Flush2D();
        m_glReadPixels(0, 0, ww, wh, GL_RGB_, GL_UNSIGNED_BYTE_, tmp);
        for (int y = 0; y < wh; ++y) {
            std::memcpy(flip + (size_t)y * rowBytes,
                        tmp  + (size_t)(wh - 1 - y) * rowBytes,
                        rowBytes);
        }
        std::free(tmp);
        return flip;
    }

    // Create the output directory tree for 'name' and build the full
    // "<FN_TEST_SCREENSHOT_DIR>/<name>.<ext>" path into outBuf. Shared by
    // ScreenshotPng and SavePng so both honour the <suite>/<case> convention
    // and identical directory auto-creation.
    void BuildScreenshotPath_(const char* name, const char* ext,
                              char* outBuf, size_t bufSize) {
        MakeScreenshotDir_(name);
        std::snprintf(outBuf, bufSize, "%s/%s.%s", FN_TEST_SCREENSHOT_DIR, name, ext);
    }

    // Portable recursive mkdir: creates 'path' and every missing parent
    // component (mirrors `mkdir -p`). Works for both relative and absolute
    // paths (FN_TEST_SCREENSHOT_DIR is typically absolute). Silently no-ops
    // on components that already exist; EEXIST/ENOENT-from-existing-parent
    // are expected, not errors.
    static void MkdirRecursive_(const char* path) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", path);
        size_t len = std::strlen(buf);
        // Normalize backslashes (Windows absolute paths may carry them) so
        // the single '/'-splitting loop below handles both separators.
        for (size_t i = 0; i < len; ++i) {
            if (buf[i] == '\\') buf[i] = '/';
        }
        for (size_t i = 1; i < len; ++i) {
            if (buf[i] != '/') continue;
            buf[i] = '\0';
#ifdef _WIN32
            // Skip a bare drive letter component ("C:") -- not a creatable dir.
            if (!(i == 2 && buf[1] == ':')) _mkdir(buf);
#else
            mkdir(buf, 0755);
#endif
            buf[i] = '/';
        }
#ifdef _WIN32
        _mkdir(buf);
#else
        mkdir(buf, 0755);
#endif
    }

    // Sanitize a test label into a filesystem-safe directory-name component
    // (replaces anything outside [A-Za-z0-9_-] with '_'). Used to give each
    // ctest entry its own save-dir leaf under FN_TEST_SAVE_DIR.
    static std::string SanitizeForPath_(const char* label_) {
        std::string s(label_ ? label_ : "test");
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!ok) s[i] = '_';
        }
        if (s.empty()) s = "test";
        return s;
    }

    // Task #124: create "<FN_TEST_SAVE_DIR>/<sanitized label>_<pid>", delete
    // the three known save filenames inside it (belt-and-suspenders start-
    // clean guard -- see below), then point FN_SAVE_DIR_OVERRIDE (read by
    // Mortar_ResolveSaveDir, src/platform/SaveDirSDL.h) at it.
    //
    // The PID suffix -- not just the label -- matters: several binaries
    // register MULTIPLE ctest NAMEs against the SAME fixed compile-time
    // label (e.g. test_dojoscreen's "dojoscreen" / "screenshot_dojo_english"
    // / "screenshot_dojo_chinese" ctest entries all pass the same label to
    // TestHarness's ctor). Under `ctest -j`, two of those can boot
    // concurrently; a label-only dir would let both processes' GameInit
    // boot-time save read and GameExit save write race the same file. Each
    // ctest NAME still runs this test binary as its own OS process, so the
    // PID makes every concurrent invocation unique regardless of label
    // collisions. The label prefix is kept purely for human-readable
    // directory names when inspecting the build tree's save dir after
    // a failure.
    //
    // The dir itself is intentionally NOT wiped/recreated (MkdirRecursive_
    // has no matching rmdir -r) -- only its known contents are deleted,
    // which are the only files any save-path helper ever writes there (see
    // FruitSaveData::GetSavePath / SettingsSave::GetSettingsSavePath /
    // ItemManager::BuildItemSaveFullPath). With the PID suffix this is a
    // pure safety net (a fresh PID directory is already empty); it only
    // matters in the astronomically unlikely event of PID reuse.
    void SetupHermeticSaveDir_() {
        char pidbuf[32];
#if defined(_WIN32)
        std::snprintf(pidbuf, sizeof(pidbuf), "%d", (int)_getpid());
#else
        std::snprintf(pidbuf, sizeof(pidbuf), "%d", (int)getpid());
#endif
        std::string dir = std::string(FN_TEST_SAVE_DIR) + "/" +
                           SanitizeForPath_(label) + "_" + pidbuf;
        MkdirRecursive_(dir.c_str());
        static const char* const kSaveFiles[] = {
            "FruitySave.xml", "SettingsSave.xml", "ItemSave.xml"
        };
        for (size_t i = 0; i < sizeof(kSaveFiles) / sizeof(kSaveFiles[0]); ++i) {
            std::remove((dir + "/" + kSaveFiles[i]).c_str());
        }
        SDL_setenv("FN_SAVE_DIR_OVERRIDE", dir.c_str(), 1);
    }

    // Create FN_TEST_SCREENSHOT_DIR and any subdirectory implied by 'name'.
    // If 'name' contains a '/' (e.g. "gameover/classic"), the part before
    // the slash is treated as a suite subdirectory and is created under
    // FN_TEST_SCREENSHOT_DIR. Names without '/' only create the base dir.
    static void MakeScreenshotDir_(const char* name = NULL) {
        MkdirRecursive_(FN_TEST_SCREENSHOT_DIR);
        if (!name) return;
        const char* slash = std::strchr(name, '/');
        if (!slash) return;
        // Build "<FN_TEST_SCREENSHOT_DIR>/<suite>" and mkdir it.
        char suiteDir[512];
        int suiteLen = (int)(slash - name);
        std::snprintf(suiteDir, sizeof(suiteDir), "%s/%.*s",
                      FN_TEST_SCREENSHOT_DIR, suiteLen, name);
        MkdirRecursive_(suiteDir);
    }
};

} // namespace fn

#endif // FN_TEST_HARNESS_H
