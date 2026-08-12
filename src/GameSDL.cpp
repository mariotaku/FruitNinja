// SDL backend for Game — init / run / runFrames live here so the rest of
// Game.cpp stays portable for the asm-verify cross-build. The void* fields
// in Game.h are cast to SDL_Window* / SDL_GLContext at the SDL boundary.

#include "Game.h"
#include "game/GameWork.h"
#include <SDL.h>
#include "platform/InputTranslatorSDL.h"
#include "platform/FixedStepDriver.h"
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
#include "platform/RenderInterp.h"
#endif
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include "core/SystemManager.h"
#include "game/GameTaskState.h"
#include "game/GameTaskInput.h"   // Port specific: RegressMenuCallback for ESC-as-back
#include "screens/PauseScreen.h"
#include "screens/MainScreen.h"   // IsInGameplay() for the background-freeze gate
#include "hud/HUD.h"              // Port specific: walk HUD::controls for ESC/wheel routing
#include "hud/ScrollingMenu.h"    // Port specific: mouse-wheel scroll target
#include <cmath>                  // Port specific: fabsf for the pause/transition guards
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "debug/OSD.h"    // Port specific: dev toast overlay (binary OSD is a dead stub)
#include "config.h"
#include "render/gl_funcs.h"
#include "render/Layout.h"
#include "render/Renderer.h"   // Port specific: stage-2 Flush2D end-of-frame barrier
#include "platform/AppDirSDL.h"
#include "platform/SaveDirSDL.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <vector>
#if defined(_WIN32)
    #include <direct.h>      // _mkdir on Windows
#else
    #include <sys/stat.h>    // mkdir on POSIX / Emscripten
#endif

#if defined(FRUIT_PLATFORM_WEBOS)
// Port specific: scancode the webOS TV remote's Back key arrives on.
//
// Value 482, cross-checked in three places, all agreeing:
//   - the buildroot SDK's SDL2 2.30.12 headers -- SDL_WEBOS_SCANCODE_BACK in
//     <SDL_webOS.h>;
//   - webosbrew/SDL-webOS @ webOS-2.30.x -- SDL_SCANCODE_WEBOS_BACK in
//     <SDL_scancode.h>;
//   - webosbrew/SDL-webOS @ webOS-2.28.x -- same, same value.
//
// Spelled as a local literal rather than either upstream name on purpose: the
// symbol NAME and the header it lives in move between SDL-webOS snapshots
// (SDL_webOS.h in the 2.30.12 the SDK ships, SDL_scancode.h at branch HEAD),
// while the NUMBER has never moved. Comparing the number can't break when the
// build headers and the TV's system libSDL2 are different snapshots -- the
// same runtime-older-than-headers hazard fn_wheel_caps() below guards against.
//
// The webOS scancodes are purely additive: they occupy 340-505, which stock
// SDL2 leaves unused. No standard scancode shifts (SDL_SCANCODE_AC_BACK is 270
// and SDL_NUM_SCANCODES is 512 in both stock and fork), so there is no scancode
// ABI break to work around here.
#define FN_SCANCODE_WEBOS_BACK 482
#endif

// Port specific: is this key press a "back" request?
//
// The binary has no back-key concept at all to be faithful to -- Bada's only
// key listener is GlesForm::OnKeyPressed @0x001f0a84, which handles just
// KEY_SIDE_UP/KEY_SIDE_DOWN for master volume and never tests KEY_CLEAR (the
// Wave hardware Back key). So both keys below are port additions, and desktop
// ESC is the behaviour reference for the TV remote's Back.
static bool fn_is_back_key(const SDL_Keysym& ks) {
    if (ks.sym == SDLK_ESCAPE) return true;
#if defined(FRUIT_PLATFORM_WEBOS)
    // Cast because 482 is outside the stock SDL_Scancode enum's named range.
    // NOT also accepting SDL_SCANCODE_AC_BACK (270): that is the browser-back
    // key of an attached keyboard, a different button. The TV remote's Back
    // arrives as 482 -- LG's own libSDL2 emits it, which is where the webosbrew
    // fork's value was RE'd from (keyboard_handle_key).
    if ((int)ks.scancode == FN_SCANCODE_WEBOS_BACK) return true;
#endif
    return false;
}

// Port specific: SDL_GL_GetDrawableSize isn't present on some older webOS TVs'
// system SDL2 (webosbrew-elf-verify flagged it as an unresolved import). On
// webOS, drawable size == window size (no HiDPI on a TV panel), so
// SDL_GetWindowSize is equivalent there. Host/web keep the real drawable size
// (HiDPI-aware) via SDL_GL_GetDrawableSize, unchanged.
static inline void fn_gl_drawable_size(SDL_Window* w, int* pw, int* ph) {
#if defined(FRUIT_PLATFORM_WEBOS)
    SDL_GetWindowSize(w, pw, ph);
#else
    SDL_GL_GetDrawableSize(w, pw, ph);
#endif
}

// Port specific: cached runtime probe for SDL mouse-wheel capabilities.
// webOS links the SDK's SYSTEM SDL2 via pkg-config, so the runtime library
// can be OLDER than the build headers (lowest supported target ships SDL
// 2.0.1). Struct fields the runtime never writes read as stale event-queue
// memory, NOT zero -- so reading them must be gated by BOTH a compile-time
// SDL_VERSION_ATLEAST guard (so old headers still build) AND this runtime
// check:
//   SDL_MouseWheelEvent::preciseX/preciseY -- added in 2.0.18
//   SDL_MouseWheelEvent::direction (SDL_MOUSEWHEEL_FLIPPED) -- added in 2.0.4
// Emscripten statically links its own SDL (no skew) but goes through the same
// path; the probe simply always reports true there.
static void fn_wheel_caps(bool* hasPrecise, bool* hasDirection) {
    static bool probed = false;
    static bool sPrecise = false;
    static bool sDirection = false;
    if (!probed) {
        SDL_version v;
        SDL_GetVersion(&v);
        sPrecise   = (v.major > 2) || (v.major == 2 && (v.minor > 0 || v.patch >= 18));
        sDirection = (v.major > 2) || (v.major == 2 && (v.minor > 0 || v.patch >= 4));
        probed = true;
    }
    *hasPrecise   = sPrecise;
    *hasDirection = sDirection;
}

// Port specific: screenshot capture flag. Set from the F12 key handler and
// read+cleared in frameTick before SDL_GL_SwapWindow (same thread, no signal).
static bool g_takeScreenshot = false;

// Port specific: FPS measurement for the g_ShowFps overlay.
// Updated every render frame in run(); read in renderFrame() for the draw call.
// Uses a ~0.5s sliding window: accumulate frame count + elapsed seconds, recompute
// once the window fills, then reset.  Decoupled from the sim rate (60Hz fixed); this
// measures the actual display-frame interval including any vsync stall.
static float  s_currentFps      = 0.0f;
static double s_fpsWindowSecs   = 0.0;
static int    s_fpsWindowFrames = 0;
static const double kFpsWindowTarget = 0.5;  // recompute every ~0.5 seconds

// Port specific: wall-clock timestamp of the previous renderFrame call, used
// to age OSD toasts at display cadence (independent of the 60 Hz sim rate).
static Uint64 s_osdLastCounter = 0;

// Port specific: perform glReadPixels + SDL_SaveBMP when g_takeScreenshot is set.
// Called just before SDL_GL_SwapWindow so GL_BACK holds the finished frame.
// webOS: no-op -- SDL_CreateRGBSurfaceWithFormatFrom isn't available on some
// older TVs' system SDL2 (webosbrew-elf-verify flagged it as an unresolved
// import), and a TV build has no F12/dev-screenshot use case anyway.
#if !defined(FRUIT_PLATFORM_WEBOS)
static void do_screenshot_if_requested(SDL_Window* window) {
    if (!g_takeScreenshot) return;
    g_takeScreenshot = false;

    int w = 0, h = 0;
    fn_gl_drawable_size(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    // Read pixels bottom-up (GL convention).
    std::vector<unsigned char> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    // Flip rows vertically into a second buffer (GL gives bottom-up, BMP needs top-down).
    std::vector<unsigned char> flipped(px.size());
    const size_t row = static_cast<size_t>(w) * 4u;
    for (int y = 0; y < h; ++y) {
        memcpy(flipped.data() + static_cast<size_t>(y) * row,
               px.data() + static_cast<size_t>(h - 1 - y) * row,
               row);
    }

    // Build SDL_Surface from the top-down RGBA buffer.
    // GL_RGBA / GL_UNSIGNED_BYTE gives bytes in R,G,B,A order.
    // SDL_PIXELFORMAT_ABGR8888 interprets a 32-bit little-endian word as A<<24|B<<16|G<<8|R,
    // which matches byte order R,G,B,A in memory -- correct for GL_RGBA readback.
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), w, h, 32, w * 4,
        SDL_PIXELFORMAT_ABGR8888);
    if (!surf) {
        LOG_ERROR("Screenshot", "SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return;
    }

    // Ensure screenshots/ directory exists.
#if defined(_WIN32)
    _mkdir("screenshots");
#else
    mkdir("screenshots", 0755);
#endif

    // Build filename from monotonic counter (avoids overwriting previous shots).
    static int s_counter = 0;
    char path[64];
    snprintf(path, sizeof(path), "screenshots/shot_%04d.bmp", s_counter++);

    if (SDL_SaveBMP(surf, path) != 0) {
        LOG_ERROR("Screenshot", "SDL_SaveBMP failed: %s", SDL_GetError());
    } else {
        // Print absolute path so it's easy to find.
        char abspath[512] = {0};
#if defined(_WIN32)
        _fullpath(abspath, path, sizeof(abspath));
#else
        if (!realpath(path, abspath)) {
            strncpy(abspath, path, sizeof(abspath) - 1);
        }
#endif
        LOG_INFO("Screenshot", "saved %s", abspath);
        // Port specific: OSD toast confirmation, posted only on successful save
        // (binary OSD is a dead stub). Shows on the NEXT frame -- the current
        // frame's pixels were already read back, so the toast never appears in
        // the screenshot itself.
        char osd[64];
        snprintf(osd, sizeof(osd), "Screenshot saved %s", path);
        OSD_AddMessage(osd);
    }

    SDL_FreeSurface(surf);
}
#else
static void do_screenshot_if_requested(SDL_Window* window) {
    (void)window;
    g_takeScreenshot = false;
}
#endif // !FRUIT_PLATFORM_WEBOS

// Matches: FruitNinja::OnAppInitializing flow
bool Game::init(void* win, void* gl) {
    window = win;        // SDL_Window* stored as void* in the header
    gl_context = gl;     // SDL_GLContext stored as void*
    if (!inputTranslator) {
        inputTranslator = new InputTranslatorSDL();
        inputTranslator->Init();   // pre-compute action hashes; safe before GameInitialise
    }
#if defined(FRUIT_PLATFORM_WEBOS)
    // Port specific: ignore the compile-time FN_DATA_DIR on webOS -- resolve
    // the read-only asset dir relative to the app's own install directory
    // instead (see fn_webos_app_dir above). Data/ sits directly under the
    // app root (install() rules in CMakeLists.txt).
    std::string appDir = fn_webos_app_dir();
    data_dir = appDir + "/Data";
#else
    data_dir = FN_DATA_DIR;
    std::string appDir = FN_DATA_DIR;
#endif
    // Port specific: shared resolver so this can never drift from mainSDL.cpp's
    // pre-init resolution of the same save_dir (see SaveDirSDL.h).
    save_dir = Mortar_ResolveSaveDir(appDir.c_str());
    Mortar::TextureManager::SetDataDir(data_dir.c_str());

    // DisplayManager holds game-space dimensions (480×320), not SDL pixel dimensions.
    // glViewport in run() handles pixel scaling independently.
    // Constructor already sets this; explicit here to match original Bada GlesForm init.
    Mortar::DisplayManager::GetInstance().SetWindowSize(0, FN_SCREEN_H, 0, FN_SCREEN_W);

    if (!renderer.init()) {
        LOG_ERROR("GameSDL", "Failed to init renderer");
        return false;
    }

    // One-shot GL state init — matches FruitNinja::InitGL @ 0x00181e54.
    int initW, initH;
    fn_gl_drawable_size(static_cast<SDL_Window*>(window), &initW, &initH);
    renderer.InitGL(initW, initH);

    // Matches original lifecycle:
    GamePreInitialise();   // zero game fields
    SetHardware("BADA", true);   // v1.6.1 Game::Init @0x00120374: SetHardware between PreInit and Init.
                                  // Sets m_bFastHardware=true (Bada Wave = fast HW); gates fruit_flight
                                  // trail fallback, jib count, ScreenEffect filters.
    GameInitialise(nullptr, nullptr);  // boot all engine singletons + load shared data

    // Start in Splash state (will auto-transition to Game)
    game_work.taskStateIndex = 0;
    running = true;
    return true;
}

// Port specific: drain SDL events into per-channel pending state (no touch dispatch).
// Called ONCE per display frame (before accumulator drain).
// Touch events (FINGERDOWN/MOTION/UP, MOUSEBUTTONUP) are accumulated into the
// translator's pending state; actual dispatch to InputManager happens in
// stepUpdate() via DispatchForSimTick() so m_PointCount only advances inside a
// tick that also runs UpdatePoints (head-cap reconcile). This is the #173 fix.
// Focus-loss / WINDOW events still fire ReleaseAllFingers() immediately (#162).
// Non-touch keyboard/debug events are handled inline as before (#163 fidelity).
#ifdef FN_DEBUG_TOUCH
// Port specific: FN_DEBUG_TOUCH sink. Mirrors each input event to a file as
// well as the log, so the events can be read back off a device whose stdout we
// do not own (a webOS app launched from the TV's own launcher). First writable
// path wins; a NULL file just drops the line.
static void FnTouchDebugLog(const char* fmt, ...) {
    static FILE* s_pFile   = NULL;
    static bool  s_bTried  = false;
    if (!s_bTried) {
        s_bTried = true;
        const char* paths[] = { "/tmp/fn-touch.log", "fn-touch.log" };
        for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            s_pFile = fopen(paths[i], "w");
            if (s_pFile) {
                LOG_INFO("TOUCH", "event log -> %s", paths[i]);
                break;
            }
        }
    }

    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    LOG_DEBUG("TOUCH", "poll %s\n", line);
    if (s_pFile) {
        fprintf(s_pFile, "%s\n", line);
        fflush(s_pFile);   // the app is killed, not closed -- flush every line
    }
}
#endif

void Game::pollInput() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
#ifdef FN_DEBUG_TOUCH
        // Confirm which SDL event types arrive for mouse/finger input.
        // SDL_HINT_MOUSE_TOUCH_EVENTS=1 should synthesize FINGER* from MOUSE*;
        // if MOUSEBUTTONDOWN shows here instead of FINGERDOWN, the hint is not active.
        // MOUSEMOTION is intentionally excluded -- it fires every frame the
        // cursor moves and we don't handle it (touch uses FINGER* events).
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            // which == SDL_TOUCH_MOUSEID marks a mouse event SDL synthesized
            // from a real touch; a device that only has a pointer never sets it.
            FnTouchDebugLog("%s button=%d which=%u x=%d y=%d",
                ev.type == SDL_MOUSEBUTTONDOWN ? "MOUSEBUTTONDOWN" : "MOUSEBUTTONUP",
                (int)ev.button.button, (unsigned)ev.button.which,
                ev.button.x, ev.button.y);
        } else if (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERMOTION ||
                   ev.type == SDL_FINGERUP) {
            FnTouchDebugLog("%s finger=%d nx=%.4f ny=%.4f",
                ev.type == SDL_FINGERDOWN   ? "FINGERDOWN" :
                ev.type == SDL_FINGERMOTION ? "FINGERMOTION" : "FINGERUP",
                (int)ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
        } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            // A TV remote's OK/click can arrive as a key rather than a pointer
            // button. Log it so we can tell the two apart on device.
            FnTouchDebugLog("%s scancode=%d keycode=%d (%s)",
                ev.type == SDL_KEYDOWN ? "KEYDOWN" : "KEYUP",
                (int)ev.key.keysym.scancode, (int)ev.key.keysym.sym,
                SDL_GetKeyName(ev.key.keysym.sym));
        }
#endif
        if (ev.type == SDL_QUIT) {
            running = false;
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F1) {
            // Port specific: 4-level debug overlay cycle (dev aid, not in the binary).
            static const char* kDebugHitboxLevelNames[4] = {
                "off", "entity", "entity+HUD", "entity+HUD+font"
            };
            FN::g_DebugHitboxes = (FN::g_DebugHitboxes + 1) % 4;
            LOG_DEBUG("Debug", "Hitboxes level %d (%s)", FN::g_DebugHitboxes,
                      kDebugHitboxLevelNames[FN::g_DebugHitboxes]);
            // Port specific: OSD toast confirmation (binary OSD is a dead stub).
            char osd[64];
            snprintf(osd, sizeof(osd), "Hitbox: %s",
                     kDebugHitboxLevelNames[FN::g_DebugHitboxes]);
            OSD_AddMessage(osd);
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F2) {
            // Port specific: glPolygonMode(GL_LINE) around the 3D
            // entity draw pass. Desktop GL only -- no-op under GLES.
            FN::g_DebugWireframe = !FN::g_DebugWireframe;
            LOG_DEBUG("Debug", "Wireframe %s", FN::g_DebugWireframe ? "ON" : "OFF");
            // Port specific: OSD toast confirmation (binary OSD is a dead stub).
            OSD_AddMessage(FN::g_DebugWireframe ? "Wireframe: ON" : "Wireframe: OFF");
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F3) {
            // Port specific: FPS counter overlay (DebugFps_Draw, top-left corner).
            FN::g_ShowFps = !FN::g_ShowFps;
            LOG_DEBUG("Debug", "FPS overlay %s", FN::g_ShowFps ? "ON" : "OFF");
            // Port specific: OSD toast confirmation (binary OSD is a dead stub).
            OSD_AddMessage(FN::g_ShowFps ? "FPS overlay: ON" : "FPS overlay: OFF");
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F4) {
            // Port specific: OSD toast per SFX played (SoundManager::SFXPlay).
            // Display-only diagnostic; never gates actual audio.
            FN::g_bOsdSfx = !FN::g_bOsdSfx;
            LOG_DEBUG("Debug", "SFX OSD %s", FN::g_bOsdSfx ? "ON" : "OFF");
            // Port specific: OSD toast confirmation (binary OSD is a dead stub).
            OSD_AddMessage(FN::g_bOsdSfx ? "SFX OSD: ON" : "SFX OSD: OFF");
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F7) {
            // Port specific: debug-only, no binary equivalent
            FN::g_DebugTimeScale = (FN::g_DebugTimeScale == 1.0f) ? 0.1f : 1.0f;
            LOG_DEBUG("Debug", "timeScale = %.1f", FN::g_DebugTimeScale);
            // Port specific: OSD toast confirmation (binary OSD is a dead stub).
            if (FN::g_DebugTimeScale == 1.0f) {
                OSD_AddMessage("Slow-mo: OFF");
            } else {
                char osd[64];
                snprintf(osd, sizeof(osd), "Slow-mo: ON (x%.1f)", FN::g_DebugTimeScale);
                OSD_AddMessage(osd);
            }
        } else if (ev.type == SDL_KEYDOWN &&
                   ev.key.keysym.scancode == SDL_SCANCODE_F12) {
            // Port specific: screenshot on F12.
            g_takeScreenshot = true;
        } else if (ev.type == SDL_MOUSEWHEEL) {
            // Port specific: desktop/web mouse-wheel scrolls a hovered
            // ScrollingMenu (no binary counterpart -- the binary is
            // touch-only Bada hardware). When the RUNTIME SDL provides
            // fractional deltas (>= 2.0.18, see fn_wheel_caps) the wheel
            // drives ScrollByPixels for smooth sub-row motion (trackpads
            // emit many ~0.03-step events); otherwise it falls back to the
            // discrete one-row-per-notch ScrollByItems path.
            // Guard: don't scroll a screen mid-transition (same shape as
            // the in-game m_PauseAmount guard below -- see GetTransitionAlpha).
            if (game_work.mHud) {
                ScrollingMenu* activeList = nullptr;
                float transitionAlpha = 1.0f;
                for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
                     it != game_work.mHud->controls.end(); ++it) {
                    ScrollingMenu* list = (*it)->GetScrollList();
                    if (list) {
                        activeList = list;
                        transitionAlpha = (*it)->GetTransitionAlpha();
                        break;
                    }
                }
                bool mid = transitionAlpha > 0.0f && transitionAlpha < 0.999f;
                if (activeList && !mid) {
                    int ww = 0, wh = 0;
                    if (window) SDL_GetWindowSize(static_cast<SDL_Window*>(window), &ww, &wh);
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    if (ww > 0 && wh > 0) {
                        float nx = (float)mx / (float)ww;
                        float ny = (float)my / (float)wh;
                        float gx, gy;
                        Layout::TouchToGame(nx, ny, &gx, &gy);
                        if (activeList->ContainsPoint(gx, gy)) {
                            bool hasPrecise = false, hasDirection = false;
                            fn_wheel_caps(&hasPrecise, &hasDirection);

                            // Wheel delta in notch units. On an old runtime
                            // (< 2.0.4 / < 2.0.18) the direction/precise
                            // fields are never written -- do not even read
                            // them (stale queue memory, not zero).
                            float stepY = (float)ev.wheel.y;
                            bool usePrecise = false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                            if (hasPrecise) {
                                stepY = ev.wheel.preciseY;
                                usePrecise = true;
                            }
#endif
#if SDL_VERSION_ATLEAST(2, 0, 4)
                            // Natural-scroll flip (macOS/X11): SDL reports
                            // deltas pre-inverted; undo so hover-scroll
                            // matches the platform's scroll direction.
                            if (hasDirection &&
                                ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                                stepY = -stepY;
                            }
#endif
#ifdef __EMSCRIPTEN__
                            // Port specific: Chrome bakes the OS scroll-lines
                            // setting * 33.3px into deltaY, and SDL-emscripten's
                            // /100 normalisation assumes 3 lines, so one notch
                            // can arrive as 1.0-2.0 depending on the user's OS
                            // setting (WheelScrollLines=5 -> 1.667). Desktop SDL
                            // normalises by WHEEL_DELTA and always yields +/-1
                            // per notch; ceiling-clamp the magnitude to restore
                            // parity. Trackpad deltas (~0.03/event) stay below
                            // 1.0 and pass through untouched.
                            if (stepY > 1.0f) stepY = 1.0f;
                            else if (stepY < -1.0f) stepY = -1.0f;
#endif
                            if (usePrecise) {
                                // Map notches to scroll-position units: one
                                // notch (preciseY == 1.0) ~= one row. Derive
                                // the row height from the focal item; the
                                // shop list's rows are 80 (SetItemHeight(80)),
                                // kept as the fallback for an empty focus.
                                float rowH = 80.0f;
                                ScrollingMenuItem* row = activeList->GetItemClosestToZero();
                                if (row && row->GetHeight() > 0.0f) {
                                    rowH = row->GetHeight();
                                }
                                // +preciseY (wheel up) scrolls toward earlier
                                // items = larger m_Velocity.y, so no negation
                                // here (unlike ScrollByItems' index delta).
                                activeList->ScrollByPixels(stepY * rowH);
                            } else {
                                activeList->ScrollByItems(-(int)stepY);
                            }
                        }
                    }
                }
            }
        } else if (ev.type == SDL_KEYDOWN && fn_is_back_key(ev.key.keysym)) {
            // Port specific: ESC-as-back on desktop, and the TV remote's Back
            // key on webOS (see fn_is_back_key above). No binary counterpart --
            // the binary is touch-only Bada hardware and its one key listener
            // handles volume only.
            //
            // At the root screen (MainScreen) this is what exits the app, and
            // it does so through the binary's own quit chain rather than a
            // port-side shortcut: MainScreen arms m_bBackdropActive on its QUIT
            // fruit (@0x00196a5c), so HasActiveBackBomb() is true there and the
            // RegressMenuCallback path below force-slices that fruit ->
            // QuitGamesCallback (v1.6.1 @0x00196024) -> SystemManager::RequestQuit.
            // That matches the webOS convention of Back-at-root closing the app.
            //
            // taskStateIndex alone can't distinguish menu vs. live round: State 1
            // (Frontend) is dead code (FrontendTask.cpp / SplashTask.cpp both jump
            // straight to State 2), so BOTH menus and gameplay run under
            // taskStateIndex==2 -- the menu screens ARE the paused Game task.
            // Discriminator is TWO conditions, and the back-bomb ring alone is not
            // enough. Every menu screen (Main/GameMode/Dojo/Shop/About/GameOver)
            // arms m_bBackdropActive=1 on its back/regress MenuButton -- but so
            // does PauseScreen on its resume button (+0x150, faithful to the
            // binary), which is live DURING a round. So gate on bM_bPaused too:
            // the binary uses +0x05 as its menu-vs-play gate (GameUpdate @0x1cf9c4
            // `ldrb r3,[gctx,#0x5]; cmp; bne` suppresses miss penalty / gank /
            // GameOver on the menu), so bM_bPaused==0 IS a live round.
            //
            // Port specific: do NOT swap this for m_bRespondsToBackKey/+0x138.
            // That field is the click-EDGE selector (m_bClickOnRelease) whose
            // binary default is 1 on EVERY button, so it discriminates nothing.
            if (game_work.taskStateIndex == 2) {
                bool menuBackActive = false;
                bool mid = false;
                if (game_work.mHud) {
                    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
                         it != game_work.mHud->controls.end(); ++it) {
                        float a = (*it)->GetTransitionAlpha();
                        if (a > 0.0f && a < 0.999f) mid = true;
                        if ((*it)->HasActiveBackBomb()) menuBackActive = true;
                    }
                }
                if (menuBackActive && game_work.bM_bPaused != 0) {
                    // Menu: block while any HUD control's transition alpha is
                    // mid-fade (BaseScreen subclasses / ShopScreen override
                    // GetTransitionAlpha; everything else reports 1.0f, a no-op
                    // for this check). Otherwise fire the canonical back-key
                    // handler, which auto-routes to whichever menu screen's
                    // back-bomb button is currently active.
                    if (!mid) {
                        InputEvent ev2;
                        memset(&ev2, 0, sizeof(ev2));
                        RegressMenuCallback(&ev2);
                    }
                } else {
                    // Live round: same gate PauseScreen::IsEnabled uses -- don't
                    // fire mid pause/camera transition.
                    if (fabsf(game_work.m_PauseAmount) < 0.001f) {
                        PauseScreen* pauseScreen = GetTaskState()->pPauseScreen;
                        if (pauseScreen) pauseScreen->PauseGameCallback();
                    }
                }
            }
            // taskStateIndex == 0 (splash): back does nothing.
            // taskStateIndex == 1 (Frontend): dead code, never reached in v1.6.1.
        } else if (ev.type == SDL_WINDOWEVENT &&
                   (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    ev.window.event == SDL_WINDOWEVENT_MINIMIZED  ||
                    ev.window.event == SDL_WINDOWEVENT_HIDDEN)) {
            // Port specific: SDL focus-loss -> binary FruitNinja::OnBackground @0x001ef660
            // -> Game::Paused (vtable +0x48), called UNCONDITIONALLY (no state/mode gate).
            // Whether the pause overlay actually snaps in is decided downstream in
            // Game::Paused -> SkipToPause(false) -> PauseScreen::IsEnabled() (|m_PauseAmount|
            // < 0.001), so the menu->play intro (m_PauseAmount ramping, e.g. "60 SECONDS"/GO)
            // is exempt on its own. Clears touch so no blade stays held (#154, #162).
            Paused();
            LOG_INFO("GameSDL", "focus-loss pause (SDL_WINDOWEVENT %d)", (int)ev.window.event);
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
            // Port specific: OS-timer-cancel gate (see Game.h m_bBackgrounded) -- freezes
            // stepUpdate() in run() so intro/gameplay don't advance while backgrounded.
            m_bBackgrounded = true;
        } else if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
            // Port specific: mobile background event -> binary FruitNinja::OnBackground @0x001ef660
            // -> Game::Paused (vtable +0x48), unconditional (see focus-loss note above).
            // Equivalent to Bada OnBackground on iOS/Android.
            Paused();
            LOG_INFO("GameSDL", "app-background pause (SDL_APP_WILLENTERBACKGROUND)");
            if (inputTranslator) inputTranslator->ReleaseAllFingers();
            // Port specific: OS-timer-cancel gate (see Game.h m_bBackgrounded).
            m_bBackgrounded = true;
        } else if (ev.type == SDL_WINDOWEVENT &&
                   (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                    ev.window.event == SDL_WINDOWEVENT_RESTORED)) {
            // Port specific: window focus restored -> binary FruitNinja::OnForeground @0x001ef6cc
            // -> Game::UnPaused (vtable +0x4c). Ends audio interruption. Game stays paused
            // (user must dismiss PauseScreen) because UnPaused gates UnpauseGame on m_PauseAmount!=0,
            // and SkipToPause has set m_PauseAmount=0.
            UnPaused();
            LOG_INFO("GameSDL", "focus-gained unpause (SDL_WINDOWEVENT %d)", (int)ev.window.event);
            // Port specific: OS-timer-cancel gate (see Game.h m_bBackgrounded) -- restarts
            // stepUpdate() next iteration. No explicit dt-reference reset needed: run()'s
            // `last` timestamp is refreshed every loop iteration (including while
            // backgrounded), so the resume frame's `ms` is just since the previous
            // iteration, not the whole backgrounded span -- no catch-up burst.
            m_bBackgrounded = false;
        } else if (ev.type == SDL_APP_DIDENTERFOREGROUND) {
            // Port specific: mobile foreground event -> binary FruitNinja::OnForeground @0x001ef6cc
            // -> Game::UnPaused (vtable +0x4c). Restores audio; gameplay stays paused per above.
            UnPaused();
            LOG_INFO("GameSDL", "app-foreground unpause (SDL_APP_DIDENTERFOREGROUND)");
            // Port specific: OS-timer-cancel gate (see Game.h m_bBackgrounded).
            m_bBackgrounded = false;
        } else {
            // Port specific: accumulate touch events into pending state; dispatch
            // happens in stepUpdate()->DispatchForSimTick() (#173 fix).
            if (inputTranslator) inputTranslator->DrainSDLEvent(ev, static_cast<SDL_Window*>(window));
        }
    }

}

// Port specific: one simulation step.
// Matches FruitNinja::Draw (0x1824e0): dt=0 -> SystemManager::Update(&dt)
// writes fixed 1/60 -> GameTaskUpdate(dt).  g_DebugTimeScale scales dt for
// the slow-motion debug path only; game logic always sees 1/60 at 1.0x scale.
//
// Port specific: DispatchForSimTick() is called BEFORE GameTaskUpdate so that
// touch dispatch (AddPoint -> m_PointCount advance) and geometry reconcile
// (UpdatePoints inside GameTaskUpdate) happen in the same tick, matching the
// binary's strict 1:1 input->update ordering (#173 bridge-to-origin fix).
// On steps==0 (pure interpolated frame), stepUpdate() does not run, so neither
// DispatchForSimTick nor AddPoint runs -> m_PointCount unchanged -> DrawSlice
// draws the already-reconciled buffer, no stale head-cap.
// DispatchForSimTick contains NO SDL live-finger queries; this makes it safe to
// call from stepUpdate on web.
void Game::stepUpdate() {
    // Port specific: bump the log-line sim-tick counter once per fixed
    // 1/60s step (see Debug::g_LogTick, src/debug/Logger.h).
    ++Debug::g_LogTick;

    // Flush deferred touch dispatch for this sim tick (before update).
    if (inputTranslator) inputTranslator->DispatchForSimTick();

    game_work.dt = 0.0f;
    SystemManager::GetInstance().Update(&game_work.dt);
    game_work.dt *= FN::g_DebugTimeScale;
    GameTaskUpdate(game_work.dt);
}

// Port specific: allow platform main loops (mainEmscripten) to feed the FPS value
// that DebugFps_Draw reads inside renderFrame.
void Game::setCurrentFps(float fps) {
    s_currentFps = fps;
}

// Port specific: one render pass (no simulation).
// Per-frame GL setup mirrors DisplayManagerBada::BeginFrame (0x0019dfec).
// glViewport is re-applied each call so window resizes are picked up immediately.
// alpha = fractional sim residual [0,1) for render interpolation; 0 = no interp.
// steps = sim steps advanced this display frame; 0 on pure-interp frames (120 Hz
// second render with no new sim tick).
void Game::renderFrame(float alpha, int steps) {
    int ww, wh;
    fn_gl_drawable_size(static_cast<SDL_Window*>(window), &ww, &wh);
    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful full-window viewport
    // when the layout isn't wide (Layout::SetWideLayout(false), the default). When
    // wide, letterbox/pillarbox the drawable to Layout::EffectiveAspect() (clamped to
    // [3:2, 16:9]) instead of stretching the raw window aspect, so an ultra-wide or
    // ultra-tall window doesn't distort the game beyond the 16:9 cap.
    // Pass 3: viewport rect computed by Layout::ComputeViewport (single source of
    // truth shared with InputTranslatorSDL::TransformTouchNormalized via
    // Layout::TouchToGame) instead of inline pillarbox/letterbox math here.
    Layout::SetWindowAspect((float)ww, (float)wh);
    int vpX, vpY, vpW, vpH;
    Layout::ComputeViewport(ww, wh, &vpX, &vpY, &vpW, &vpH);
    glViewport(vpX, vpY, vpW, vpH);
    Layout::SetActiveViewport(vpX, vpY, vpW, vpH, ww, wh);
    Mortar::DisplayManager::GetInstance().BeginFrame();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().ApplyForDraw(alpha);
#endif
    // Port specific: #168/#337 -- GameTaskDraw no longer uses this dt param (it
    // ignores it and consumes the s_drawDt accumulator internally, matching the
    // binary). The manual "steps==0 -> zero dt" band-aid this used to need is
    // gone: s_drawDt is naturally 0 on interpolated (no-sim-tick) frames since
    // GameTaskUpdate didn't run, and naturally sums all accumulated ticks on
    // steps>1 catch-up frames -- both cases the old zeroing handled wrongly.
    float savedDt = game_work.dt;
    GameTaskDraw(game_work.dt);
    game_work.dt = savedDt;
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().RestoreAfterDraw();
#endif
    // Port specific: FPS counter overlay -- additive, after all game draw calls.
    // Intentionally outside the dt-zero block: DebugFps_Draw does not use game_work.dt.
    FN::DebugFps_Draw(s_currentFps);
    // Port specific: OSD dev toasts -- aged by wall-clock render dt, drawn
    // below the FPS counter. Not gated by any debug flag (toasts are
    // user-triggered confirmations). Binary OSD is a dead stub; see
    // src/debug/OSD.h.
    {
        Uint64 now = SDL_GetPerformanceCounter();
        float osdDt = 0.0f;
        if (s_osdLastCounter != 0) {
            osdDt = static_cast<float>(
                static_cast<double>(now - s_osdLastCounter) /
                static_cast<double>(SDL_GetPerformanceFrequency()));
            if (osdDt > 0.25f) osdDt = 0.25f;   // clamp across stalls/breakpoints
        }
        s_osdLastCounter = now;
        OSD_Update(osdDt);
        OSD_Draw();
    }
    // Port specific (stage-2 2D batching): drain pending 2D draws so the F12
    // glReadPixels below and the swap both see the complete frame.
    if (Renderer* r = Renderer::GetInstance()) {
        r->Flush2D();
    }
    do_screenshot_if_requested(static_cast<SDL_Window*>(window));
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
}

// Port specific: one complete game tick — poll, step, render.
// Kept as a thin wrapper so existing callers (legacy / external) are unaffected.
// Passes alpha=0 to renderFrame (no interpolation; frameTick is used by runFrames
// which is the deterministic headless path -- Apply/Restore must not fire there).
void Game::frameTick() {
    pollInput();
    stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
    fn::RenderInterp::Get().SnapshotAfterStep();
#endif
    renderFrame(0.0f, 1);
}

// Port specific: main game loop with fixed-step accumulator.
// The accumulator decouples simulation rate (60 ticks/s) from display refresh
// so the game runs at correct wall-clock speed on 60/120/144 Hz panels.
// vsync (SDL_GL_SetSwapInterval(1) in mainSDL.cpp) paces the render; the
// accumulator owns the sim rate.
// Port specific: vsync (SDL_GL_SetSwapInterval) isn't guaranteed; when inactive,
// pace render to the display refresh so we don't render unbounded.
void Game::run() {
    fn::FixedStepDriver driver;
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    // Detect actual vsync state and display refresh target once at loop entry.
    // SDL_GL_SetSwapInterval(1) is a request that drivers may ignore; check
    // the actual interval rather than assuming the request was honoured.
    bool vsyncOn = (SDL_GL_GetSwapInterval() != 0);
    double targetMs = 1000.0 / 60.0;
    {
        SDL_DisplayMode dm;
        int win = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(window));
        if (SDL_GetCurrentDisplayMode(win < 0 ? 0 : win, &dm) == 0 && dm.refresh_rate > 0) {
            targetMs = 1000.0 / dm.refresh_rate;
        }
    }

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double ms = static_cast<double>(now - last) * 1000.0 / freq;
        last = now;

        // Port specific: accumulate render-frame intervals for the FPS overlay.
        // Only active when the overlay is on to avoid redundant work.
        if (FN::g_ShowFps) {
            s_fpsWindowSecs   += ms * 0.001;
            s_fpsWindowFrames += 1;
            if (s_fpsWindowSecs >= kFpsWindowTarget) {
                s_currentFps      = static_cast<float>(s_fpsWindowFrames / s_fpsWindowSecs);
                s_fpsWindowSecs   = 0.0;
                s_fpsWindowFrames = 0;
            }
        }

        // frameStart reuses `now` (already read at loop top) so the cap
        // accounts for the full iteration including any vsync-blocked SwapWindow.
        Uint64 frameStart = now;

        pollInput();
        if (!running) break;

        // Port specific: OS-timer-cancel gate (see Game.h m_bBackgrounded). While
        // backgrounded, skip driver.advance()/stepUpdate() entirely so `ms` for the
        // backgrounded span never reaches the fixed-step accumulator -- mirrors the
        // binary's cancelled OS timer (FruitNinja::OnBackground @0x001ef660), which
        // means literally no ticks are generated while away, not just a capped catch-up.
        // `last` is still refreshed every iteration above (loop keeps polling for the
        // resume event), so `ms` measured on the very frame focus returns is just since
        // the previous iteration -- not the whole backgrounded duration -- so no reset
        // of `last`/accumulator is needed on the resume edge; stepUpdate() simply starts
        // being called again with normal per-frame `ms` values, restarting the fixed
        // 1/60s cadence with zero backlog (matching OnForeground @0x001ef6cc restarting
        // the timer at its normal rate).
        // Port improvement: only freeze the loop while backgrounded DURING actual
        // gameplay (so the intro/gameplay can't fast-forward while alt-tabbed). In
        // the menu/shop/dojo, keep ticking so their animations don't stall when the
        // window loses focus -- an unfocused desktop window is not the same as the
        // Bada device being fully backgrounded. Gate on MainScreen's gameplay state.
        bool freeze = m_bBackgrounded &&
                      game_work.mMainScreen && game_work.mMainScreen->IsInGameplay();
        int steps = freeze ? 0 : driver.advance(ms);
        for (int i = 0; i < steps && running; ++i) {
            stepUpdate();
#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP
            fn::RenderInterp::Get().SnapshotAfterStep();
#endif
        }

        // Port specific: no binary counterpart. Per-PRESENT UI tick (see
        // Game.h tickRealtimeUi) -- runs once per rendered/presented frame
        // here in run() (the real gameplay loop), NOT in renderFrame() itself,
        // so the deterministic test paths (Game::runFrames/frameTick, used by
        // every headless render test) never pick up a wall-clock-dependent
        // nudge. dtPresent is real elapsed time since the last call to THIS
        // tick (independent of the sim accumulator's `ms`, which measures
        // since the last LOOP iteration, not the last UI tick specifically --
        // same value in practice since both are computed once per iteration
        // here, but tracked separately for clarity/future-proofing). Clamped
        // in each HUDControl::UpdateRealtime override (e.g. SettingsScreen,
        // ScrollingMenu) against stalls.
        {
            static Uint64 s_lastRealtimeUiCounter = 0;
            double dtPresent = 0.0;
            if (s_lastRealtimeUiCounter != 0) {
                dtPresent = static_cast<double>(now - s_lastRealtimeUiCounter) / freq;
            }
            s_lastRealtimeUiCounter = now;
            tickRealtimeUi(static_cast<float>(dtPresent));
        }

        renderFrame(static_cast<float>(driver.alpha()), steps);

        // Port specific: "Limit to 60 FPS" (FN::g_FpsCap60, SettingsScreen
        // checkbox) -- caps PRESENT rate only, sim stays the fixed 60 Hz
        // accumulator above (driver.advance/stepUpdate are untouched). vsync
        // stays ON (SDL_GL_SetSwapInterval is never touched here); on a 60 Hz
        // panel vsync already paces to ~16.7ms so this is a no-op, on 120/144 Hz
        // it adds the remainder of a 16.667ms budget on top of vsync's shorter
        // interval. Read live (not cached) so toggling the checkbox takes
        // effect the very next frame.
        //
        // Boundary accumulator (mirrors mainEmscripten.cpp's s_nextPresentMs),
        // not a fixed since-frame-start budget: s_nextPresentMs advances by
        // exactly kCapPeriodMs (16.6667ms) from the SCHEDULED time, so the
        // delay target stays locked to the 1/60s grid and self-corrects when
        // a frame overshoots -- a fixed "sleep the remainder of this frame's
        // budget" resets from `now` every frame, so on a 120Hz panel an
        // overshoot (SDL_Delay ~1ms granularity + jitter) snaps the following
        // SwapWindow to the next vsync tick (25ms => a 40fps frame) instead of
        // catching back up, dragging the average under 60. Target the delay
        // ~1ms short of the schedule so SwapWindow reliably catches the
        // intended vsync tick rather than overshooting into the next one.
        static double s_nextPresentMs = -1.0;
        const double kCapPeriodMs = 1000.0 / 60.0;
        double capTargetMs = kCapPeriodMs;
        if (vsyncOn && FN::g_FpsCap60) {
            double nowMs = static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / freq;
            if (s_nextPresentMs < 0.0) {
                s_nextPresentMs = nowMs;
            }
            double delayMs = s_nextPresentMs - nowMs - 1.0;
            if (delayMs > 0.0) {
                SDL_Delay(static_cast<Uint32>(delayMs));
            }
            s_nextPresentMs += kCapPeriodMs;
            if (s_nextPresentMs < nowMs) {
                s_nextPresentMs = nowMs + kCapPeriodMs;
            }
        } else {
            s_nextPresentMs = -1.0;
        }

        if (!vsyncOn) {
            double frameMs = static_cast<double>(SDL_GetPerformanceCounter() - frameStart) * 1000.0 / freq;
            double cappedTargetMs = (FN::g_FpsCap60 && capTargetMs > targetMs) ? capTargetMs : targetMs;
            if (frameMs < cappedTargetMs) {
                SDL_Delay(static_cast<Uint32>(cappedTargetMs - frameMs));
            }
        } else if (steps == 0 && !FN::g_FpsCap60) {
            SDL_Delay(1);   // vsync on but not blocking (e.g. minimized) -- don't peg a core
        }
    }
}

// Test-only: run a fixed number of game ticks (no SDL_Delay, no accumulator).
// Wall-clock-free and deterministic: each iteration drains only SDL_QUIT then
// calls stepUpdate() + renderFrame() exactly once.  No full pollInput() so
// held-finger shim and focus-loss logic don't fire during headless tests.
// Behaviour is identical to the pre-Phase-1 implementation; headless tests
// (test_screen, test_gameplay, etc.) are unaffected by the run() rewrite.
void Game::runFrames(int frameCount) {
    for (int i = 0; i < frameCount && running; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
        }
        stepUpdate();
        renderFrame(0.0f, 1);
    }
}
